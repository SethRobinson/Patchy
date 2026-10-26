// The JS API wrapper objects (docs/scripting.md). Everything resolves through
// ScriptEngineHost services by session id + LayerId on every call: wrappers
// survive across event-loop turns while layers get deleted and the layers
// vector reallocates, so a stored pointer would be a use-after-free. Reads go
// through const documents (mutable layer accessors bump revisions on access);
// mutations run prepare_mutation() first so the run's single undo entry exists.

#include "ui/script_api.hpp"

#include "filters/gmic/gmic_engine.hpp"
#include "ui/gmic_library.hpp"

#include "core/artboard.hpp"
#include "core/document_depth.hpp"
#include "core/invisible_watermark.hpp"
#include "ai/trustmark_watermark.hpp"
#include "core/layer_metadata.hpp"
#include "core/trustmark_payload.hpp"
#include "ui/invisible_watermark_dialog.hpp"
#include "core/pixel_depth.hpp"
#include "ui/image_document_io.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "formats/animation_timing.hpp"
#include "formats/apng_writer.hpp"
#include "formats/webp_animation_io.hpp"

#include "ui/pdf_export.hpp"

#include "core/image_trace.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_shape.hpp"
#include "core/vector_raster.hpp"
#include "core/shape_combine.hpp"
#include "core/path_simplify.hpp"

#include "core/layer_metadata.hpp"
#include "core/pixel_grid.hpp"
#include "core/smart_object.hpp"
#include "formats/document_flatten.hpp"
#include "formats/document_trim.hpp"
#include "render/compositor.hpp"
#include "formats/palette_io.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_tools.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/display_hdr_info.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/document_recovery.hpp"
#include "ui/legacy_plugin_folder.hpp"
#include "ui/layer_merge.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/qt_paths.hpp"
#include "color/rgb_profiles.hpp"
#include "ui/qt_text_file.hpp"
#include "ui/script_canvas_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/script_save_options.hpp"
#include "ui/script_vector.hpp"
#include "ui/watermark_scan_report.hpp"
#include "core/text_area.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QRegion>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace patchy::ui {

namespace {

// The document size limit newDocument/resizeImage/resizeCanvas enforce; rect-shaped API
// arguments that size buffers or regions share it so oversized input is a JS error, never a
// bad_alloc escaping the engine.
constexpr int kMaxScriptDimension = 30000;

void promote_script_rgb_pixels(Layer& layer) {
  const auto& source = std::as_const(layer).pixels();
  if (source.format().channels != 3 || source.empty()) {
    return;
  }
  if (source.format().bit_depth != BitDepth::UInt8) {
    // 16/32-bit layers (docs/high-bit-depth.md) gain alpha at their depth.
    PixelBuffer rgba(source.width(), source.height(), with_bit_depth(PixelFormat::rgba8(), source.format().bit_depth));
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        store_pixel(rgba.format(), rgba.pixel(x, y), load_pixel(source.format(), source.pixel(x, y)));
      }
    }
    layer.set_pixels(std::move(rgba));
    return;
  }
  PixelBuffer rgba(source.width(), source.height(), PixelFormat::rgba8());
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      const auto* input = source.pixel(x, y);
      auto* output = rgba.pixel(x, y);
      std::copy_n(input, 3, output);
      output[3] = 255;
    }
  }
  layer.set_pixels(std::move(rgba));
}

// A script color as one RGBA pixel of `format`: 8-bit bytes as they are, 16-bit on the
// deep scale, 32-bit decoded to linear light (docs/high-bit-depth.md).
std::vector<std::uint8_t> script_fill_pixel(PixelFormat format, const QColor& color) {
  std::vector<std::uint8_t> bytes(bytes_per_pixel(format));
  std::array<float, 4> values{static_cast<float>(color.red()), static_cast<float>(color.green()),
                              static_cast<float>(color.blue()), static_cast<float>(color.alpha())};
  if (format.bit_depth == BitDepth::Float32) {
    for (std::size_t c = 0; c < 3U; ++c) {
      values[c] = static_cast<float>(srgb_decode(static_cast<double>(values[c]) / 255.0) * 255.0);
    }
  }
  store_pixel(format, bytes.data(), values);
  return bytes;
}

// A fresh RGBA buffer for an empty layer, at the document's depth.
PixelBuffer script_fresh_pixels(const Document* document, int width, int height) {
  const auto depth = document != nullptr ? document->color_state().bit_depth : BitDepth::UInt8;
  return PixelBuffer(width, height, with_bit_depth(PixelFormat::rgba8(), depth));
}

// Script-facing blend mode ids. Append-only and aligned with the BlendMode
// enum order (core/layer.hpp); scripts hard-code these strings.
constexpr std::array<const char*, 58> kBlendModeIds = {
    "pass-through", "normal",       "multiply",    "screen",       "overlay",
    "darken",       "lighten",      "color-dodge", "color-burn",   "hard-light",
    "soft-light",   "difference",   "linear-burn", "pin-light",    "saturation",
    "luminosity",   "exclusion",    "hue",         "color",        "linear-dodge",
    "subtract",     "divide",       "vivid-light", "linear-light", "hard-mix",
    "darker-color", "lighter-color", "dissolve",     "reflect",      "glow",
    "negation",     "xor",          "grain-extract", "grain-merge",
    "hsv-hue",
    "hsv-saturation",
    "hsl-color",
    "hsv-value",
    "lch-hue",
    "lch-chroma",
    "lch-color",
    "lch-lightness",
    "luminance",
    "luma-darken-only",
    "luma-lighten-only",
    "linear-multiply",
    "linear-addition",
    "linear-subtract",
    "linear-divide", "soft-light-svg", "additive-subtractive", "arc-tangent", "geometric-mean", "allanon", "parallel", "hard-overlay", "interpolation", "easy-dodge"};

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

namespace {

bool text_align_name_is_valid(const QString& align) {
  return align == QLatin1String("left") || align == QLatin1String("center") ||
         align == QLatin1String("right") || align == QLatin1String("justify");
}

// An array of {text, font?, size?, bold?, italic?, color?} objects (a bare string counts as a
// run with no overrides). False after throwing on a malformed run.
// Paragraph metrics object ({firstLineIndent, startIndent, endIndent, spaceBefore, spaceAfter},
// document pixels; a missing field stays unset so the setter leaves it alone).
bool parse_paragraph_metrics(ScriptEngineHost& host, const QJSValue& value, const char* verb,
                             TextParagraphMetrics* out) {
  if (!value.isObject() || value.isArray() || value.isCallable()) {
    host.throw_js_error(ScriptEngineHost::tr("%1: paragraph must be an object with firstLineIndent, startIndent, "
                                             "endIndent, spaceBefore and spaceAfter numbers (document pixels).")
                            .arg(QString::fromLatin1(verb)));
    return false;
  }
  const auto read = [&](const char* key, std::optional<double>* field) {
    const auto property = value.property(QString::fromLatin1(key));
    if (property.isUndefined() || property.isNull()) {
      return true;
    }
    if (!property.isNumber() || !std::isfinite(property.toNumber())) {
      host.throw_js_error(ScriptEngineHost::tr("%1: paragraph.%2 must be a number (document pixels).")
                              .arg(QString::fromLatin1(verb), QString::fromLatin1(key)));
      return false;
    }
    *field = property.toNumber();
    return true;
  };
  return read("firstLineIndent", &out->first_line_indent) && read("startIndent", &out->start_indent) &&
         read("endIndent", &out->end_indent) && read("spaceBefore", &out->space_before) &&
         read("spaceAfter", &out->space_after);
}

bool parse_text_runs(ScriptEngineHost& host, const QJSValue& value, const char* verb,
                     std::vector<ScriptEngineHost::TextRunParams>* runs) {
  if (!value.isArray()) {
    host.throw_js_error(ScriptEngineHost::tr("%1: runs must be an array of {text, font, size, bold, italic, color} objects.")
                            .arg(QLatin1String(verb)));
    return false;
  }
  const auto count = value.property(QStringLiteral("length")).toInt();
  for (int index = 0; index < count; ++index) {
    const auto item = value.property(static_cast<quint32>(index));
    ScriptEngineHost::TextRunParams run;
    if (item.isString()) {
      run.text = item.toString();
    } else if (item.isObject() && !item.isArray() && !item.isCallable()) {
      const auto text = item.property(QStringLiteral("text"));
      if (!text.isString()) {
        host.throw_js_error(ScriptEngineHost::tr("%1: run %2 needs a text string.").arg(QLatin1String(verb)).arg(index));
        return false;
      }
      run.text = text.toString();
      if (const auto font = item.property(QStringLiteral("font")); font.isString()) {
        run.family = font.toString();
      }
      if (const auto size = item.property(QStringLiteral("size")); size.isNumber()) {
        run.size_px = size.toNumber();
        if (!std::isfinite(run.size_px) || run.size_px <= 0.0) {
          host.throw_js_error(ScriptEngineHost::tr("%1: run %2 has a non-positive size.").arg(QLatin1String(verb)).arg(index));
          return false;
        }
      }
      if (const auto bold = item.property(QStringLiteral("bold")); bold.isBool()) {
        run.bold = bold.toBool();
      }
      if (const auto italic = item.property(QStringLiteral("italic")); italic.isBool()) {
        run.italic = italic.toBool();
      }
      if (const auto color = item.property(QStringLiteral("color")); color.isString()) {
        QColor parsed;
        if (!parse_color(host, color.toString(), &parsed)) {
          return false;
        }
        run.color = parsed;
      }
    } else {
      host.throw_js_error(ScriptEngineHost::tr("%1: run %2 must be a string or an object.").arg(QLatin1String(verb)).arg(index));
      return false;
    }
    runs->push_back(std::move(run));
  }
  if (runs->empty()) {
    host.throw_js_error(ScriptEngineHost::tr("%1: runs must not be empty.").arg(QLatin1String(verb)));
    return false;
  }
  return true;
}

}  // namespace


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

// Moves a layer subtree the way the Move tool commits a drag: the bounds, then
// the placement data translate_moved_layer_metadata carries (a linked mask,
// the shape model, a linked vector mask, smart-object quads, the text
// transform). That helper owns the linked mask, so nothing here shifts mask
// bounds by hand, and an unlinked mask stays where it is.
void offset_layer_recursive(Layer& layer, int dx, int dy, std::int32_t document_width,
                            std::int32_t document_height) {
  // An artboard's rect travels with its content (docs/artboards.md).
  layer.translate_artboard(dx, dy);
  if (const auto bounds = std::as_const(layer).bounds(); !bounds.empty()) {
    layer.set_bounds(Rect{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height});
  }
  translate_moved_layer_metadata(layer, dx, dy, document_width, document_height);
  for (auto& child : layer.children()) {
    offset_layer_recursive(child, dx, dy, document_width, document_height);
  }
}

// Smart objects in the subtree whose Smart Filter stack re-renders after a move.
void collect_smart_filter_rerender_ids(const Layer& layer, std::vector<LayerId>& ids) {
  if (move_layer_requires_smart_filter_rerender(layer)) {
    ids.push_back(layer.id());
  }
  for (const auto& child : layer.children()) {
    collect_smart_filter_rerender_ids(child, ids);
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

QString script_paint_mode_id(PaintMode mode) {
  switch (mode.kind) {
    case PaintModeKind::Behind:
      return QStringLiteral("behind");
    case PaintModeKind::Clear:
      return QStringLiteral("clear");
    case PaintModeKind::Blend:
      break;
  }
  return blend_mode_is_paint_mode(mode.blend) ? script_blend_mode_id(mode.blend) : QStringLiteral("normal");
}

bool script_paint_mode_from_id(const QString& id, PaintMode* mode) {
  if (id == QLatin1String("behind")) {
    *mode = PaintMode::behind();
    return true;
  }
  if (id == QLatin1String("clear")) {
    *mode = PaintMode::clear();
    return true;
  }
  BlendMode blend{};
  if (!script_blend_mode_from_id(id, &blend) || !blend_mode_is_paint_mode(blend)) {
    return false;
  }
  *mode = PaintMode::of(blend);
  return true;
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
  if (document == nullptr || document->find_layer(layer_id_) == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return nullptr;
  }
  if (!host_.prepare_mutation(session_id_)) {
    return nullptr;
  }
  // prepare_mutation snapshots the pre-edit document (a copy, so nothing moves), but its
  // busy indicator may pump user input while a script canvas window is open, and a click
  // in the Layers panel can delete or reorder layers meanwhile: resolve after it.
  document = host_.session_document(session_id_);
  auto* layer = document != nullptr ? document->find_layer(layer_id_) : nullptr;
  if (layer == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
    return nullptr;
  }
  return layer;
}

QString ScriptLayerObject::name() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? QString::fromStdString(layer->name()) : QString();
}

void ScriptLayerObject::set_name(const QString& name) {
  const ScriptApiCall api_call(host_);
  if (auto* layer = write_layer()) {
    layer->set_name(name.toStdString());
    host_.note_structure_changed(session_id_);
  }
}

double ScriptLayerObject::opacity() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? static_cast<double>(layer->opacity()) * 100.0 : 0.0;
}

void ScriptLayerObject::set_opacity(double opacity) {
  const ScriptApiCall api_call(host_);
  if (!std::isfinite(opacity)) {
    // std::clamp passes NaN straight through, and a NaN opacity renders undefined.
    host_.throw_js_error(ScriptEngineHost::tr("opacity needs a number between 0 and 100."));
    return;
  }
  if (auto* layer = write_layer()) {
    const auto before = to_qrect(layer_render_bounds(std::as_const(*layer)));
    layer->set_opacity(static_cast<float>(std::clamp(opacity, 0.0, 100.0) / 100.0));
    host_.note_pixels_changed(session_id_, before, false);
    host_.note_structure_changed(session_id_);
  }
}

bool ScriptLayerObject::visible() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr && layer->visible();
}

void ScriptLayerObject::set_visible(bool visible) {
  const ScriptApiCall api_call(host_);
  if (auto* layer = write_layer()) {
    layer->set_visible(visible);
    // set_visible deliberately does not bump revisions; repaint the layer's
    // reach and refresh the panel's eye toggle.
    host_.note_pixels_changed(session_id_, to_qrect(layer_render_bounds(std::as_const(*layer))), false);
    host_.note_structure_changed(session_id_);
  }
}

QString ScriptLayerObject::blend_mode() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? script_blend_mode_id(layer->blend_mode()) : QString();
}

void ScriptLayerObject::set_blend_mode(const QString& mode) {
  const ScriptApiCall api_call(host_);
  BlendMode parsed{};
  if (!script_blend_mode_from_id(mode, &parsed)) {
    host_.throw_js_error(ScriptEngineHost::tr("Unknown blend mode: %1").arg(mode));
    return;
  }
  if (const auto* document = host_.session_document_const(session_id_);
      document != nullptr && !blend_mode_supported_at_depth(parsed, document->color_state().bit_depth)) {
    host_.throw_js_error(ScriptEngineHost::tr("Blend mode %1 is not available in 32-bit documents.").arg(mode));
    return;
  }
  if (auto* layer = write_layer()) {
    layer->set_blend_mode(parsed);
    host_.note_pixels_changed(session_id_, to_qrect(layer_render_bounds(std::as_const(*layer))), false);
    host_.note_structure_changed(session_id_);
  }
}

bool ScriptLayerObject::locked() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr && (layer->lock_flags() & kLayerLockAll) != kLayerLockNone;
}

void ScriptLayerObject::set_locked(bool locked) {
  const ScriptApiCall api_call(host_);
  if (auto* layer = write_layer()) {
    layer->set_lock_flags((layer->lock_flags() & kLayerLockArtboardAutonest) | (locked ? kLayerLockAll : kLayerLockNone));
    host_.note_structure_changed(session_id_);
  }
}

QString ScriptLayerObject::color_label() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  const auto id = layer_color_label_id(layer != nullptr ? layer->color_label() : kLayerColorNone);
  return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}

void ScriptLayerObject::set_color_label(const QString& label) {
  const ScriptApiCall api_call(host_);
  const auto parsed = layer_color_label_from_id(label.toStdString());
  if (!parsed.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("Unknown layer color label: %1").arg(label));
    return;
  }
  if (auto* layer = write_layer()) {
    layer->set_color_label(*parsed);
    host_.note_structure_changed(session_id_);
  }
}

int ScriptLayerObject::x() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? script_layer_position_bounds(*layer).x : 0;
}

int ScriptLayerObject::y() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? script_layer_position_bounds(*layer).y : 0;
}

void ScriptLayerObject::set_x(double x) {
  const ScriptApiCall api_call(host_);
  const auto* current = read_layer();
  if (current != nullptr) {
    moveTo(x, script_layer_position_bounds(*current).y);
  }
}

void ScriptLayerObject::set_y(double y) {
  const ScriptApiCall api_call(host_);
  const auto* current = read_layer();
  if (current != nullptr) {
    moveTo(script_layer_position_bounds(*current).x, y);
  }
}

