// The JS API wrapper objects (docs/scripting.md). Everything resolves through
// ScriptEngineHost services by session id + LayerId on every call: wrappers
// survive across event-loop turns while layers get deleted and the layers
// vector reallocates, so a stored pointer would be a use-after-free. Reads go
// through const documents (mutable layer accessors bump revisions on access);
// mutations run prepare_mutation() first so the run's single undo entry exists.

#include "ui/script_api.hpp"

#include "core/image_trace.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_shape.hpp"
#include "core/vector_raster.hpp"
#include "core/shape_combine.hpp"
#include "core/path_simplify.hpp"

#include "core/layer_metadata.hpp"
#include "formats/document_flatten.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_tools.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/script_canvas_window.hpp"
#include "ui/script_engine.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QRegion>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace patchy::ui {

namespace {

// Script-facing blend mode ids. Append-only and aligned with the BlendMode
// enum order (core/layer.hpp); scripts hard-code these strings.
constexpr std::array<const char*, 28> kBlendModeIds = {
    "pass-through", "normal",       "multiply",    "screen",       "overlay",
    "darken",       "lighten",      "color-dodge", "color-burn",   "hard-light",
    "soft-light",   "difference",   "linear-burn", "pin-light",    "saturation",
    "luminosity",   "exclusion",    "hue",         "color",        "linear-dodge",
    "subtract",     "divide",       "vivid-light", "linear-light", "hard-mix",
    "darker-color", "lighter-color", "dissolve"};

QJSValue rect_to_js(QJSEngine* engine, const Rect& rect) {
  auto value = engine->newObject();
  value.setProperty(QStringLiteral("x"), rect.x);
  value.setProperty(QStringLiteral("y"), rect.y);
  value.setProperty(QStringLiteral("width"), rect.width);
  value.setProperty(QStringLiteral("height"), rect.height);
  return value;
}

// Parses "#rrggbb" / "#aarrggbb" / named colors; throws a JS error when invalid.
bool parse_color(ScriptEngineHost& host, const QString& text, QColor* color) {
  QColor parsed(text);
  if (!parsed.isValid()) {
    host.throw_js_error(
        ScriptEngineHost::tr("Invalid color: %1 (use \"#rrggbb\" or a named color)").arg(text));
    return false;
  }
  *color = parsed;
  return true;
}

// Locates the vector holding `id` plus its index, walking const for the search;
// the caller re-walks non-const only when it actually mutates.
const std::vector<Layer>* find_parent_vector(const Document& document, LayerId id,
                                             std::size_t* index) {
  std::function<const std::vector<Layer>*(const std::vector<Layer>&)> search =
      [&](const std::vector<Layer>& layers) -> const std::vector<Layer>* {
    for (std::size_t i = 0; i < layers.size(); ++i) {
      if (layers[i].id() == id) {
        *index = i;
        return &layers;
      }
      if (const auto* found = search(layers[i].children())) {
        return found;
      }
    }
    return nullptr;
  };
  return search(document.layers());
}

std::vector<Layer>* find_parent_vector_mutable(Document& document, LayerId id, std::size_t* index) {
  std::function<std::vector<Layer>*(std::vector<Layer>&)> search =
      [&](std::vector<Layer>& layers) -> std::vector<Layer>* {
    for (std::size_t i = 0; i < layers.size(); ++i) {
      if (layers[i].id() == id) {
        *index = i;
        return &layers;
      }
      if (auto* found = search(layers[i].children())) {
        return found;
      }
    }
    return nullptr;
  };
  return search(document.layers());
}

Layer clone_layer_with_fresh_ids(Document& document, const Layer& source) {
  Layer copy = source.clone_with_id(document.allocate_layer_id());
  auto& children = copy.children();
  for (auto& child : children) {
    child = clone_layer_with_fresh_ids(document, child);
  }
  return copy;
}

void offset_layer_recursive(Layer& layer, int dx, int dy) {
  auto bounds = layer.bounds();
  if (!bounds.empty()) {
    bounds.x += dx;
    bounds.y += dy;
    layer.set_bounds(bounds);
  }
  if (layer.mask().has_value()) {
    auto& mask = *layer.mask();
    mask.bounds.x += dx;
    mask.bounds.y += dy;
  }
  for (auto& child : layer.children()) {
    offset_layer_recursive(child, dx, dy);
  }
}

}  // namespace

QString script_blend_mode_id(BlendMode mode) {
  const auto index = static_cast<std::size_t>(mode);
  if (index >= kBlendModeIds.size()) {
    return QStringLiteral("normal");
  }
  return QString::fromLatin1(kBlendModeIds[index]);
}

bool script_blend_mode_from_id(const QString& id, BlendMode* mode) {
  for (std::size_t i = 0; i < kBlendModeIds.size(); ++i) {
    if (id == QLatin1String(kBlendModeIds[i])) {
      *mode = static_cast<BlendMode>(i);
      return true;
    }
  }
  return false;
}

QJSValue make_document_value(ScriptEngineHost& host, std::int64_t session_id) {
  return host.engine()->newQObject(new ScriptDocumentObject(host, session_id));
}

QJSValue make_layer_value(ScriptEngineHost& host, std::int64_t session_id, LayerId layer_id) {
  return host.engine()->newQObject(new ScriptLayerObject(host, session_id, layer_id));
}

// ---------------------------------------------------------------------------
// ScriptLayerObject

ScriptLayerObject::ScriptLayerObject(ScriptEngineHost& host, std::int64_t session_id,
                                     LayerId layer_id)
    : host_(host), session_id_(session_id), layer_id_(layer_id) {}

const Layer* ScriptLayerObject::read_layer() const {
  const auto* document = host_.session_document_const(session_id_);
  const auto* layer = document != nullptr ? document->find_layer(layer_id_) : nullptr;
  if (layer == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
  }
  return layer;
}

