#include "formats/pdf_content.hpp"

#include "formats/pdf_function.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace patchy::pdf {
namespace {

using formats::map_point;
using formats::multiply;

constexpr double kPointsPerInch = 72.0;

// --- Colour spaces -------------------------------------------------------------

enum class SpaceKind { Gray, Rgb, Cmyk, Indexed, Separation, Lab, Pattern, Unknown };

struct ColorSpace {
  SpaceKind kind{SpaceKind::Gray};
  int components{1};
  // Indexed: the base space's components and the decoded lookup table.
  std::shared_ptr<ColorSpace> base;
  std::vector<std::uint8_t> lookup;
  int high_value{0};
  // Pattern: the space patterns paint through, when the pattern is uncoloured.
  Object pattern_dict;
  // Separation/DeviceN: the tint transform into `base` (reused as the alternate
  // space). Null when the function could not be loaded; the grey fallback applies.
  std::shared_ptr<PdfFunction> tint;
};

RgbColor to_rgb_color(double red, double green, double blue) {
  const auto clamp_byte = [](double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
  };
  return RgbColor{clamp_byte(red), clamp_byte(green), clamp_byte(blue)};
}

// CMYK to sRGB by the naive ink mix. Patchy uses the same formula wherever no ICC
// profile is available (see the PSD CMYK notes in docs/file-formats.md), so PDF and
// PSD imports agree on colours that carry no profile.
RgbColor cmyk_to_rgb(double cyan, double magenta, double yellow, double black) {
  return to_rgb_color((1.0 - std::clamp(cyan, 0.0, 1.0)) * (1.0 - std::clamp(black, 0.0, 1.0)),
                      (1.0 - std::clamp(magenta, 0.0, 1.0)) * (1.0 - std::clamp(black, 0.0, 1.0)),
                      (1.0 - std::clamp(yellow, 0.0, 1.0)) * (1.0 - std::clamp(black, 0.0, 1.0)));
}

BlendMode blend_mode_from_name(std::string_view name) {
  // Clause 11.3.5. The names match Patchy's modes one for one except Compatible,
  // which is a legacy alias for Normal.
  if (name == "Multiply") return BlendMode::Multiply;
  if (name == "Screen") return BlendMode::Screen;
  if (name == "Overlay") return BlendMode::Overlay;
  if (name == "Darken") return BlendMode::Darken;
  if (name == "Lighten") return BlendMode::Lighten;
  if (name == "ColorDodge") return BlendMode::ColorDodge;
  if (name == "ColorBurn") return BlendMode::ColorBurn;
  if (name == "HardLight") return BlendMode::HardLight;
  if (name == "SoftLight") return BlendMode::SoftLight;
  if (name == "Difference") return BlendMode::Difference;
  if (name == "Exclusion") return BlendMode::Exclusion;
  if (name == "Hue") return BlendMode::Hue;
  if (name == "Saturation") return BlendMode::Saturation;
  if (name == "Color") return BlendMode::Color;
  if (name == "Luminosity") return BlendMode::Luminosity;
  return BlendMode::Normal;
}

// --- Graphics state ------------------------------------------------------------

struct TextState {
  double character_spacing{0.0};
  double word_spacing{0.0};
  double horizontal_scale{1.0};
  double leading{0.0};
  double font_size{0.0};
  double rise{0.0};
  int render_mode{0};
  std::string font_resource;
};

struct GraphicsState {
  Affine ctm;
  ColorSpace fill_space;
  ColorSpace stroke_space;
  Paint fill;
  Paint stroke;
  StrokeStyle stroke_style;
  BlendMode blend{BlendMode::Normal};
  VectorPath clip;
  bool has_clip{false};
  TextState text;
};

// --- Path building -------------------------------------------------------------

// Accumulates PDF path operators into a core VectorPath, mapping through the CTM as
// it goes so the result is already in document space (core anchors are absolute).
class PathBuilder {
public:
  void move_to(const Affine& ctm, double x, double y) {
    flush();
    const auto point = map_point(ctm, x, y);
    current_.anchors.clear();
    current_.closed = false;
    append_anchor(point[0], point[1]);
    open_ = true;
    start_x_ = x;
    start_y_ = y;
    last_x_ = x;
    last_y_ = y;
  }

  void line_to(const Affine& ctm, double x, double y) {
    if (!open_) {
      move_to(ctm, x, y);
      return;
    }
    const auto point = map_point(ctm, x, y);
    append_anchor(point[0], point[1]);
    last_x_ = x;
    last_y_ = y;
  }

  // A cubic segment: the previous anchor gains an outgoing handle, the new anchor an
  // incoming one, which is exactly how core stores beziers.
  void curve_to(const Affine& ctm, double c1x, double c1y, double c2x, double c2y, double x, double y) {
    if (!open_) {
      move_to(ctm, c1x, c1y);
    }
    const auto control1 = map_point(ctm, c1x, c1y);
    const auto control2 = map_point(ctm, c2x, c2y);
    const auto end = map_point(ctm, x, y);
    if (!current_.anchors.empty()) {
      auto& previous = current_.anchors.back();
      previous.out_x = control1[0];
      previous.out_y = control1[1];
    }
    append_anchor(end[0], end[1]);
    auto& anchor = current_.anchors.back();
    anchor.in_x = control2[0];
    anchor.in_y = control2[1];
    last_x_ = x;
    last_y_ = y;
  }

  void close() {
    if (open_ && current_.anchors.size() > 1) {
      current_.closed = true;
      flush();
      // PDF leaves the current point at the subpath start after `h`.
      open_ = false;
      last_x_ = start_x_;
      last_y_ = start_y_;
    }
  }

  void rectangle(const Affine& ctm, double x, double y, double width, double height) {
    flush();
    current_.anchors.clear();
    for (const auto& corner : {std::pair{x, y}, std::pair{x + width, y}, std::pair{x + width, y + height},
                               std::pair{x, y + height}}) {
      const auto point = map_point(ctm, corner.first, corner.second);
      append_anchor(point[0], point[1]);
    }
    current_.closed = true;
    flush();
    open_ = false;
    last_x_ = x;
    last_y_ = y;
  }

  [[nodiscard]] VectorPath take(bool even_odd) {
    flush();
    // Even-odd is core's within-group rule, so every subpath shares group 0.
    // Nonzero needs the containment decomposition, which the sink applies because
    // it owns the shared helper; the flag rides along on fill_rule_value.
    for (auto& subpath : path_.subpaths) {
      subpath.shape_group = 0;
      subpath.op = PathCombineOp::Add;
    }
    path_.fill_rule_value = even_odd ? 1 : 0;
    auto result = std::move(path_);
    path_ = VectorPath{};
    open_ = false;
    return result;
  }

  [[nodiscard]] bool empty() const noexcept { return path_.subpaths.empty() && current_.anchors.empty(); }
  [[nodiscard]] double current_x() const noexcept { return last_x_; }
  [[nodiscard]] double current_y() const noexcept { return last_y_; }
  [[nodiscard]] bool has_current_point() const noexcept { return open_ || !path_.subpaths.empty(); }