void ScriptLayerObject::transform(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (read_layer() == nullptr) {
    return;
  }
  if (!options.isObject() || options.isArray()) {
    host_.throw_js_error(ScriptEngineHost::tr("layer.transform needs {matrix} or {quad}, not both."));
    return;
  }
  for (QJSValueIterator it(options); it.hasNext();) {
    it.next();
    const auto key = it.name();
    if (key != QStringLiteral("matrix") && key != QStringLiteral("quad") && key != QStringLiteral("interpolation")) {
      host_.throw_js_error(ScriptEngineHost::tr("layer.transform: unknown option %1").arg(key));
      return;
    }
  }
  const auto present = [](const QJSValue& value) { return !value.isUndefined() && !value.isNull(); };
  const auto matrix_value = options.property(QStringLiteral("matrix"));
  const auto quad_value = options.property(QStringLiteral("quad"));
  if (present(matrix_value) == present(quad_value)) {
    host_.throw_js_error(ScriptEngineHost::tr("layer.transform needs {matrix} or {quad}, not both."));
    return;
  }
  const auto finite_number = [](const QJSValue& value, double& out) {
    if (!value.isNumber()) {
      return false;
    }
    out = value.toNumber();
    return std::isfinite(out) && std::abs(out) <= 1e7;
  };
  std::optional<std::array<double, 6>> matrix;
  std::optional<std::array<std::array<double, 2>, 4>> quad;
  if (present(matrix_value)) {
    std::array<double, 6> values{};
    bool valid = matrix_value.isArray() && matrix_value.property(QStringLiteral("length")).toInt() == 6;
    for (quint32 i = 0; valid && i < 6U; ++i) {
      valid = finite_number(matrix_value.property(i), values[i]);
    }
    if (!valid) {
      host_.throw_js_error(ScriptEngineHost::tr("layer.transform: matrix must be six finite numbers."));
      return;
    }
    matrix = values;
  } else {
    std::array<std::array<double, 2>, 4> points{};
    bool valid = quad_value.isArray() && quad_value.property(QStringLiteral("length")).toInt() == 4;
    for (quint32 i = 0; valid && i < 4U; ++i) {
      const auto point = quad_value.property(i);
      valid = point.isArray() && point.property(QStringLiteral("length")).toInt() == 2 &&
              finite_number(point.property(0U), points[i][0]) && finite_number(point.property(1U), points[i][1]);
    }
    if (!valid) {
      host_.throw_js_error(ScriptEngineHost::tr("layer.transform: quad must be four [x, y] points."));
      return;
    }
    quad = points;
  }
  std::optional<ResampleMethod> interpolation;
  if (const auto value = options.property(QStringLiteral("interpolation")); present(value)) {
    interpolation = parse_resample_method(value.toString().toStdString());
    if (!interpolation.has_value()) {
      host_.throw_js_error(ScriptEngineHost::tr("layer.transform: unknown interpolation %1").arg(value.toString()));
      return;
    }
  }
  host_.transform_layer(session_id_, layer_id_, matrix, quad, interpolation);
}

void ScriptLayerObject::moveTo(double x, double y) {
  const ScriptApiCall api_call(host_);
  const auto* current = read_layer();
  if (current == nullptr) {
    return;
  }
  const auto bounds = script_layer_position_bounds(*current);
  const auto valid_integer = [](double value) {
    return std::isfinite(value) && value >= std::numeric_limits<int>::min() &&
           value <= std::numeric_limits<int>::max();
  };
  // Photoshop's whole-pixel rule (halves round up), not truncation toward zero.
  const auto snapped_x = snap_to_pixel_grid(x);
  const auto snapped_y = snap_to_pixel_grid(y);
  if (!valid_integer(snapped_x) || !valid_integer(snapped_y) ||
      !valid_integer(snapped_x - bounds.x) || !valid_integer(snapped_y - bounds.y) ||
      !valid_integer(snapped_x + bounds.width) || !valid_integer(snapped_y + bounds.height)) {
    host_.throw_js_error(ScriptEngineHost::tr("Layer position is outside the supported range."));
    return;
  }
  const int dx = static_cast<int>(snapped_x) - bounds.x;
  const int dy = static_cast<int>(snapped_y) - bounds.y;
  if (dx == 0 && dy == 0) {
    return;
  }
  if (layer_is_artboard(*current)) {
    // The canvas grows to follow an artboard, within the Artboard tool's limits; checked
    // on a copy so a refusal changes nothing (docs/artboards.md).
    const auto* source = host_.session_document_const(session_id_);
    if (source == nullptr) {
      return;
    }
    auto probe = *source;
    for (auto& top : probe.layers()) {
      if (top.id() == layer_id_) {
        top.translate_artboard(dx, dy);
      }
    }
    if (!artboard_canvas_within_limit(probe)) {
      host_.throw_js_error(ScriptEngineHost::tr("Artboards cannot make the canvas larger than %1 x %1 px.")
                               .arg(kMaxArtboardCanvasSide));
      return;
    }
    if (artboard_canvas_would_grow(probe) &&
        (document_contains_smart_filters(probe) || document_contains_unparsed_smart_objects(probe))) {
      host_.throw_js_error(MainWindow::tr("Rasterize Smart Filters before changing document geometry"));
      return;
    }
  }
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    return;
  }
  // The Move tool's rule for Smart Filters: re-render at the new place, and put
  // the document back when a render fails.
  std::vector<LayerId> rerender_ids;
  collect_smart_filter_rerender_ids(std::as_const(*layer), rerender_ids);
  std::optional<Document> rollback;
  if (!rerender_ids.empty()) {
    rollback.emplace(std::as_const(*document));
  }
  const auto before = to_qrect(layer_render_bounds(std::as_const(*layer)));
  offset_layer_recursive(*layer, dx, dy, document->width(), document->height());
  for (const auto id : rerender_ids) {
    if (!host_.rerender_moved_smart_filters(session_id_, id)) {
      *document = std::move(*rollback);
      host_.throw_js_error(MainWindow::tr("Could not rebuild the Smart Filter preview and cache"));
      return;
    }
  }
  const auto* moved = std::as_const(*document).find_layer(layer_id_);
  if (moved != nullptr && layer_is_artboard(*moved)) {
    // An artboard moved past the canvas edge grows the canvas, as in the Artboard tool.
    if (!std::as_const(*document).metadata().artboard_settings.has_value()) {
      document->metadata().artboard_settings.emplace();
    }
    static_cast<void>(grow_canvas_to_artboards(*document));
    host_.note_structure_changed(session_id_);
    return;
  }
  const auto after = moved != nullptr ? to_qrect(layer_render_bounds(*moved)) : before;
  host_.note_pixels_changed(session_id_, before.united(after));
}

QJSValue ScriptLayerObject::bounds() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr ? rect_to_js(host_.engine(), script_layer_position_bounds(*layer)) : QJSValue();
}

bool ScriptLayerObject::is_group() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr && layer->kind() == LayerKind::Group;
}

bool ScriptLayerObject::is_text() const {
  const ScriptApiCall api_call(host_);
  return host_.layer_is_text_layer(session_id_, layer_id_);
}

bool ScriptLayerObject::is_smart_object() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  return layer != nullptr && layer_is_smart_object(*layer);
}

QJSValue ScriptLayerObject::getSmartObject() const {
  const ScriptApiCall api_call(host_);
  if (read_layer() == nullptr) {
    return QJSValue();
  }
  const auto info = host_.smart_object_info(session_id_, layer_id_);
  if (!info.has_value()) {
    return QJSValue(QJSValue::NullValue);
  }
  auto object = host_.engine()->newObject();
  object.setProperty(QStringLiteral("linked"), info->linked);
  object.setProperty(QStringLiteral("fileName"), info->file_name);
  object.setProperty(QStringLiteral("path"), info->path);
  object.setProperty(QStringLiteral("relativePath"), info->relative_path);
  object.setProperty(QStringLiteral("missing"), info->missing);
  object.setProperty(QStringLiteral("changed"), info->changed);
  object.setProperty(QStringLiteral("sourceId"), info->source_id);
  object.setProperty(QStringLiteral("width"), info->width);
  object.setProperty(QStringLiteral("height"), info->height);
  object.setProperty(QStringLiteral("resolution"), info->resolution);
  auto quad = host_.engine()->newArray(static_cast<quint32>(info->quad.size()));
  for (quint32 i = 0; i < info->quad.size(); ++i) {
    quad.setProperty(i, info->quad[i]);
  }
  object.setProperty(QStringLiteral("quad"), quad);
  return object;
}

int ScriptLayerObject::updateSmartObject() {
  const ScriptApiCall api_call(host_);
  if (read_layer() == nullptr) {
    return 0;
  }
  QString error;
  const auto updated = host_.update_smart_object(session_id_, layer_id_, &error);
  if (updated == 0 && !error.isEmpty()) {
    host_.throw_js_error(error);
  }
  return updated;
}

int ScriptLayerObject::rerenderSmartObject() {
  const ScriptApiCall api_call(host_);
  if (read_layer() == nullptr) {
    return 0;
  }
  QString error;
  const auto rendered = host_.rerender_smart_object(session_id_, layer_id_, &error);
  if (rendered == 0 && !error.isEmpty()) {
    host_.throw_js_error(error);
  }
  return rendered;
}

QJSValue ScriptLayerObject::children() const {
  const ScriptApiCall api_call(host_);
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
  const ScriptApiCall api_call(host_);
  return host_.text_layer_text(session_id_, layer_id_);
}

void ScriptLayerObject::set_text(const QString& text) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (!host_.set_text_layer_text(session_id_, layer_id_, text)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

QString ScriptLayerObject::text_orientation() const {
  const ScriptApiCall api_call(host_);
  return host_.text_layer_orientation(session_id_, layer_id_);
}

void ScriptLayerObject::set_text_orientation(const QString& orientation) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (orientation != QLatin1String("horizontal") && orientation != QLatin1String("vertical")) {
    host_.throw_js_error(ScriptEngineHost::tr("textOrientation must be 'horizontal' or 'vertical'."));
    return;
  }
  if (!host_.set_text_layer_orientation(session_id_, layer_id_, orientation)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

QString ScriptLayerObject::text_direction() const {
  const ScriptApiCall api_call(host_);
  return host_.text_layer_direction(session_id_, layer_id_);
}

QString ScriptLayerObject::text_font() const {
  const ScriptApiCall api_call(host_);
  return host_.text_layer_font(session_id_, layer_id_);
}

QJSValue ScriptLayerObject::text_runs() const {
  const ScriptApiCall api_call(host_);
  const auto runs = host_.text_layer_runs(session_id_, layer_id_);
  auto array = host_.engine()->newArray(static_cast<quint32>(runs.size()));
  quint32 index = 0;
  for (const auto& run : runs) {
    auto object = host_.engine()->newObject();
    object.setProperty(QStringLiteral("text"), run.text);
    object.setProperty(QStringLiteral("font"), run.family);
    object.setProperty(QStringLiteral("style"), run.style);
    object.setProperty(QStringLiteral("size"), run.size);
    object.setProperty(QStringLiteral("bold"), run.bold);
    object.setProperty(QStringLiteral("italic"), run.italic);
    object.setProperty(QStringLiteral("color"), run.color);
    array.setProperty(index++, object);
  }
  return array;
}

QJSValue ScriptLayerObject::text_box() const {
  const ScriptApiCall api_call(host_);
  const auto box = host_.text_layer_box(session_id_, layer_id_);
  if (!box.isValid()) {
    return QJSValue(QJSValue::NullValue);
  }
  auto object = host_.engine()->newObject();
  object.setProperty(QStringLiteral("width"), box.width());
  object.setProperty(QStringLiteral("height"), box.height());
  return object;
}

QJSValue ScriptLayerObject::text_area() const {
  return script_vector::guarded(host_, [&]() -> QJSValue {
    const auto& layer = script_vector::layer(host_, session_id_, layer_id_);
    const auto area = text_area_in_document(layer);
    return area ? script_vector::to_js(host_, script_vector::path_json(*area)) : QJSValue(QJSValue::NullValue);
  });
}

void ScriptLayerObject::set_text_area(const QJSValue& value) {
  script_vector::guarded(host_, [&] {
    std::optional<VectorPath> area;
    if (!value.isNull()) {
      area = script_vector::parse_path(script_vector::object(value), false);
      if (!valid_text_area(*area)) script_vector::invalid(QStringLiteral("textArea.closedContour"));
    }
    const auto& layer = script_vector::layer(host_, session_id_, layer_id_, true);
    if (!layer_is_text(layer) || text_geometry_is_protected(layer)) script_vector::invalid(QStringLiteral("textArea.layer"));
    if (!host_.set_text_layer_area(session_id_, layer_id_, area)) script_vector::invalid(QStringLiteral("textArea"));
  });
}

QString ScriptLayerObject::text_align() const {
  const ScriptApiCall api_call(host_);
  return host_.text_layer_align(session_id_, layer_id_);
}

void ScriptLayerObject::set_text_align(const QString& align) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (!text_align_name_is_valid(align)) {
    host_.throw_js_error(ScriptEngineHost::tr("textAlign must be 'left', 'center', 'right' or 'justify'."));
    return;
  }
  if (!host_.set_text_layer_align(session_id_, layer_id_, align)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

QJSValue ScriptLayerObject::text_paragraph() const {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    return QJSValue(QJSValue::NullValue);
  }
  const auto metrics = host_.text_layer_paragraph(session_id_, layer_id_);
  auto object = host_.engine()->newObject();
  object.setProperty(QStringLiteral("firstLineIndent"), metrics.first_line_indent.value_or(0.0));
  object.setProperty(QStringLiteral("startIndent"), metrics.start_indent.value_or(0.0));
  object.setProperty(QStringLiteral("endIndent"), metrics.end_indent.value_or(0.0));
  object.setProperty(QStringLiteral("spaceBefore"), metrics.space_before.value_or(0.0));
  object.setProperty(QStringLiteral("spaceAfter"), metrics.space_after.value_or(0.0));
  return object;
}

void ScriptLayerObject::set_text_paragraph(const QJSValue& paragraph) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  TextParagraphMetrics metrics;
  if (!parse_paragraph_metrics(host_, paragraph, "textParagraph", &metrics)) {
    return;
  }
  if (!host_.set_text_layer_paragraph(session_id_, layer_id_, metrics)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

void ScriptLayerObject::setTextRuns(const QJSValue& runs) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  std::vector<ScriptEngineHost::TextRunParams> parsed;
  if (!parse_text_runs(host_, runs, "setTextRuns", &parsed)) {
    return;
  }
  if (!host_.set_text_layer_runs(session_id_, layer_id_, parsed)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

void ScriptLayerObject::rerenderText() {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (!host_.rerender_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

void ScriptLayerObject::set_text_direction(const QString& direction) {
  const ScriptApiCall api_call(host_);
  if (!host_.layer_is_text_layer(session_id_, layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("This layer is not a text layer."));
    return;
  }
  if (direction != QLatin1String("auto") && direction != QLatin1String("ltr") && direction != QLatin1String("rtl")) {
    host_.throw_js_error(ScriptEngineHost::tr("textDirection must be 'auto', 'ltr' or 'rtl'."));
    return;
  }
  if (!host_.set_text_layer_direction(session_id_, layer_id_, direction)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not edit the text layer."));
  }
}

QJSValue ScriptLayerObject::duplicate(const QJSValue& target) {
  const ScriptApiCall api_call(host_);
  if (!target.isUndefined() && !target.isNull()) {
    const auto* wrapper = qobject_cast<ScriptDocumentObject*>(target.toQObject());
    if (wrapper == nullptr) {
      host_.throw_js_error(ScriptEngineHost::tr("duplicate needs an open document as its target."));
      return QJSValue();
    }
    if (wrapper->session_id() != session_id_) {
      // Another document: the copy lands above its active layer at the same
      // coordinates (centered when the sizes differ) and belongs to it.
      QString error;
      const auto root_ids =
          host_.duplicate_layers_to_session(session_id_, {layer_id_}, wrapper->session_id(), &error);
      if (root_ids.empty()) {
        host_.throw_js_error(error.isEmpty() ? ScriptEngineHost::tr("The document is no longer open.") : error);
        return QJSValue();
      }
      return make_layer_value(host_, wrapper->session_id(), root_ids.front());
    }
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
  Layer copy = clone_layer_with_fresh_ids(*document, (*parent)[index]);
  copy.set_name(copy.name() + " copy");
  const auto copy_id = copy.id();
  parent->insert(parent->begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
  host_.note_structure_changed(session_id_);
  return make_layer_value(host_, session_id_, copy_id);
}

void ScriptLayerObject::remove() {
  const ScriptApiCall api_call(host_);
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
  const ScriptApiCall api_call(host_);
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
  const ScriptApiCall api_call(host_);
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
    layer->set_pixels(script_fresh_pixels(document, box.width(), box.height()));
    layer->set_bounds(Rect{box.x(), box.y(), box.width(), box.height()});
  }
  const auto bounds = std::as_const(*layer).bounds();
  promote_script_rgb_pixels(*layer);
  auto& pixels = layer->pixels();
  if (pixels.format().channels != 4) {
    host_.throw_js_error(ScriptEngineHost::tr("fill supports 8-bit RGB and RGBA layers only."));
    return;
  }
  const auto fill_pixel = script_fill_pixel(pixels.format(), parsed);
  // A soft selection (feather, Object Selection edge) blends the fill in by its coverage.
  const auto unfilled = host_.selection_has_soft_coverage(session_id_) ? std::optional<PixelBuffer>(pixels)
                                                                       : std::nullopt;
  for (const QRect& rect : target) {
    const QRect layer_rect =
        rect.intersected(QRect(bounds.x, bounds.y, pixels.width(), pixels.height()));
    for (int y = layer_rect.top(); y <= layer_rect.bottom(); ++y) {
      for (int x = layer_rect.left(); x <= layer_rect.right(); ++x) {
        std::copy(fill_pixel.begin(), fill_pixel.end(), pixels.pixel(x - bounds.x, y - bounds.y));
      }
    }
  }
  if (unfilled.has_value()) {
    host_.blend_by_selection_coverage(session_id_, pixels, *unfilled, bounds);
  }
  host_.note_pixels_changed(session_id_, target.boundingRect());
}

// Partial in-place write: overwrites RGBA (a transparent color clears) inside
// the given document-space rect, clipped to the layer's buffer. An empty layer
// allocates a buffer covering exactly the rect, so tiny sprite layers can be
// created with one call and then animated via x/y (much cheaper per frame than
// re-uploading pixels). Palette mode snaps like every tool write.
void ScriptLayerObject::fillRect(int x, int y, int width, int height, const QString& color) {
  const ScriptApiCall api_call(host_);
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("fillRect needs a positive size."));
    return;
  }
  if (width > kMaxScriptDimension || height > kMaxScriptDimension) {
    // On an empty layer the rect sizes a fresh buffer; a bad_alloc would not be a JS error.
    host_.throw_js_error(
        ScriptEngineHost::tr("fillRect needs a size between 1 and %1.").arg(kMaxScriptDimension));
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
    layer->set_pixels(script_fresh_pixels(host_.session_document_const(session_id_), width, height));
    layer->set_bounds(Rect{x, y, width, height});
  }
  const auto bounds = std::as_const(*layer).bounds();
  promote_script_rgb_pixels(*layer);
  auto& pixels = layer->pixels();
  if (pixels.format().channels != 4) {
    host_.throw_js_error(ScriptEngineHost::tr("fillRect supports 8-bit RGB and RGBA layers only."));
    return;
  }
  const auto fill_pixel = script_fill_pixel(pixels.format(), parsed);
  const QRect target = QRect(x, y, width, height)
                           .intersected(QRect(bounds.x, bounds.y, pixels.width(), pixels.height()));
  if (target.isEmpty()) {
    return;
  }
  for (int py = target.top(); py <= target.bottom(); ++py) {
    for (int px = target.left(); px <= target.right(); ++px) {
      std::copy(fill_pixel.begin(), fill_pixel.end(), pixels.pixel(px - bounds.x, py - bounds.y));
    }
  }
  host_.note_pixels_changed(session_id_, target);
}

void ScriptLayerObject::applyFilter(const QString& filterId, const QJSValue& params) {
  const ScriptApiCall api_call(host_);
  host_.apply_filter_to_layer(session_id_, layer_id_, filterId, params);
}

void ScriptLayerObject::applyCameraRawFilter(const QJSValue& settings) {
  const ScriptApiCall api_call(host_);
  host_.apply_camera_raw_filter_to_layer(session_id_, layer_id_, settings);
}

void ScriptLayerObject::applyPlugin(const QString& pluginId, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  bool dialog = true;
  QString capture_path;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      if (it.name() == QLatin1String("dialog")) {
        dialog = it.value().toBool();
      } else if (it.name() == QLatin1String("captureDialog")) {
        capture_path = it.value().toString();
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("applyPlugin: unknown option %1.").arg(it.name()));
        return;
      }
    }
  }
  host_.apply_legacy_plugin_to_layer(session_id_, layer_id_, pluginId, dialog, capture_path);
}

QJSValue ScriptLayerObject::applyGmic(const QString& filterId, const QJSValue& values, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  return host_.apply_gmic_to_layer(session_id_, layer_id_, filterId, values, options);
}

// Remove Object through the session's canvas, which is why the layer has to
// be the active one: the fill writes through the canvas's edit target.
// `method` is "contentAware" (default, the exhaustive exemplar fill) or
// "nearestEdge" (the shape-derived mirror); `attempt` (0-based, wrapping)
// picks the nearest-edge candidate, otherwise the canvas's repeat cycle applies.
QJSValue ScriptLayerObject::removeObject(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  int attempt = -1;
  int tone_match = 0;
  int feather = 0;
  bool content_aware = true;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      if (it.name() == QLatin1String("attempt")) {
        attempt = std::max(0, it.value().toInt());
      } else if (it.name() == QLatin1String("toneMatch")) {
        tone_match = std::clamp(it.value().toInt(), 0, 100);
      } else if (it.name() == QLatin1String("feather")) {
        feather = std::clamp(it.value().toInt(), 0, 250);
      } else if (it.name() == QLatin1String("method")) {
        const auto method = it.value().toString();
        if (method == QLatin1String("contentAware")) {
          content_aware = true;
        } else if (method == QLatin1String("nearestEdge")) {
          content_aware = false;
        } else {
          host_.throw_js_error(ScriptEngineHost::tr("removeObject: method must be contentAware or nearestEdge."));
          return QJSValue();
        }
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("removeObject: unknown option %1.").arg(it.name()));
        return QJSValue();
      }
    }
  }
  bool used_content_aware = false;
  int source = 0;
  int source_count = 0;
  std::int64_t patches = 0;
  int attempt_used = 0;
  if (!host_.remove_object_in_selection(session_id_, layer_id_, content_aware, attempt, tone_match, feather,
                                        &used_content_aware, &source, &source_count, &patches, &attempt_used)) {
    return QJSValue();
  }
  auto result = host_.engine()->newObject();
  result.setProperty(QStringLiteral("method"),
                     used_content_aware ? QStringLiteral("contentAware") : QStringLiteral("nearestEdge"));
  result.setProperty(QStringLiteral("patches"), static_cast<double>(patches));
  result.setProperty(QStringLiteral("source"), source);
  result.setProperty(QStringLiteral("sourceCount"), source_count);
  result.setProperty(QStringLiteral("attempt"), attempt_used);
  return result;
}