Layer* ScriptLayerObject::write_layer() {
  auto* document = host_.session_document(session_id_);
  auto* layer = document != nullptr ? document->find_layer(layer_id_) : nullptr;
  if (layer == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return nullptr;
  }
  if (!host_.prepare_mutation(session_id_)) {
    return nullptr;
  }
  // prepare_mutation snapshots the pre-edit document, which copies it; the
  // layer pointer stays valid (snapshotting copies, it does not move).
  return layer;
}

QString ScriptLayerObject::name() const {
  const auto* layer = read_layer();
  return layer != nullptr ? QString::fromStdString(layer->name()) : QString();
}

void ScriptLayerObject::set_name(const QString& name) {
  if (auto* layer = write_layer()) {
    layer->set_name(name.toStdString());
    host_.note_structure_changed(session_id_);
  }
}

double ScriptLayerObject::opacity() const {
  const auto* layer = read_layer();
  return layer != nullptr ? static_cast<double>(layer->opacity()) * 100.0 : 0.0;
}

void ScriptLayerObject::set_opacity(double opacity) {
  if (auto* layer = write_layer()) {
    const auto before = to_qrect(layer_render_bounds(std::as_const(*layer)));
    layer->set_opacity(static_cast<float>(std::clamp(opacity, 0.0, 100.0) / 100.0));
    host_.note_pixels_changed(session_id_, before);
    host_.note_structure_changed(session_id_);
  }
}

bool ScriptLayerObject::visible() const {
  const auto* layer = read_layer();
  return layer != nullptr && layer->visible();
}

void ScriptLayerObject::set_visible(bool visible) {
  if (auto* layer = write_layer()) {
    layer->set_visible(visible);
    // set_visible deliberately does not bump revisions; repaint the layer's
    // reach and refresh the panel's eye toggle.
    host_.note_pixels_changed(session_id_, to_qrect(layer_render_bounds(std::as_const(*layer))));
    host_.note_structure_changed(session_id_);
  }
}

QString ScriptLayerObject::blend_mode() const {
  const auto* layer = read_layer();
  return layer != nullptr ? script_blend_mode_id(layer->blend_mode()) : QString();
}

void ScriptLayerObject::set_blend_mode(const QString& mode) {
  BlendMode parsed{};
  if (!script_blend_mode_from_id(mode, &parsed)) {
    host_.throw_js_error(ScriptEngineHost::tr("Unknown blend mode: %1").arg(mode));
    return;
  }
  if (auto* layer = write_layer()) {
    layer->set_blend_mode(parsed);
    host_.note_pixels_changed(session_id_, to_qrect(layer_render_bounds(std::as_const(*layer))));
    host_.note_structure_changed(session_id_);
  }
}

bool ScriptLayerObject::locked() const {
  const auto* layer = read_layer();
  return layer != nullptr && layer->lock_flags() != kLayerLockNone;
}

void ScriptLayerObject::set_locked(bool locked) {
  if (auto* layer = write_layer()) {
    layer->set_lock_flags(locked ? kLayerLockAll : kLayerLockNone);
    host_.note_structure_changed(session_id_);
  }
}

int ScriptLayerObject::x() const {
  const auto* layer = read_layer();
  return layer != nullptr ? layer->bounds().x : 0;
}

int ScriptLayerObject::y() const {
  const auto* layer = read_layer();
  return layer != nullptr ? layer->bounds().y : 0;
}

void ScriptLayerObject::set_x(int x) {
  const auto* current = read_layer();
  if (current != nullptr) {
    moveTo(x, current->bounds().y);
  }
}

void ScriptLayerObject::set_y(int y) {
  const auto* current = read_layer();
  if (current != nullptr) {
    moveTo(current->bounds().x, y);
  }
}

void ScriptLayerObject::moveTo(int x, int y) {
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  const auto bounds = std::as_const(*layer).bounds();
  const int dx = x - bounds.x;
  const int dy = y - bounds.y;
  if (dx == 0 && dy == 0) {
    return;
  }
  const auto before = to_qrect(layer_render_bounds(std::as_const(*layer)));
  offset_layer_recursive(*layer, dx, dy);
  const auto after = to_qrect(layer_render_bounds(std::as_const(*layer)));
  host_.note_pixels_changed(session_id_, before.united(after));
}

QJSValue ScriptLayerObject::bounds() const {
  const auto* layer = read_layer();
  return layer != nullptr ? rect_to_js(host_.engine(), layer->bounds()) : QJSValue();
}

bool ScriptLayerObject::is_group() const {
  const auto* layer = read_layer();
  return layer != nullptr && layer->kind() == LayerKind::Group;
}

bool ScriptLayerObject::is_text() const {
  return host_.layer_is_text_layer(session_id_, layer_id_);
}

QJSValue ScriptLayerObject::children() const {
  const auto* layer = read_layer();
  if (layer == nullptr) {
    return QJSValue();
  }
  auto array = host_.engine()->newArray(static_cast<quint32>(layer->children().size()));
  quint32 index = 0;
  for (const auto& child : layer->children()) {
    array.setProperty(index++, make_layer_value(host_, session_id_, child.id()));
  }
  return array;
}

QString ScriptLayerObject::text() const {
  return host_.text_layer_text(session_id_, layer_id_);
}

