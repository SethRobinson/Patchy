#pragma once

// Photoshop's document-level text engine block ('Txt2'): one text object per type layer,
// addressed by the layer's TySh TextIndex, which Photoshop trusts over the TySh itself.
// Patchy parses an existing block (or starts from a Photoshop 2026 template), authors an
// object for every regenerated type layer from the same runs the TySh gets, keeps untouched
// objects, and writes the block back without the cached layout trees (Photoshop recomposes
// from the model; a stale cache is what corrupts). Format and key map: docs/txt2.md.

#include "psd/engine_data.hpp"
#include "psd/psd_text_runs.hpp"
#include "core/vector_shape.hpp"
#include "core/document.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::psd {

// The GUI supplies native face names for newly authored area frames on platforms
// without DirectWrite. Ordinary point/box output keeps its established codec.
using AreaTextFontResolver = std::optional<std::string> (*)(std::string_view family,
    std::string_view style, bool bold, bool italic);
void set_area_text_font_resolver(AreaTextFontResolver resolver);

// What one type layer's text object is authored from (document pixels, the TySh engine units).
struct TextEngineInputs {
  // Photoshop engine text: '\r' paragraph separators and a trailing '\r'.
  std::string text;
  std::vector<PsdTextStyleRun> runs;
  std::vector<PsdTextParagraphRun> paragraph_runs;
  // Photoshop PostScript names per run (font_index_for_run resolution), parallel to `runs`.
  std::vector<std::string> run_font_names;
  bool boxed{false};
  double box_width{0.0};
  double box_height{0.0};
  bool vertical{false};
  std::optional<VectorPath> area;
};

struct TextFrameGeometry {
  enum class Kind { Point, Box, Area, Unsupported };
  Kind kind{Kind::Unsupported};
  std::optional<VectorPath> area;
};

// Unaddressed TySh index for regenerated text beside a verbatim legacy engine,
// beyond the contiguous native object indices (the 999 probe already used TySh).
inline constexpr std::int32_t kRegeneratedTextIndexBase = 100000;

// Geometry directory only. Recognizes the older named-key point/box frames too,
// without changing the writer's verbatim preservation of that engine format.
std::vector<TextFrameGeometry> read_text_frame_geometries(std::span<const std::uint8_t> payload);

// Called after layers and document-level blocks have both been decoded.
void import_text_frame_geometry(Document& document);

// The Photoshop 2026 resources and document settings Patchy authors into (no objects, no
// frames, FontSet = AdobeInvisFont + MyriadPro-Regular). Generated: psd_text_engine_template.cpp.
std::span<const std::uint8_t> text_engine_template_bytes();

class TextEngineBlock {
 public:
  static std::optional<TextEngineBlock> parse(std::span<const std::uint8_t> payload);
  static TextEngineBlock from_template();

  [[nodiscard]] std::vector<std::uint8_t> serialize() const;
  [[nodiscard]] std::size_t object_count() const;
  // The object's engine text (with its '\r's), for tests and diagnostics.
  [[nodiscard]] std::optional<std::string> object_text(std::size_t index) const;
  // The frame index an object's view references, or nullopt.
  [[nodiscard]] std::optional<std::size_t> object_frame_index(std::size_t index) const;
  [[nodiscard]] TextFrameGeometry object_geometry(std::size_t index) const;

  // Authors a text object from the inputs and stores it at `index` (replacing the object and
  // its frame there, or appending when `index` is past the end); fonts join the FontSet as
  // needed. Returns the index the object landed at.
  std::size_t set_object(std::size_t index, const TextEngineInputs& inputs);
  // Appends an authored object; returns its index.
  std::size_t append_object(const TextEngineInputs& inputs);
  // Drops every object's cached layout tree so Photoshop recomposes from the model.
  void strip_layout_caches();

  [[nodiscard]] const EngineNode& root() const noexcept { return root_; }
  [[nodiscard]] EngineNode& root() noexcept { return root_; }

 private:
  explicit TextEngineBlock(EngineNode root) : root_(std::move(root)) {}
  [[nodiscard]] EngineNode* objects();
  [[nodiscard]] const EngineNode* objects() const;
  [[nodiscard]] EngineNode* frames();
  int font_index(const std::string& postscript_name);
  [[nodiscard]] EngineNode author_object(const TextEngineInputs& inputs, std::size_t frame_index);
  [[nodiscard]] EngineNode author_frame(const TextEngineInputs& inputs) const;

  EngineNode root_;
};

}  // namespace patchy::psd