// The Red Eye tool through the session's canvas (the layer must be the active one): a click
// at {x, y}, or a drag box when width and height are given too.
QJSValue ScriptLayerObject::redEye(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (!options.isObject()) {
    host_.throw_js_error(ScriptEngineHost::tr("redEye: pass {x, y} or {x, y, width, height}."));
    return QJSValue();
  }
  std::optional<int> x;
  std::optional<int> y;
  std::optional<int> width;
  std::optional<int> height;
  int pupil_size = 50;
  int darken_amount = 50;
  QJSValueIterator it(options);
  while (it.hasNext()) {
    it.next();
    const auto key = it.name();
    const auto value = it.value();
    if (!value.isNumber()) {
      host_.throw_js_error(ScriptEngineHost::tr("redEye: %1 must be a number.").arg(key));
      return QJSValue();
    }
    const auto number = static_cast<int>(std::lround(value.toNumber()));
    if (key == QLatin1String("x")) {
      x = number;
    } else if (key == QLatin1String("y")) {
      y = number;
    } else if (key == QLatin1String("width")) {
      width = number;
    } else if (key == QLatin1String("height")) {
      height = number;
    } else if (key == QLatin1String("pupilSize")) {
      pupil_size = std::clamp(number, 1, 100);
    } else if (key == QLatin1String("darkenAmount")) {
      darken_amount = std::clamp(number, 1, 100);
    } else {
      host_.throw_js_error(ScriptEngineHost::tr("redEye: unknown option %1.").arg(key));
      return QJSValue();
    }
  }
  if (!x.has_value() || !y.has_value() || width.has_value() != height.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("redEye: pass {x, y} or {x, y, width, height}."));
    return QJSValue();
  }
  std::optional<QRect> box;
  QPoint point(*x, *y);
  if (width.has_value()) {
    if (*width < 1 || *height < 1) {
      host_.throw_js_error(ScriptEngineHost::tr("redEye: width and height must be at least 1."));
      return QJSValue();
    }
    box = QRect(*x, *y, *width, *height);
    point = box->center();
  }
  std::int64_t pupil_pixels = 0;
  QRect changed;
  if (!host_.red_eye(session_id_, layer_id_, point, box, pupil_size, darken_amount, &pupil_pixels, &changed)) {
    return QJSValue();
  }
  auto result = host_.engine()->newObject();
  result.setProperty(QStringLiteral("pupilPixels"), static_cast<double>(pupil_pixels));
  result.setProperty(QStringLiteral("x"), changed.x());
  result.setProperty(QStringLiteral("y"), changed.y());
  result.setProperty(QStringLiteral("width"), changed.width());
  result.setProperty(QStringLiteral("height"), changed.height());
  return result;
}

QJSValue ScriptLayerObject::traceToShapes(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
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
      } else if (key == QLatin1String("mergeColors")) {
        trace_options.merge_colors = value.toInt();
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
  const ScriptApiCall api_call(host_);
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
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  if (layer == nullptr) {
    return QJSValue();
  }
  const auto& stored = layer->pixels();
  // 16/32-bit layers read as their display values, RGBA8 like every other layer
  // (docs/high-bit-depth.md).
  const auto pixels = stored.format().bit_depth != BitDepth::UInt8 && stored.format().channels >= 3
                          ? convert_pixel_buffer_depth(stored, BitDepth::UInt8, SampleKind::Color)
                          : stored;
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
  const ScriptApiCall api_call(host_);
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
  // A 16/32-bit document takes the RGBA8 data at its depth.
  if (const auto* document = host_.session_document_const(session_id_);
      document != nullptr && document->color_state().bit_depth != BitDepth::UInt8) {
    pixels = convert_pixel_buffer_depth(pixels, document->color_state().bit_depth, SampleKind::Color);
  }
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

bool ScriptSelectionObject::exists() const { const ScriptApiCall api_call(host_); return host_.has_selection(session_id_); }

QJSValue ScriptSelectionObject::bounds() const {
  const ScriptApiCall api_call(host_);
  const auto region = host_.selection_region(session_id_);
  if (region.isEmpty()) {
    return QJSValue();
  }
  const auto rect = region.boundingRect();
  return rect_to_js(host_.engine(),
                    Rect{rect.x(), rect.y(), rect.width(), rect.height()});
}

void ScriptSelectionObject::selectAll() { const ScriptApiCall api_call(host_); host_.select_all(session_id_); }

void ScriptSelectionObject::deselect() { const ScriptApiCall api_call(host_); host_.deselect(session_id_); }

void ScriptSelectionObject::selectRect(int x, int y, int width, int height) {
  const ScriptApiCall api_call(host_);
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("selectRect needs a positive size."));
    return;
  }
  if (width > kMaxScriptDimension || height > kMaxScriptDimension) {
    host_.throw_js_error(
        ScriptEngineHost::tr("selectRect needs a size between 1 and %1.").arg(kMaxScriptDimension));
    return;
  }
  host_.select_region(session_id_, QRegion(x, y, width, height));
}

void ScriptSelectionObject::selectEllipse(int x, int y, int width, int height) {
  const ScriptApiCall api_call(host_);
  if (width < 1 || height < 1) {
    host_.throw_js_error(ScriptEngineHost::tr("selectEllipse needs a positive size."));
    return;
  }
  if (width > kMaxScriptDimension || height > kMaxScriptDimension) {
    // QRegion scan-converts the whole ellipse; an unbounded size is a hang or a bad_alloc.
    host_.throw_js_error(
        ScriptEngineHost::tr("selectEllipse needs a size between 1 and %1.").arg(kMaxScriptDimension));
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

QJSValue ScriptDocumentObject::getPalette() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (!document || (!document->palette_editing() && !document->indexed_palette())) {
    return QJSValue(QJSValue::NullValue);
  }
  const auto& editing = document->palette_editing();
  const auto& colors = editing ? editing->palette.colors : document->indexed_palette()->colors;
  auto result = host_.engine()->newObject();
  auto values = host_.engine()->newArray(static_cast<quint32>(colors.size()));
  for (quint32 i = 0; i < static_cast<quint32>(colors.size()); ++i) {
    const auto& c = colors[i];
    values.setProperty(i, QColor(c.red, c.green, c.blue).name());
  }
  result.setProperty(QStringLiteral("colors"), values);
  const auto& names = editing ? editing->palette.names : document->indexed_palette()->names;
  auto labels = host_.engine()->newArray(static_cast<quint32>(colors.size()));
  for (quint32 i = 0; i < static_cast<quint32>(colors.size()); ++i) {
    labels.setProperty(i, i < names.size() ? QString::fromUtf8(names[i]) : QString());
  }
  result.setProperty(QStringLiteral("names"), labels);
  result.setProperty(QStringLiteral("enabled"), editing.has_value());
  result.setProperty(QStringLiteral("alphaThreshold"), editing ? QJSValue(editing->alpha_threshold)
                                                              : QJSValue(QJSValue::NullValue));
  result.setProperty(QStringLiteral("sourceBitDepth"), document->indexed_palette()
      ? document->indexed_palette()->source_bit_depth : 0);
  return result;
}

void ScriptDocumentObject::setPalette(const QJSValue& values, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (!read_document()) { return; }
  const auto count = values.property(QStringLiteral("length")).toUInt();
  if (!values.isArray() || count == 0 || count > 256) {
    host_.throw_js_error(ScriptEngineHost::tr("A palette needs 1 to 256 opaque colors."));
    return;
  }
  std::vector<RgbColor> colors;
  colors.reserve(count);
  for (quint32 i = 0; i < count; ++i) {
    const auto value = values.property(i);
    const QColor color(value.isString() ? value.toString() : QString());
    if (!color.isValid() || color.alpha() != 255) {
      host_.throw_js_error(ScriptEngineHost::tr("A palette needs 1 to 256 opaque colors."));
      return;
    }
    colors.push_back({static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                      static_cast<std::uint8_t>(color.blue())});
  }
  bool enabled = true;
  int threshold = 128;
  std::vector<std::string> names;
  if (!options.isUndefined()) {
    if (!options.isObject() || options.isArray() || options.isNull()) {
      host_.throw_js_error(ScriptEngineHost::tr("Palette options must be an object."));
      return;
    }
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      if (it.name() == QStringLiteral("names") && it.value().isArray() &&
          it.value().property(QStringLiteral("length")).toUInt() == count) {
        for (quint32 i = 0; i < count; ++i) {
          const auto value = it.value().property(i);
          const auto name = value.toString().toUtf8();
          if (!value.isString() || name.size() > static_cast<qsizetype>(kMaxPaletteColorNameBytes) ||
              name.contains('\n') || name.contains('\r') || name.contains('\0')) {
            host_.throw_js_error(ScriptEngineHost::tr("Palette names must be single lines of at most 4096 UTF-8 bytes."));
            return;
          }
          names.emplace_back(name.constData(), static_cast<std::size_t>(name.size()));
        }
      } else if (it.name() == QStringLiteral("enabled") && it.value().isBool()) {
        enabled = it.value().toBool();
      } else if (it.name() == QStringLiteral("alphaThreshold") && it.value().isNumber()) {
        const auto value = it.value().toNumber();
        if (!std::isfinite(value) || value < 0 || value > 255 || std::floor(value) != value) {
          host_.throw_js_error(ScriptEngineHost::tr("Palette alphaThreshold must be an integer from 0 to 255."));
          return;
        }
        threshold = static_cast<int>(value);
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("Unknown or invalid palette option: %1").arg(it.name()));
        return;
      }
    }
  }
  (void)host_.set_session_palette(session_id_, std::move(colors), enabled, static_cast<std::uint8_t>(threshold),
                                std::move(names));
}

QJSValue ScriptDocumentObject::loadPalette(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (!read_document()) { return QJSValue(); }
  palette_io::PaletteFileData loaded;
  try {
    loaded = palette_io::read_palette_file(to_filesystem_path(path));
  } catch (const std::exception& error) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not load palette: %1").arg(QObject::tr(error.what())));
    return QJSValue();
  }
  auto colors = host_.engine()->newArray(static_cast<quint32>(loaded.colors.size()));
  for (quint32 i = 0; i < static_cast<quint32>(loaded.colors.size()); ++i) {
    const auto& c = loaded.colors[i];
    colors.setProperty(i, QColor(c.red, c.green, c.blue).name());
  }
  // Preserve the file's labels unless the caller explicitly supplies replacements.
  auto effective_options = options;
  if (effective_options.isUndefined()) { effective_options = host_.engine()->newObject(); }
  if (effective_options.isObject() && !effective_options.isArray() &&
      !effective_options.hasProperty(QStringLiteral("names"))) {
    auto copy = host_.engine()->newObject();
    QJSValueIterator it(effective_options);
    while (it.hasNext()) { it.next(); copy.setProperty(it.name(), it.value()); }
    auto names = host_.engine()->newArray(static_cast<quint32>(loaded.colors.size()));
    for (quint32 i = 0; i < static_cast<quint32>(loaded.colors.size()); ++i) {
      names.setProperty(i, i < loaded.names.size() ? QString::fromUtf8(loaded.names[i]) : QString());
    }
    copy.setProperty(QStringLiteral("names"), names);
    effective_options = copy;
  }
  setPalette(colors, effective_options);
  return getPalette();
}