void ScriptLayerObject::set_text(const QString& text) {
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (!host_.set_text_layer_text(session_id_, layer_id_, text)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

QJSValue ScriptLayerObject::duplicate() {
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return QJSValue();
  }
  std::size_t index = 0;
  if (find_parent_vector(std::as_const(*document), layer_id_, &index) == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return QJSValue();
  }
  if (!host_.prepare_mutation(session_id_)) {
    return QJSValue();
  }
  auto* parent = find_parent_vector_mutable(*document, layer_id_, &index);
  Layer copy = clone_layer_with_fresh_ids(*document, (*parent)[index]);
  copy.set_name(copy.name() + " copy");
  const auto copy_id = copy.id();
  parent->insert(parent->begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
  host_.note_structure_changed(session_id_);
  return make_layer_value(host_, session_id_, copy_id);
}

void ScriptLayerObject::remove() {
  auto* document = host_.session_document(session_id_);
  if (document == nullptr || document->find_layer(layer_id_) == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return;
  }
  if (!host_.prepare_mutation(session_id_)) {
    return;
  }
  document->remove_layer(layer_id_);
  host_.note_structure_changed(session_id_);
}

QJSValue ScriptLayerObject::ungroup() {
  auto* document = host_.session_document(session_id_);
  const auto* view = document != nullptr ? std::as_const(*document).find_layer(layer_id_) : nullptr;
  if (view == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return QJSValue();
  }
  if (view->kind() != LayerKind::Group) {
    host_.throw_js_error(ScriptEngineHost::tr("ungroup needs a group layer."));
    return QJSValue();
  }
  if (!host_.prepare_mutation(session_id_)) {
    return QJSValue();
  }
  const auto released = ungroup_layer(document->layers(), layer_id_);
  if (!released.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("ungroup needs a group layer."));
    return QJSValue();
  }
  if (!released->empty()) {
    document->set_active_layer(released->front());
  }
  host_.note_structure_changed(session_id_);
  auto array = host_.engine()->newArray(static_cast<uint>(released->size()));
  for (std::size_t i = 0; i < released->size(); ++i) {
    array.setProperty(static_cast<quint32>(i), make_layer_value(host_, session_id_, (*released)[i]));
  }
  return array;
}

void ScriptLayerObject::fill(const QString& color) {
  QColor parsed;
  if (!parse_color(host_, color, &parsed)) {
    return;
  }
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  if (layer->kind() == LayerKind::Group) {
    host_.throw_js_error(ScriptEngineHost::tr("fill needs a pixel layer, not a group."));
    return;
  }
  const auto* document = host_.session_document_const(session_id_);
  parsed = host_.palette_snap_color(session_id_, parsed);

  // Fill target: the selection when one exists, otherwise the whole canvas. An
  // empty layer allocates a buffer covering the target.
  const QRect canvas_rect(0, 0, document->width(), document->height());
  QRegion target = host_.has_selection(session_id_) ? host_.selection_region(session_id_)
                                                    : QRegion(canvas_rect);
  target &= canvas_rect;
  if (target.isEmpty()) {
    return;
  }
  if (std::as_const(*layer).pixels().empty()) {
    const QRect box = target.boundingRect();
    PixelBuffer fresh(box.width(), box.height(), PixelFormat::rgba8());
    layer->set_pixels(std::move(fresh));
    layer->set_bounds(Rect{box.x(), box.y(), box.width(), box.height()});
  }
  const auto bounds = std::as_const(*layer).bounds();
  auto& pixels = layer->pixels();
  if (pixels.format().channels != 4 || pixels.format().bit_depth != BitDepth::UInt8) {
    host_.throw_js_error(ScriptEngineHost::tr("fill supports 8-bit RGBA layers only."));
    return;
  }
  const std::array<std::uint8_t, 4> rgba{static_cast<std::uint8_t>(parsed.red()),
                                         static_cast<std::uint8_t>(parsed.green()),
                                         static_cast<std::uint8_t>(parsed.blue()),
                                         static_cast<std::uint8_t>(parsed.alpha())};
  for (const QRect& rect : target) {
    const QRect layer_rect =
        rect.intersected(QRect(bounds.x, bounds.y, pixels.width(), pixels.height()));
    for (int y = layer_rect.top(); y <= layer_rect.bottom(); ++y) {
      for (int x = layer_rect.left(); x <= layer_rect.right(); ++x) {
        auto* px = pixels.pixel(x - bounds.x, y - bounds.y);
        px[0] = rgba[0];
        px[1] = rgba[1];
        px[2] = rgba[2];
        px[3] = rgba[3];
      }
    }
  }
  host_.note_pixels_changed(session_id_, target.boundingRect());
}

// Partial in-place write: overwrites RGBA (a transparent color clears) inside
// the given document-space rect, clipped to the layer's buffer. An empty layer
// allocates a buffer covering exactly the rect, so tiny sprite layers can be
// created with one call and then animated via x/y (much cheaper per frame than
// re-uploading pixels). Palette mode snaps like every tool write.
void ScriptLayerObject::fillRect(int x, int y, int width, int height, const QString& color) {
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("fillRect needs a positive size."));
    return;
  }
  QColor parsed;
  if (!parse_color(host_, color, &parsed)) {
    return;
  }
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  if (layer->kind() == LayerKind::Group) {
    host_.throw_js_error(ScriptEngineHost::tr("fillRect needs a pixel layer, not a group."));
    return;
  }
  parsed = host_.palette_snap_color(session_id_, parsed);
  if (std::as_const(*layer).pixels().empty()) {
    PixelBuffer fresh(width, height, PixelFormat::rgba8());
    layer->set_pixels(std::move(fresh));
    layer->set_bounds(Rect{x, y, width, height});
  }
  const auto bounds = std::as_const(*layer).bounds();
  auto& pixels = layer->pixels();
  if (pixels.format().channels != 4 || pixels.format().bit_depth != BitDepth::UInt8) {
    host_.throw_js_error(ScriptEngineHost::tr("fillRect supports 8-bit RGBA layers only."));
    return;
  }
  const QRect target = QRect(x, y, width, height)
                           .intersected(QRect(bounds.x, bounds.y, pixels.width(), pixels.height()));
  if (target.isEmpty()) {
    return;
  }
  const std::array<std::uint8_t, 4> rgba{static_cast<std::uint8_t>(parsed.red()),
                                         static_cast<std::uint8_t>(parsed.green()),
                                         static_cast<std::uint8_t>(parsed.blue()),
                                         static_cast<std::uint8_t>(parsed.alpha())};
  for (int py = target.top(); py <= target.bottom(); ++py) {
    for (int px = target.left(); px <= target.right(); ++px) {
      auto* pixel = pixels.pixel(px - bounds.x, py - bounds.y);
      pixel[0] = rgba[0];
      pixel[1] = rgba[1];
      pixel[2] = rgba[2];
      pixel[3] = rgba[3];
    }
  }
  host_.note_pixels_changed(session_id_, target);
}

