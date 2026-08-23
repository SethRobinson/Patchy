#include "formats/pdf_document_io.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "formats/gradient_placement.hpp"
#include "formats/pdf_content.hpp"
#include "formats/pdf_png_writer.hpp"
#include "formats/vector_fill_rule.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace patchy::pdf {
namespace {

using formats::Affine;

// A page bigger than this in either axis is refused rather than allocating it; the
// PSD ceiling is the same order and nothing useful is larger.
constexpr int kMaximumCanvasPixels = 30000;

std::string format_number(double value) {
  if (!std::isfinite(value)) {
    value = 0.0;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(10) << value;
  return stream.str();
}

std::string format_affine(const Affine& matrix) {
  return format_number(matrix.a) + " " + format_number(matrix.b) + " " + format_number(matrix.c) + " " +
         format_number(matrix.d) + " " + format_number(matrix.e) + " " + format_number(matrix.f);
}

std::string hex_color(RgbColor color) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text = "#";
  for (const auto channel : {color.red, color.green, color.blue}) {
    text.push_back(kDigits[(channel >> 4) & 0xF]);
    text.push_back(kDigits[channel & 0xF]);
  }
  return text;
}

// The PDF's own resource name is meaningless to a person, so layers are named after
// what they are, numbered in page order like Photoshop names pasted shapes.
std::string numbered_name(std::string_view prefix, int index) {
  return std::string(prefix) + " " + std::to_string(index);
}

// A short, human-readable stand-in for a text layer's name.
std::string text_layer_name(const std::string& utf8) {
  constexpr std::size_t kMaximum = 32;
  std::string name;
  for (const char character : utf8) {
    if (static_cast<unsigned char>(character) < 0x20) {
      name.push_back(' ');
      continue;
    }
    name.push_back(character);
    if (name.size() >= kMaximum) {
      break;
    }
  }
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  return name.empty() ? std::string("Text") : name;
}

bool paint_is_visible(const Paint& paint) {
  return paint.kind != Paint::Kind::None && paint.alpha > 0.004;
}

VectorFill fill_from_paint(const Paint& paint) {
  VectorFill fill;
  fill.kind = VectorFillKind::Solid;
  fill.color = paint.color;
  return fill;
}

// An evaluated shading becomes a real gradient fill: stops from the sampled ramp
// (collinear samples pruned so a plain two-colour ramp imports as two stops) and
// geometry through the shared placement kernel against the CANVAS box, because PDF
// pattern space is page-anchored (align_with_layer stays false).
VectorFill gradient_fill_from_shading(const ResolvedShading& shading, Rect canvas) {
  VectorFill fill;
  fill.kind = VectorFillKind::Gradient;
  auto& gradient = fill.gradient;
  gradient.name = "PDF Gradient";
  gradient.form = GradientDefinitionForm::Solid;
  // Linear interpolation between stops: smoothness 0 turns the Classic
  // catmull-rom ease off in the fill renderer, matching how the ramp was sampled.
  gradient.smoothness = 0;
  gradient.interpolation = GradientInterpolationMethod::Linear;
  gradient.type = shading.radial ? LayerStyleGradientType::Radial : LayerStyleGradientType::Linear;
  gradient.align_with_layer = false;

  gradient.color_stops.clear();
  gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F}, GradientAlphaStop{1.0F, 1.0F, 0.5F}};
  for (std::size_t index = 0; index < shading.stops.size(); ++index) {
    const auto& [location, color] = shading.stops[index];
    if (index > 0 && index + 1 < shading.stops.size()) {
      // Drop a sample that sits on the line between its neighbours; the renderer
      // interpolates linearly, so nothing changes but the stop count.
      const auto& [previous_location, previous_color] = shading.stops[index - 1];
      const auto& [next_location, next_color] = shading.stops[index + 1];
      const double spread = next_location - previous_location;
      const double fraction = spread > 1e-9 ? (location - previous_location) / spread : 0.5;
      const auto lerp = [fraction](std::uint8_t from, std::uint8_t to) {
        return from + fraction * (to - from);
      };
      const bool collinear = std::abs(lerp(previous_color.red, next_color.red) - color.red) <= 1.0 &&
                             std::abs(lerp(previous_color.green, next_color.green) - color.green) <= 1.0 &&
                             std::abs(lerp(previous_color.blue, next_color.blue) - color.blue) <= 1.0;
      if (collinear) {
        continue;
      }
    }
    gradient.color_stops.push_back({static_cast<float>(location), color, 0.5F});
  }
  if (gradient.color_stops.size() < 2) {
    const RgbColor only = gradient.color_stops.empty() ? RgbColor{0, 0, 0} : gradient.color_stops.front().color;
    gradient.color_stops = {GradientColorStop{0.0F, only, 0.5F}, GradientColorStop{1.0F, only, 0.5F}};
  }

  const formats::GradientReferenceBox box{static_cast<double>(canvas.x), static_cast<double>(canvas.y),
                                          static_cast<double>(canvas.width), static_cast<double>(canvas.height)};
  if (shading.radial) {
    // The outer circle carries the ramp's end; a distinct inner circle is a focal
    // form Patchy's radial cannot express and collapses to the plain circle.
    formats::place_radial_gradient(gradient, box, shading.x1, shading.y1, std::max(shading.r1, shading.r0));
  } else {
    formats::place_linear_gradient(gradient, box, shading.x0, shading.y0, shading.x1, shading.y1);
  }
  return fill;
}