  void reset() {
    path_ = VectorPath{};
    current_ = PathSubpath{};
    open_ = false;
  }

private:
  void append_anchor(double x, double y) {
    PathAnchor anchor;
    anchor.anchor_x = x;
    anchor.anchor_y = y;
    anchor.in_x = x;
    anchor.in_y = y;
    anchor.out_x = x;
    anchor.out_y = y;
    current_.anchors.push_back(anchor);
  }

  void flush() {
    if (current_.anchors.size() >= 2) {
      path_.subpaths.push_back(current_);
    }
    current_ = PathSubpath{};
  }

  VectorPath path_;
  PathSubpath current_;
  bool open_{false};
  double start_x_{0.0};
  double start_y_{0.0};
  double last_x_{0.0};
  double last_y_{0.0};
};

// --- The interpreter -----------------------------------------------------------

class Interpreter {
public:
  Interpreter(const File& file, const ContentOptions& options, ContentSink& sink)
      : file_(file), options_(options), sink_(sink) {}

  void run(std::span<const std::uint8_t> content, const Object& resources, int form_depth) {
    if (form_depth > options_.maximum_form_depth) {
      notice("A PDF form was nested too deeply and was skipped.");
      return;
    }
    Lexer lexer(content);
    std::vector<Object> operands;

    while (primitives_ < options_.maximum_primitives) {
      const auto before = lexer.position();
      auto token = lexer.next();
      if (!token.has_value() || lexer.position() == before) {
        break;
      }
      if (!token->is_keyword()) {
        if (operands.size() < 64) {
          operands.push_back(std::move(token->object));
        }
        continue;
      }
      // "BI ... ID <bytes> EI" needs the raw byte range, so it is handled by the
      // lexer position rather than by operand collection.
      if (token->keyword == "BI") {
        read_inline_image(lexer, resources);
        operands.clear();
        continue;
      }
      execute(token->keyword, operands, resources, form_depth);
      operands.clear();
    }
  }

  [[nodiscard]] GraphicsState& state() { return state_; }
  void set_state(GraphicsState state) { state_ = std::move(state); }

private:
  double operand(const std::vector<Object>& operands, std::size_t index_from_end, double fallback = 0.0) const {
    if (index_from_end >= operands.size()) {
      return fallback;
    }
    return operands[operands.size() - 1 - index_from_end].number(fallback);
  }

  void notice(const std::string& text) {
    if (reported_.insert(text).second) {
      sink_.on_notice(text);
    }
  }

  // --- Colour --------------------------------------------------------------

  ColorSpace resolve_color_space(const Object& spec, const Object& resources, int depth = 0) {
    ColorSpace space;
    if (depth > 8) {
      space.kind = SpaceKind::Unknown;
      return space;
    }
    const auto& resolved = file_.resolve(spec);

    if (resolved.is_name()) {
      const auto name = resolved.name();
      if (name == "DeviceGray" || name == "G" || name == "CalGray") {
        space.kind = SpaceKind::Gray;
        space.components = 1;
        return space;
      }
      if (name == "DeviceRGB" || name == "RGB" || name == "CalRGB") {
        space.kind = SpaceKind::Rgb;
        space.components = 3;
        return space;
      }
      if (name == "DeviceCMYK" || name == "CMYK") {
        space.kind = SpaceKind::Cmyk;
        space.components = 4;
        return space;
      }
      if (name == "Pattern") {
        space.kind = SpaceKind::Pattern;
        space.components = 1;
        return space;
      }
      // A named space defined in the resource dictionary.
      const auto& named = file_.get(file_.get(resources, "ColorSpace"), name);
      if (!named.is_null()) {
        return resolve_color_space(named, resources, depth + 1);
      }
      space.kind = SpaceKind::Unknown;
      return space;
    }

    const auto* array = resolved.array();
    if (array == nullptr || array->empty()) {
      space.kind = SpaceKind::Unknown;
      return space;
    }
    const auto family = file_.resolve((*array)[0]).name();

    if (family == "ICCBased") {
      // The profile is not applied: /N tells us the component count, and PDF
      // requires the alternate to be the matching device space anyway.
      const auto& stream = array->size() > 1 ? file_.resolve((*array)[1]) : null_object();
      const auto components = static_cast<int>(file_.get(stream, "N").integer(3));
      space.components = components;
      space.kind = components == 4 ? SpaceKind::Cmyk : components == 1 ? SpaceKind::Gray : SpaceKind::Rgb;
      return space;
    }
    if (family == "CalRGB") {
      space.kind = SpaceKind::Rgb;
      space.components = 3;
      return space;
    }
    if (family == "CalGray") {
      space.kind = SpaceKind::Gray;
      space.components = 1;
      return space;
    }
    if (family == "Lab") {
      space.kind = SpaceKind::Lab;
      space.components = 3;
      return space;
    }
    if (family == "Indexed" || family == "I") {
      space.kind = SpaceKind::Indexed;
      space.components = 1;
      if (array->size() >= 4) {
        space.base = std::make_shared<ColorSpace>(resolve_color_space((*array)[1], resources, depth + 1));
        space.high_value = static_cast<int>(file_.resolve((*array)[2]).integer(0));
        const auto& table = file_.resolve((*array)[3]);
        if (table.is_string()) {
          const auto text = table.string();
          space.lookup.assign(text.begin(), text.end());
        } else if (table.stream() != nullptr) {
          space.lookup = file_.stream_data(table).data;
        }
      }
      return space;
    }
    if (family == "Separation" || family == "DeviceN") {
      space.kind = SpaceKind::Separation;
      if (family == "Separation") {
        space.components = 1;
      } else {
        const auto& names = array->size() > 1 ? file_.resolve((*array)[1]) : null_object();
        const auto* name_array = names.array();
        space.components = name_array != nullptr ? static_cast<int>(name_array->size()) : 1;
      }
      // [/Separation name alternateSpace tintTransform]: the function turns a tint
      // into alternate-space components, which is the colour the file really means.
      if (array->size() >= 4) {
        space.base = std::make_shared<ColorSpace>(resolve_color_space((*array)[2], resources, depth + 1));
        space.tint = load_function(file_, (*array)[3]);
      }
      if (space.tint == nullptr || space.base == nullptr || space.base->kind == SpaceKind::Unknown) {
        space.tint = nullptr;
        // Without the transform the honest approximation is "more ink is darker",
        // right for the spot colours these spaces almost always carry.
        notice("A PDF spot or separation colour was approximated as a shade of grey.");
      }
      return space;
    }
    if (family == "Pattern") {
      space.kind = SpaceKind::Pattern;
      space.components = 1;
      if (array->size() > 1) {
        space.base = std::make_shared<ColorSpace>(resolve_color_space((*array)[1], resources, depth + 1));
      }
      return space;
    }

    space.kind = SpaceKind::Unknown;
    return space;
  }