void ScriptLayerObject::applyFilter(const QString& filterId, const QJSValue& params) {
  host_.apply_filter_to_layer(session_id_, layer_id_, filterId, params);
}

QJSValue ScriptLayerObject::traceToShapes(const QJSValue& options) {
  ImageTraceOptions trace_options;
  bool palette_from_layer = true;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      if (key == QLatin1String("mode")) {
        const auto mode = value.toString();
        if (mode == QLatin1String("color")) {
          trace_options.mode = ImageTraceOptions::Mode::Color;
        } else if (mode == QLatin1String("grayscale")) {
          trace_options.mode = ImageTraceOptions::Mode::Grayscale;
        } else if (mode == QLatin1String("blackAndWhite")) {
          trace_options.mode = ImageTraceOptions::Mode::BlackAndWhite;
        } else {
          host_.throw_js_error(ScriptEngineHost::tr("traceToShapes: mode must be color, grayscale, or blackAndWhite."));
          return QJSValue();
        }
      } else if (key == QLatin1String("method")) {
        const auto method = value.toString();
        if (method == QLatin1String("abutting")) {
          trace_options.method = ImageTraceOptions::Method::Abutting;
        } else if (method == QLatin1String("overlapping")) {
          trace_options.method = ImageTraceOptions::Method::Overlapping;
        } else {
          host_.throw_js_error(ScriptEngineHost::tr("traceToShapes: method must be abutting or overlapping."));
          return QJSValue();
        }
      } else if (key == QLatin1String("colors")) {
        trace_options.colors = value.toInt();
      } else if (key == QLatin1String("threshold")) {
        trace_options.threshold = value.toInt();
      } else if (key == QLatin1String("paths")) {
        trace_options.paths = value.toInt();
      } else if (key == QLatin1String("corners")) {
        trace_options.corners = value.toInt();
      } else if (key == QLatin1String("noise")) {
        trace_options.noise = value.toInt();
      } else if (key == QLatin1String("smoothing")) {
        trace_options.smoothing = value.toInt();
      } else if (key == QLatin1String("maxAnchors")) {
        trace_options.max_anchors = value.toInt();
      } else if (key == QLatin1String("snapCurvesToLines")) {
        trace_options.snap_curves_to_lines = value.toBool();
      } else if (key == QLatin1String("ignoreWhite")) {
        trace_options.ignore_white = value.toBool();
      } else if (key == QLatin1String("paletteFromLayer")) {
        palette_from_layer = value.toBool();
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("traceToShapes: unknown option %1").arg(key));
        return QJSValue();
      }
    }
  }
  const auto* layer = read_layer();
  if (layer == nullptr) {
    return QJSValue();
  }
  if (layer->kind() != LayerKind::Pixel || layer->pixels().empty()) {
    host_.throw_js_error(ScriptEngineHost::tr("traceToShapes needs a pixel layer."));
    return QJSValue();
  }
  // The document selection limits the traced area, exactly like the dialog;
  // by default the palette still comes from the whole layer (paletteFromLayer:
  // false restores selection-scoped colors). The masked buffer is a named
  // local so the palette-source pointer never outlives it.
  const bool selection_active = host_.has_selection(session_id_);
  const auto masked = host_.pixels_limited_to_selection(session_id_, layer->pixels(), layer->bounds());
  const auto result = trace_image(masked, trace_options, {}, 0,
                                  selection_active && palette_from_layer ? &layer->pixels() : nullptr);
  if (result.layers.empty()) {
    return QJSValue(QJSValue::NullValue);
  }
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return QJSValue();
  }
  std::size_t index = 0;
  if (find_parent_vector(std::as_const(*document), layer_id_, &index) == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return QJSValue();
  }
  if (!host_.prepare_mutation(session_id_)) {
    return QJSValue();
  }
  auto* parent = find_parent_vector_mutable(*document, layer_id_, &index);
  auto& source = (*parent)[index];
  const auto bounds = std::as_const(source).bounds();
  auto group = build_image_trace_group(
      *document, result, bounds.x, bounds.y,
      ScriptEngineHost::tr("Traced %1").arg(QString::fromStdString(source.name())).toStdString());
  const auto group_id = group.id();
  source.set_visible(false);
  parent->insert(parent->begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(group));
  document->set_active_layer(group_id);
  host_.note_structure_changed(session_id_);
  return make_layer_value(host_, session_id_, group_id);
}