bool ScriptDocumentObject::savePalette(const QString& path, const QString& name) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (!document) { return false; }
  if (!document->palette_editing() && !document->indexed_palette()) {
    host_.throw_js_error(ScriptEngineHost::tr("The document has no palette."));
    return false;
  }
  const auto extension = QFileInfo(path).suffix().toLower().toUtf8();
  const auto format = palette_io::palette_format_for_extension(extension.constData());
  if (!format) {
    host_.throw_js_error(ScriptEngineHost::tr("Use .pal, .gpl, .hex, .act, or .aco to save a palette."));
    return false;
  }
  const auto& editing = document->palette_editing();
  const auto& colors = editing ? editing->palette.colors : document->indexed_palette()->colors;
  try {
    const auto& names = editing ? editing->palette.names : document->indexed_palette()->names;
    palette_io::write_palette_file(to_filesystem_path(path), colors, *format, name.toUtf8().constData(), names);
  } catch (const std::exception& error) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not save palette: %1").arg(QObject::tr(error.what())));
    return false;
  }
  return true;
}

int ScriptDocumentObject::width() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  return document != nullptr ? document->width() : 0;
}

int ScriptDocumentObject::height() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  return document != nullptr ? document->height() : 0;
}

QString ScriptDocumentObject::name() const { const ScriptApiCall api_call(host_); return host_.session_title(session_id_); }

QString ScriptDocumentObject::path() const { const ScriptApiCall api_call(host_); return host_.session_file_path(session_id_); }

// The reader's notes from the open that made this document (what it dropped or
// approximated), as the status bar shows them; empty for a new document. A plain array
// copy: a QStringList property would reach scripts as a live sequence that empties once the
// document closes.
QJSValue ScriptDocumentObject::import_notes() const {
  const ScriptApiCall api_call(host_);
  const auto notes = host_.session_import_notes(session_id_);
  auto array = host_.engine()->newArray(static_cast<quint32>(notes.size()));
  for (qsizetype i = 0; i < notes.size(); ++i) {
    array.setProperty(static_cast<quint32>(i), notes.at(i));
  }
  return array;
}

// Image > Mode > CMYK Proof (docs/cmyk.md). Refused (a JS error) in palette mode or
// without a CMYK profile, like the menu command.
bool ScriptDocumentObject::cmyk_proof() const {
  const ScriptApiCall api_call(host_);
  return read_document() != nullptr && host_.session_cmyk_proof(session_id_);
}

void ScriptDocumentObject::set_cmyk_proof(bool enabled) {
  const ScriptApiCall api_call(host_);
  if (read_document() == nullptr) {
    return;
  }
  if (const auto refusal = host_.set_session_cmyk_proof(session_id_, enabled); !refusal.isEmpty()) {
    host_.throw_js_error(refusal);
  }
}

QString ScriptDocumentObject::color_profile() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return {};
  }
  return QString::fromStdString(document_rgb_profile_name(document->color_state().embedded_icc_profile));
}

int ScriptDocumentObject::bit_depth() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return 0;
  }
  const auto depth = document->color_state().bit_depth;
  return depth == BitDepth::Float32 ? 32 : depth == BitDepth::UInt16 ? 16 : 8;
}

namespace {

// fileInfo keys (permanent scripting identifiers) and their FileInfoFields members.
struct ScriptTextField {
  const char* key;
  std::string metadata::FileInfoFields::*member;
};
struct ScriptListField {
  const char* key;
  std::vector<std::string> metadata::FileInfoFields::*member;
};

const std::array<ScriptTextField, 18> kScriptTextFields{{
    {"title", &metadata::FileInfoFields::title},
    {"authorTitle", &metadata::FileInfoFields::author_title},
    {"description", &metadata::FileInfoFields::description},
    {"descriptionWriter", &metadata::FileInfoFields::description_writer},
    {"copyrightNotice", &metadata::FileInfoFields::copyright_notice},
    {"copyrightUrl", &metadata::FileInfoFields::copyright_url},
    {"headline", &metadata::FileInfoFields::headline},
    {"dateCreated", &metadata::FileInfoFields::date_created},
    {"city", &metadata::FileInfoFields::city},
    {"state", &metadata::FileInfoFields::state},
    {"country", &metadata::FileInfoFields::country},
    {"countryCode", &metadata::FileInfoFields::country_code},
    {"sublocation", &metadata::FileInfoFields::sublocation},
    {"credit", &metadata::FileInfoFields::credit},
    {"source", &metadata::FileInfoFields::source},
    {"instructions", &metadata::FileInfoFields::instructions},
    {"jobIdentifier", &metadata::FileInfoFields::transmission_reference},
    {"usageTerms", &metadata::FileInfoFields::usage_terms},
}};
const std::array<ScriptTextField, 8> kScriptContactFields{{
    {"address", &metadata::FileInfoFields::contact_address},
    {"city", &metadata::FileInfoFields::contact_city},
    {"region", &metadata::FileInfoFields::contact_region},
    {"postalCode", &metadata::FileInfoFields::contact_postal_code},
    {"country", &metadata::FileInfoFields::contact_country},
    {"phone", &metadata::FileInfoFields::contact_phone},
    {"email", &metadata::FileInfoFields::contact_email},
    {"url", &metadata::FileInfoFields::contact_url},
}};
const std::array<ScriptListField, 4> kScriptListFields{{
    {"authors", &metadata::FileInfoFields::authors},
    {"keywords", &metadata::FileInfoFields::keywords},
    {"scene", &metadata::FileInfoFields::scene},
    {"subjectCode", &metadata::FileInfoFields::subject_code},
}};

QString copyright_status_token(metadata::CopyrightStatus status) {
  switch (status) {
    case metadata::CopyrightStatus::Copyrighted:
      return QStringLiteral("copyrighted");
    case metadata::CopyrightStatus::PublicDomain:
      return QStringLiteral("publicDomain");
    case metadata::CopyrightStatus::Unknown:
      break;
  }
  return QStringLiteral("unknown");
}

QJSValue string_array(QJSEngine& engine, const std::vector<std::string>& values) {
  auto array = engine.newArray(static_cast<quint32>(values.size()));
  for (quint32 i = 0; i < static_cast<quint32>(values.size()); ++i) {
    array.setProperty(i, QString::fromStdString(values[i]));
  }
  return array;
}

}  // namespace

QJSValue ScriptDocumentObject::file_info() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return QJSValue(QJSValue::NullValue);
  }
  const auto fields = metadata::read_fields(document->file_info());
  auto& engine = *host_.engine();
  auto result = engine.newObject();
  for (const auto& field : kScriptTextFields) {
    result.setProperty(QString::fromLatin1(field.key), QString::fromStdString(fields.*field.member));
  }
  for (const auto& field : kScriptListFields) {
    result.setProperty(QString::fromLatin1(field.key), string_array(engine, fields.*field.member));
  }
  auto contact = engine.newObject();
  for (const auto& field : kScriptContactFields) {
    contact.setProperty(QString::fromLatin1(field.key), QString::fromStdString(fields.*field.member));
  }
  result.setProperty(QStringLiteral("contact"), contact);
  result.setProperty(QStringLiteral("rating"), fields.rating);
  result.setProperty(QStringLiteral("copyrightStatus"), copyright_status_token(fields.copyright_status));
  result.setProperty(QStringLiteral("intellectualGenre"), QString::fromStdString(fields.intellectual_genre));
  result.setProperty(QStringLiteral("created"), QString::fromStdString(fields.created));
  result.setProperty(QStringLiteral("modified"), QString::fromStdString(fields.modified));
  result.setProperty(QStringLiteral("creatorTool"), QString::fromStdString(fields.creator_tool));
  result.setProperty(QStringLiteral("sourceFormat"), QString::fromStdString(document->file_info().source_format));
  return result;
}

QString ScriptDocumentObject::xmp() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr || !document->file_info().xmp) {
    return {};
  }
  const auto& bytes = *document->file_info().xmp;
  return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

void ScriptDocumentObject::setFileInfo(const QJSValue& values) {
  const ScriptApiCall api_call(host_);
  if (!values.isObject() || values.isArray()) {
    host_.throw_js_error(ScriptEngineHost::tr("setFileInfo needs an object such as {title: \"Harbor\"}."));
    return;
  }
  const auto* current = read_document();
  if (current == nullptr) {
    return;
  }
  const auto before = metadata::read_fields(current->file_info());
  auto after = before;
  const auto fail = [this](const QString& key, const QString& expected) {
    host_.throw_js_error(ScriptEngineHost::tr("setFileInfo: %1 must be %2.").arg(key, expected));
  };
  QJSValueIterator it(values);
  while (it.hasNext()) {
    it.next();
    const auto key = it.name();
    const auto value = it.value();
    const auto latin = key.toLatin1();
    bool handled = false;
    for (const auto& field : kScriptTextFields) {
      if (latin == field.key) {
        if (!value.isString()) {
          fail(key, ScriptEngineHost::tr("a string"));
          return;
        }
        after.*field.member = value.toString().trimmed().toStdString();
        handled = true;
      }
    }
    for (const auto& field : kScriptListFields) {
      if (latin == field.key) {
        if (!value.isArray()) {
          fail(key, ScriptEngineHost::tr("an array of strings"));
          return;
        }
        std::vector<std::string> list;
        const auto length = value.property(QStringLiteral("length")).toUInt();
        for (quint32 i = 0; i < length; ++i) {
          const auto item = value.property(i);
          if (!item.isString()) {
            fail(key, ScriptEngineHost::tr("an array of strings"));
            return;
          }
          if (const auto text = item.toString().trimmed(); !text.isEmpty()) {
            list.push_back(text.toStdString());
          }
        }
        after.*field.member = std::move(list);
        handled = true;
      }
    }
    if (handled) {
      continue;
    }
    if (key == QStringLiteral("intellectualGenre") && value.isString()) {
      after.intellectual_genre = value.toString().trimmed().toStdString();
    } else if (key == QStringLiteral("rating")) {
      if (!value.isNumber() || value.toNumber() != std::floor(value.toNumber()) || value.toNumber() < -1 ||
          value.toNumber() > 5) {
        fail(key, ScriptEngineHost::tr("a whole number from -1 to 5"));
        return;
      }
      after.rating = value.toInt();
    } else if (key == QStringLiteral("copyrightStatus")) {
      const auto token = value.toString();
      if (!value.isString() || (token != QStringLiteral("unknown") && token != QStringLiteral("copyrighted") &&
                                token != QStringLiteral("publicDomain"))) {
        fail(key, QStringLiteral("\"unknown\", \"copyrighted\" or \"publicDomain\""));
        return;
      }
      after.copyright_status = token == QStringLiteral("copyrighted")    ? metadata::CopyrightStatus::Copyrighted
                               : token == QStringLiteral("publicDomain") ? metadata::CopyrightStatus::PublicDomain
                                                                         : metadata::CopyrightStatus::Unknown;
    } else if (key == QStringLiteral("contact") && value.isObject() && !value.isArray()) {
      QJSValueIterator contact(value);
      while (contact.hasNext()) {
        contact.next();
        const auto contact_key = contact.name().toLatin1();
        bool known = false;
        for (const auto& field : kScriptContactFields) {
          if (contact_key == field.key) {
            if (!contact.value().isString()) {
              fail(QStringLiteral("contact.") + contact.name(), ScriptEngineHost::tr("a string"));
              return;
            }
            after.*field.member = contact.value().toString().trimmed().toStdString();
            known = true;
          }
        }
        if (!known) {
          host_.throw_js_error(ScriptEngineHost::tr("setFileInfo: unknown option %1.")
                                   .arg(QStringLiteral("contact.") + contact.name()));
          return;
        }
      }
    } else {
      host_.throw_js_error(ScriptEngineHost::tr("setFileInfo: unknown option %1.").arg(key));
      return;
    }
  }
  if (after == before) {
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  metadata::apply_fields(document->file_info(), before, after);
}

QJSValue ScriptDocumentObject::metadataEntries() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  auto& engine = *host_.engine();
  if (document == nullptr) {
    return engine.newArray(0);
  }
  const auto entries =
      metadata::describe_file_info(document->file_info(), document->color_state().embedded_icc_profile);
  auto array = engine.newArray(static_cast<quint32>(entries.size()));
  for (quint32 i = 0; i < static_cast<quint32>(entries.size()); ++i) {
    const auto& entry = entries[i];
    auto row = engine.newObject();
    row.setProperty(QStringLiteral("group"), QString::fromStdString(entry.group));
    row.setProperty(QStringLiteral("name"), QString::fromStdString(entry.name));
    row.setProperty(QStringLiteral("value"), QString::fromStdString(entry.value));
    row.setProperty(QStringLiteral("source"), QString::fromStdString(entry.source));
    row.setProperty(QStringLiteral("privacy"),
                    QString::fromLatin1(metadata::privacy_class_id(entry.privacy).data(),
                                        static_cast<qsizetype>(metadata::privacy_class_id(entry.privacy).size())));
    array.setProperty(i, row);
  }
  return array;
}

void ScriptDocumentObject::stripMetadata(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  bool keep_color_profile = true;
  if (!options.isUndefined()) {
    if (!options.isObject() || options.isArray()) {
      host_.throw_js_error(ScriptEngineHost::tr("stripMetadata options must be an object such as "
                                                "{keepColorProfile: false}."));
      return;
    }
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      if (it.name() != QStringLiteral("keepColorProfile") || !it.value().isBool()) {
        host_.throw_js_error(ScriptEngineHost::tr("stripMetadata: unknown option %1.").arg(it.name()));
        return;
      }
      keep_color_profile = it.value().toBool();
    }
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  metadata::strip_metadata(*document, keep_color_profile);
}

bool ScriptDocumentObject::removeLocation() {
  const ScriptApiCall api_call(host_);
  const auto* current = read_document();
  if (current == nullptr) {
    return false;
  }
  auto probe = current->file_info();
  if (!metadata::remove_location(probe)) {
    return false;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return false;
  }
  document->file_info() = std::move(probe);
  return true;
}

QJSValue ScriptDocumentObject::scanForWatermarks() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  auto& engine = *host_.engine();
  auto result = engine.newObject();
  if (document == nullptr) {
    return result;
  }
  // TrustMark runs when its models are downloaded (docs/watermarks.md).
  configure_automatic_selection_models();
  const bool available = patchy::ai::trustmark_models_available();
  const auto report = scan_watermarks(Compositor{}.flatten_rgb8(*document), document->file_info().observed);
  if (!report.trustmark_error.empty()) {
    host_.throw_js_error(
        ScriptEngineHost::tr("TrustMark could not run: %1").arg(QString::fromStdString(report.trustmark_error)));
    return {};
  }
  const auto latin1 = [](std::string_view text) {
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
  };
  const auto match_object = [&](const InvisibleWatermarkMatch& match) {
    auto row = engine.newObject();
    row.setProperty(QStringLiteral("id"), latin1(match.id));
    row.setProperty(QStringLiteral("name"), latin1(match.name));
    row.setProperty(QStringLiteral("bits"), match.bit_count);
    row.setProperty(QStringLiteral("differingBits"), match.differing_bits);
    row.setProperty(QStringLiteral("redBlueSwapped"), match.red_blue_swapped);
    return row;
  };
  const auto mark_object = [&](const patchy::ai::TrustMarkMark& mark) {
    auto row = engine.newObject();
    row.setProperty(QStringLiteral("variant"), latin1(patchy::ai::trustmark_variant_name(mark.variant)));
    row.setProperty(QStringLiteral("schema"), latin1(trustmark::schema_name(mark.schema)));
    row.setProperty(QStringLiteral("text"), QString::fromStdString(mark.text));
    row.setProperty(QStringLiteral("id"), QString::fromStdString(mark.hex));
    QString bits;
    for (const auto bit : mark.data_bits) {
      bits.append(bit != 0 ? QLatin1Char('1') : QLatin1Char('0'));
    }
    row.setProperty(QStringLiteral("bits"), bits);
    row.setProperty(QStringLiteral("correctedBits"), mark.corrected_bits);
    row.setProperty(QStringLiteral("confidence"), static_cast<double>(mark.confidence));
    return row;
  };
  result.setProperty(QStringLiteral("scanned"), report.stable_diffusion.scanned);
  const auto& sd_matches = report.stable_diffusion.matches;
  auto matches = engine.newArray(static_cast<quint32>(sd_matches.size()));
  for (quint32 i = 0; i < static_cast<quint32>(sd_matches.size()); ++i) {
    matches.setProperty(i, match_object(sd_matches[i]));
  }
  result.setProperty(QStringLiteral("matches"), matches);
  auto trustmark = engine.newObject();
  trustmark.setProperty(QStringLiteral("available"), available);
  const auto found_marks = report.trustmark.has_value() ? report.trustmark->marks : std::vector<patchy::ai::TrustMarkMark>{};
  auto marks = engine.newArray(static_cast<quint32>(found_marks.size()));
  for (quint32 i = 0; i < static_cast<quint32>(found_marks.size()); ++i) {
    marks.setProperty(i, mark_object(found_marks[i]));
  }
  trustmark.setProperty(QStringLiteral("marks"), marks);
  result.setProperty(QStringLiteral("trustmark"), trustmark);
  // One row per kind checked, in File Info's order (docs/watermarks.md, "Scan results").
  auto checks = engine.newArray(static_cast<quint32>(report.checks.size()));
  for (quint32 i = 0; i < static_cast<quint32>(report.checks.size()); ++i) {
    const auto& check = report.checks[i];
    auto row = engine.newObject();
    row.setProperty(QStringLiteral("id"), check.id);
    row.setProperty(QStringLiteral("name"), check.name);
    row.setProperty(QStringLiteral("status"), watermark_check_status_token(check.status));
    row.setProperty(QStringLiteral("reason"), check.reason);
    row.setProperty(QStringLiteral("detail"), check.detail);
    if (check.match) {
      row.setProperty(QStringLiteral("match"), match_object(*check.match));
    }
    if (check.mark) {
      row.setProperty(QStringLiteral("mark"), mark_object(*check.mark));
    }
    if (check.id == QStringLiteral("c2paDeclared")) {
      auto declarations = engine.newArray(static_cast<quint32>(check.declarations.size()));
      for (quint32 k = 0; k < static_cast<quint32>(check.declarations.size()); ++k) {
        declarations.setProperty(k, QString::fromStdString(check.declarations[k]));
      }
      row.setProperty(QStringLiteral("declarations"), declarations);
    }
    checks.setProperty(i, row);
  }
  result.setProperty(QStringLiteral("checks"), checks);
  return result;
}