  RgbColor color_from_components(const ColorSpace& space, const std::vector<double>& values) {
    switch (space.kind) {
      case SpaceKind::Gray: {
        const double gray = values.empty() ? 0.0 : values.back();
        return to_rgb_color(gray, gray, gray);
      }
      case SpaceKind::Rgb: {
        if (values.size() < 3) {
          return to_rgb_color(0.0, 0.0, 0.0);
        }
        const auto base = values.size() - 3;
        return to_rgb_color(values[base], values[base + 1], values[base + 2]);
      }
      case SpaceKind::Cmyk: {
        if (values.size() < 4) {
          return to_rgb_color(0.0, 0.0, 0.0);
        }
        const auto base = values.size() - 4;
        return cmyk_to_rgb(values[base], values[base + 1], values[base + 2], values[base + 3]);
      }
      case SpaceKind::Lab: {
        // L* alone is a close enough neutral for the rare Lab fill; a full
        // conversion would need the white point and gains little here.
        const double lightness = values.empty() ? 0.0 : std::clamp(values.front() / 100.0, 0.0, 1.0);
        return to_rgb_color(lightness, lightness, lightness);
      }
      case SpaceKind::Separation: {
        if (space.tint != nullptr && space.base != nullptr) {
          std::vector<double> alternate;
          space.tint->evaluate(values, alternate);
          return color_from_components(*space.base, alternate);
        }
        // Tint 0 is no ink (white), tint 1 is full ink (black).
        const double tint = values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
        const double level = 1.0 - std::clamp(tint, 0.0, 1.0);
        return to_rgb_color(level, level, level);
      }
      case SpaceKind::Indexed: {
        if (space.base == nullptr || space.lookup.empty()) {
          return to_rgb_color(0.0, 0.0, 0.0);
        }
        const auto index = static_cast<std::size_t>(
            std::clamp(values.empty() ? 0.0 : values.back(), 0.0, static_cast<double>(space.high_value)));
        const auto components = static_cast<std::size_t>(space.base->components);
        std::vector<double> base_values;
        base_values.reserve(components);
        for (std::size_t offset = 0; offset < components; ++offset) {
          const auto position = index * components + offset;
          base_values.push_back(position < space.lookup.size() ? space.lookup[position] / 255.0 : 0.0);
        }
        return color_from_components(*space.base, base_values);
      }
      case SpaceKind::Pattern:
      case SpaceKind::Unknown: break;
    }
    return to_rgb_color(0.0, 0.0, 0.0);
  }

  // --- Painting ------------------------------------------------------------

  // Every painting operator ends the current path, and a pending W clip uses that
  // same path, so both are served from one take().
  void paint(bool fill, bool stroke, bool even_odd) {
    auto path = builder_.take(even_odd);
    if (!path.subpaths.empty() && (fill || stroke)) {
      PaintedPath painted;
      painted.path = path;
      painted.has_fill = fill;
      painted.fill_even_odd = even_odd;
      painted.fill = state_.fill;
      painted.has_stroke = stroke;
      painted.stroke = state_.stroke;
      painted.stroke_style = state_.stroke_style;
      // A stroke width is a length, so a non-uniform CTM cannot be expressed
      // exactly; the area scale is the same approximation the SVG reader makes.
      painted.stroke_style.width *= formats::average_scale(state_.ctm);
      painted.blend = state_.blend;
      if (state_.has_clip) {
        painted.clip = state_.clip;
      }
      sink_.on_path(painted);
      ++primitives_;
    }
    apply_pending_clip(std::move(path));
    builder_.reset();
  }

  void apply_pending_clip(VectorPath clip_path) {
    if (pending_clip_ == PendingClip::None) {
      return;
    }
    // The clip takes effect after the painting operator that follows W (clause
    // 8.5.4). Intersecting is expressed as separate shape groups combined with
    // Intersect, which is what core's rasterizer does between groups.
    if (!clip_path.subpaths.empty()) {
      if (!state_.has_clip) {
        state_.clip = std::move(clip_path);
        for (auto& subpath : state_.clip.subpaths) {
          subpath.shape_group = 0;
          subpath.op = PathCombineOp::Add;
        }
        state_.has_clip = true;
      } else {
        const auto group = state_.clip.next_shape_group();
        for (auto& subpath : clip_path.subpaths) {
          subpath.shape_group = group;
          subpath.op = PathCombineOp::Intersect;
          state_.clip.subpaths.push_back(subpath);
        }
      }
    }
    pending_clip_ = PendingClip::None;
  }

  // --- Text ----------------------------------------------------------------

  const Font& font_for(const std::string& resource_name, const Object& resources) {
    const auto cached = fonts_.find(resource_name);
    if (cached != fonts_.end()) {
      return cached->second;
    }
    const auto& font_dict = file_.get(file_.get(resources, "Font"), resource_name);
    auto font = load_font(file_, font_dict);
    if (font.family.empty()) {
      font.family = "Helvetica";
      notice("A PDF font resource was missing and was substituted.");
    }
    return fonts_.emplace(resource_name, std::move(font)).first->second;
  }

  void show_text(std::string_view bytes, const Object& resources) {
    if (state_.text.font_resource.empty()) {
      return;
    }
    const auto& font = font_for(state_.text.font_resource, resources);
    const auto glyphs = font.decode(bytes);
    if (glyphs.empty()) {
      return;
    }

    // Clause 9.4.4: the rendering matrix is [size*Th 0 0 size 0 rise] x Tm x CTM.
    const Affine parameters{state_.text.font_size * state_.text.horizontal_scale,
                            0.0,
                            0.0,
                            state_.text.font_size,
                            0.0,
                            state_.text.rise};
    const auto render_matrix = multiply(state_.ctm, multiply(text_matrix_, parameters));

    std::string utf8;
    double advance = 0.0;
    bool recovered_any = false;
    for (const auto& glyph : glyphs) {
      if (glyph.unicode != 0xFFFD) {
        append_utf8(utf8, glyph.unicode);
        recovered_any = true;
      }
      advance += glyph.width * state_.text.font_size + state_.text.character_spacing +
                 (glyph.is_word_space ? state_.text.word_spacing : 0.0);
    }
    advance *= state_.text.horizontal_scale;

    if (state_.text.render_mode != 3 && state_.text.render_mode != 7 && recovered_any) {
      TextRun run;
      run.utf8 = std::move(utf8);
      run.transform = render_matrix;
      run.font_size = state_.text.font_size;
      run.family = font.family;
      run.style = font.style;
      run.bold = font.bold;
      run.italic = font.italic;
      run.fill = state_.fill;
      run.stroke = state_.stroke;
      run.render_mode = state_.text.render_mode;
      run.character_spacing = state_.text.character_spacing;
      run.word_spacing = state_.text.word_spacing;
      run.horizontal_scale = state_.text.horizontal_scale;
      run.rise = state_.text.rise;
      run.intended_width = advance;
      run.width_is_known = font.has_widths;
      run.blend = state_.blend;
      if (state_.has_clip) {
        run.clip = state_.clip;
      }
      sink_.on_text(run);
      ++primitives_;
    } else if (!recovered_any && state_.text.render_mode != 3) {
      notice("Some PDF text used a font with no Unicode mapping and could not be recovered as text.");
    }

    // The text position advances even for invisible text, which is how scanned
    // pages carry their OCR layer.
    text_matrix_ = multiply(text_matrix_, Affine{1.0, 0.0, 0.0, 1.0, advance, 0.0});
  }