QJSValue ScriptLayerObject::simplifyPath(const QJSValue& options) {
  PathSimplifyOptions simplify;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      if (key == QLatin1String("tolerance")) {
        simplify.tolerance = std::clamp(value.toNumber(), 0.1, 100.0);
      } else if (key == QLatin1String("cornerAngle")) {
        simplify.corner_angle_degrees = std::clamp(value.toNumber(), 1.0, 179.0);
      } else if (key == QLatin1String("snapCurvesToLines")) {
        simplify.snap_curves_to_lines = value.toBool();
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("simplifyPath: unknown option %1").arg(key));
        return QJSValue();
      }
    }
  }
  const auto* view = read_layer();
  if (view == nullptr) {
    return QJSValue();
  }
  const bool shape = layer_is_vector_shape(*view) && vector_lock_reason(*view).empty() &&
                     view->vector_shape() != nullptr;
  const bool mask = !shape && view->vector_mask() != nullptr;
  if (!shape && !mask) {
    host_.throw_js_error(ScriptEngineHost::tr("simplifyPath needs a shape layer or a layer with a vector mask."));
    return QJSValue();
  }
  auto* document = host_.session_document(session_id_);
  auto* layer = write_layer();
  if (document == nullptr || layer == nullptr) {
    return QJSValue();
  }
  const auto canvas = Rect::from_size(document->width(), document->height());
  PathSimplifyResult result;
  if (shape) {
    auto content = *std::as_const(*layer).vector_shape();
    result = simplify_vector_path(content.path, simplify);
    content.path = result.path;
    drop_live_shape_origination(content, result.changed_groups);
    layer->set_vector_shape(std::move(content));
    layer->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    mark_layer_vector_block_dirty(*layer);
    update_vector_shape_raster(*layer, canvas, &document->metadata().patterns);
  } else {
    auto vector_mask = *std::as_const(*layer).vector_mask();
    result = simplify_vector_path(vector_mask.path, simplify);
    vector_mask.path = result.path;
    layer->set_vector_mask(std::move(vector_mask));
    mark_layer_vector_block_dirty(*layer);
    update_vector_mask_raster(*layer, canvas);
  }
  host_.note_pixels_changed(session_id_, QRect());
  auto value = host_.engine()->newObject();
  value.setProperty(QStringLiteral("anchorsBefore"), static_cast<int>(result.anchors_before));
  value.setProperty(QStringLiteral("anchorsAfter"), static_cast<int>(result.anchors_after));
  return value;
}

QJSValue ScriptLayerObject::getPixels() {
  const auto* layer = read_layer();
  if (layer == nullptr) {
    return QJSValue();
  }
  const auto& pixels = layer->pixels();
  const bool is_rgba8 =
      pixels.format().channels == 4 && pixels.format().bit_depth == BitDepth::UInt8;
  const bool is_rgb8 =
      pixels.format().channels == 3 && pixels.format().bit_depth == BitDepth::UInt8;
  if (!pixels.empty() && !is_rgba8 && !is_rgb8) {
    host_.throw_js_error(
        ScriptEngineHost::tr("getPixels supports 8-bit RGB and RGBA layers only."));
    return QJSValue();
  }
  const auto bounds = layer->bounds();
  auto result = host_.engine()->newObject();
  result.setProperty(QStringLiteral("x"), bounds.x);
  result.setProperty(QStringLiteral("y"), bounds.y);
  result.setProperty(QStringLiteral("width"), pixels.width());
  result.setProperty(QStringLiteral("height"), pixels.height());
  QByteArray data;
  if (!pixels.empty()) {
    const auto span = pixels.data();
    if (is_rgba8) {
      data = QByteArray(reinterpret_cast<const char*>(span.data()),
                        static_cast<qsizetype>(span.size()));
    } else {
      // Opaque images (JPEG and friends) open as 3-channel RGB layers; scripts
      // always see RGBA (alpha 255). A later setPixels writes the layer back
      // as RGBA8, the format every script write path produces.
      const auto pixel_count =
          static_cast<qsizetype>(pixels.width()) * pixels.height();
      data.resize(pixel_count * 4);
      const auto* source = span.data();
      auto* target = reinterpret_cast<std::uint8_t*>(data.data());
      for (qsizetype i = 0; i < pixel_count; ++i) {
        target[i * 4] = source[i * 3];
        target[i * 4 + 1] = source[i * 3 + 1];
        target[i * 4 + 2] = source[i * 3 + 2];
        target[i * 4 + 3] = 255;
      }
    }
  }
  result.setProperty(QStringLiteral("data"), host_.engine()->toScriptValue(data));
  return result;
}

void ScriptLayerObject::setPixels(const QJSValue& imageData) {
  if (!imageData.isObject()) {
    host_.throw_js_error(
        ScriptEngineHost::tr("setPixels needs a {width, height, data} object."));
    return;
  }
  const int width = imageData.property(QStringLiteral("width")).toInt();
  const int height = imageData.property(QStringLiteral("height")).toInt();
  const auto data =
      imageData.property(QStringLiteral("data")).toVariant().toByteArray();
  if (width < 1 || height < 1 ||
      data.size() != static_cast<qsizetype>(width) * height * 4) {
    host_.throw_js_error(ScriptEngineHost::tr(
        "setPixels: data must hold width * height * 4 RGBA bytes."));
    return;
  }
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  if (layer->kind() == LayerKind::Group) {
    host_.throw_js_error(ScriptEngineHost::tr("setPixels needs a pixel layer, not a group."));
    return;
  }
  const auto old_bounds = std::as_const(*layer).bounds();
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  std::copy(data.begin(), data.end(), reinterpret_cast<char*>(pixels.data().data()));
  host_.palette_snap_buffer(session_id_, pixels);
  const QJSValue x_value = imageData.property(QStringLiteral("x"));
  const QJSValue y_value = imageData.property(QStringLiteral("y"));
  const int x = x_value.isNumber() ? x_value.toInt() : old_bounds.x;
  const int y = y_value.isNumber() ? y_value.toInt() : old_bounds.y;
  layer->set_pixels(std::move(pixels));
  layer->set_bounds(Rect{x, y, width, height});
  const QRect before = to_qrect(old_bounds);
  const QRect after(x, y, width, height);
  host_.note_pixels_changed(session_id_, before.united(after));
}