// Builds the document and receives everything the interpreter emits, in page order,
// which is also bottom-to-top layer order.
class LayerSink final : public ContentSink {
public:
  LayerSink(Document& document, Rect canvas, const VectorReadOptions& options)
      : document_(document), canvas_(canvas), options_(options) {}

  void on_path(const PaintedPath& painted) override {
    if (painted.path.subpaths.empty()) {
      return;
    }
    const bool fill_visible = painted.has_fill && paint_is_visible(painted.fill);
    const bool stroke_visible = painted.has_stroke && paint_is_visible(painted.stroke);
    if (!fill_visible && !stroke_visible) {
      return;
    }
    if (options_.discard_offscreen && !intersects_canvas(painted.path)) {
      return;
    }
    if (painted.fill.kind == Paint::Kind::Tiling ||
        (painted.fill.kind == Paint::Kind::Shading && painted.fill.shading == nullptr)) {
      unmodelled_ = true;
      notice(painted.fill.kind == Paint::Kind::Tiling
                 ? "A PDF pattern fill was imported as a flat colour."
                 : "A PDF gradient mesh was imported as a flat colour.");
    }

    VectorShapeContent content;
    content.path = painted.path;
    // The interpreter marks the winding rule but leaves the decomposition to here,
    // because core expresses nonzero as combine ops between shape groups.
    if (painted.fill_even_odd) {
      formats::apply_even_odd(content.path);
    } else {
      formats::decompose_nonzero(content.path);
    }

    if (fill_visible) {
      content.fill = painted.fill.kind == Paint::Kind::Shading && painted.fill.shading != nullptr
                         ? gradient_fill_from_shading(*painted.fill.shading, canvas_)
                         : fill_from_paint(painted.fill);
    } else {
      content.fill.kind = VectorFillKind::None;
    }

    if (stroke_visible) {
      content.stroke.enabled = true;
      content.stroke.width = std::max(painted.stroke_style.width, 0.01);
      content.stroke.cap = painted.stroke_style.cap;
      content.stroke.join = painted.stroke_style.join;
      content.stroke.miter_limit = painted.stroke_style.miter_limit;
      // PDF strokes are always centred on the path.
      content.stroke.alignment = VectorStrokeAlignment::Center;
      content.stroke.content = fill_from_paint(painted.stroke);
      content.stroke.opacity = painted.stroke.alpha;
      // Core stores dashes as multiples of the stroke width, matching the PSD
      // descriptor; PDF writes them in user-space units.
      for (const auto dash : painted.stroke_style.dashes) {
        content.stroke.dashes.push_back(dash / std::max(content.stroke.width, 0.01));
      }
      content.stroke.dash_offset = painted.stroke_style.dash_offset / std::max(content.stroke.width, 0.01);
    }

    Layer layer(document_.allocate_layer_id(), numbered_name("Shape", ++shape_count_), LayerKind::Pixel);
    layer.metadata()[kLayerMetadataVectorShape] = "1";
    mark_layer_vector_block_dirty(layer);
    layer.set_blend_mode(painted.blend);
    // A path's constant alpha becomes layer opacity; the stroke keeps its own.
    layer.set_opacity(static_cast<float>(fill_visible ? painted.fill.alpha : painted.stroke.alpha));
    layer.set_vector_shape(std::move(content));
    attach_clip(layer, painted.clip);
    update_vector_shape_raster(layer, canvas_, &document_.metadata().patterns);
    update_vector_mask_raster(layer, canvas_);
    document_.add_layer(std::move(layer));
  }