  static void append_utf8(std::string& out, char32_t code_point) {
    if (code_point < 0x80) {
      out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
  }

  void show_text_array(const Object& array_object, const Object& resources) {
    const auto* array = array_object.array();
    if (array == nullptr) {
      return;
    }
    for (const auto& entry : *array) {
      if (entry.is_string()) {
        show_text(entry.string(), resources);
        continue;
      }
      if (!entry.is_number()) {
        continue;
      }
      // A number moves the pen back by that many thousandths of an em (clause
      // 9.4.3), which is how kerning is expressed inside a single run.
      const double shift = -entry.number(0.0) / 1000.0 * state_.text.font_size * state_.text.horizontal_scale;
      text_matrix_ = multiply(text_matrix_, Affine{1.0, 0.0, 0.0, 1.0, shift, 0.0});
    }
  }

  // --- XObjects and images -------------------------------------------------

  void draw_xobject(const std::string& name, const Object& resources, int form_depth) {
    const auto& xobject = file_.get(file_.get(resources, "XObject"), name);
    if (xobject.stream() == nullptr) {
      return;
    }
    const auto subtype = file_.get(xobject, "Subtype").name();
    if (subtype == "Form") {
      const auto saved_state = state_;
      const auto saved_text = text_matrix_;
      if (auto matrix = file_.numbers(xobject.get("Matrix")); matrix.size() >= 6) {
        state_.ctm = multiply(state_.ctm, Affine{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]});
      }
      // A form's /BBox clips its content.
      if (auto box = file_.numbers(xobject.get("BBox")); box.size() >= 4) {
        PathBuilder clip_builder;
        clip_builder.rectangle(state_.ctm, std::min(box[0], box[2]), std::min(box[1], box[3]),
                               std::abs(box[2] - box[0]), std::abs(box[3] - box[1]));
        pending_clip_ = PendingClip::NonZero;
        apply_pending_clip(clip_builder.take(false));
      }
      const auto& form_resources = file_.get(xobject, "Resources");
      const auto data = file_.stream_data(xobject);
      // A form has its own font cache scope: the same resource name can mean a
      // different font inside it.
      auto saved_fonts = std::move(fonts_);
      fonts_.clear();
      run(data.data, form_resources.is_dictionary() ? form_resources : resources, form_depth + 1);
      fonts_ = std::move(saved_fonts);
      state_ = saved_state;
      text_matrix_ = saved_text;
      return;
    }
    if (subtype == "Image") {
      emit_image(xobject, resources);
    }
  }

  // Decodes an image XObject's samples to RGBA, or keeps the codec's own bytes when
  // Qt will do a better job of it than we would.
  void emit_image(const Object& image_object, const Object& resources) {
    PlacedImage placed;
    placed.transform = state_.ctm;
    placed.width = static_cast<int>(file_.get_any(image_object, "Width", "W").integer(0));
    placed.height = static_cast<int>(file_.get_any(image_object, "Height", "H").integer(0));
    placed.alpha = state_.fill.alpha;
    placed.blend = state_.blend;
    if (state_.has_clip) {
      placed.clip = state_.clip;
    }
    if (placed.width <= 0 || placed.height <= 0) {
      return;
    }
    // A guard against a damaged /Width that would allocate the world.
    if (static_cast<std::int64_t>(placed.width) * placed.height > 80'000'000) {
      notice("A PDF image was too large to import and was skipped.");
      return;
    }

    const bool stencil = file_.get_any(image_object, "ImageMask", "IM").boolean(false);
    const auto data = file_.stream_data(image_object);
    if (data.image_codec != FilterKind::None) {
      // JPEG and JPEG 2000 go to Qt still encoded, so nothing is transcoded and the
      // original bytes are what land in the smart object.
      placed.codec = data.image_codec;
      placed.encoded = data.data;
      sink_.on_image(placed);
      ++primitives_;
      return;
    }
    if (data.data.empty()) {
      return;
    }

    if (stencil) {
      placed.is_stencil = true;
      placed.stencil_fill = state_.fill;
      const auto decode = file_.numbers(file_.get_any(image_object, "Decode", "D"));
      const bool invert = decode.size() >= 1 && decode[0] == 1.0;
      placed.rgba = expand_stencil(data.data, placed.width, placed.height, invert, state_.fill.color);
      sink_.on_image(placed);
      ++primitives_;
      return;
    }

    const auto bits = static_cast<int>(file_.get_any(image_object, "BitsPerComponent", "BPC").integer(8));
    const auto space = resolve_color_space(file_.get_any(image_object, "ColorSpace", "CS"), resources);
    placed.rgba = expand_samples(data.data, placed.width, placed.height, bits, space);
    if (placed.rgba.empty()) {
      notice("A PDF image used a colour format Patchy could not decode and was skipped.");
      return;
    }
    apply_soft_mask(image_object, placed);
    sink_.on_image(placed);
    ++primitives_;
  }