QJSValue ScriptDocumentObject::addInvisibleWatermark(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  auto schema = trustmark::Schema::Bch5;
  QString text;
  QString id;
  double strength = patchy::ai::kTrustMarkDefaultStrength;
  if (!options.isUndefined()) {
    if (!options.isObject() || options.isArray()) {
      host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark options must be an object such as "
                                                "{text: \"Patchy\"}."));
      return {};
    }
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      if (key == QStringLiteral("text") && value.isString()) {
        text = value.toString();
      } else if (key == QStringLiteral("id") && value.isString()) {
        id = value.toString();
      } else if (key == QStringLiteral("schema") && value.isString()) {
        const auto parsed = trustmark::schema_from_name(value.toString().toStdString());
        if (!parsed) {
          host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: unknown schema %1.").arg(value.toString()));
          return {};
        }
        schema = *parsed;
      } else if (key == QStringLiteral("strength") && value.isNumber()) {
        strength = value.toNumber();
        if (!(strength >= 0.1 && strength <= 2.0)) {
          host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: strength must be between 0.1 and 2."));
          return {};
        }
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: unknown option %1.").arg(key));
        return {};
      }
    }
  }
  if (!text.isEmpty() && !id.isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark takes text or id, not both."));
    return {};
  }
  const int capacity = trustmark::schema_text_capacity(schema);
  for (const auto ch : text) {
    if (ch.unicode() < 32 || ch.unicode() > 126) {
      host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: text must be printable ASCII."));
      return {};
    }
  }
  if (text.size() > capacity) {
    host_.throw_js_error(
        ScriptEngineHost::tr("addInvisibleWatermark: %1 holds at most %2 characters.")
            .arg(QString::fromLatin1(trustmark::schema_name(schema).data()))
            .arg(capacity));
    return {};
  }
  const auto data_length = static_cast<std::size_t>(trustmark::schema_data_bits(schema));
  std::vector<std::uint8_t> bits;
  if (!text.isEmpty()) {
    bits = trustmark::text_to_bits(text.toLatin1().toStdString());
  } else if (!id.isEmpty()) {
    // "0x" and hexadecimal digits, most significant first, as scanForWatermarks reports it.
    auto digits = id.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? id.mid(2) : id;
    for (const auto ch : digits) {
      bool ok = false;
      const auto nibble = QString(ch).toUInt(&ok, 16);
      if (!ok) {
        host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: id must be hexadecimal."));
        return {};
      }
      for (int bit = 3; bit >= 0; --bit) {
        bits.push_back(static_cast<std::uint8_t>((nibble >> static_cast<unsigned>(bit)) & 1U));
      }
    }
    if (bits.size() > (data_length + 3) / 4 * 4) {
      host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark: the id is longer than %1 bits.")
                               .arg(static_cast<int>(data_length)));
      return {};
    }
  } else {
    bits = random_watermark_id_bits(static_cast<int>(data_length));
  }
  bits.resize(data_length, 0);

  configure_automatic_selection_models();
  if (!patchy::ai::trustmark_models_available()) {
    host_.throw_js_error(ScriptEngineHost::tr("The watermark models are not downloaded; call "
                                              "app.downloadWatermarkModels() first."));
    return {};
  }
  const auto* current = read_document();
  if (current == nullptr) {
    return {};
  }
  const auto active = current->active_layer_id();
  const auto* source = active.has_value() ? current->find_layer(*active) : nullptr;
  if (current->color_state().bit_depth != BitDepth::UInt8) {
    host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark needs an 8-bit document."));
    return {};
  }
  if (source == nullptr || source->kind() != LayerKind::Pixel || source->pixels().empty() ||
      layer_effectively_locks_image_pixels(current->layers(), *active)) {
    host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark needs an unlocked pixel layer."));
    return {};
  }
  const auto bounds = source->bounds();
  if (bounds.x != 0 || bounds.y != 0 || bounds.width != current->width() || bounds.height != current->height()) {
    host_.throw_js_error(ScriptEngineHost::tr("addInvisibleWatermark needs a layer that fills the whole image."));
    return {};
  }
  auto marked = source->pixels();
  try {
    patchy::ai::embed_trustmark_q(marked, trustmark::encode_packet(bits, schema), static_cast<float>(strength));
  } catch (const std::exception& error) {
    host_.throw_js_error(ScriptEngineHost::tr("The watermark could not be added: %1").arg(QString::fromUtf8(error.what())));
    return {};
  }
  auto* document = write_document();
  if (document == nullptr) {
    return {};
  }
  if (auto* layer = document->find_layer(*active)) {
    layer->set_pixels(std::move(marked));
  }
  auto& engine = *host_.engine();
  auto result = engine.newObject();
  result.setProperty(QStringLiteral("schema"), QString::fromLatin1(trustmark::schema_name(schema).data()));
  result.setProperty(QStringLiteral("text"), text);
  result.setProperty(QStringLiteral("id"), QString::fromStdString(trustmark::bits_to_hex(bits)));
  return result;
}

bool ScriptDocumentObject::removeEditHistory() {
  const ScriptApiCall api_call(host_);
  const auto* current = read_document();
  if (current == nullptr) {
    return false;
  }
  auto probe = current->file_info();
  if (!metadata::remove_edit_history(probe)) {
    return false;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return false;
  }
  document->file_info() = std::move(probe);
  return true;
}

double ScriptDocumentObject::resolution() const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  return document != nullptr ? document->print_settings().horizontal_ppi : 0.0;
}

QJSValue ScriptDocumentObject::layers() const {
  const ScriptApiCall api_call(host_);
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
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr || !document->active_layer_id().has_value()) {
    return QJSValue();
  }
  return make_layer_value(host_, session_id_, *document->active_layer_id());
}

void ScriptDocumentObject::set_active_layer(const QJSValue& layer) {
  const ScriptApiCall api_call(host_);
  auto* document = host_.session_document(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return;
  }
  const auto* wrapper = qobject_cast<ScriptLayerObject*>(layer.toQObject());
  // LayerIds restart per document, so a wrapper from another document must be refused by
  // session, not just by id, or it would activate an unrelated layer here.
  if (wrapper == nullptr || wrapper->session_id() != session_id_ ||
      document->find_layer(wrapper->layer_id()) == nullptr) {
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
  const ScriptApiCall api_call(host_);
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

QJSValue ScriptDocumentObject::mergeLayers(const QJSValue& layers, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return QJSValue();
  }
  LayerMergeOptions choice;
  if (!options.isUndefined()) {
    if (!options.isObject() || options.isArray() || options.isCallable()) {
      host_.throw_js_error(ScriptEngineHost::tr("mergeLayers: options must be an object."));
      return QJSValue();
    }
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      if (key == QLatin1String("effectsFrom")) {
        const auto* wrapper = qobject_cast<ScriptLayerObject*>(it.value().toQObject());
        if (wrapper == nullptr || wrapper->session_id() != session_id_ || document->find_layer(wrapper->layer_id()) == nullptr) {
          host_.throw_js_error(ScriptEngineHost::tr("mergeLayers: effectsFrom must be a layer of this document."));
          return QJSValue();
        }
        choice.effects_source = wrapper->layer_id();
        continue;
      }
      if (!it.value().isBool()) {
        host_.throw_js_error(ScriptEngineHost::tr("mergeLayers: %1 must be a boolean.").arg(key));
        return QJSValue();
      }
      if (key == QLatin1String("keepVectors")) {
        choice.keep_vectors = it.value().toBool();
      } else if (key == QLatin1String("withinGroups")) {
        choice.within_groups = it.value().toBool();
      } else if (key == QLatin1String("separateVectorTypes")) {
        choice.separate_vector_types = it.value().toBool();
      } else if (key == QLatin1String("singleVector")) {
        choice.single_vector = it.value().toBool();
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("mergeLayers: unknown option %1").arg(key));
        return QJSValue();
      }
    }
  }
  if (choice.effects_source && !choice.single_vector) {
    host_.throw_js_error(ScriptEngineHost::tr("mergeLayers: effectsFrom requires singleVector."));
    return QJSValue();
  }
  std::vector<LayerId> ids;
  const auto length = layers.isArray() ? layers.property(QStringLiteral("length")).toUInt() : 0U;
  if (length == 0 || length > layer_tree_count(document->layers())) {
    host_.throw_js_error(ScriptEngineHost::tr("mergeLayers needs a nonempty array of layers of this document."));
    return QJSValue();
  }
  for (quint32 i = 0; i < length; ++i) {
    const auto* wrapper = qobject_cast<ScriptLayerObject*>(layers.property(i).toQObject());
    if (wrapper == nullptr || wrapper->session_id() != session_id_ || document->find_layer(wrapper->layer_id()) == nullptr) {
      host_.throw_js_error(ScriptEngineHost::tr("mergeLayers needs a nonempty array of layers of this document."));
      return QJSValue();
    }
    ids.push_back(wrapper->layer_id());
  }
  LayerMergePlan plan;
  std::optional<Document> prepared;
  try {
    plan = plan_layer_merge(*document, ids, choice);
    if (!plan.blockers.empty()) {
      host_.throw_js_error(layer_merge_blocker_messages(*document, plan).join(QChar('\n')));
      return QJSValue();
    }
    if (plan.changed) {
      prepared = render_layer_merge(*document, plan);
    }
  } catch (const std::exception&) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not merge the layers. The original layers are unchanged."));
    return QJSValue();
  }
  if (prepared.has_value()) {
    auto* target = write_document();
    if (target == nullptr) {
      return QJSValue();
    }
    *target = std::move(*prepared);
    host_.note_structure_changed(session_id_);
  }
  auto result = host_.engine()->newArray(static_cast<uint>(plan.result_ids.size()));
  for (quint32 i = 0; i < plan.result_ids.size(); ++i) {
    result.setProperty(i, make_layer_value(host_, session_id_, plan.result_ids[i]));
  }
  return result;
}

QJSValue ScriptDocumentObject::selection() const {
  const ScriptApiCall api_call(host_);
  return host_.engine()->newQObject(new ScriptSelectionObject(host_, session_id_));
}

QJSValue ScriptDocumentObject::addLayer(const QString& name) {
  const ScriptApiCall api_call(host_);
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

QJSValue ScriptDocumentObject::importFilesAsLayers(const QJSValue& paths) {
  const ScriptApiCall api_call(host_);
  if (read_document() == nullptr) {
    return QJSValue();
  }
  QStringList list;
  if (paths.isString()) {
    list.push_back(paths.toString());
  } else if (paths.isArray()) {
    const auto count = paths.property(QStringLiteral("length")).toUInt();
    for (quint32 i = 0; i < count; ++i) {
      const auto value = paths.property(i);
      if (!value.isString() || value.toString().isEmpty()) {
        list.clear();
        break;
      }
      list.push_back(value.toString());
    }
  }
  if (list.isEmpty()) {
    host_.throw_js_error(
        ScriptEngineHost::tr("importFilesAsLayers needs a file path or a non-empty array of paths."));
    return QJSValue();
  }
  QString error;
  const auto ids_top_to_bottom = host_.import_files_as_layers(session_id_, list, &error);
  if (ids_top_to_bottom.empty()) {
    host_.throw_js_error(error);
    return QJSValue();
  }
  // Argument order is bottom to top: the first file is the lowest new layer.
  auto result = host_.engine()->newArray(static_cast<uint>(ids_top_to_bottom.size()));
  quint32 index = 0;
  for (auto it = ids_top_to_bottom.rbegin(); it != ids_top_to_bottom.rend(); ++it) {
    result.setProperty(index++, make_layer_value(host_, session_id_, *it));
  }
  return result;
}

QJSValue ScriptDocumentObject::addSmartObject(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (read_document() == nullptr) {
    return QJSValue();
  }
  if (path.isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("addSmartObject needs a file path."));
    return QJSValue();
  }
  ScriptEngineHost::SmartObjectParams params;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      std::optional<double>* number = key == QLatin1String("x")        ? &params.x
                                      : key == QLatin1String("y")      ? &params.y
                                      : key == QLatin1String("width")  ? &params.width
                                      : key == QLatin1String("height") ? &params.height
                                      : key == QLatin1String("scale")  ? &params.scale
                                                                       : nullptr;
      if (number != nullptr) {
        // Range checks belong to the placement itself; a non-number is a script bug.
        if (!value.isNumber() || !std::isfinite(value.toNumber())) {
          host_.throw_js_error(ScriptEngineHost::tr("addSmartObject: %1 must be a finite number.").arg(key));
          return QJSValue();
        }
        *number = value.toNumber();
      } else if (key == QLatin1String("linked")) {
        params.linked = value.toBool();
      } else if (key == QLatin1String("name")) {
        params.name = value.toString();
      } else {
        host_.throw_js_error(
            ScriptEngineHost::tr("%1: unknown option %2.").arg(QStringLiteral("addSmartObject"), key));
        return QJSValue();
      }
    }
  } else if (!options.isUndefined() && !options.isNull()) {
    host_.throw_js_error(ScriptEngineHost::tr("%1: unknown option %2.")
                             .arg(QStringLiteral("addSmartObject"), options.toString()));
    return QJSValue();
  }
  QString error;
  const auto placed = host_.add_smart_object(session_id_, path, params, &error);
  if (!placed.has_value()) {
    if (!error.isEmpty()) {
      host_.throw_js_error(error);
    }
    return QJSValue();
  }
  return make_layer_value(host_, session_id_, *placed);
}