  void on_text(const TextRun& run) override {
    if (run.utf8.empty()) {
      return;
    }
    // Render mode 7 is clip-only and 3 is invisible; the interpreter already drops
    // those, so anything here is meant to be seen.
    const auto& paint = run.render_mode == 1 ? run.stroke : run.fill;
    if (!paint_is_visible(paint)) {
      return;
    }

    Layer layer(document_.allocate_layer_id(), text_layer_name(run.utf8), LayerKind::Pixel);
    auto& metadata = layer.metadata();
    metadata[kLayerMetadataText] = run.utf8;
    metadata[kLayerMetadataTextFlow] = "point";
    metadata[kLayerMetadataTextFont] = run.family;
    // The layer-level size is an integer; the run below carries the real value.
    metadata[kLayerMetadataTextSize] = std::to_string(std::max(1, static_cast<int>(std::lround(run.font_size))));
    metadata[kLayerMetadataTextColor] = hex_color(paint.color);
    metadata[kLayerMetadataTextBold] = run.bold ? "1" : "0";
    metadata[kLayerMetadataTextItalic] = run.italic ? "1" : "0";

    // One style run covering the whole string, which is what carries the real
    // (fractional) size and the family the missing-font machinery reads.
    psd::PsdTextStyleRun style;
    style.start = 0;
    style.length = utf16_length(run.utf8);
    style.family = run.family;
    style.size = run.font_size;
    style.color = paint.color;
    style.bold = run.bold;
    style.italic = run.italic;
    // Photoshop's rule, which Patchy follows: only Regular and Bold flatten into the
    // bold flag; any other face keeps its name so the right weight resolves.
    if (!run.style.empty() && !style_is_plain(run.style)) {
      style.style = run.style;
    }
    style.horizontal_scale = run.horizontal_scale;
    // PDF character spacing is in unscaled text-space units, the same units
    // Photoshop tracking measures in thousandths of an em.
    if (run.font_size > 0.0) {
      style.tracking = run.character_spacing / run.font_size * 1000.0;
    }
    const std::array<psd::PsdTextStyleRun, 1> runs{style};
    metadata[kLayerMetadataTextRuns] = psd::serialize_patchy_text_runs(runs);

    // The Qt side finishes the job: it needs font metrics to rasterize and to
    // correct tracking against the width the PDF laid the run out at.
    metadata[kLayerMetadataPdfPendingText] = "1";
    metadata[kLayerMetadataPdfTextXfrm] = format_affine(run.transform);
    if (run.width_is_known) {
      // The advance is in text space; the transform's horizontal scale takes it to
      // document pixels.
      const double scale = std::hypot(run.transform.a, run.transform.b) / std::max(run.font_size, 1e-6);
      metadata[kLayerMetadataPdfTextIntendedWidth] = format_number(run.intended_width * scale);
    }

    layer.set_blend_mode(run.blend);
    layer.set_opacity(static_cast<float>(paint.alpha));
    // Provisional bounds; the render pass sets the real ones from the glyphs.
    layer.set_bounds(Rect{static_cast<std::int32_t>(std::lround(run.transform.e)),
                          static_cast<std::int32_t>(std::lround(run.transform.f)), 1, 1});
    attach_clip(layer, run.clip);
    document_.add_layer(std::move(layer));
    ++text_count_;
  }

  void on_image(const PlacedImage& image) override {
    std::vector<std::uint8_t> bytes;
    std::string filetype;
    if (image.codec == FilterKind::Dct) {
      // The original JPEG, untranscoded: the smart object holds exactly the bytes
      // the PDF carried.
      bytes = image.encoded;
      filetype = "JPEG";
    } else if (image.codec != FilterKind::None) {
      unmodelled_ = true;
      notice("A PDF image used a codec Patchy cannot import and was skipped.");
      return;
    } else {
      bytes = formats::encode_png_rgba8(image.rgba, image.width, image.height);
      filetype = "png ";
    }
    if (bytes.empty()) {
      return;
    }

    // The CTM maps the unit square onto the placement, so its four corners are the
    // quad. This is why images are smart objects rather than plain pixel layers: a
    // rotated or sheared image survives intact.
    const std::array<std::pair<double, double>, 4> unit = {
        std::pair{0.0, 1.0}, std::pair{1.0, 1.0}, std::pair{1.0, 0.0}, std::pair{0.0, 0.0}};
    SmartObjectPlacement placement;
    placement.uuid = generate_smart_object_uuid();
    for (std::size_t corner = 0; corner < unit.size(); ++corner) {
      const auto point = formats::map_point(image.transform, unit[corner].first, unit[corner].second);
      placement.transform[corner * 2] = point[0];
      placement.transform[corner * 2 + 1] = point[1];
    }
    placement.width = image.width;
    placement.height = image.height;
    placement.placed_type = 2;  // raster

    const auto shared = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    document_.metadata().smart_objects.add_embedded(placement.uuid,
                                                    numbered_name("Image", image_count_ + 1) +
                                                        (filetype == "JPEG" ? ".jpg" : ".png"),
                                                    filetype, shared);

    Layer layer(document_.allocate_layer_id(), numbered_name("Image", ++image_count_), LayerKind::Pixel);
    set_layer_smart_object_metadata(layer, placement, placement.uuid, {}, {}, {});
    layer.metadata()[kLayerMetadataPdfPendingImage] = "1";
    layer.set_blend_mode(image.blend);
    layer.set_opacity(static_cast<float>(image.alpha));
    layer.set_bounds(placement_bounds(placement));
    attach_clip(layer, image.clip);
    document_.add_layer(std::move(layer));
  }