// ---------------------------------------------------------------------------
// ScriptSelectionObject

ScriptSelectionObject::ScriptSelectionObject(ScriptEngineHost& host, std::int64_t session_id)
    : host_(host), session_id_(session_id) {}

bool ScriptSelectionObject::exists() const { return host_.has_selection(session_id_); }

QJSValue ScriptSelectionObject::bounds() const {
  const auto region = host_.selection_region(session_id_);
  if (region.isEmpty()) {
    return QJSValue();
  }
  const auto rect = region.boundingRect();
  return rect_to_js(host_.engine(),
                    Rect{rect.x(), rect.y(), rect.width(), rect.height()});
}

void ScriptSelectionObject::selectAll() { host_.select_all(session_id_); }

void ScriptSelectionObject::deselect() { host_.deselect(session_id_); }

void ScriptSelectionObject::selectRect(int x, int y, int width, int height) {
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("selectRect needs a positive size."));
    return;
  }
  host_.select_region(session_id_, QRegion(x, y, width, height));
}

void ScriptSelectionObject::selectEllipse(int x, int y, int width, int height) {
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("selectEllipse needs a positive size."));
    return;
  }
  host_.select_region(session_id_, QRegion(x, y, width, height, QRegion::Ellipse));
}

// ---------------------------------------------------------------------------
// ScriptDocumentObject

ScriptDocumentObject::ScriptDocumentObject(ScriptEngineHost& host, std::int64_t session_id)
    : host_(host), session_id_(session_id) {}

const Document* ScriptDocumentObject::read_document() const {
  const auto* document = host_.session_document_const(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
  }
  return document;
}

Document* ScriptDocumentObject::write_document() {
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return nullptr;
  }
  if (!host_.prepare_mutation(session_id_)) {
    return nullptr;
  }
  return document;
}

int ScriptDocumentObject::width() const {
  const auto* document = read_document();
  return document != nullptr ? document->width() : 0;
}

int ScriptDocumentObject::height() const {
  const auto* document = read_document();
  return document != nullptr ? document->height() : 0;
}

QString ScriptDocumentObject::name() const { return host_.session_title(session_id_); }

QString ScriptDocumentObject::path() const { return host_.session_file_path(session_id_); }

double ScriptDocumentObject::resolution() const {
  const auto* document = read_document();
  return document != nullptr ? document->print_settings().horizontal_ppi : 0.0;
}

QJSValue ScriptDocumentObject::layers() const {
  const auto* document = read_document();
  if (document == nullptr) {
    return QJSValue();
  }
  auto array = host_.engine()->newArray(static_cast<quint32>(document->layers().size()));
  quint32 index = 0;
  for (const auto& layer : document->layers()) {
    array.setProperty(index++, make_layer_value(host_, session_id_, layer.id()));
  }
  return array;
}

QJSValue ScriptDocumentObject::active_layer() const {
  const auto* document = read_document();
  if (document == nullptr || !document->active_layer_id().has_value()) {
    return QJSValue();
  }
  return make_layer_value(host_, session_id_, *document->active_layer_id());
}

void ScriptDocumentObject::set_active_layer(const QJSValue& layer) {
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return;
  }
  const auto* wrapper = qobject_cast<ScriptLayerObject*>(layer.toQObject());
  if (wrapper == nullptr || document->find_layer(wrapper->layer_id()) == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("activeLayer needs a layer of this document."));
    return;
  }
  document->set_active_layer(wrapper->layer_id());
  // Reveal the row: expand collapsed ancestor folders and scroll it into view,
  // so a script's selection is visible exactly like a user's click would be.
  host_.reveal_layer_row(session_id_, wrapper->layer_id());
  host_.note_structure_changed(session_id_);
}

QJSValue ScriptDocumentObject::combineShapes(const QJSValue& layers, const QString& op) {
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return QJSValue();
  }
  PathCombineOp combine = PathCombineOp::Add;
  if (op == QLatin1String("unite")) {
    combine = PathCombineOp::Add;
  } else if (op == QLatin1String("subtract")) {
    combine = PathCombineOp::Subtract;
  } else if (op == QLatin1String("intersect")) {
    combine = PathCombineOp::Intersect;
  } else if (op == QLatin1String("exclude")) {
    combine = PathCombineOp::Xor;
  } else {
    host_.throw_js_error(
        ScriptEngineHost::tr("combineShapes: unknown op %1 (unite, subtract, intersect, exclude)").arg(op));
    return QJSValue();
  }
  std::vector<LayerId> ids;
  bool valid = layers.isArray();
  const auto length = valid ? layers.property(QStringLiteral("length")).toInt() : 0;
  for (int i = 0; valid && i < length; ++i) {
    const auto* wrapper = qobject_cast<ScriptLayerObject*>(layers.property(static_cast<quint32>(i)).toQObject());
    if (wrapper == nullptr || wrapper->session_id() != session_id_ ||
        std::as_const(*document).find_layer(wrapper->layer_id()) == nullptr) {
      valid = false;
      break;
    }
    ids.push_back(wrapper->layer_id());
  }
  if (!valid) {
    host_.throw_js_error(ScriptEngineHost::tr("combineShapes needs an array of layers of this document."));
    return QJSValue();
  }
  const auto candidates = combine_shape_candidates(std::as_const(*document).layers(), ids);
  switch (candidates.refusal) {
    case ShapeCombineRefusal::NeedTwoLayers:
      host_.throw_js_error(ScriptEngineHost::tr("combineShapes needs two or more shape layers."));
      return QJSValue();
    case ShapeCombineRefusal::NotShapeLayer:
      host_.throw_js_error(ScriptEngineHost::tr("combineShapes: only editable shape layers can be combined."));
      return QJSValue();
    case ShapeCombineRefusal::Locked:
      host_.throw_js_error(ScriptEngineHost::tr("combineShapes: the shape layers are locked."));
      return QJSValue();
    case ShapeCombineRefusal::EmptyPath:
      host_.throw_js_error(ScriptEngineHost::tr("combineShapes: fill layers without a path cannot be combined."));
      return QJSValue();
    case ShapeCombineRefusal::DifferentParents:
      host_.throw_js_error(ScriptEngineHost::tr("combineShapes: the shape layers must share one folder."));
      return QJSValue();
    case ShapeCombineRefusal::None:
      break;
  }
  auto* mutable_document = write_document();
  if (mutable_document == nullptr) {
    return QJSValue();
  }
  const auto result = combine_shape_layers(*mutable_document, candidates.bottom_to_top, combine);
  if (!result.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("combineShapes: only editable shape layers can be combined."));
    return QJSValue();
  }
  mutable_document->set_active_layer(result->layer_id);
  host_.note_structure_changed(session_id_);
  return make_layer_value(host_, session_id_, result->layer_id);
}