QJSValue ScriptDocumentObject::addTextLayer(const QJSValue& text, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  ScriptEngineHost::TextLayerParams params;
  if (text.isString()) {
    params.text = text.toString();
  } else if (text.isArray()) {
    if (!parse_text_runs(host_, text, "addTextLayer", &params.runs)) {
      return QJSValue();
    }
  } else {
    host_.throw_js_error(ScriptEngineHost::tr("addTextLayer: text must be a string or an array of runs."));
    return QJSValue();
  }
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
    const auto orientation = options.property(QStringLiteral("orientation"));
    if (orientation.isString()) {
      params.orientation = orientation.toString();
      if (params.orientation != QLatin1String("horizontal") && params.orientation != QLatin1String("vertical")) {
        host_.throw_js_error(ScriptEngineHost::tr("orientation must be 'horizontal' or 'vertical'."));
        return QJSValue();
      }
    }
    const auto direction = options.property(QStringLiteral("direction"));
    if (direction.isString()) {
      params.direction = direction.toString();
      if (params.direction != QLatin1String("auto") && params.direction != QLatin1String("ltr") &&
          params.direction != QLatin1String("rtl")) {
        host_.throw_js_error(ScriptEngineHost::tr("direction must be 'auto', 'ltr' or 'rtl'."));
        return QJSValue();
      }
    }
    if (const auto box = options.property(QStringLiteral("box")); !box.isUndefined() && !box.isNull()) {
      const auto width = box.property(QStringLiteral("width"));
      const auto height = box.property(QStringLiteral("height"));
      // The Type tool's smallest drag box (kMinimumTextBoxDocumentSize in main_window.cpp);
      // anything smaller would silently open as point text.
      if (!box.isObject() || !width.isNumber() || !height.isNumber() || width.toNumber() < 16.0 ||
          height.toNumber() < 16.0) {
        host_.throw_js_error(ScriptEngineHost::tr("box must be {width, height} of at least 16 document pixels each."));
        return QJSValue();
      }
      params.box = QSize(static_cast<int>(std::lround(width.toNumber())),
                         static_cast<int>(std::lround(height.toNumber())));
    }
    if (const auto area = options.property(QStringLiteral("area")); !area.isUndefined() && !area.isNull()) {
      try {
        if (params.box.isValid()) script_vector::invalid(QStringLiteral("area/box"));
        params.area = script_vector::parse_path(script_vector::object(area), false);
        if (!valid_text_area(*params.area)) script_vector::invalid(QStringLiteral("area.closedContour"));
      } catch (const std::exception& error) {
        host_.throw_js_error(ScriptEngineHost::tr("Invalid vector option or target: %1.").arg(QString::fromUtf8(error.what())));
        return QJSValue();
      }
    }
    if (const auto align = options.property(QStringLiteral("align")); align.isString()) {
      params.align = align.toString();
      if (!text_align_name_is_valid(params.align)) {
        host_.throw_js_error(ScriptEngineHost::tr("align must be 'left', 'center', 'right' or 'justify'."));
        return QJSValue();
      }
    }
    if (const auto paragraph = options.property(QStringLiteral("paragraph"));
        !paragraph.isUndefined() && !paragraph.isNull()) {
      if (!parse_paragraph_metrics(host_, paragraph, "addTextLayer", &params.paragraph)) {
        return QJSValue();
      }
    }
  }
  const auto created = host_.add_text_layer(session_id_, params);
  if (!created.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not create the text layer."));
    return QJSValue();
  }
  return make_layer_value(host_, session_id_, *created);
}

QJSValue ScriptDocumentObject::findLayer(const QString& name) {
  const ScriptApiCall api_call(host_);
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

namespace {

// Shared option parsing for alignLayers / distributeLayers: an optional array
// of this document's layer wrappers plus, for Align, the alignTo choice.
struct AlignmentOptions {
  std::vector<LayerId> layer_ids;
  bool align_to_canvas{false};
  bool valid{true};
};

AlignmentOptions parse_alignment_options(ScriptEngineHost& host, const Document& document, std::int64_t session_id,
                                         const QJSValue& options, const char* verb, bool allow_align_to) {
  AlignmentOptions parsed;
  if (options.isUndefined() || options.isNull()) {
    return parsed;
  }
  if (!options.isObject() || options.isArray() || options.isCallable()) {
    host.throw_js_error(ScriptEngineHost::tr("%1: options must be an object.").arg(QLatin1String(verb)));
    parsed.valid = false;
    return parsed;
  }
  QJSValueIterator it(options);
  while (it.hasNext()) {
    it.next();
    if (it.name() == QLatin1String("layers")) {
      const auto layers = it.value();
      const auto length = layers.isArray() ? layers.property(QStringLiteral("length")).toUInt() : 0U;
      if (!layers.isArray() || length == 0) {
        host.throw_js_error(
            ScriptEngineHost::tr("%1: layers must be a nonempty array of layers of this document.")
                .arg(QLatin1String(verb)));
        parsed.valid = false;
        return parsed;
      }
      for (quint32 i = 0; i < length; ++i) {
        const auto* wrapper = qobject_cast<ScriptLayerObject*>(layers.property(i).toQObject());
        if (wrapper == nullptr || wrapper->session_id() != session_id ||
            document.find_layer(wrapper->layer_id()) == nullptr) {
          host.throw_js_error(
              ScriptEngineHost::tr("%1: layers must be a nonempty array of layers of this document.")
                  .arg(QLatin1String(verb)));
          parsed.valid = false;
          return parsed;
        }
        parsed.layer_ids.push_back(wrapper->layer_id());
      }
    } else if (allow_align_to && it.name() == QLatin1String("alignTo")) {
      const auto value = it.value().toString();
      if (value == QLatin1String("canvas")) {
        parsed.align_to_canvas = true;
      } else if (value == QLatin1String("selection")) {
        parsed.align_to_canvas = false;
      } else {
        host.throw_js_error(
            ScriptEngineHost::tr("%1: alignTo must be \"selection\" or \"canvas\".").arg(QLatin1String(verb)));
        parsed.valid = false;
        return parsed;
      }
    } else {
      host.throw_js_error(
          ScriptEngineHost::tr("%1: unknown option %2.").arg(QLatin1String(verb), it.name()));
      parsed.valid = false;
      return parsed;
    }
  }
  return parsed;
}

}  // namespace

int ScriptDocumentObject::alignLayers(const QString& edge, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return -1;
  }
  const auto parsed_edge = align_edge_from_id(edge.toStdString());
  if (!parsed_edge.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("alignLayers: unknown edge %1 (left, hcenter, right, top, vcenter, bottom)")
                             .arg(edge));
    return -1;
  }
  const auto parsed = parse_alignment_options(host_, *document, session_id_, options, "alignLayers", true);
  if (!parsed.valid) {
    return -1;
  }
  return host_.align_layers(session_id_, parsed.layer_ids, *parsed_edge, parsed.align_to_canvas);
}

int ScriptDocumentObject::distributeLayers(const QString& mode, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return -1;
  }
  const auto parsed_mode = distribute_mode_from_id(mode.toStdString());
  if (!parsed_mode.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("distributeLayers: unknown mode %1 (left, hcenter, right, top, "
                                              "vcenter, bottom, hspacing, vspacing)")
                             .arg(mode));
    return -1;
  }
  const auto parsed = parse_alignment_options(host_, *document, session_id_, options, "distributeLayers", false);
  if (!parsed.valid) {
    return -1;
  }
  return host_.distribute_layers(session_id_, parsed.layer_ids, *parsed_mode);
}

void ScriptDocumentObject::flatten() {
  const ScriptApiCall api_call(host_);
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  // 16 and 32-bit documents flatten at their depth, as Image > Flatten Image does; the
  // 8-bit flatten refuses deep layers.
  auto flattened = std::as_const(*document).color_state().bit_depth == BitDepth::UInt8
                       ? flatten_document_rgba8(*document)
                       : Compositor{}.flatten_rgba_deep(*document);
  document->clear_active_layer();
  document->layers().clear();
  document->add_pixel_layer(ScriptEngineHost::tr("Background").toStdString(),
                            std::move(flattened));
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::resizeImage(int width, int height, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (width < 1 || height < 1 || width > 30000 || height > 30000) {
    host_.throw_js_error(ScriptEngineHost::tr("resizeImage needs a size between 1 and 30000."));
    return;
  }
  // The Image Size dialog's method, by its permanent id (core/resample.hpp); Automatic
  // picks Bicubic Sharper for a reduction and Bicubic Smoother for an enlargement.
  auto method = ResampleMethod::Automatic;
  if (options.isObject()) {
    if (const auto value = options.property(QStringLiteral("method")); !value.isUndefined() && !value.isNull()) {
      const auto parsed = parse_resample_method(value.toString().toStdString());
      if (!parsed.has_value()) {
        QStringList ids;
        for (const auto candidate : all_resample_methods()) {
          const auto id = resample_method_id(candidate);
          ids.push_back(QStringLiteral("\"%1\"").arg(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()))));
        }
        host_.throw_js_error(
            ScriptEngineHost::tr("resizeImage method must be one of %1.").arg(ids.join(QStringLiteral(", "))));
        return;
      }
      method = *parsed;
    }
  }
  if (read_document()) { host_.resize_session_image(session_id_, width, height, method); }
}

void ScriptDocumentObject::resizeCanvas(int width, int height) {
  const ScriptApiCall api_call(host_);
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

void ScriptDocumentObject::convertBitDepth(int bits) {
  const ScriptApiCall api_call(host_);
  if (bits != 8 && bits != 16 && bits != 32) {
    host_.throw_js_error(ScriptEngineHost::tr("convertBitDepth takes 8, 16 or 32."));
    return;
  }
  if (bits != 8 && !deep_editing_enabled()) {
    host_.throw_js_error(ScriptEngineHost::tr("16 and 32-bit editing is not enabled."));
    return;
  }
  if (bits == 32 && !depth_supported_on_platform(BitDepth::Float32)) {
    host_.throw_js_error(ScriptEngineHost::tr("The web version edits up to 16 bits per channel."));
    return;
  }
  const auto* current = read_document();
  if (current == nullptr) {
    return;
  }
  const auto depth = bits == 32 ? BitDepth::Float32 : bits == 16 ? BitDepth::UInt16 : BitDepth::UInt8;
  if (current->color_state().bit_depth == depth) {
    return;
  }
  if (depth != BitDepth::UInt8 && current->palette_editing().has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("Indexed documents are 8-bit only."));
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  convert_document_depth(*document, depth);
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::convertToProfile(const QString& profile, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  auto intent = RenderingIntent::RelativeColorimetric;
  bool black_point_compensation = true;
  if (!options.isUndefined() && !options.isNull()) {
    if (!options.isObject()) {
      host_.throw_js_error(ScriptEngineHost::tr("convertToProfile options must be an object."));
      return;
    }
    if (const auto value = options.property(QStringLiteral("intent")); !value.isUndefined()) {
      const auto parsed = rendering_intent_from_token(value.toString().toStdString());
      if (!value.isString() || !parsed.has_value()) {
        host_.throw_js_error(ScriptEngineHost::tr("intent must be \"perceptual\", \"relative-colorimetric\", "
                                                  "\"saturation\" or \"absolute-colorimetric\"."));
        return;
      }
      intent = *parsed;
    }
    if (const auto value = options.property(QStringLiteral("blackPointCompensation")); !value.isUndefined()) {
      if (!value.isBool()) {
        host_.throw_js_error(ScriptEngineHost::tr("blackPointCompensation must be true or false."));
        return;
      }
      black_point_compensation = value.toBool();
    }
  }
  std::vector<std::uint8_t> destination;
  if (profile.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) != 0) {
    destination = read_rgb_profile(to_filesystem_path(profile));
    if (destination.empty()) {
      host_.throw_js_error(
          ScriptEngineHost::tr("convertToProfile takes \"sRGB\" or the path of an RGB profile file: %1").arg(profile));
      return;
    }
  }
  const auto* current = read_document();
  if (current == nullptr) {
    return;
  }
  if (current->palette_editing().has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("Convert an Indexed document to RGB Color first."));
    return;
  }
  if (host_.session_cmyk_proof(session_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("Leave CMYK Proof before converting to another RGB profile."));
    return;
  }
  // Converted on a copy, so an unusable combination leaves the document and its history alone.
  auto converted = *current;
  if (!convert_document_rgb_profile(converted, destination, intent, black_point_compensation).has_value()) {
    host_.throw_js_error(current->color_state().bit_depth == BitDepth::Float32
                             ? ScriptEngineHost::tr("32-bit documents convert only between matrix-based RGB profiles.")
                             : ScriptEngineHost::tr("The profile cannot be used for this conversion."));
    return;
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  *document = std::move(converted);
  host_.note_structure_changed(session_id_);
}

void ScriptDocumentObject::crop(int x, int y, int width, int height) {
  const ScriptApiCall api_call(host_);
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

void ScriptDocumentObject::perspectiveCrop(const QJSValue& corners, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  std::array<double, 8> quad{};
  bool corners_ok = corners.isArray();
  const auto finite_number = [](const QJSValue& value) {
    return value.isNumber() && std::isfinite(value.toNumber());
  };
  // Four points, each [x, y] (layer.transform's quad form) or {x, y}.
  corners_ok = corners_ok && corners.property(QStringLiteral("length")).toInt() == 4;
  for (quint32 i = 0; i < 4U && corners_ok; ++i) {
    const auto corner = corners.property(i);
    const auto x = corner.isArray() ? corner.property(0U) : corner.property(QStringLiteral("x"));
    const auto y = corner.isArray() ? corner.property(1U) : corner.property(QStringLiteral("y"));
    corners_ok = (!corner.isArray() || corner.property(QStringLiteral("length")).toInt() == 2) &&
                 finite_number(x) && finite_number(y);
    quad[i * 2U] = x.toNumber();
    quad[i * 2U + 1U] = y.toNumber();
  }
  if (!corners_ok) {
    host_.throw_js_error(ScriptEngineHost::tr(
        "perspectiveCrop needs four corners: [[x, y], [x, y], [x, y], [x, y]] or four {x, y} points."));
    return;
  }
  const auto auto_size = perspective_crop_auto_size(quad);
  int width = auto_size[0];
  int height = auto_size[1];
  double resolution = 0.0;
  if (!options.isUndefined() && !options.isNull()) {
    const auto read_option = [&options, &finite_number](const char* key, double minimum,
                                                        std::optional<double>& out) {
      const auto value = options.property(QLatin1String(key));
      if (value.isUndefined() || value.isNull()) {
        return true;
      }
      if (!finite_number(value) || value.toNumber() < minimum) {
        return false;
      }
      out = value.toNumber();
      return true;
    };
    std::optional<double> width_option;
    std::optional<double> height_option;
    std::optional<double> resolution_option;
    if (!options.isObject() || !read_option("width", 1.0, width_option) ||
        !read_option("height", 1.0, height_option) || !read_option("resolution", 0.0, resolution_option) ||
        width_option.value_or(1.0) > 300000.0 || height_option.value_or(1.0) > 300000.0) {
      host_.throw_js_error(ScriptEngineHost::tr(
          "perspectiveCrop options are {width, height, resolution}: sizes from 1 to 300000, resolution 0 or more."));
      return;
    }
    width = width_option.has_value() ? static_cast<int>(std::lround(*width_option)) : width;
    height = height_option.has_value() ? static_cast<int>(std::lround(*height_option)) : height;
    resolution = resolution_option.value_or(0.0);
  }
  auto* document = write_document();
  if (document == nullptr) {
    return;
  }
  if (!perspective_crop_document(*document, quad, width, height, EditColor{255, 255, 255, 255})) {
    host_.throw_js_error(ScriptEngineHost::tr(
        "perspectiveCrop corners must form a convex shape, and documents with artboards cannot be cropped."));
    return;
  }
  if (resolution > 0.0) {
    document->print_settings().horizontal_ppi = resolution;
    document->print_settings().vertical_ppi = resolution;
  }
  host_.note_structure_changed(session_id_);
}

QJSValue ScriptDocumentObject::trim(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  auto basis = TrimBasis::TransparentPixels;
  TrimSides sides;
  int tolerance = 0;
  if (!options.isUndefined() && !options.isNull()) {
    if (!options.isObject()) {
      host_.throw_js_error(ScriptEngineHost::tr("trim options must be an object."));
      return {};
    }
    if (const auto value = options.property(QStringLiteral("basedOn")); !value.isUndefined() && !value.isNull()) {
      const auto parsed = parse_trim_basis(value.toString().toStdString());
      if (!parsed.has_value()) {
        host_.throw_js_error(ScriptEngineHost::tr(
            "trim basedOn must be one of \"transparent\", \"topLeft\", \"bottomRight\"."));
        return {};
      }
      basis = *parsed;
    }
    const auto side = [&options](const char* key, bool& out) {
      if (const auto value = options.property(QString::fromLatin1(key)); !value.isUndefined() && !value.isNull()) {
        out = value.toBool();
      }
    };
    side("top", sides.top);
    side("bottom", sides.bottom);
    side("left", sides.left);
    side("right", sides.right);
    if (const auto value = options.property(QStringLiteral("tolerance")); !value.isUndefined() && !value.isNull()) {
      const auto number = value.toNumber();
      if (!std::isfinite(number) || number < 0.0 || number > 255.0) {
        host_.throw_js_error(ScriptEngineHost::tr("trim tolerance must be a number from 0 to 255."));
        return {};
      }
      tolerance = static_cast<int>(std::lround(number));
    }
  }
  const auto* current = read_document();
  if (current == nullptr) {
    return {};
  }
  if (document_has_artboards(*current)) {
    host_.throw_js_error(ScriptEngineHost::tr("trim does not apply to a document with artboards."));
    return {};
  }
  const auto bounds = trim_bounds(flatten_document_for_trim(*current), basis, sides, tolerance);
  if (!bounds.has_value() || *bounds == Rect::from_size(current->width(), current->height())) {
    return QJSValue(QJSValue::NullValue);
  }
  auto* document = write_document();
  if (document == nullptr || !crop_document(*document, *bounds)) {
    return QJSValue(QJSValue::NullValue);
  }
  host_.note_structure_changed(session_id_);
  return rect_to_js(host_.engine(), *bounds);
}

bool ScriptDocumentObject::saveAs(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  return save_to_path(QStringLiteral("saveAs"), path, options, /*export_copy=*/false);
}

bool ScriptDocumentObject::exportAs(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  return save_to_path(QStringLiteral("exportAs"), path, options, /*export_copy=*/true);
}

bool ScriptDocumentObject::save_to_path(const QString& method, const QString& path, const QJSValue& options,
                                        bool export_copy) {
  if (read_document() == nullptr) {
    return false;
  }
  if (path.trimmed().isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("%1 needs an output path.").arg(method));
    return false;
  }
  std::optional<ImageSaveOptions> save_options;
  if (!options.isUndefined()) {
    // Explicit choices start from the per-document defaults (never the session's remembered
    // options) so that an unspecified key follows the user's defaults and a specified one
    // always wins.
    save_options = host_.save_options_for_session(session_id_);
    if (!save_options.has_value()) {
      return false;
    }
    const auto error =
        apply_script_image_save_options(method, QFileInfo(path).suffix(), options, *save_options);
    if (!error.isEmpty()) {
      host_.throw_js_error(error);
      return false;
    }
  }
  return host_.save_session_to_path(session_id_, path, std::move(save_options), export_copy);
}

bool ScriptDocumentObject::exportAnimatedWebp(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) return false;
  if (path.trimmed().isEmpty() || QFileInfo(path).suffix().compare(QStringLiteral("webp"), Qt::CaseInsensitive) != 0) {
    host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedWebp needs a .webp output path."));
    return false;
  }
  ImageSaveOptions output;
  output.webp_animate = true;
  const auto& values = document->metadata().values;
  if (const auto found = values.find(webp::kLoopCountMetadata); found != values.end()) {
    output.webp_loop_count = std::clamp(QString::fromStdString(found->second).toInt(), 0, 65535);
  }
  if (!options.isUndefined() && (!options.isObject() || options.isArray() || options.isNull())) {
    host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedWebp options must be an object."));
    return false;
  }
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      if (key == QStringLiteral("lossless") && value.isBool()) {
        output.webp_lossless = value.toBool();
        continue;
      }
      int* target = nullptr;
      int limit = 0;
      if (key == QStringLiteral("quality")) { target = &output.webp_quality; limit = 100; }
      if (key == QStringLiteral("loopCount")) { target = &output.webp_loop_count; limit = 65535; }
      if (key == QStringLiteral("frameDelayMs")) {
        target = &output.animation_frame_delay_ms;
        limit = static_cast<int>(animation::kMaxFrameDelayMs);
      }
      const auto number = value.toNumber();
      if (target == nullptr || !value.isNumber() || !std::isfinite(number) ||
          number < 0 || number > limit || number != std::floor(number)) {
        host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedWebp: invalid option %1.").arg(key));
        return false;
      }
      *target = static_cast<int>(number);
    }
  }
  QString error;
  if (!host_.export_session_animated_webp(session_id_, path, output, &error)) {
    host_.throw_js_error(error);
    return false;
  }
  return true;
}