  void on_shading(std::shared_ptr<const ResolvedShading> shading, const VectorPath& clip) override {
    if (shading == nullptr) {
      unmodelled_ = true;
      notice("A PDF gradient mesh was not imported.");
      return;
    }
    // `sh` paints the shading across the clip region (the whole page when nothing
    // clips), so the layer's own path IS that region and no separate mask is needed.
    VectorShapeContent content;
    if (!clip.subpaths.empty()) {
      content.path = clip;
    } else {
      PathSubpath page;
      for (const auto& corner :
           {std::pair{0.0, 0.0}, std::pair{static_cast<double>(canvas_.width), 0.0},
            std::pair{static_cast<double>(canvas_.width), static_cast<double>(canvas_.height)},
            std::pair{0.0, static_cast<double>(canvas_.height)}}) {
        PathAnchor anchor;
        anchor.anchor_x = corner.first;
        anchor.anchor_y = corner.second;
        anchor.in_x = corner.first;
        anchor.in_y = corner.second;
        anchor.out_x = corner.first;
        anchor.out_y = corner.second;
        page.anchors.push_back(anchor);
      }
      page.closed = true;
      content.path.subpaths.push_back(std::move(page));
    }
    content.fill = gradient_fill_from_shading(*shading, canvas_);

    Layer layer(document_.allocate_layer_id(), numbered_name("Shape", ++shape_count_), LayerKind::Pixel);
    layer.metadata()[kLayerMetadataVectorShape] = "1";
    mark_layer_vector_block_dirty(layer);
    layer.set_vector_shape(std::move(content));
    update_vector_shape_raster(layer, canvas_, &document_.metadata().patterns);
    document_.add_layer(std::move(layer));
  }

  void on_notice(const std::string& text) override { notice(text); }

  [[nodiscard]] std::vector<std::string> take_notices() { return std::move(notices_); }
  [[nodiscard]] int shape_layers() const noexcept { return shape_count_; }
  [[nodiscard]] int text_layers() const noexcept { return text_count_; }
  [[nodiscard]] int image_layers() const noexcept { return image_count_; }
  [[nodiscard]] bool has_unmodelled_content() const noexcept { return unmodelled_; }

private:
  static bool style_is_plain(const std::string& style) {
    // Anything Patchy can express with the bold and italic flags alone.
    static constexpr std::string_view kPlain[] = {"Regular", "Normal",     "Bold",   "Italic",
                                                  "Oblique", "BoldItalic", "BoldOblique"};
    return std::any_of(std::begin(kPlain), std::end(kPlain),
                       [&style](std::string_view candidate) { return style == candidate; });
  }

  static std::int32_t utf16_length(const std::string& utf8) {
    // Run offsets are UTF-16 code units, matching the text model.
    std::int32_t units = 0;
    for (std::size_t index = 0; index < utf8.size();) {
      const auto lead = static_cast<unsigned char>(utf8[index]);
      const int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
      units += length == 4 ? 2 : 1;  // astral characters are a surrogate pair
      index += static_cast<std::size_t>(length);
    }
    return units;
  }

  Rect placement_bounds(const SmartObjectPlacement& placement) const {
    double left = placement.transform[0];
    double top = placement.transform[1];
    double right = left;
    double bottom = top;
    for (std::size_t corner = 1; corner < 4; ++corner) {
      left = std::min(left, placement.transform[corner * 2]);
      right = std::max(right, placement.transform[corner * 2]);
      top = std::min(top, placement.transform[corner * 2 + 1]);
      bottom = std::max(bottom, placement.transform[corner * 2 + 1]);
    }
    return Rect{static_cast<std::int32_t>(std::floor(left)), static_cast<std::int32_t>(std::floor(top)),
                std::max(1, static_cast<std::int32_t>(std::ceil(right - left))),
                std::max(1, static_cast<std::int32_t>(std::ceil(bottom - top)))};
  }