  std::vector<std::uint8_t> expand_stencil(const std::vector<std::uint8_t>& data, int width, int height, bool invert,
                                           RgbColor color) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
    const std::size_t row_bytes = (static_cast<std::size_t>(width) + 7) / 8;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(x) / 8;
        if (index >= data.size()) {
          continue;
        }
        const bool bit = (data[index] >> (7 - (x % 8))) & 1U;
        // Sample 0 paints by default (clause 8.9.6.2); /Decode [1 0] flips that.
        const bool paint = invert ? bit : !bit;
        const auto out = (static_cast<std::size_t>(y) * width + x) * 4;
        rgba[out + 0] = color.red;
        rgba[out + 1] = color.green;
        rgba[out + 2] = color.blue;
        rgba[out + 3] = paint ? 255 : 0;
      }
    }
    return rgba;
  }

  std::vector<std::uint8_t> expand_samples(const std::vector<std::uint8_t>& data, int width, int height, int bits,
                                           const ColorSpace& space) {
    const int components = std::max(1, space.components);
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
      return {};
    }
    if (space.kind == SpaceKind::Unknown || space.kind == SpaceKind::Pattern) {
      return {};
    }
    const std::size_t bits_per_row = static_cast<std::size_t>(width) * components * bits;
    const std::size_t row_bytes = (bits_per_row + 7) / 8;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
    const double maximum = static_cast<double>((1U << std::min(bits, 16)) - 1U);

    std::vector<double> values(static_cast<std::size_t>(components));
    for (int y = 0; y < height; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * row_bytes;
      for (int x = 0; x < width; ++x) {
        for (int component = 0; component < components; ++component) {
          const std::size_t sample_index = (static_cast<std::size_t>(x) * components + component);
          double raw = 0.0;
          if (bits == 8) {
            const auto position = row + sample_index;
            raw = position < data.size() ? data[position] : 0.0;
          } else if (bits == 16) {
            const auto position = row + sample_index * 2;
            raw = position + 1 < data.size() ? (data[position] << 8 | data[position + 1]) : 0.0;
          } else {
            const std::size_t bit_offset = sample_index * static_cast<std::size_t>(bits);
            const std::size_t position = row + bit_offset / 8;
            if (position < data.size()) {
              const int shift = 8 - bits - static_cast<int>(bit_offset % 8);
              raw = (data[position] >> shift) & ((1U << bits) - 1U);
            }
          }
          // Indexed samples are palette indices, not fractions.
          values[static_cast<std::size_t>(component)] =
              space.kind == SpaceKind::Indexed ? raw : raw / maximum;
        }
        const auto color = color_from_components(space, values);
        const auto out = (static_cast<std::size_t>(y) * width + x) * 4;
        rgba[out + 0] = color.red;
        rgba[out + 1] = color.green;
        rgba[out + 2] = color.blue;
        rgba[out + 3] = 255;
      }
    }
    return rgba;
  }

  // /SMask is a separate grayscale image supplying alpha (clause 11.6.5.3).
  void apply_soft_mask(const Object& image_object, PlacedImage& placed) {
    const auto& mask = file_.get(image_object, "SMask");
    if (mask.stream() == nullptr) {
      return;
    }
    const auto mask_width = static_cast<int>(file_.get(mask, "Width").integer(0));
    const auto mask_height = static_cast<int>(file_.get(mask, "Height").integer(0));
    if (mask_width <= 0 || mask_height <= 0) {
      return;
    }
    const auto mask_data = file_.stream_data(mask);
    if (mask_data.image_codec != FilterKind::None || mask_data.data.empty()) {
      notice("A PDF image's transparency mask used a codec Patchy could not decode; the image imported opaque.");
      return;
    }
    const auto bits = static_cast<int>(file_.get(mask, "BitsPerComponent").integer(8));
    ColorSpace gray;
    gray.kind = SpaceKind::Gray;
    gray.components = 1;
    const auto mask_rgba = expand_samples(mask_data.data, mask_width, mask_height, bits, gray);
    if (mask_rgba.empty()) {
      return;
    }
    // The mask may be a different resolution; sample it nearest-neighbour.
    for (int y = 0; y < placed.height; ++y) {
      const int mask_y = mask_height == placed.height ? y : y * mask_height / std::max(1, placed.height);
      for (int x = 0; x < placed.width; ++x) {
        const int mask_x = mask_width == placed.width ? x : x * mask_width / std::max(1, placed.width);
        const auto source = (static_cast<std::size_t>(mask_y) * mask_width + mask_x) * 4;
        const auto target = (static_cast<std::size_t>(y) * placed.width + x) * 4;
        if (source + 3 < mask_rgba.size() && target + 3 < placed.rgba.size()) {
          placed.rgba[target + 3] = mask_rgba[source];  // grey level is the alpha
        }
      }
    }
  }

  void read_inline_image(Lexer& lexer, const Object& resources) {
    // BI <key value pairs> ID <bytes> EI. The dictionary uses abbreviated keys.
    Dictionary dict;
    while (true) {
      const auto before = lexer.position();
      auto token = lexer.next();
      if (!token.has_value() || lexer.position() == before) {
        return;
      }
      if (token->is_keyword()) {
        if (token->keyword == "ID") {
          break;
        }
        continue;
      }
      if (!token->object.is_name()) {
        continue;
      }
      auto key = std::string(token->object.name());
      auto value = lexer.next_object();
      dict.emplace(std::move(key), std::move(value));
    }
    // Exactly one whitespace byte separates ID from the data.
    lexer.seek(lexer.position() + 1);
    const auto data_start = lexer.position();

    // Scan for "EI" delimited by whitespace, which is the only way to find the end:
    // an inline image's length is not required to be declared.
    const auto haystack = lexer.remaining();
    std::size_t end = haystack.size();
    for (std::size_t index = 0; index + 1 < haystack.size(); ++index) {
      if (haystack[index] != 'E' || haystack[index + 1] != 'I') {
        continue;
      }
      const bool before_ok = index == 0 || is_whitespace(haystack[index - 1]);
      const bool after_ok = index + 2 >= haystack.size() || !is_regular(haystack[index + 2]);
      if (before_ok && after_ok) {
        end = index;
        break;
      }
    }
    std::size_t length = end;
    while (length > 0 && is_whitespace(haystack[length - 1])) {
      --length;
    }
    lexer.seek(data_start + std::min(end + 2, haystack.size()));

    // Reuse the XObject path by presenting the inline image as a stream object.
    Object inline_stream(RawStream{std::move(dict), data_start, length});
    emit_inline_image(inline_stream, haystack.subspan(0, length), resources);
  }

  void emit_inline_image(const Object& stream_object, std::span<const std::uint8_t> data, const Object& resources) {
    PlacedImage placed;
    placed.transform = state_.ctm;
    placed.width = static_cast<int>(file_.get_any(stream_object, "Width", "W").integer(0));
    placed.height = static_cast<int>(file_.get_any(stream_object, "Height", "H").integer(0));
    placed.alpha = state_.fill.alpha;
    placed.blend = state_.blend;
    if (state_.has_clip) {
      placed.clip = state_.clip;
    }
    if (placed.width <= 0 || placed.height <= 0 ||
        static_cast<std::int64_t>(placed.width) * placed.height > 80'000'000) {
      return;
    }

    auto decoded = apply_filter_chain(data, file_.filter_chain(stream_object));
    if (decoded.image_codec != FilterKind::None) {
      placed.codec = decoded.image_codec;
      placed.encoded = std::move(decoded.data);
      sink_.on_image(placed);
      ++primitives_;
      return;
    }
    if (decoded.data.empty()) {
      return;
    }
    if (file_.get_any(stream_object, "ImageMask", "IM").boolean(false)) {
      const auto decode = file_.numbers(file_.get_any(stream_object, "Decode", "D"));
      placed.is_stencil = true;
      placed.stencil_fill = state_.fill;
      placed.rgba = expand_stencil(decoded.data, placed.width, placed.height,
                                   !decode.empty() && decode[0] == 1.0, state_.fill.color);
    } else {
      const auto bits = static_cast<int>(file_.get_any(stream_object, "BitsPerComponent", "BPC").integer(8));
      const auto space = resolve_color_space(file_.get_any(stream_object, "ColorSpace", "CS"), resources);
      placed.rgba = expand_samples(decoded.data, placed.width, placed.height, bits, space);
    }
    if (placed.rgba.empty()) {
      return;
    }
    sink_.on_image(placed);
    ++primitives_;
  }

  // --- ExtGState -----------------------------------------------------------

  void apply_ext_gstate(const std::string& name, const Object& resources) {
    const auto& gstate = file_.get(file_.get(resources, "ExtGState"), name);
    if (!gstate.is_dictionary()) {
      return;
    }
    if (const auto& value = file_.get(gstate, "ca"); value.is_number()) {
      state_.fill.alpha = std::clamp(value.number(1.0), 0.0, 1.0);
    }
    if (const auto& value = file_.get(gstate, "CA"); value.is_number()) {
      state_.stroke.alpha = std::clamp(value.number(1.0), 0.0, 1.0);
    }
    if (const auto& value = file_.get(gstate, "LW"); value.is_number()) {
      state_.stroke_style.width = value.number(1.0);
    }
    if (const auto& value = file_.get(gstate, "LC"); value.is_number()) {
      state_.stroke_style.cap = cap_from_code(static_cast<int>(value.integer(0)));
    }
    if (const auto& value = file_.get(gstate, "LJ"); value.is_number()) {
      state_.stroke_style.join = join_from_code(static_cast<int>(value.integer(0)));
    }
    if (const auto& value = file_.get(gstate, "ML"); value.is_number()) {
      state_.stroke_style.miter_limit = value.number(10.0);
    }
    const auto& blend = file_.get(gstate, "BM");
    if (blend.is_name()) {
      state_.blend = blend_mode_from_name(blend.name());
    } else if (const auto* array = blend.array(); array != nullptr && !array->empty()) {
      state_.blend = blend_mode_from_name(file_.resolve((*array)[0]).name());
    }
    const auto& soft_mask = file_.get(gstate, "SMask");
    if (soft_mask.is_dictionary()) {
      notice("A PDF soft mask was not applied; the affected artwork imported without it.");
    }
    if (const auto& font = file_.get(gstate, "Font"); font.is_array()) {
      notice("A PDF graphics state set a font directly; that text may be positioned differently.");
    }
  }

  static VectorStrokeCap cap_from_code(int code) {
    switch (code) {
      case 1: return VectorStrokeCap::Round;
      case 2: return VectorStrokeCap::Square;
      default: return VectorStrokeCap::Butt;
    }
  }

  static VectorStrokeJoin join_from_code(int code) {
    switch (code) {
      case 1: return VectorStrokeJoin::Round;
      case 2: return VectorStrokeJoin::Bevel;
      default: return VectorStrokeJoin::Miter;
    }
  }

  // --- The operator table --------------------------------------------------

  void execute(const std::string& op, const std::vector<Object>& operands, const Object& resources,
               int form_depth) {
    // Graphics state
    if (op == "q") {
      if (stack_.size() < 64) {
        stack_.push_back(state_);
      }
      return;
    }
    if (op == "Q") {
      if (!stack_.empty()) {
        state_ = stack_.back();
        stack_.pop_back();
      }
      return;
    }
    if (op == "cm") {
      if (operands.size() >= 6) {
        const Affine matrix{operand(operands, 5), operand(operands, 4), operand(operands, 3),
                            operand(operands, 2), operand(operands, 1), operand(operands, 0)};
        state_.ctm = multiply(state_.ctm, matrix);
      }
      return;
    }
    if (op == "w") {
      state_.stroke_style.width = operand(operands, 0, 1.0);
      return;
    }
    if (op == "J") {
      state_.stroke_style.cap = cap_from_code(static_cast<int>(operand(operands, 0)));
      return;
    }
    if (op == "j") {
      state_.stroke_style.join = join_from_code(static_cast<int>(operand(operands, 0)));
      return;
    }
    if (op == "M") {
      state_.stroke_style.miter_limit = operand(operands, 0, 10.0);
      return;
    }
    if (op == "d") {
      state_.stroke_style.dashes.clear();
      state_.stroke_style.dash_offset = operand(operands, 0);
      if (operands.size() >= 2) {
        if (const auto* array = operands[operands.size() - 2].array(); array != nullptr) {
          for (const auto& entry : *array) {
            state_.stroke_style.dashes.push_back(entry.number(0.0));
          }
        }
      }
      return;
    }
    if (op == "gs") {
      if (!operands.empty() && operands.back().is_name()) {
        apply_ext_gstate(std::string(operands.back().name()), resources);
      }
      return;
    }
    if (op == "i" || op == "ri" || op == "MP" || op == "DP" || op == "BMC" || op == "BDC" || op == "EMC" ||
        op == "BX" || op == "EX") {
      return;  // rendering hints and marked content have no bearing on the artwork
    }

    // Path construction
    if (op == "m") {
      builder_.move_to(state_.ctm, operand(operands, 1), operand(operands, 0));
      return;
    }
    if (op == "l") {
      builder_.line_to(state_.ctm, operand(operands, 1), operand(operands, 0));
      return;
    }
    if (op == "c") {
      builder_.curve_to(state_.ctm, operand(operands, 5), operand(operands, 4), operand(operands, 3),
                        operand(operands, 2), operand(operands, 1), operand(operands, 0));
      return;
    }
    if (op == "v") {
      // The first control point is the current point.
      const double x = operand(operands, 1);
      const double y = operand(operands, 0);
      builder_.curve_to(state_.ctm, builder_.current_x(), builder_.current_y(), operand(operands, 3),
                        operand(operands, 2), x, y);
      return;
    }
    if (op == "y") {
      // The second control point is the endpoint.
      const double x = operand(operands, 1);
      const double y = operand(operands, 0);
      builder_.curve_to(state_.ctm, operand(operands, 3), operand(operands, 2), x, y, x, y);
      return;
    }
    if (op == "h") {
      builder_.close();
      return;
    }
    if (op == "re") {
      builder_.rectangle(state_.ctm, operand(operands, 3), operand(operands, 2), operand(operands, 1),
                         operand(operands, 0));
      return;
    }

    // Path painting
    if (op == "n") {
      // Paints nothing; its only job is to end a path, usually after W.
      paint(false, false, pending_clip_ == PendingClip::EvenOdd);
      return;
    }
    if (op == "f" || op == "F" || op == "f*") {
      paint(true, false, op == "f*");
      return;
    }
    if (op == "S" || op == "s") {
      if (op == "s") {
        builder_.close();
      }
      paint(false, true, false);
      return;
    }
    if (op == "B" || op == "B*" || op == "b" || op == "b*") {
      if (op == "b" || op == "b*") {
        builder_.close();
      }
      const bool even_odd = op == "B*" || op == "b*";
      paint(true, true, even_odd);
      return;
    }
    if (op == "W") {
      pending_clip_ = PendingClip::NonZero;
      return;
    }
    if (op == "W*") {
      pending_clip_ = PendingClip::EvenOdd;
      return;
    }

    // Colour
    if (op == "g" || op == "G") {
      auto& space = op == "g" ? state_.fill_space : state_.stroke_space;
      space = ColorSpace{SpaceKind::Gray, 1, nullptr, {}, 0, {}};
      set_color(op == "g", to_rgb_color(operand(operands, 0), operand(operands, 0), operand(operands, 0)));
      return;
    }
    if (op == "rg" || op == "RG") {
      auto& space = op == "rg" ? state_.fill_space : state_.stroke_space;
      space = ColorSpace{SpaceKind::Rgb, 3, nullptr, {}, 0, {}};
      set_color(op == "rg", to_rgb_color(operand(operands, 2), operand(operands, 1), operand(operands, 0)));
      return;
    }
    if (op == "k" || op == "K") {
      auto& space = op == "k" ? state_.fill_space : state_.stroke_space;
      space = ColorSpace{SpaceKind::Cmyk, 4, nullptr, {}, 0, {}};
      set_color(op == "k",
                cmyk_to_rgb(operand(operands, 3), operand(operands, 2), operand(operands, 1), operand(operands, 0)));
      return;
    }
    if (op == "cs" || op == "CS") {
      if (operands.empty()) {
        return;
      }
      auto space = resolve_color_space(operands.back(), resources);
      const bool is_fill = op == "cs";
      (is_fill ? state_.fill_space : state_.stroke_space) = space;
      // Setting a space resets the colour to its initial value (clause 8.6.8):
      // black for the device spaces, which is what the default components give.
      auto& paint = is_fill ? state_.fill : state_.stroke;
      paint.kind = space.kind == SpaceKind::Pattern ? Paint::Kind::None : Paint::Kind::Solid;
      paint.color = space.kind == SpaceKind::Cmyk ? cmyk_to_rgb(0, 0, 0, 1) : to_rgb_color(0, 0, 0);
      return;
    }
    if (op == "sc" || op == "SC" || op == "scn" || op == "SCN") {
      const bool is_fill = op == "sc" || op == "scn";
      const auto& space = is_fill ? state_.fill_space : state_.stroke_space;
      auto& paint = is_fill ? state_.fill : state_.stroke;
      if (!operands.empty() && operands.back().is_name()) {
        // A pattern name rather than components.
        const auto& pattern = file_.get(file_.get(resources, "Pattern"), operands.back().name());
        const auto pattern_type = file_.get(pattern, "PatternType").integer(0);
        paint.kind = pattern_type == 2 ? Paint::Kind::Shading : Paint::Kind::Tiling;
        paint.source = pattern;
        paint.shading.reset();
        if (pattern_type == 2) {
          // Pattern space is the page's DEFAULT user space, not the CTM in force
          // when the pattern is used (clause 8.7.3.1), so the geometry maps through
          // the base transform and the pattern's own /Matrix.
          Affine to_device = options_.base_transform;
          if (auto matrix = file_.numbers(pattern.get("Matrix")); matrix.size() >= 6) {
            to_device = multiply(to_device,
                                 Affine{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]});
          }
          paint.shading = resolve_shading(file_.get(pattern, "Shading"), resources, to_device);
          if (paint.shading != nullptr && !paint.shading->stops.empty()) {
            // The ramp midpoint is the flat fallback wherever a gradient cannot go
            // (a stroke, an unmodelled consumer).
            paint.color = paint.shading->stops[paint.shading->stops.size() / 2].second;
          }
        }
        // An uncoloured tiling pattern paints in the components that precede the
        // name; a shading pattern carries its own colours.
        if (pattern_type != 2 && operands.size() > 1 && space.base != nullptr) {
          std::vector<double> values;
          for (std::size_t index = 0; index + 1 < operands.size(); ++index) {
            values.push_back(operands[index].number(0.0));
          }
          paint.color = color_from_components(*space.base, values);
        }
        return;
      }
      std::vector<double> values;
      values.reserve(operands.size());
      for (const auto& item : operands) {
        values.push_back(item.number(0.0));
      }
      paint.kind = Paint::Kind::Solid;
      paint.color = color_from_components(space, values);
      paint.shading.reset();
      return;
    }

    // Text
    if (op == "BT") {
      text_matrix_ = Affine{};
      text_line_matrix_ = Affine{};
      return;
    }
    if (op == "ET") {
      return;
    }
    if (op == "Tf") {
      state_.text.font_size = operand(operands, 0);
      if (operands.size() >= 2 && operands[operands.size() - 2].is_name()) {
        state_.text.font_resource = std::string(operands[operands.size() - 2].name());
      }
      return;
    }
    if (op == "Td") {
      text_line_matrix_ = multiply(text_line_matrix_,
                                   Affine{1.0, 0.0, 0.0, 1.0, operand(operands, 1), operand(operands, 0)});
      text_matrix_ = text_line_matrix_;
      return;
    }
    if (op == "TD") {
      state_.text.leading = -operand(operands, 0);
      text_line_matrix_ = multiply(text_line_matrix_,
                                   Affine{1.0, 0.0, 0.0, 1.0, operand(operands, 1), operand(operands, 0)});
      text_matrix_ = text_line_matrix_;
      return;
    }
    if (op == "Tm") {
      if (operands.size() >= 6) {
        text_line_matrix_ = Affine{operand(operands, 5), operand(operands, 4), operand(operands, 3),
                                   operand(operands, 2), operand(operands, 1), operand(operands, 0)};
        text_matrix_ = text_line_matrix_;
      }
      return;
    }
    if (op == "T*") {
      text_line_matrix_ = multiply(text_line_matrix_, Affine{1.0, 0.0, 0.0, 1.0, 0.0, -state_.text.leading});
      text_matrix_ = text_line_matrix_;
      return;
    }
    if (op == "TL") {
      state_.text.leading = operand(operands, 0);
      return;
    }
    if (op == "Tc") {
      state_.text.character_spacing = operand(operands, 0);
      return;
    }
    if (op == "Tw") {
      state_.text.word_spacing = operand(operands, 0);
      return;
    }
    if (op == "Tz") {
      state_.text.horizontal_scale = operand(operands, 0, 100.0) / 100.0;
      return;
    }
    if (op == "Ts") {
      state_.text.rise = operand(operands, 0);
      return;
    }
    if (op == "Tr") {
      state_.text.render_mode = static_cast<int>(operand(operands, 0));
      return;
    }
    if (op == "Tj") {
      if (!operands.empty() && operands.back().is_string()) {
        show_text(operands.back().string(), resources);
      }
      return;
    }
    if (op == "TJ") {
      if (!operands.empty()) {
        show_text_array(operands.back(), resources);
      }
      return;
    }
    if (op == "'" || op == "\"") {
      if (op == "\"" && operands.size() >= 3) {
        state_.text.word_spacing = operand(operands, 2);
        state_.text.character_spacing = operand(operands, 1);
      }
      text_line_matrix_ = multiply(text_line_matrix_, Affine{1.0, 0.0, 0.0, 1.0, 0.0, -state_.text.leading});
      text_matrix_ = text_line_matrix_;
      if (!operands.empty() && operands.back().is_string()) {
        show_text(operands.back().string(), resources);
      }
      return;
    }
    if (op == "d0" || op == "d1") {
      return;  // Type3 glyph metrics
    }

    // XObjects and shadings
    if (op == "Do") {
      if (!operands.empty() && operands.back().is_name()) {
        draw_xobject(std::string(operands.back().name()), resources, form_depth);
      }
      return;
    }
    if (op == "sh") {
      if (!operands.empty() && operands.back().is_name()) {
        const auto& shading = file_.get(file_.get(resources, "Shading"), operands.back().name());
        if (!shading.is_null()) {
          // Unlike a shading PATTERN, `sh` paints in the current user space.
          sink_.on_shading(resolve_shading(shading, resources, state_.ctm),
                           state_.has_clip ? state_.clip : VectorPath{});
          ++primitives_;
        }
      }
      return;
    }
  }

  // Evaluates an axial (type 2) or radial (type 3) shading into document space.
  // Mesh and function-based shadings (1, 4-7) return null; the caller reports.
  std::shared_ptr<const ResolvedShading> resolve_shading(const Object& shading_object, const Object& resources,
                                                         const Affine& to_device) {
    const auto& shading = file_.resolve(shading_object);
    const auto type = file_.get(shading, "ShadingType").integer(0);
    if (type != 2 && type != 3) {
      return nullptr;
    }
    const auto coords = file_.numbers(shading.get("Coords"));
    if ((type == 2 && coords.size() < 4) || (type == 3 && coords.size() < 6)) {
      return nullptr;
    }
    auto functions = FunctionSet::load(file_, file_.get(shading, "Function"));
    if (!functions.valid()) {
      return nullptr;
    }
    const auto space = resolve_color_space(file_.get(shading, "ColorSpace"), resources);
    if (space.kind == SpaceKind::Unknown || space.kind == SpaceKind::Pattern) {
      return nullptr;
    }

    auto resolved = std::make_shared<ResolvedShading>();
    resolved->radial = type == 3;
    if (type == 2) {
      const auto start = map_point(to_device, coords[0], coords[1]);
      const auto end = map_point(to_device, coords[2], coords[3]);
      resolved->x0 = start[0];
      resolved->y0 = start[1];
      resolved->x1 = end[0];
      resolved->y1 = end[1];
    } else {
      const auto start = map_point(to_device, coords[0], coords[1]);
      const auto end = map_point(to_device, coords[3], coords[4]);
      const double radius_scale = formats::average_scale(to_device);
      resolved->x0 = start[0];
      resolved->y0 = start[1];
      resolved->r0 = coords[2] * radius_scale;
      resolved->x1 = end[0];
      resolved->y1 = end[1];
      resolved->r1 = coords[5] * radius_scale;
    }

    auto domain = file_.numbers(shading.get("Domain"));
    if (domain.size() < 2) {
      domain = {0.0, 1.0};
    }
    // Sixteen segments approximate any of the function types well; the PSD gradient
    // model interpolates linearly between stops, matching how the samples are taken.
    constexpr int kSampleCount = 17;
    for (int sample = 0; sample < kSampleCount; ++sample) {
      const double fraction = static_cast<double>(sample) / (kSampleCount - 1);
      const double t = domain[0] + fraction * (domain[1] - domain[0]);
      const auto components = functions.evaluate(t);
      resolved->stops.emplace_back(fraction, color_from_components(space, components));
    }

    const auto& extend = file_.get(shading, "Extend");
    if (const auto* array = extend.array(); array != nullptr && array->size() >= 2) {
      resolved->extend_start = file_.resolve((*array)[0]).boolean(false);
      resolved->extend_end = file_.resolve((*array)[1]).boolean(false);
    }
    return resolved;
  }

  void set_color(bool is_fill, RgbColor color) {
    auto& paint = is_fill ? state_.fill : state_.stroke;
    paint.kind = Paint::Kind::Solid;
    paint.color = color;
    paint.source = Object();
    paint.shading.reset();
  }

  const File& file_;
  ContentOptions options_;
  ContentSink& sink_;
  GraphicsState state_;
  std::vector<GraphicsState> stack_;
  PathBuilder builder_;
  Affine text_matrix_;
  Affine text_line_matrix_;
  std::unordered_map<std::string, Font> fonts_;
  std::set<std::string> reported_;
  std::size_t primitives_{0};

  enum class PendingClip { None, NonZero, EvenOdd };
  PendingClip pending_clip_{PendingClip::None};
};

}  // namespace