bool ScriptDocumentObject::exportAnimatedPng(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) return false;
  const auto suffix = QFileInfo(path).suffix();
  if (path.trimmed().isEmpty() || (suffix.compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0 &&
                                   suffix.compare(QStringLiteral("apng"), Qt::CaseInsensitive) != 0)) {
    host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedPng needs a .png or .apng output path."));
    return false;
  }
  ImageSaveOptions output;
  output.apng_animate = true;
  const auto& values = document->metadata().values;
  if (const auto found = values.find(apng::kLoopCountMetadata); found != values.end()) {
    output.apng_loop_count = std::clamp(QString::fromStdString(found->second).toInt(), 0, 65535);
  }
  if (!options.isUndefined() && (!options.isObject() || options.isArray() || options.isNull())) {
    host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedPng options must be an object."));
    return false;
  }
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      const auto value = it.value();
      int* target = nullptr;
      int limit = 0;
      if (key == QStringLiteral("loopCount")) { target = &output.apng_loop_count; limit = 65535; }
      if (key == QStringLiteral("frameDelayMs")) {
        target = &output.animation_frame_delay_ms;
        limit = static_cast<int>(animation::kMaxFrameDelayMs);
      }
      const auto number = value.toNumber();
      if (target == nullptr || !value.isNumber() || !std::isfinite(number) ||
          number < 0 || number > limit || number != std::floor(number)) {
        host_.throw_js_error(ScriptEngineHost::tr("exportAnimatedPng: invalid option %1.").arg(key));
        return false;
      }
      *target = static_cast<int>(number);
    }
  }
  QString error;
  if (!host_.export_session_animated_png(session_id_, path, output, &error)) {
    host_.throw_js_error(error);
    return false;
  }
  return true;
}

void ScriptDocumentObject::close() {
  const ScriptApiCall api_call(host_);
  if (read_document() == nullptr) {
    return;
  }
  host_.close_session(session_id_);
}

void ScriptDocumentObject::activate() {
  const ScriptApiCall api_call(host_);
  if (read_document() == nullptr) {
    return;
  }
  host_.activate_session(session_id_);
}

// ---------------------------------------------------------------------------
// ScriptAppObject

ScriptAppObject::ScriptAppObject(ScriptEngineHost& host) : host_(host) {}

QString ScriptAppObject::version() const { const ScriptApiCall api_call(host_); return QCoreApplication::applicationVersion(); }

QJSValue ScriptAppObject::documents() const {
  const ScriptApiCall api_call(host_);
  const auto ids = host_.session_ids();
  auto array = host_.engine()->newArray(static_cast<quint32>(ids.size()));
  quint32 index = 0;
  for (const auto id : ids) {
    array.setProperty(index++, make_document_value(host_, id));
  }
  return array;
}

QJSValue ScriptAppObject::active_document() const {
  const ScriptApiCall api_call(host_);
  const auto id = host_.active_session_id();
  return id != 0 ? make_document_value(host_, id) : QJSValue();
}

bool ScriptAppObject::undo_enabled() const { const ScriptApiCall api_call(host_); return host_.undo_enabled(); }

void ScriptAppObject::set_undo_enabled(bool enabled) {
  const ScriptApiCall api_call(host_);
  if (host_.connector_mode() && !enabled) {
    host_.throw_js_error(ScriptEngineHost::tr("Undo history cannot be disabled in a connector session."));
    return;
  }
  host_.set_undo_enabled(enabled);
}

QJSValue ScriptAppObject::open(const QString& path) {
  const ScriptApiCall api_call(host_);
  const auto id = host_.open_document_file(path);
  if (id == 0) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Could not open %1").arg(QDir::toNativeSeparators(path)));
    return QJSValue();
  }
  return make_document_value(host_, id);
}

QJSValue ScriptAppObject::newDocument(int width, int height, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  // The New Document dialog's Name and Color Mode (GitHub issues 360 and 374); both checked
  // before anything is created.
  QString name;
  bool cmyk_proof = false;
  if (options.isObject()) {
    if (const auto value = options.property(QStringLiteral("name")); !value.isUndefined() && !value.isNull()) {
      name = sanitized_new_document_name(value.toString());
      if (name.isEmpty()) {
        host_.throw_js_error(ScriptEngineHost::tr("newDocument name must hold a character a file name can use."));
        return QJSValue();
      }
    }
    if (const auto value = options.property(QStringLiteral("colorMode")); !value.isUndefined() && !value.isNull()) {
      const auto mode = value.toString();
      if (mode != QStringLiteral("rgb") && mode != QStringLiteral("cmykProof")) {
        host_.throw_js_error(ScriptEngineHost::tr("newDocument colorMode must be \"rgb\" or \"cmykProof\"."));
        return QJSValue();
      }
      cmyk_proof = mode == QStringLiteral("cmykProof");
      if (cmyk_proof && !working_cmyk_space()->has_profile()) {
        host_.throw_js_error(ScriptEngineHost::tr("CMYK Proof needs a CMYK profile. Choose one in Preferences."));
        return QJSValue();
      }
    }
  }
  const auto id = host_.create_document(width, height, name);
  if (id == 0) {
    host_.throw_js_error(
        ScriptEngineHost::tr("newDocument needs a size between 1 and 30000."));
    return QJSValue();
  }
  if (cmyk_proof) {
    if (const auto refusal = host_.set_session_cmyk_proof(id, true); !refusal.isEmpty()) {
      host_.throw_js_error(refusal);
      return QJSValue();
    }
  }
  return make_document_value(host_, id);
}

void ScriptAppObject::alert(const QString& text) { const ScriptApiCall api_call(host_); host_.show_alert(text); }

QJSValue ScriptAppObject::prompt(const QString& text, const QString& defaultValue) {
  const ScriptApiCall api_call(host_);
  bool accepted = false;
  const auto result = host_.show_prompt(text, defaultValue, &accepted);
  return accepted ? QJSValue(result) : QJSValue(QJSValue::NullValue);
}

QString ScriptAppObject::chooseFolder(const QString& title) { const ScriptApiCall api_call(host_); return host_.choose_folder(title); }

QString ScriptAppObject::chooseOpenFile(const QString& title, const QString& filter) {
  const ScriptApiCall api_call(host_);
  return host_.choose_open_file(title, filter);
}

QString ScriptAppObject::chooseSaveFile(const QString& title, const QString& filter) {
  const ScriptApiCall api_call(host_);
  return host_.choose_save_file(title, filter);
}

bool ScriptAppObject::exportPdf(const QJSValue& documents, const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  std::vector<std::int64_t> session_ids;
  const auto add_document = [&](const QJSValue& value) {
    const auto* wrapper = qobject_cast<ScriptDocumentObject*>(value.toQObject());
    if (wrapper == nullptr) {
      return false;
    }
    session_ids.push_back(wrapper->session_id());
    return true;
  };
  bool well_formed = true;
  if (documents.isArray()) {
    const auto count = documents.property(QStringLiteral("length")).toUInt();
    for (quint32 index = 0; index < count && well_formed; ++index) {
      well_formed = add_document(documents.property(index));
    }
  } else {
    well_formed = add_document(documents);
  }
  if (!well_formed || session_ids.empty()) {
    host_.throw_js_error(ScriptEngineHost::tr("exportPdf needs one open document or an array of them."));
    return false;
  }
  if (path.trimmed().isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("exportPdf needs an output path."));
    return false;
  }
  PdfExportOptions export_options;
  if (options.isObject()) {
    if (const auto value = options.property(QStringLiteral("lossless")); value.isBool()) {
      export_options.lossless = value.toBool();
    }
    if (const auto value = options.property(QStringLiteral("editableLayers")); value.isBool()) {
      export_options.editable_layers = value.toBool();
    }
    if (const auto value = options.property(QStringLiteral("missingFontsAsImages")); value.isBool()) {
      export_options.missing_fonts_as_images = value.toBool();
    }
    if (const auto value = options.property(QStringLiteral("artboardPages")); value.isBool()) {
      export_options.pages = value.toBool() ? PdfPageSource::Artboards : PdfPageSource::Canvas;
    }
    if (const auto value = options.property(QStringLiteral("pages")); value.isString()) {
      const auto pages = pdf_page_source_from_token(value.toString());
      if (!pages.has_value()) {
        host_.throw_js_error(ScriptEngineHost::tr("exportPdf pages must be \"artboards\", \"groups\" or \"canvas\"."));
        return false;
      }
      export_options.pages = *pages;
    }
    if (const auto value = options.property(QStringLiteral("keepOriginalImageData")); value.isBool()) {
      export_options.keep_original_image_data = value.toBool();
    }
    // A preset id; it names both halves of the choice, so it wins over `lossless`.
    if (const auto value = options.property(QStringLiteral("imageQuality")); value.isString()) {
      if (!apply_pdf_image_quality(value.toString(), export_options)) {
        host_.throw_js_error(ScriptEngineHost::tr(
            "exportPdf imageQuality must be \"lossless\", \"high\", \"medium\", or \"low\"."));
        return false;
      }
    }
  }
  QString error;
  if (!host_.export_sessions_to_pdf(session_ids, path, export_options, &error)) {
    host_.throw_js_error(ScriptEngineHost::tr("Could not export %1: %2").arg(QDir::toNativeSeparators(path), error));
    return false;
  }
  return true;
}

bool ScriptAppObject::runCommand(const QString& commandId) {
  const ScriptApiCall api_call(host_);
  return host_.run_app_command(commandId);
}

QStringList ScriptAppObject::commandIds() { const ScriptApiCall api_call(host_); return host_.app_command_ids(); }

QJSValue ScriptAppObject::cmyk_profile() {
  const ScriptApiCall api_call(host_);
  const auto space = working_cmyk_space();
  const auto intent = rendering_intent_token(space->intent());
  auto result = host_.engine()->newObject();
  result.setProperty(QStringLiteral("name"), QString::fromStdString(space->profile_description()));
  result.setProperty(QStringLiteral("hasProfile"), space->has_profile());
  result.setProperty(QStringLiteral("intent"),
                     QString::fromLatin1(intent.data(), static_cast<qsizetype>(intent.size())));
  result.setProperty(QStringLiteral("blackPointCompensation"), space->black_point_compensation());
  return result;
}

QJSValue ScriptAppObject::colorToCmyk(const QString& color) {
  const ScriptApiCall api_call(host_);
  QColor parsed;
  if (!parse_color(host_, color, &parsed)) {
    return {};
  }
  const auto ink = working_cmyk_space()->cmyk_from_rgb({static_cast<std::uint8_t>(parsed.red()),
                                                        static_cast<std::uint8_t>(parsed.green()),
                                                        static_cast<std::uint8_t>(parsed.blue())});
  auto result = host_.engine()->newObject();
  result.setProperty(QStringLiteral("c"), ink.cyan);
  result.setProperty(QStringLiteral("m"), ink.magenta);
  result.setProperty(QStringLiteral("y"), ink.yellow);
  result.setProperty(QStringLiteral("k"), ink.black);
  return result;
}

bool ScriptAppObject::colorInCmykGamut(const QString& color) {
  const ScriptApiCall api_call(host_);
  QColor parsed;
  if (!parse_color(host_, color, &parsed)) {
    return false;
  }
  const auto table = cmyk_gamut_check();
  // Without a profile there is no gamut to leave.
  return table == nullptr || !table->out_of_gamut({static_cast<std::uint8_t>(parsed.red()),
                                                   static_cast<std::uint8_t>(parsed.green()),
                                                   static_cast<std::uint8_t>(parsed.blue())});
}

QJSValue ScriptAppObject::cmykToColor(double cyan, double magenta, double yellow, double black) {
  const ScriptApiCall api_call(host_);
  for (const auto percent : {cyan, magenta, yellow, black}) {
    if (!(percent >= 0.0 && percent <= 100.0)) {
      host_.throw_js_error(ScriptEngineHost::tr("CMYK values are percentages from 0 to 100"));
      return {};
    }
  }
  const auto rgb = working_cmyk_space()->rgb_from_cmyk({cyan, magenta, yellow, black});
  return QColor(rgb.red, rgb.green, rgb.blue).name(QColor::HexRgb);
}

QJSValue ScriptAppObject::listFonts() {
  const ScriptApiCall api_call(host_);
  ensure_headless_system_fonts_loaded();
  auto* engine = host_.engine();
  auto result = engine->newArray();
  quint32 index = 0;
  for (const auto& family : QFontDatabase::families()) {
    if (QFontDatabase::isPrivateFamily(family)) {
      continue;
    }
    auto entry = engine->newObject();
    entry.setProperty(QStringLiteral("family"), family);
    auto styles = engine->newArray();
    quint32 style_index = 0;
    for (const auto& style : QFontDatabase::styles(family)) {
      styles.setProperty(style_index++, style);
    }
    entry.setProperty(QStringLiteral("styles"), styles);
    auto scripts = engine->newArray();
    quint32 script_index = 0;
    for (const auto system : QFontDatabase::writingSystems(family)) {
      scripts.setProperty(script_index++, QFontDatabase::writingSystemName(system));
    }
    entry.setProperty(QStringLiteral("writingSystems"), scripts);
    result.setProperty(index++, entry);
  }
  return result;
}

