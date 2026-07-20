// The JS API wrapper objects (docs/scripting.md). Everything resolves through
// ScriptEngineHost services by session id + LayerId on every call: wrappers
// survive across event-loop turns while layers get deleted and the layers
// vector reallocates, so a stored pointer would be a use-after-free. Reads go
// through const documents (mutable layer accessors bump revisions on access);
// mutations run prepare_mutation() first so the run's single undo entry exists.

#include "ui/script_api.hpp"

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
constexpr std::array<const char*, 27> kBlendModeIds = {
    "pass-through", "normal",       "multiply",    "screen",       "overlay",
    "darken",       "lighten",      "color-dodge", "color-burn",   "hard-light",
    "soft-light",   "difference",   "linear-burn", "pin-light",    "saturation",
    "luminosity",   "exclusion",    "hue",         "color",        "linear-dodge",
    "subtract",     "divide",       "vivid-light", "linear-light", "hard-mix",
    "darker-color", "lighter-color"};

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
  host_.note_structure_changed(session_id_);
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
      params.size_pt = size.toNumber();
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
  auto* window = new ScriptCanvasWindow(host_, width, height, title);
  host_.adopt_canvas_window(window);
  return host_.engine()->newQObject(window);
}

QJSValue ScriptUiObject::showDialog(const QJSValue& spec) { return host_.show_form_dialog(spec); }

QJSValue ScriptUiObject::showOptions(const QJSValue& spec) {
  return host_.show_options_dialog(spec);
}

}  // namespace patchy::ui
