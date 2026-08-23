#pragma once

#include "core/vector_shape.hpp"
#include "formats/affine.hpp"
#include "formats/pdf_file.hpp"
#include "formats/pdf_fonts.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The PDF content-stream interpreter (ISO 32000-1 clause 8 and 9): it walks the
// operators of a page, maintains the graphics state, and hands finished drawing
// primitives to a sink. Qt-free.
//
// The interpreter deliberately does NOT build layers. It emits paths, text runs,
// and image placements in page order, and the sink decides what each becomes:
// pdf_document_read.cpp turns them into shape layers, text layers, and smart
// objects, while tests use a recording sink. That split is what keeps form-XObject
// recursion and clip accumulation out of the layer-building code.

namespace patchy::pdf {

using formats::Affine;

// An axial or radial shading, already evaluated: geometry in DOCUMENT space and
// the colour ramp sampled from the shading's function through its colour space.
struct ResolvedShading {
  bool radial{false};
  double x0{0.0};
  double y0{0.0};
  double r0{0.0};
  double x1{0.0};
  double y1{0.0};
  double r1{0.0};
  // location 0..1, already sorted.
  std::vector<std::pair<double, RgbColor>> stops;
  bool extend_start{false};
  bool extend_end{false};
};

// A resolved paint. PDF colour spaces are collapsed to sRGB here because every
// consumer downstream is sRGB; the conversion notes live beside the code.
struct Paint {
  enum class Kind {
    None,      // the colour space said "do not paint"
    Solid,
    Shading,   // a shading pattern; `shading` carries the evaluated ramp when modelled
    Tiling,    // a tiling pattern the sink must approximate or rasterize
    Unknown,   // an unmodelled space; the sink should fall back to raster
  };
  Kind kind{Kind::Solid};
  RgbColor color{0, 0, 0};
  // Constant alpha from the ExtGState (/ca for fill, /CA for stroke).
  double alpha{1.0};
  // Set when kind is Shading or Tiling: the pattern or shading dictionary, so the
  // sink can inspect it without the interpreter modelling every shading type.
  Object source;
  // Present for axial/radial shading patterns the interpreter could evaluate; the
  // colour member holds the ramp's midpoint as the flat fallback either way.
  std::shared_ptr<const ResolvedShading> shading;
};

struct StrokeStyle {
  double width{1.0};
  VectorStrokeCap cap{VectorStrokeCap::Butt};
  VectorStrokeJoin join{VectorStrokeJoin::Miter};
  double miter_limit{10.0};
  // In user-space units, as written; the sink converts to width multiples.
  std::vector<double> dashes;
  double dash_offset{0.0};
};

// Everything a painted path carries. The path is already in device space: the CTM
// is baked into the anchors, matching how core's vector model stores geometry.
struct PaintedPath {
  VectorPath path;
  bool has_fill{false};
  bool fill_even_odd{false};
  Paint fill;
  bool has_stroke{false};
  Paint stroke;
  StrokeStyle stroke_style;
  BlendMode blend{BlendMode::Normal};
  // The accumulated clip, also in device space, empty when nothing clips.
  VectorPath clip;
};

// One run of text placed by a single show-text operator. PDF has no paragraphs:
// every run is positioned absolutely, so a run is the largest unit that can be
// turned into one text layer without guessing.
struct TextRun {
  std::string utf8;
  // Maps text space to device space at the start of the run, font size folded in.
  Affine transform;
  double font_size{0.0};
  // Family, style, and flags from the font resource, already subset-tag-stripped.
  std::string family;
  std::string style;
  bool bold{false};
  bool italic{false};
  Paint fill;
  Paint stroke;
  // Clause 9.3.6: 0 fill, 1 stroke, 2 fill+stroke, 3 invisible, 4-7 also clip.
  int render_mode{0};
  double character_spacing{0.0};
  double word_spacing{0.0};
  double horizontal_scale{1.0};
  double rise{0.0};
  // Advance width the PDF intends for this run, in text-space units before the
  // transform. The sink corrects tracking against it so a substituted font keeps
  // the original run width. Zero when the font declared no widths.
  double intended_width{0.0};
  bool width_is_known{false};
  BlendMode blend{BlendMode::Normal};
  VectorPath clip;
};

// An image XObject or inline image, placed by the CTM that maps the unit square.
struct PlacedImage {
  Affine transform;
  int width{0};
  int height{0};
  // Either the still-encoded bytes of an image codec (JPEG, JPEG 2000) with
  // `codec` set, or decoded 8-bit RGBA samples with `codec` None.
  FilterKind codec{FilterKind::None};
  std::vector<std::uint8_t> encoded;
  std::vector<std::uint8_t> rgba;
  // A stencil mask paints the current fill colour through a 1-bit mask.
  bool is_stencil{false};
  Paint stencil_fill;
  double alpha{1.0};
  BlendMode blend{BlendMode::Normal};
  VectorPath clip;
};

class ContentSink {
public:
  virtual ~ContentSink() = default;
  virtual void on_path(const PaintedPath& path) = 0;
  virtual void on_text(const TextRun& run) = 0;
  virtual void on_image(const PlacedImage& image) = 0;
  // A shading painted directly by `sh`, covering the current clip (empty = the whole
  // page). `shading` is null when the type could not be evaluated.
  virtual void on_shading(std::shared_ptr<const ResolvedShading> shading, const VectorPath& clip) = 0;
  // Reports something the interpreter could not model, once per distinct reason.
  virtual void on_notice(const std::string& text) = 0;
};

struct ContentOptions {
  // Maps PDF user space (y up, origin at the crop box corner) to Patchy document
  // pixels (y down, origin top-left). collect_page_content builds it from the page.
  Affine base_transform;
  // A guard against pathological files: form XObjects can nest, and a malicious or
  // damaged one can nest forever.
  int maximum_form_depth{12};
  // Stops runaway content; a real page is far below this.
  std::size_t maximum_primitives{200000};
};

// Runs one content stream. `resources` is the page's or form's resource dictionary.
void execute_content(const File& file, std::span<const std::uint8_t> content, const Object& resources,
                     const ContentOptions& options, ContentSink& sink);

// Concatenates a page's /Contents (which may be an array of streams that only make
// sense joined) and runs it with the transform that maps the page's crop box onto a
// document of `pixels_per_point` scale.
void execute_page(const File& file, const Page& page, double pixels_per_point, ContentSink& sink);

// The transform from PDF user space to document pixels for a page, including the
// y flip, the crop-box origin, and /Rotate.
[[nodiscard]] Affine page_base_transform(const Page& page, double pixels_per_point);
// Size in document pixels a page occupies under that transform.
[[nodiscard]] std::array<int, 2> page_pixel_size(const Page& page, double pixels_per_point);

}  // namespace patchy::pdf