QJSValue ScriptAppObject::displayInfo() {
  const ScriptApiCall api_call(host_);
  auto* engine = host_.engine();
  const auto nits = [](const std::optional<double>& value) {
    return value.has_value() ? QJSValue(*value) : QJSValue(QJSValue::NullValue);
  };
  auto result = engine->newArray();
  quint32 index = 0;
  for (const auto& screen : query_display_hdr_info()) {
    auto entry = engine->newObject();
    entry.setProperty(QStringLiteral("name"), screen.name);
    entry.setProperty(QStringLiteral("x"), screen.geometry.x());
    entry.setProperty(QStringLiteral("y"), screen.geometry.y());
    entry.setProperty(QStringLiteral("width"), screen.geometry.width());
    entry.setProperty(QStringLiteral("height"), screen.geometry.height());
    entry.setProperty(QStringLiteral("devicePixelRatio"), static_cast<double>(screen.device_pixel_ratio));
    entry.setProperty(QStringLiteral("primary"), screen.primary);
    entry.setProperty(QStringLiteral("hdrQueried"), screen.hdr.queried);
    entry.setProperty(QStringLiteral("hdrEnabled"), screen.hdr.hdr_enabled);
    entry.setProperty(QStringLiteral("headroom"), screen.hdr.headroom);
    entry.setProperty(QStringLiteral("potentialHeadroom"), screen.hdr.potential_headroom);
    entry.setProperty(QStringLiteral("sdrWhiteNits"), nits(screen.hdr.sdr_white_nits));
    entry.setProperty(QStringLiteral("peakNits"), nits(screen.hdr.peak_nits));
    entry.setProperty(QStringLiteral("fullFrameNits"), nits(screen.hdr.full_frame_nits));
    entry.setProperty(QStringLiteral("minNits"), nits(screen.hdr.min_nits));
    entry.setProperty(QStringLiteral("remoteSession"), screen.hdr.remote_session);
    result.setProperty(index++, entry);
  }
  return result;
}

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
  constexpr qint64 kMaxTextFileBytes = 256LL * 1024 * 1024;
  if (file.size() > kMaxTextFileBytes) {
    host_.throw_js_error(ScriptEngineHost::tr("readTextFile: %1 is larger than 256 MB")
                             .arg(QDir::toNativeSeparators(path)));
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

void ScriptIoObject::writeTextFile(const QString& path, const QString& text) {
  // Replacement save (QSaveFile): a failed write leaves the previous file and reaches
  // the script as an exception instead of a silently truncated file.
  QString error;
  if (!save_text_file_atomically(path, text.toUtf8(), &error)) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), error));
  }
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

// ---------------------------------------------------------------------------
// ScriptRecoveryObject

#ifndef Q_OS_WASM
namespace {

QJSValue recovery_entry_value(ScriptEngineHost& host, const QString& directory,
                              const patchy::recovery::RecoveryEntry& entry, bool with_directory) {
  auto value = host.engine()->newObject();
  if (with_directory) {
    value.setProperty(QStringLiteral("directory"), directory);
  }
  value.setProperty(QStringLiteral("file"), directory + QLatin1Char('/') + QString::fromStdString(entry.file_stem) +
                                                QLatin1String(patchy::recovery::kDocumentExtension.data(),
                                                              static_cast<int>(patchy::recovery::kDocumentExtension.size())));
  value.setProperty(QStringLiteral("title"), QString::fromStdString(entry.title));
  value.setProperty(QStringLiteral("originalPath"), QString::fromStdString(entry.original_path));
  value.setProperty(QStringLiteral("savedAt"), static_cast<double>(entry.saved_at_unix_ms));
  return value;
}

}  // namespace
#endif

// ---------------------------------------------------------------------------
// ScriptPluginsObject

ScriptPluginsObject::ScriptPluginsObject(ScriptEngineHost& host) : host_(host) {}

QStringList ScriptPluginsObject::folders() const { return stored_legacy_plugin_folders(); }

QString ScriptPluginsObject::folder() const {
  const auto path = legacy_plugins_folder_path();
  if (path.isEmpty()) {
    return QString();
  }
  (void)ensure_legacy_plugins_folder();
  return QDir::fromNativeSeparators(path);
}

void ScriptPluginsObject::set_folders(const QStringList& folders) {
  const ScriptApiCall api_call(host_);
  set_stored_legacy_plugin_folders(folders);
  host_.rescan_legacy_plugins();
}

QJSValue ScriptPluginsObject::list() {
  const ScriptApiCall api_call(host_);
  return host_.legacy_plugin_list();
}

QJSValue ScriptPluginsObject::rescan() {
  const ScriptApiCall api_call(host_);
  host_.rescan_legacy_plugins();
  return host_.legacy_plugin_list();
}

// ---------------------------------------------------------------------------
// ScriptGmicObject

ScriptGmicObject::ScriptGmicObject(ScriptEngineHost& host) : host_(host) {}

bool ScriptGmicObject::available() const { return gmic_filters_available(); }

QString ScriptGmicObject::version() const { return QString::fromStdString(gmic::engine_version()); }

QJSValue ScriptGmicObject::list() {
  const ScriptApiCall api_call(host_);
  return host_.gmic_filter_list();
}

ScriptLibraryObject::ScriptLibraryObject(ScriptEngineHost& host) : host_(host) {}

QString ScriptLibraryObject::user_folder() const {
  return QDir::fromNativeSeparators(MainWindow::user_scripts_directory());
}

QString ScriptLibraryObject::bundled_folder() const {
  return QDir::fromNativeSeparators(MainWindow::bundled_scripts_directory());
}

QJSValue ScriptLibraryObject::entries_value(const ScriptScan& scan) {
  auto* engine = host_.engine();
  auto result = engine->newArray();
  quint32 index = 0;
  const std::function<void(const std::vector<ScriptFolderEntry>&, bool)> visit =
      [&](const std::vector<ScriptFolderEntry>& entries, bool bundled) {
        for (const auto& entry : entries) {
          if (entry.is_folder) {
            visit(entry.children, bundled);
            continue;
          }
          auto object = engine->newObject();
          object.setProperty(QStringLiteral("name"), entry.display_name);
          object.setProperty(QStringLiteral("fileName"), entry.file_name);
          object.setProperty(QStringLiteral("relativePath"), entry.relative_path);
          object.setProperty(QStringLiteral("path"), QDir::fromNativeSeparators(entry.path));
          object.setProperty(QStringLiteral("bundled"), bundled);
          object.setProperty(QStringLiteral("modified"), entry.is_override);
          object.setProperty(QStringLiteral("description"), entry.description);
          object.setProperty(QStringLiteral("author"), entry.author);
          object.setProperty(QStringLiteral("hotkey"), host_.script_hotkey(entry.relative_path));
          object.setProperty(QStringLiteral("defaultHotkey"), entry.hotkey);
          object.setProperty(QStringLiteral("commandId"), script_hotkey_command_id(entry.relative_path));
          result.setProperty(index++, object);
        }
      };
  visit(scan.bundled, true);
  visit(scan.user, false);
  return result;
}

QJSValue ScriptLibraryObject::entry_value(const QString& relativePath) {
  const auto entries = entries_value(host_.rescan_script_library());
  const auto count = entries.property(QStringLiteral("length")).toUInt();
  for (quint32 index = 0; index < count; ++index) {
    auto entry = entries.property(index);
    if (entry.property(QStringLiteral("relativePath")).toString() == relativePath) {
      return entry;
    }
  }
  return QJSValue(QJSValue::NullValue);
}

QJSValue ScriptLibraryObject::list() {
  const ScriptApiCall api_call(host_);
  return entries_value(host_.rescan_script_library());
}

QJSValue ScriptLibraryObject::rescan() {
  const ScriptApiCall api_call(host_);
  return entries_value(host_.rescan_script_library());
}

QJSValue ScriptLibraryObject::install(const QString& relativePath, const QString& source, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  QString hotkey;
  if (!options.isUndefined()) {
    if (!options.isObject() || options.isArray() || options.isNull()) {
      host_.throw_js_error(ScriptEngineHost::tr("patchy.scripts.install: options must be an object such as {hotkey: \"Ctrl+Alt+D\"}."));
      return QJSValue(QJSValue::NullValue);
    }
    const auto value = options.property(QStringLiteral("hotkey"));
    if (!value.isUndefined()) {
      if (!value.isString()) {
        host_.throw_js_error(ScriptEngineHost::tr("patchy.scripts.install: hotkey must be a string such as \"Ctrl+Alt+D\"."));
        return QJSValue(QJSValue::NullValue);
      }
      hotkey = value.toString();
    }
  }
  QString error;
  if (!host_.install_script(relativePath, source, &error)) {
    host_.throw_js_error(error);
    return QJSValue(QJSValue::NullValue);
  }
  const auto relative = QDir::cleanPath(QDir::fromNativeSeparators(relativePath.trimmed()));
  if (!hotkey.isEmpty() && !host_.set_script_hotkey(relative, hotkey, &error)) {
    host_.throw_js_error(error);
    return QJSValue(QJSValue::NullValue);
  }
  return entry_value(relative);
}

QString ScriptLibraryObject::setHotkey(const QString& relativePath, const QString& shortcut) {
  const ScriptApiCall api_call(host_);
  QString error;
  if (!host_.set_script_hotkey(relativePath, shortcut, &error)) {
    host_.throw_js_error(error);
    return {};
  }
  return host_.script_hotkey(relativePath);
}

QString ScriptLibraryObject::getHotkey(const QString& relativePath) {
  const ScriptApiCall api_call(host_);
  return host_.script_hotkey(relativePath);
}

ScriptRecoveryObject::ScriptRecoveryObject(ScriptEngineHost& host) : host_(host) {}

bool ScriptRecoveryObject::enabled() const { return stored_recovery_enabled(); }

void ScriptRecoveryObject::set_enabled(bool enabled) {
  const ScriptApiCall api_call(host_);
  set_stored_recovery_enabled(enabled);
#ifndef Q_OS_WASM
  host_.window().apply_recovery_preferences();
#endif
}

int ScriptRecoveryObject::interval_minutes() const { return stored_recovery_interval_minutes(); }

void ScriptRecoveryObject::set_interval_minutes(int minutes) {
  const ScriptApiCall api_call(host_);
  if (normalize_recovery_interval_minutes(minutes) != minutes) {
    host_.throw_js_error(ScriptEngineHost::tr("intervalMinutes must be 5, 10, 15, 30, or 60"));
    return;
  }
  set_stored_recovery_interval_minutes(minutes);
#ifndef Q_OS_WASM
  host_.window().apply_recovery_preferences();
#endif
}

QString ScriptRecoveryObject::directory() const {
#ifdef Q_OS_WASM
  return QString();
#else
  return QDir::fromNativeSeparators(host_.window().recovery_directory());
#endif
}

QStringList ScriptRecoveryObject::writeNow() {
  const ScriptApiCall api_call(host_);
#ifdef Q_OS_WASM
  return {};
#else
  auto written = host_.window().write_recovery_now(/*wait=*/true);
  for (auto& path : written) {
    path = QDir::fromNativeSeparators(path);
  }
  return written;
#endif
}

QJSValue ScriptRecoveryObject::listFiles() {
  const ScriptApiCall api_call(host_);
  auto array = host_.engine()->newArray();
#ifndef Q_OS_WASM
  const auto directory = this->directory();
  quint32 index = 0;
  for (const auto& entry : host_.window().list_recovery_entries()) {
    array.setProperty(index++, recovery_entry_value(host_, directory, entry, false));
  }
#endif
  return array;
}

QJSValue ScriptRecoveryObject::listOrphaned() {
  const ScriptApiCall api_call(host_);
  auto array = host_.engine()->newArray();
#ifndef Q_OS_WASM
  quint32 index = 0;
  for (const auto& orphan : host_.window().list_orphaned_recovery()) {
    const auto directory = QDir::fromNativeSeparators(to_qstring(orphan.directory));
    for (const auto& entry : orphan.entries) {
      array.setProperty(index++, recovery_entry_value(host_, directory, entry, true));
    }
  }
#endif
  return array;
}

QJSValue ScriptRecoveryObject::recoverAll() {
  const ScriptApiCall api_call(host_);
  auto array = host_.engine()->newArray();
#ifndef Q_OS_WASM
  quint32 index = 0;
  for (const auto id : host_.window().recover_orphaned_documents()) {
    array.setProperty(index++, make_document_value(host_, id));
  }
#endif
  return array;
}

int ScriptRecoveryObject::discardOrphaned() {
  const ScriptApiCall api_call(host_);
#ifdef Q_OS_WASM
  return 0;
#else
  return host_.window().discard_orphaned_recovery();
#endif
}

ScriptUiObject::ScriptUiObject(ScriptEngineHost& host) : host_(host) {}

QJSValue ScriptUiObject::createCanvas(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  if (host_.connector_mode()) {
    host_.throw_js_error(ScriptEngineHost::tr("Script windows are unavailable in the background connector. Use a document preview."));
    return {};
  }
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

QJSValue ScriptUiObject::showDialog(const QJSValue& spec) { const ScriptApiCall api_call(host_); return host_.show_form_dialog(spec); }

QJSValue ScriptUiObject::showOptions(const QJSValue& spec) {
  const ScriptApiCall api_call(host_);
  return host_.show_options_dialog(spec);
}

void ScriptUiObject::playTone(const QJSValue& frequency, const QJSValue& durationMs,
                              const QJSValue& volume, const QJSValue& wave) {
  const ScriptApiCall api_call(host_);
  host_.play_tone(frequency.isNumber() ? frequency.toNumber() : 880.0,
                  durationMs.isNumber() ? static_cast<int>(durationMs.toNumber()) : 120,
                  volume.isNumber() ? volume.toNumber() : 0.5,
                  wave.isString() ? wave.toString() : QStringLiteral("sine"));
}

void ScriptUiObject::playSound(const QString& path) { const ScriptApiCall api_call(host_); host_.play_sound_file(path); }

void ScriptUiObject::setWindowSize(int width, int height) { const ScriptApiCall api_call(host_); host_.set_window_size(width, height); }

void ScriptUiObject::setSidePanelWidth(int width) { const ScriptApiCall api_call(host_); host_.set_side_panel_width(width); }

void ScriptUiObject::setStatusMessage(const QString& message) { const ScriptApiCall api_call(host_); host_.set_status_message(message); }

void ScriptUiObject::expandPanel(const QString& name) { const ScriptApiCall api_call(host_); host_.expand_side_panel(name); }

void ScriptUiObject::collapsePanel(const QString& name) { const ScriptApiCall api_call(host_); host_.collapse_side_panel(name); }

void ScriptUiObject::closePanel(const QString& name) { const ScriptApiCall api_call(host_); host_.close_side_panel(name); }

QJSValue ScriptUiObject::panelState(const QString& name) { const ScriptApiCall api_call(host_); return host_.side_panel_state(name); }

double ScriptUiObject::zoom() const { const ScriptApiCall api_call(host_); return host_.view_zoom_percent(); }

double ScriptUiObject::preview_exposure() const {
  const ScriptApiCall api_call(host_);
  return host_.view_preview_exposure();
}
void ScriptUiObject::set_preview_exposure(double stops) {
  const ScriptApiCall api_call(host_);
  if (!std::isfinite(stops)) {
    host_.throw_js_error(ScriptEngineHost::tr("previewExposure needs a number of stops."));
    return;
  }
  host_.set_view_preview_exposure(stops);
}
void ScriptUiObject::set_zoom(double percent) {
  const ScriptApiCall api_call(host_);
  if (!std::isfinite(percent) || percent <= 0.0) {
    // std::clamp passes NaN straight through, and a zero zoom has no view.
    host_.throw_js_error(ScriptEngineHost::tr("zoom needs a number greater than 0 (percent)."));
    return;
  }
  host_.set_view_zoom_percent(percent);
}

void ScriptUiObject::fitOnScreen() { const ScriptApiCall api_call(host_); host_.fit_view_on_screen(); }
bool ScriptUiObject::proof_colors() const { const ScriptApiCall api_call(host_); return host_.view_proof_colors(); }
void ScriptUiObject::set_proof_colors(bool enabled) { const ScriptApiCall api_call(host_); host_.set_view_proof_colors(enabled); }
bool ScriptUiObject::gamut_warning() const { const ScriptApiCall api_call(host_); return host_.view_gamut_warning(); }
void ScriptUiObject::set_gamut_warning(bool enabled) { const ScriptApiCall api_call(host_); host_.set_view_gamut_warning(enabled); }
bool ScriptUiObject::pixel_grid() const { const ScriptApiCall api_call(host_); return host_.view_pixel_grid(); }
void ScriptUiObject::set_pixel_grid(bool enabled) { const ScriptApiCall api_call(host_); host_.set_view_pixel_grid(enabled); }
QJSValue ScriptUiObject::tool_palette() const { const ScriptApiCall api_call(host_); return host_.tool_palette_state(); }
void ScriptUiObject::set_tool_palette(const QJSValue& state) {
  const ScriptApiCall api_call(host_);
  host_.set_tool_palette_state(state);
}
bool ScriptUiObject::slow_mode() const { return host_.slow_mode(); }
void ScriptUiObject::set_slow_mode(bool enabled) { host_.set_slow_mode(enabled); }
bool ScriptUiObject::paused() const { return host_.paused(); }
void ScriptUiObject::set_paused(bool paused) { host_.set_paused(paused); }

void ScriptUiObject::present(const QJSValue& delayMs) {
  const double delay = delayMs.isUndefined() ? 0 : delayMs.toNumber();
  if ((!delayMs.isUndefined() && !delayMs.isNumber()) || !std::isfinite(delay) ||
      delay < 0 || delay > 1000 || delay != std::floor(delay)) {
    host_.throw_js_error(ScriptEngineHost::tr("present needs a delay from 0 to 1000 milliseconds."));
    return;
  }
  host_.present_script_view(static_cast<int>(delay));
}

bool ScriptUiObject::captureWindow(const QString& path) {
  const ScriptApiCall api_call(host_);
  if (path.trimmed().isEmpty()) {
    host_.throw_js_error(ScriptEngineHost::tr("captureWindow needs an output file path."));
    return false;
  }
  return host_.capture_window_to_file(path);
}

}  // namespace patchy::ui