Affine page_base_transform(const Page& page, double pixels_per_point) {
  const double width = page.crop_box[2] - page.crop_box[0];
  const double height = page.crop_box[3] - page.crop_box[1];
  const double scale = pixels_per_point;

  // PDF user space is y-up with the origin at the crop box's lower-left corner;
  // document space is y-down from the top-left. The flip lives here so every path,
  // text run, and image downstream is already in document pixels.
  Affine flip{scale, 0.0, 0.0, -scale, -page.crop_box[0] * scale, page.crop_box[3] * scale};

  switch (page.rotate) {
    case 90:
      // Rotate clockwise about the origin, then shift back into view.
      return multiply(Affine{0.0, 1.0, -1.0, 0.0, height * scale, 0.0}, flip);
    case 180:
      return multiply(Affine{-1.0, 0.0, 0.0, -1.0, width * scale, height * scale}, flip);
    case 270:
      return multiply(Affine{0.0, -1.0, 1.0, 0.0, 0.0, width * scale}, flip);
    default: return flip;
  }
}

std::array<int, 2> page_pixel_size(const Page& page, double pixels_per_point) {
  const double width = (page.crop_box[2] - page.crop_box[0]) * pixels_per_point;
  const double height = (page.crop_box[3] - page.crop_box[1]) * pixels_per_point;
  const bool swapped = page.rotate == 90 || page.rotate == 270;
  return {std::max(1, static_cast<int>(std::lround(swapped ? height : width))),
          std::max(1, static_cast<int>(std::lround(swapped ? width : height)))};
}