QJSValue ScriptDocumentObject::selection() const {
  return host_.engine()->newQObject(new ScriptSelectionObject(host_, session_id_));
}

QJSValue ScriptDocumentObject::addLayer(const QString& name) {
  auto* document = write_document();
  if (document == nullptr) {
    return QJSValue();
  }
  Layer layer(document->allocate_layer_id(),
              name.isEmpty() ? ScriptEngineHost::tr("Layer").toStdString() : name.toStdString(),
              PixelBuffer{});
  const auto id = layer.id();
  document->add_layer(std::move(layer));
  document->set_active_layer(id);
  host_.note_structure_changed(session_id_);
  return make_layer_value(host_, session_id_, id);
}

QJSValue ScriptDocumentObject::addTextLayer(const QString& text, const QJSValue& options) {
  ScriptEngineHost::TextLayerParams params;
  params.text = text;
  if (options.isObject()) {
    const auto font = options.property(QStringLiteral("font"));
    if (font.isString()) {
      params.family = font.toString();
    }
    const auto size = options.property(QStringLiteral("size"));
    if (size.isNumber()) {
      params.size_px = size.toNumber();
    }
    params.bold = options.property(QStringLiteral("bold")).toBool();
    params.italic = options.property(QStringLiteral("italic")).toBool();
    const auto color = options.property(QStringLiteral("color"));
    if (color.isString()) {
      QColor parsed;
      if (!parse_color(host_, color.toString(), &parsed)) {
        return QJSValue();
      }
      params.color = parsed;
    }
    params.position = QPoint(options.property(QStringLiteral("x")).toInt(),
                             options.property(QStringLiteral("y")).toInt());
  }
  const auto created = host_.add_text_layer(session_id_, params);
  if (!created.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not create the text layer."));
    return QJSValue();
  }
  return make_layer_value(host_, session_id_, *created);
}

QJSValue ScriptDocumentObject::findLayer(const QString& name) {
  const auto* document = read_document();
  if (document == nullptr) {
    return QJSValue();
  }
  const auto wanted = name.toStdString();
  std::optional<LayerId> found;
  std::function<void(const std::vector<Layer>&)> search = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (found.has_value()) {
        return;
      }
      if (layer.name() == wanted) {
        found = layer.id();
        return;
      }
      search(layer.children());
    }
  };
  search(document->layers());
  return found.has_value() ? make_layer_value(host_, session_id_, *found) : QJSValue();
}