  void attach_clip(Layer& layer, const VectorPath& clip) {
    if (clip.subpaths.empty()) {
      return;
    }
    LayerVectorMask mask;
    mask.path = clip;
    layer.set_vector_mask(std::move(mask));
    update_vector_mask_raster(layer, canvas_);
  }

  bool intersects_canvas(const VectorPath& path) const {
    const auto bounds = path.bounds();
    if (!bounds.has_value()) {
      return false;
    }
    return bounds->right >= 0.0 && bounds->bottom >= 0.0 &&
           bounds->left <= static_cast<double>(canvas_.width) &&
           bounds->top <= static_cast<double>(canvas_.height);
  }

  void notice(const std::string& text) {
    if (std::find(notices_.begin(), notices_.end(), text) == notices_.end()) {
      notices_.push_back(text);
    }
  }

  Document& document_;
  Rect canvas_;
  VectorReadOptions options_;
  std::vector<std::string> notices_;
  int shape_count_{0};
  int text_count_{0};
  int image_count_{0};
  bool unmodelled_{false};
};

}  // namespace

std::vector<std::string> pdf_extensions() {
  return {"pdf"};
}

bool sniff(std::span<const std::uint8_t> bytes) {
  // The header may sit up to 1024 bytes in (clause 7.5.2).
  const std::string_view window(reinterpret_cast<const char*>(bytes.data()),
                                std::min<std::size_t>(bytes.size(), 1029));
  return window.find("%PDF-") != std::string_view::npos;
}

int page_count(std::span<const std::uint8_t> bytes) {
  auto file = File::open({bytes.begin(), bytes.end()}, nullptr);
  return file.has_value() ? static_cast<int>(file->pages().size()) : 0;
}

std::array<int, 2> page_size_in_pixels(std::span<const std::uint8_t> bytes, int page, double pixels_per_point) {
  auto file = File::open({bytes.begin(), bytes.end()}, nullptr);
  if (!file.has_value() || page < 0 || page >= static_cast<int>(file->pages().size())) {
    return {0, 0};
  }
  return page_pixel_size(file->pages()[static_cast<std::size_t>(page)], pixels_per_point);
}

VectorReadResult read_page_as_vectors(std::span<const std::uint8_t> bytes, const VectorReadOptions& options) {
  std::vector<std::string> open_notices;
  auto file = File::open({bytes.begin(), bytes.end()}, &open_notices, options.password);
  if (!file.has_value()) {
    throw std::runtime_error("This file is not a readable PDF.");
  }
  if (!file->decryption_ok()) {
    // Emitting ciphertext as if it were artwork would be worse than refusing.
    throw std::runtime_error("This PDF is password protected.");
  }
  if (file->pages().empty()) {
    throw std::runtime_error("This PDF has no pages.");
  }
  if (options.page < 0 || options.page >= static_cast<int>(file->pages().size())) {
    throw std::runtime_error("The requested PDF page does not exist.");
  }

  const auto& page = file->pages()[static_cast<std::size_t>(options.page)];
  const double scale = options.pixels_per_point > 0.0 ? options.pixels_per_point : 1.0;
  const auto size = page_pixel_size(page, scale);
  if (size[0] > kMaximumCanvasPixels || size[1] > kMaximumCanvasPixels) {
    throw std::runtime_error("This PDF page is too large to import at the chosen resolution.");
  }

  VectorReadResult result{Document(size[0], size[1], PixelFormat::rgba8()), {}, 0, 0, 0, false};
  // Points to pixels: the document's resolution is what makes the imported artwork
  // print at the size the PDF intended.
  result.document.print_settings().horizontal_ppi = scale * 72.0;
  result.document.print_settings().vertical_ppi = scale * 72.0;

  const Rect canvas{0, 0, size[0], size[1]};
  LayerSink sink(result.document, canvas, options);
  execute_page(*file, page, scale, sink);

  result.notices = std::move(open_notices);
  for (auto& text : sink.take_notices()) {
    result.notices.push_back(std::move(text));
  }
  result.shape_layers = sink.shape_layers();
  result.text_layers = sink.text_layers();
  result.image_layers = sink.image_layers();
  result.has_unmodelled_content = sink.has_unmodelled_content();

  if (result.document.layers().empty()) {
    throw std::runtime_error("This PDF page holds no artwork Patchy could import as shapes or text.");
  }
  if (const auto default_layer = default_non_group_layer_id(result.document.layers()); default_layer.has_value()) {
    result.document.set_active_layer(*default_layer);
  }
  return result;
}

}  // namespace patchy::pdf