void execute_content(const File& file, std::span<const std::uint8_t> content, const Object& resources,
                     const ContentOptions& options, ContentSink& sink) {
  Interpreter interpreter(file, options, sink);
  interpreter.state().ctm = options.base_transform;
  interpreter.run(content, resources, 0);
}

void execute_page(const File& file, const Page& page, double pixels_per_point, ContentSink& sink) {
  // /Contents may be an array whose members only parse when joined: a producer is
  // free to split an operator across two streams (clause 7.7.3.3).
  std::vector<std::uint8_t> content;
  const auto& contents = file.get(page.dict, "Contents");
  const auto append = [&](const Object& stream_object) {
    const auto data = file.stream_data(stream_object);
    content.insert(content.end(), data.data.begin(), data.data.end());
    content.push_back('\n');
  };
  if (contents.stream() != nullptr) {
    append(contents);
  } else if (const auto* array = contents.array(); array != nullptr) {
    for (const auto& entry : *array) {
      const auto& stream_object = file.resolve(entry);
      if (stream_object.stream() != nullptr) {
        append(stream_object);
      }
    }
  }
  if (content.empty()) {
    return;
  }

  ContentOptions options;
  options.base_transform = page_base_transform(page, pixels_per_point);
  execute_content(file, content, page.resources, options, sink);
}

}  // namespace patchy::pdf