void ScriptDocumentObject::flatten() {
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  auto flattened = flatten_document_rgba8(*document);
  document->clear_active_layer();
  document->layers().clear();
  document->add_pixel_layer(ScriptEngineHost::tr("Background").toStdString(),
                            std::move(flattened));
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::resizeImage(int width, int height) {
  if (width < 1 || height < 1 || width > 30000 || height > 30000) {
    host_.throw_js_error(ScriptEngineHost::tr("resizeImage needs a size between 1 and 30000."));
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  resize_image_and_layers(*document, width, height);
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::resizeCanvas(int width, int height) {
  if (width < 1 || height < 1 || width > 30000 || height > 30000) {
    host_.throw_js_error(ScriptEngineHost::tr("resizeCanvas needs a size between 1 and 30000."));
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  document->resize_canvas(width, height);
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::crop(int x, int y, int width, int height) {
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("crop needs a positive size."));
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  if (!crop_document(*document, Rect{x, y, width, height})) {
    host_.throw_js_error(ScriptEngineHost::tr("crop rectangle is outside the canvas."));
    return;
  }
  host_.note_structure_changed(session_id_);
}

bool ScriptDocumentObject::saveAs(const QString& path) {
  if (read_document() == nullptr) {
    return false;
  }
  return host_.save_session_to_path(session_id_, path);
}

bool ScriptDocumentObject::exportAs(const QString& path) { return saveAs(path); }

void ScriptDocumentObject::close() {
  if (read_document() == nullptr) {
    return;
  }
  host_.close_session(session_id_);
}

void ScriptDocumentObject::activate() {
  if (read_document() == nullptr) {
    return;
  }
  host_.activate_session(session_id_);
}

// ---------------------------------------------------------------------------
// ScriptAppObject

ScriptAppObject::ScriptAppObject(ScriptEngineHost& host) : host_(host) {}

QString ScriptAppObject::version() const { return QCoreApplication::applicationVersion(); }

QJSValue ScriptAppObject::documents() const {
  const auto ids = host_.session_ids();
  auto array = host_.engine()->newArray(static_cast<quint32>(ids.size()));
  quint32 index = 0;
  for (const auto id : ids) {
    array.setProperty(index++, make_document_value(host_, id));
  }
  return array;
}

QJSValue ScriptAppObject::active_document() const {
  const auto id = host_.active_session_id();
  return id != 0 ? make_document_value(host_, id) : QJSValue();
}

bool ScriptAppObject::undo_enabled() const { return host_.undo_enabled(); }

void ScriptAppObject::set_undo_enabled(bool enabled) { host_.set_undo_enabled(enabled); }

QJSValue ScriptAppObject::open(const QString& path) {
  const auto id = host_.open_document_file(path);
  if (id == 0) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Could not open %1").arg(QDir::toNativeSeparators(path)));
    return QJSValue();
  }
  return make_document_value(host_, id);
}

QJSValue ScriptAppObject::newDocument(int width, int height) {
  const auto id = host_.create_document(width, height);
  if (id == 0) {
    host_.throw_js_error(
        ScriptEngineHost::tr("newDocument needs a size between 1 and 30000."));
    return QJSValue();
  }
  return make_document_value(host_, id);
}

void ScriptAppObject::alert(const QString& text) { host_.show_alert(text); }

QJSValue ScriptAppObject::prompt(const QString& text, const QString& defaultValue) {
  bool accepted = false;
  const auto result = host_.show_prompt(text, defaultValue, &accepted);
  return accepted ? QJSValue(result) : QJSValue(QJSValue::NullValue);
}

QString ScriptAppObject::chooseFolder(const QString& title) { return host_.choose_folder(title); }

QString ScriptAppObject::chooseOpenFile(const QString& title, const QString& filter) {
  return host_.choose_open_file(title, filter);
}

QString ScriptAppObject::chooseSaveFile(const QString& title, const QString& filter) {
  return host_.choose_save_file(title, filter);
}

bool ScriptAppObject::runCommand(const QString& commandId) {
  return host_.run_app_command(commandId);
}

QStringList ScriptAppObject::commandIds() { return host_.app_command_ids(); }

// ---------------------------------------------------------------------------
// ScriptIoObject

ScriptIoObject::ScriptIoObject(ScriptEngineHost& host) : host_(host) {}

QString ScriptIoObject::readTextFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Could not read %1").arg(QDir::toNativeSeparators(path)));
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

void ScriptIoObject::writeTextFile(const QString& path, const QString& text) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
    return;
  }
  file.write(text.toUtf8());
}

QStringList ScriptIoObject::listFiles(const QString& dir, const QString& pattern) {
  const QDir directory(dir);
  if (!directory.exists()) {
    host_.throw_js_error(
        ScriptEngineHost::tr("listFiles: no such folder: %1").arg(QDir::toNativeSeparators(dir)));
    return {};
  }
  QStringList filters;
  if (!pattern.isEmpty()) {
    filters.append(pattern);
  }
  return directory.entryList(filters, QDir::Files | QDir::Readable,
                             QDir::Name | QDir::IgnoreCase);
}

bool ScriptIoObject::fileExists(const QString& path) { return QFileInfo(path).isFile(); }

double ScriptIoObject::fileSize(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    return -1;
  }
  return static_cast<double>(info.size());
}

bool ScriptIoObject::makeDir(const QString& path) { return !path.isEmpty() && QDir().mkpath(path); }

bool ScriptIoObject::deleteFile(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    return false;
  }
  return QFile::remove(info.filePath());
}

// ---------------------------------------------------------------------------
// ScriptUiObject

ScriptUiObject::ScriptUiObject(ScriptEngineHost& host) : host_(host) {}

QJSValue ScriptUiObject::createCanvas(const QJSValue& options) {
  int width = 640;
  int height = 480;
  QString title = ScriptEngineHost::tr("Script Window");
  if (options.isObject()) {
    const auto width_value = options.property(QStringLiteral("width"));
    if (width_value.isNumber()) {
      width = width_value.toInt();
    }
    const auto height_value = options.property(QStringLiteral("height"));
    if (height_value.isNumber()) {
      height = height_value.toInt();
    }
    const auto title_value = options.property(QStringLiteral("title"));
    if (title_value.isString()) {
      title = title_value.toString();
    }
  }
  width = std::clamp(width, 64, 4096);
  height = std::clamp(height, 64, 4096);
  // Before the window exists, never after: a window shown under the app-modal
  // stop panel is born blocked by it (docs/wasm.md).
  host_.dismiss_busy_indicator();
  auto* window = new ScriptCanvasWindow(host_, width, height, title);
  host_.adopt_canvas_window(window);
  return host_.engine()->newQObject(window);
}

QJSValue ScriptUiObject::showDialog(const QJSValue& spec) { return host_.show_form_dialog(spec); }

QJSValue ScriptUiObject::showOptions(const QJSValue& spec) {
  return host_.show_options_dialog(spec);
}

void ScriptUiObject::playTone(const QJSValue& frequency, const QJSValue& durationMs,
                              const QJSValue& volume, const QJSValue& wave) {
  host_.play_tone(frequency.isNumber() ? frequency.toNumber() : 880.0,
                  durationMs.isNumber() ? static_cast<int>(durationMs.toNumber()) : 120,
                  volume.isNumber() ? volume.toNumber() : 0.5,
                  wave.isString() ? wave.toString() : QStringLiteral("sine"));
}

void ScriptUiObject::playSound(const QString& path) { host_.play_sound_file(path); }

void ScriptUiObject::setWindowSize(int width, int height) { host_.set_window_size(width, height); }

void ScriptUiObject::setSidePanelWidth(int width) { host_.set_side_panel_width(width); }

void ScriptUiObject::setStatusMessage(const QString& message) { host_.set_status_message(message); }

bool ScriptUiObject::captureWindow(const QString& path) {
  if (path.trimmed().isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("captureWindow needs an output file path."));
    return false;
  }
  return host_.capture_window_to_file(path);
}

}  // namespace patchy::ui
