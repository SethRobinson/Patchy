// Text WRITE side of the PSD text codec: the Patchy text-runs metadata
// parse/serialize helpers, imported-text preview regeneration (Win32 GDI with
// the placeholder fallback), and the generated engine-data + TySh type-tool
// payload writers. Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

int photoshop_justification_from_patchy_alignment(std::string_view alignment) {
  const auto lower = ascii_lower_copy(std::string(alignment));
  if (lower == "right") {
    return 1;
  }
  if (lower == "center") {
    return 2;
  }
  if (lower == "justify") {
    return 3;
  }
  return 0;
}

std::optional<std::string_view> layer_metadata_value(const Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  if (found == layer.metadata().end()) {
    return std::nullopt;
  }
  return std::string_view(found->second);
}

int parse_int_or(std::string_view text, int fallback) {
  const std::string copy(text);
  char* end = nullptr;
  const auto parsed = std::strtol(copy.c_str(), &end, 10);
  return end == copy.c_str() ? fallback : static_cast<int>(parsed);
}

std::optional<double> parse_double(std::string_view text) {
  const std::string copy(text);
  char* end = nullptr;
  const auto parsed = std::strtod(copy.c_str(), &end);
  if (end == copy.c_str() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<std::string_view> split_tab_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1U;
  }
  return fields;
}

}  // namespace

// Whether serialized runs carry layout data only a Photoshop import can produce (fixed leading
// or tracking; Patchy's own text UI cannot author either). Reopening a converted layer must
// restore the Photoshop layout marker even though its regenerated TySh is Patchy-signed, while
// reopened native text (auto-leading only) keeps native layout.
bool serialized_runs_have_photoshop_leading_signals(std::string_view runs_text) {
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    const auto line = line_end == std::string_view::npos
                          ? runs_text.substr(line_start)
                          : runs_text.substr(line_start, line_end - line_start);
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    const auto fields = split_tab_fields(line);
    if (fields.size() >= 8U && fields[7] != "auto") {
      if (const auto leading = parse_double(fields[7]);
          leading.has_value() && std::isfinite(*leading) && *leading > 0.0) {
        return true;
      }
    }
    if (fields.size() >= 9U) {
      if (const auto tracking = parse_double(fields[8]);
          tracking.has_value() && std::isfinite(*tracking) && std::abs(*tracking) > 0.0001) {
        return true;
      }
    }
    for (std::size_t scale_field = 9; scale_field <= 10 && scale_field < fields.size(); ++scale_field) {
      if (const auto scale = parse_double(fields[scale_field]);
          scale.has_value() && std::isfinite(*scale) && std::abs(*scale - 1.0) > 0.0001) {
        return true;
      }
    }
  }
  return false;
}

namespace {

std::vector<std::string_view> split_space_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start < line.size()) {
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])) != 0) {
      ++start;
    }
    const auto end = line.find_first_of(" \t\r\n", start);
    if (end == std::string_view::npos) {
      if (start < line.size()) {
        fields.push_back(line.substr(start));
      }
      break;
    }
    if (end > start) {
      fields.push_back(line.substr(start, end - start));
    }
    start = end + 1U;
  }
  return fields;
}

}  // namespace

std::string serialize_text_bounds(const PsdTextBoundsD& bounds) {
  return serialize_double_array(std::array<double, 4>{bounds.left, bounds.top, bounds.right, bounds.bottom});
}

std::string serialize_int_array(const std::array<int, 4>& values) {
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      result.push_back(' ');
    }
    result += std::to_string(values[index]);
  }
  return result;
}

namespace {

std::optional<std::array<double, 6>> parse_double_array6(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 6U) {
    return std::nullopt;
  }
  std::array<double, 6> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto parsed = parse_double(fields[index]);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    values[index] = *parsed;
  }
  return values;
}

std::optional<PsdTextBoundsD> parse_text_bounds_metadata(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 4U) {
    return std::nullopt;
  }
  std::array<double, 4> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto parsed = parse_double(fields[index]);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    values[index] = *parsed;
  }
  return PsdTextBoundsD{values[0], values[1], values[2], values[3]};
}

std::optional<std::array<int, 4>> parse_int_array4(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 4U) {
    return std::nullopt;
  }
  std::array<int, 4> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = parse_int_or(fields[index], 0);
  }
  return values;
}

PsdTextStyleRun fallback_text_run_from_metadata(const Layer& layer) {
  PsdTextStyleRun fallback;
  if (const auto family = layer_metadata_value(layer, kLayerMetadataTextFont);
      family.has_value() && !family->empty() && ascii_lower_copy(std::string(*family)) != "psd text") {
    fallback.family = std::string(*family);
  }
  if (const auto size = layer_metadata_value(layer, kLayerMetadataTextSize); size.has_value()) {
    fallback.size = std::clamp(static_cast<double>(parse_int_or(*size, static_cast<int>(std::lround(fallback.size)))),
                               1.0, static_cast<double>(kMaxTextSizePixels));
  }
  if (const auto color = layer_metadata_value(layer, kLayerMetadataTextColor); color.has_value()) {
    fallback.color = rgb_color_from_hex(*color).value_or(fallback.color);
  }
  fallback.bold = layer_metadata_value(layer, kLayerMetadataTextBold).value_or(std::string_view{}) == "true";
  fallback.italic = layer_metadata_value(layer, kLayerMetadataTextItalic).value_or(std::string_view{}) == "true";
  return fallback;
}

std::vector<PsdTextStyleRun> parse_patchy_text_runs_metadata(std::string_view runs_text,
                                                             std::string_view plain_text,
                                                             const PsdTextStyleRun& fallback) {
  std::vector<PsdTextStyleRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(plain_text).size());
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    auto line = line_end == std::string_view::npos ? runs_text.substr(line_start)
                                                   : runs_text.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    if (line.empty() || line == "v1" || line == "v2" || line == "v3") {
      continue;
    }

    const auto fields = split_tab_fields(line);
    if (fields.size() < 7U) {
      continue;
    }
    PsdTextStyleRun run = fallback;
    run.start = std::clamp(parse_int_or(fields[0], 0), 0, std::max(0, text_length));
    run.length = std::max(0, parse_int_or(fields[1], 0));
    run.size = std::clamp(parse_double(fields[2]).value_or(fallback.size), 1.0,
                          static_cast<double>(kMaxTextSizePixels));
    run.bold = parse_int_or(fields[3], fallback.bold ? 1 : 0) != 0;
    run.italic = parse_int_or(fields[4], fallback.italic ? 1 : 0) != 0;
    if (auto color = rgb_color_from_hex(fields[5]); color.has_value()) {
      run.color = *color;
    }
    run.family = percent_decode(fields[6]);
    if (run.family.empty() || ascii_lower_copy(run.family) == "psd text") {
      run.family = fallback.family;
    }
    if (fields.size() >= 8U) {
      if (fields[7] == "auto") {
        run.auto_leading = true;
        run.leading.reset();
      } else if (const auto leading = parse_double(fields[7]); leading.has_value() && std::isfinite(*leading) &&
                                                               *leading > 0.0) {
        run.leading = *leading;
      }
    }
    if (fields.size() >= 9U) {
      if (const auto tracking = parse_double(fields[8]);
          tracking.has_value() && std::isfinite(*tracking) && std::abs(*tracking) < 10000.0) {
        run.tracking = *tracking;
      }
    }
    if (fields.size() >= 10U) {
      if (const auto scale = parse_double(fields[9]);
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        run.horizontal_scale = *scale;
      }
    }
    if (fields.size() >= 11U) {
      if (const auto scale = parse_double(fields[10]);
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        run.vertical_scale = *scale;
      }
    }
    if (run.length <= 0 || run.start >= text_length) {
      continue;
    }
    run.length = std::min(run.length, text_length - run.start);
    runs.push_back(std::move(run));
  }

  std::sort(runs.begin(), runs.end(), [](const PsdTextStyleRun& lhs, const PsdTextStyleRun& rhs) {
    return lhs.start < rhs.start;
  });
  if (runs.empty() && text_length > 0) {
    auto run = fallback;
    run.start = 0;
    run.length = text_length;
    runs.push_back(std::move(run));
  } else if (!runs.empty()) {
    const auto covered = runs.back().start + runs.back().length;
    if (covered < text_length) {
      auto run = fallback;
      run.start = covered;
      run.length = text_length - covered;
      runs.push_back(std::move(run));
    }
  }
  return runs;
}

std::vector<PsdTextParagraphRun> paragraph_runs_from_text_line_breaks(std::string_view text, int justification) {
  std::vector<PsdTextParagraphRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
  if (text_length <= 0) {
    runs.push_back(PsdTextParagraphRun{0, 1, justification});
    return runs;
  }

  int start_units = 0;
  std::size_t segment_start = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\n') {
      continue;
    }
    const auto segment = text.substr(segment_start, index + 1U - segment_start);
    const auto length = static_cast<int>(utf8_to_utf16(segment).size());
    if (length > 0) {
      runs.push_back(PsdTextParagraphRun{start_units, length, justification});
      start_units += length;
    }
    segment_start = index + 1U;
  }

  if (segment_start < text.size()) {
    const auto length = static_cast<int>(utf8_to_utf16(text.substr(segment_start)).size());
    if (length > 0) {
      runs.push_back(PsdTextParagraphRun{start_units, length, justification});
    }
  }
  if (runs.empty()) {
    runs.push_back(PsdTextParagraphRun{0, text_length, justification});
  }
  return runs;
}

std::vector<PsdTextStyleRun> split_single_style_run_on_line_breaks(std::vector<PsdTextStyleRun> runs,
                                                                   std::string_view text) {
  if (text.find('\n') == std::string_view::npos || runs.size() != 1U) {
    return runs;
  }
  const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
  if (text_length <= 0 || runs.front().start != 0 || runs.front().length < text_length) {
    return runs;
  }

  const auto line_runs = paragraph_runs_from_text_line_breaks(text, 0);
  if (line_runs.size() <= 1U) {
    return runs;
  }

  std::vector<PsdTextStyleRun> split_runs;
  split_runs.reserve(line_runs.size());
  for (const auto& line : line_runs) {
    auto run = runs.front();
    run.start = line.start;
    run.length = line.length;
    split_runs.push_back(std::move(run));
  }
  return split_runs;
}

std::vector<PsdTextStyleRun> text_runs_for_layer(const Layer& layer, std::string_view text) {
  const auto fallback = fallback_text_run_from_metadata(layer);
  if (const auto runs = layer_metadata_value(layer, kLayerMetadataTextRuns); runs.has_value()) {
    return split_single_style_run_on_line_breaks(parse_patchy_text_runs_metadata(*runs, text, fallback), text);
  }
  return split_single_style_run_on_line_breaks(parse_patchy_text_runs_metadata({}, text, fallback), text);
}

std::vector<PsdTextParagraphRun> parse_patchy_paragraph_runs_metadata(std::string_view runs_text,
                                                                      std::string_view plain_text) {
  std::vector<PsdTextParagraphRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(plain_text).size());
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    auto line = line_end == std::string_view::npos ? runs_text.substr(line_start)
                                                   : runs_text.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    if (line.empty() || line == "v1" || line == "v2" || line == "v3") {
      continue;
    }
    const auto fields = split_tab_fields(line);
    if (fields.size() < 3U) {
      continue;
    }
    PsdTextParagraphRun run;
    run.start = std::clamp(parse_int_or(fields[0], 0), 0, std::max(0, text_length));
    run.length = std::max(0, parse_int_or(fields[1], 0));
    run.justification = photoshop_justification_from_patchy_alignment(fields[2]);
    if (fields.size() >= 8U) {
      run.first_line_indent = parse_double(fields[3]).value_or(0.0);
      run.start_indent = parse_double(fields[4]).value_or(0.0);
      run.end_indent = parse_double(fields[5]).value_or(0.0);
      run.space_before = parse_double(fields[6]).value_or(0.0);
      run.space_after = parse_double(fields[7]).value_or(0.0);
    }
    if (fields.size() >= 9U) {
      if (const auto fraction = parse_double(fields[8]);
          fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
        run.auto_leading_fraction = *fraction;
      }
    }
    if (run.length <= 0 || run.start >= text_length) {
      continue;
    }
    run.length = std::min(run.length, text_length - run.start);
    runs.push_back(run);
  }
  std::sort(runs.begin(), runs.end(), [](const PsdTextParagraphRun& lhs, const PsdTextParagraphRun& rhs) {
    return lhs.start < rhs.start;
  });
  if (runs.empty()) {
    runs.push_back(PsdTextParagraphRun{0, std::max(1, text_length), 0});
  }
  return runs;
}

std::vector<PsdTextParagraphRun> paragraph_runs_for_layer(const Layer& layer, std::string_view text) {
  std::vector<PsdTextParagraphRun> parsed;
  if (const auto runs = layer_metadata_value(layer, kLayerMetadataTextParagraphRuns); runs.has_value()) {
    parsed = parse_patchy_paragraph_runs_metadata(*runs, text);
  } else {
    parsed = parse_patchy_paragraph_runs_metadata({}, text);
  }
  if (text.find('\n') != std::string_view::npos && parsed.size() == 1U) {
    const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
    const auto& run = parsed.front();
    if (text_length > 0 && run.start == 0 && run.length >= text_length) {
      return paragraph_runs_from_text_line_breaks(text, run.justification);
    }
  }
  return parsed;
}

PsdTextStyleRun imported_text_fallback_run(const LayerRecord& record) {
  PsdTextStyleRun fallback;
  if (record.text_font.has_value() && !record.text_font->empty() &&
      ascii_lower_copy(*record.text_font) != "psd text") {
    fallback.family = *record.text_font;
  }
  fallback.size = std::clamp(static_cast<double>(record.text_size.value_or(static_cast<int>(std::lround(fallback.size)))),
                             1.0, static_cast<double>(kMaxTextSizePixels));
  fallback.color = record.text_color.value_or(fallback.color);
  fallback.bold = record.text_bold.value_or(false);
  fallback.italic = record.text_italic.value_or(false);
  return fallback;
}

PsdTextStyleRun imported_text_primary_run(const LayerRecord& record) {
  auto fallback = imported_text_fallback_run(record);
  if (!record.text.has_value()) {
    return fallback;
  }
  if (record.text_runs.has_value()) {
    auto runs = parse_patchy_text_runs_metadata(*record.text_runs, *record.text, fallback);
    if (!runs.empty()) {
      return runs.front();
    }
  }
  return fallback;
}

int imported_text_primary_justification(const LayerRecord& record) {
  if (!record.text.has_value() || !record.text_paragraph_runs.has_value()) {
    return 0;
  }
  const auto runs = parse_patchy_paragraph_runs_metadata(*record.text_paragraph_runs, *record.text);
  return runs.empty() ? 0 : runs.front().justification;
}

bool layer_style_has_regeneratable_outer_text_effect(const LayerStyle& style) noexcept {
  if (!style.effects_visible || style.empty()) {
    return false;
  }
  for (const auto& shadow : style.drop_shadows) {
    if (shadow.enabled && shadow.opacity > 0.0F &&
        (shadow.size >= 64.0F || (shadow.size >= 32.0F && shadow.distance >= 16.0F))) {
      return true;
    }
  }
  for (const auto& glow : style.outer_glows) {
    if (glow.enabled && glow.opacity > 0.0F && glow.size >= 64.0F) {
      return true;
    }
  }
  return false;
}

bool psd_text_preview_lacks_declared_fill_color(const PixelBuffer& pixels, const std::vector<RgbColor>& fills) {
  if (fills.empty() || pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      pixels.format().channels < 4) {
    return false;
  }

  std::uint64_t visible = 0;
  std::uint64_t fill_like = 0;
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  for (std::size_t offset = 0; offset + 3U < pixels.data().size(); offset += channels) {
    if (pixels.data()[offset + 3U] <= 16U) {
      continue;
    }
    ++visible;
    for (const auto& fill : fills) {
      const auto red_delta = std::abs(static_cast<int>(pixels.data()[offset]) - static_cast<int>(fill.red));
      const auto green_delta = std::abs(static_cast<int>(pixels.data()[offset + 1U]) - static_cast<int>(fill.green));
      const auto blue_delta = std::abs(static_cast<int>(pixels.data()[offset + 2U]) - static_cast<int>(fill.blue));
      if (std::max({red_delta, green_delta, blue_delta}) <= 18 && red_delta + green_delta + blue_delta <= 44) {
        ++fill_like;
        break;
      }
    }
  }

  return visible >= 256U && fill_like * 8U < visible;
}

// Every fill color the type layer's style runs declare (primary run first). The
// pollution check must accept ANY of them: multi-colored text is still a clean
// text-only raster.
std::vector<RgbColor> imported_text_declared_fill_colors(const LayerRecord& record) {
  std::vector<RgbColor> colors;
  const auto append_unique = [&colors](RgbColor color) {
    const auto exists = std::any_of(colors.begin(), colors.end(), [color](RgbColor existing) {
      return existing.red == color.red && existing.green == color.green && existing.blue == color.blue;
    });
    if (!exists) {
      colors.push_back(color);
    }
  };
  append_unique(imported_text_primary_run(record).color);
  if (record.text.has_value() && record.text_runs.has_value()) {
    const auto fallback = imported_text_fallback_run(record);
    for (const auto& run : parse_patchy_text_runs_metadata(*record.text_runs, *record.text, fallback)) {
      append_unique(run.color);
    }
  }
  return colors;
}

}  // namespace

bool should_regenerate_imported_text_preview(const LayerRecord& record, const PixelBuffer& pixels) {
  if (!record.text.has_value() || !record.text_source_block.has_value() || !has_visible_alpha(pixels)) {
    return false;
  }
  // Warped imports keep Photoshop's raster: the GDI regeneration path renders
  // unwarped glyphs and would flatten the warp out of the preview.
  if (record.text_geometry.has_value() && record.text_geometry->warp.has_value()) {
    return false;
  }
  if (!layer_style_has_regeneratable_outer_text_effect(record.layer_style)) {
    return false;
  }
  // Patchy-authored type blocks re-render losslessly (Patchy's own engine rendered
  // them in the first place), so a big outer effect always gets a clean glyph matte.
  if (record.text_geometry.has_value() && record.text_patchy_generated_type_block) {
    return true;
  }
  // Photoshop-authored type layers keep Photoshop's raster until the text is edited,
  // matching Photoshop's own missing-font behavior: regenerating here substitutes
  // fonts (and reflows the wrap) before the user touches the layer. Regenerate only
  // when the stored preview is visibly NOT the declared text fill -- baked-in effect
  // pixels that would corrupt the live outer-effect contour.
  return psd_text_preview_lacks_declared_fill_color(pixels, imported_text_declared_fill_colors(record));
}

namespace {

struct RectD {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

std::optional<RectD> transformed_text_bounds(const PsdTextGeometry& geometry, const PsdTextBoundsD& bounds) {
  const auto map_point = [&geometry](double x, double y) {
    return std::array<double, 2>{geometry.transform[0] * x + geometry.transform[2] * y + geometry.transform[4],
                                 geometry.transform[1] * x + geometry.transform[3] * y + geometry.transform[5]};
  };
  const std::array<std::array<double, 2>, 4> points = {
      map_point(bounds.left, bounds.top),
      map_point(bounds.right, bounds.top),
      map_point(bounds.right, bounds.bottom),
      map_point(bounds.left, bounds.bottom),
  };
  auto min_x = points.front()[0];
  auto max_x = points.front()[0];
  auto min_y = points.front()[1];
  auto max_y = points.front()[1];
  for (const auto& point : points) {
    min_x = std::min(min_x, point[0]);
    max_x = std::max(max_x, point[0]);
    min_y = std::min(min_y, point[1]);
    max_y = std::max(max_y, point[1]);
  }
  if (!std::isfinite(min_x) || !std::isfinite(max_x) || !std::isfinite(min_y) || !std::isfinite(max_y) ||
      max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return RectD{min_x, min_y, max_x, max_y};
}

// Vertical scale of the TySh transform: Photoshop's engine font size maps to rendered pixels
// through the transform's y column, so this (not an x/y average) scales font sizes and leading.
double imported_text_transform_scale(const LayerRecord& record) noexcept {
  if (!record.text_geometry.has_value()) {
    return 1.0;
  }
  const auto& transform = record.text_geometry->transform;
  const auto scale = std::hypot(transform[2], transform[3]);
  return std::isfinite(scale) && scale > 0.01 ? scale : 1.0;
}

Rect imported_text_draw_rect(const LayerRecord& record, int width, int height) {
  if (record.text_geometry.has_value()) {
    const auto& geometry = *record.text_geometry;
    const auto source_bounds = record.text_box.has_value() ? geometry.box_bounds : geometry.bounding_box;
    if (const auto transformed = transformed_text_bounds(geometry, source_bounds); transformed.has_value()) {
      Rect rect{static_cast<std::int32_t>(std::floor(transformed->left - record.bounds.x)),
                static_cast<std::int32_t>(std::floor(transformed->top - record.bounds.y)),
                static_cast<std::int32_t>(std::ceil(transformed->right - transformed->left)),
                static_cast<std::int32_t>(std::ceil(transformed->bottom - transformed->top))};
      if (rect.width > 0 && rect.height > 0 && rect.x < width && rect.y < height &&
          rect.x + rect.width > 0 && rect.y + rect.height > 0) {
        return rect;
      }
    }
  }

  if (record.text_box.has_value()) {
    const auto box_width = std::clamp(record.text_box->width, 1, std::max(1, width));
    const auto box_height = std::clamp(record.text_box->height, 1, std::max(1, height));
    return Rect{(width - box_width) / 2, (height - box_height) / 2, box_width, box_height};
  }
  return Rect{0, 0, std::max(1, width), std::max(1, height)};
}

std::wstring windows_text_line_breaks(std::wstring text) {
  std::wstring normalized;
  normalized.reserve(text.size() + 8U);
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == L'\n' && (index == 0U || text[index - 1U] != L'\r')) {
      normalized.push_back(L'\r');
    }
    normalized.push_back(text[index]);
  }
  return normalized;
}

}  // namespace

std::optional<PixelBuffer> render_regenerated_imported_text_pixels(const LayerRecord& record,
                                                                   std::int32_t width,
                                                                   std::int32_t height) {
  if (!record.text.has_value() || record.text->empty() || width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const auto run = imported_text_primary_run(record);
  const auto font_size =
      std::clamp(static_cast<int>(std::lround(static_cast<double>(run.size) * imported_text_transform_scale(record))),
                 1, kMaxTextSizePixels);
  const auto draw_rect = imported_text_draw_rect(record, width, height);
  const auto boxed = record.text_box.has_value();
  const auto justification = imported_text_primary_justification(record);

#ifdef _WIN32
  const auto wide_text = windows_text_line_breaks(wide_from_utf8(*record.text));
  if (wide_text.empty()) {
    return std::nullopt;
  }

  HDC memory_dc = CreateCompatibleDC(nullptr);
  if (memory_dc == nullptr) {
    return std::nullopt;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bitmap_bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(memory_dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
  if (bitmap == nullptr || bitmap_bits == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    DeleteDC(memory_dc);
    return std::nullopt;
  }

  const auto wide_family =
      wide_from_utf8(run.family.empty() ? std::string_view("Arial") : std::string_view(run.family));
  LOGFONTW log_font{};
  log_font.lfHeight = -std::max(1, font_size);
  log_font.lfWeight = run.bold ? FW_BOLD : FW_NORMAL;
  log_font.lfItalic = run.italic ? TRUE : FALSE;
  log_font.lfCharSet = DEFAULT_CHARSET;
  log_font.lfQuality = record.text_anti_alias.value_or(4) <= 0 ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
  const auto family_length = std::min<std::size_t>(wide_family.size(), LF_FACESIZE - 1U);
  for (std::size_t index = 0; index < family_length; ++index) {
    log_font.lfFaceName[index] = wide_family[index];
  }
  if (family_length == 0U) {
    const std::wstring fallback_family = L"Arial";
    for (std::size_t index = 0; index < fallback_family.size() && index < LF_FACESIZE - 1U; ++index) {
      log_font.lfFaceName[index] = fallback_family[index];
    }
  }

  HFONT font = CreateFontIndirectW(&log_font);
  if (font == nullptr) {
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    return std::nullopt;
  }

  const auto old_bitmap = SelectObject(memory_dc, bitmap);
  const auto old_font = SelectObject(memory_dc, font);
  const RECT full_rect{0, 0, width, height};
  HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(memory_dc, &full_rect, black);
  DeleteObject(black);
  SetBkMode(memory_dc, TRANSPARENT);
  SetTextColor(memory_dc, RGB(255, 255, 255));

  RECT text_rect{draw_rect.x, draw_rect.y, draw_rect.x + draw_rect.width, draw_rect.y + draw_rect.height};
  UINT flags = DT_NOPREFIX;
  if (boxed) {
    flags |= DT_WORDBREAK | DT_EDITCONTROL;
  } else {
    flags |= DT_NOCLIP;
  }
  if (justification == 1) {
    flags |= DT_RIGHT;
  } else if (justification == 2) {
    flags |= DT_CENTER;
  } else {
    flags |= DT_LEFT;
  }
  DrawTextW(memory_dc, wide_text.data(), static_cast<int>(std::min<std::size_t>(wide_text.size(), INT_MAX)),
            &text_rect, flags);

  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);
  const auto* source = static_cast<const std::uint8_t*>(bitmap_bits);
  bool has_alpha = false;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto source_offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
      auto alpha = std::max({source[source_offset], source[source_offset + 1U], source[source_offset + 2U]});
      if (record.text_anti_alias.value_or(4) <= 0) {
        alpha = alpha >= 128U ? 255U : 0U;
      }
      if (alpha == 0U) {
        continue;
      }
      auto* target = pixels.pixel(x, y);
      target[0] = run.color.red;
      target[1] = run.color.green;
      target[2] = run.color.blue;
      target[3] = alpha;
      has_alpha = true;
    }
  }

  SelectObject(memory_dc, old_font);
  SelectObject(memory_dc, old_bitmap);
  DeleteObject(font);
  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  return has_alpha ? std::optional<PixelBuffer>(std::move(pixels)) : std::nullopt;
#else
  auto pixels = render_placeholder_text(*record.text, width, height);
  const auto fill = run.color;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      if (pixel[3] == 0U) {
        continue;
      }
      pixel[0] = fill.red;
      pixel[1] = fill.green;
      pixel[2] = fill.blue;
    }
  }
  return pixels;
#endif
}

namespace {

std::string photoshop_engine_text(std::string_view text) {
  std::string converted(text);
  for (auto& ch : converted) {
    if (ch == '\n') {
      ch = '\r';
    }
  }
  if (converted.empty() || (converted.back() != '\r' && converted.back() != '\n')) {
    converted.push_back('\r');
  }
  return converted;
}

std::string engine_escaped_utf16_string(std::string_view text) {
  std::string escaped = "(";
  const auto append_byte = [&escaped](std::uint8_t byte) {
    if (byte == '(' || byte == ')' || byte == '\\') {
      escaped.push_back('\\');
      escaped.push_back(static_cast<char>(byte));
    } else {
      escaped.push_back(static_cast<char>(byte));
    }
  };

  append_byte(0xFEU);
  append_byte(0xFFU);
  for (const auto unit : utf8_to_utf16(text)) {
    append_byte(static_cast<std::uint8_t>((unit >> 8U) & 0xFFU));
    append_byte(static_cast<std::uint8_t>(unit & 0xFFU));
  }
  escaped.push_back(')');
  return escaped;
}

std::vector<std::uint8_t> utf16be_text_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const auto unit : utf8_to_utf16(text)) {
    bytes.push_back(static_cast<std::uint8_t>((unit >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(unit & 0xFFU));
  }
  return bytes;
}

bool replace_all_bytes(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> old_value,
                       std::span<const std::uint8_t> new_value) {
  if (old_value.empty() || old_value.size() != new_value.size()) {
    return false;
  }
  bool replaced = false;
  auto search_start = bytes.begin();
  while (search_start != bytes.end()) {
    const auto found = std::search(search_start, bytes.end(), old_value.begin(), old_value.end());
    if (found == bytes.end()) {
      break;
    }
    std::copy(new_value.begin(), new_value.end(), found);
    search_start = found + static_cast<std::ptrdiff_t>(new_value.size());
    replaced = true;
  }
  return replaced;
}

std::optional<std::vector<std::uint8_t>> photoshop_type_tool_payload_from_template(const Layer& layer,
                                                                                   std::string_view new_text) {
  const auto source_block = layer_metadata_value(layer, kLayerMetadataTextSourceBlock);
  if (!source_block.has_value() || (*source_block != "TySh" && *source_block != "tySh")) {
    return std::nullopt;
  }
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key != "TySh" && block.key != "tySh") {
      continue;
    }
    const auto old_text = extract_engine_data_text(block.payload);
    if (!old_text.has_value()) {
      continue;
    }
    const auto old_engine_text = photoshop_engine_text(*old_text);
    const auto new_engine_text = photoshop_engine_text(new_text);
    const auto old_units = utf8_to_utf16(old_engine_text);
    const auto new_units = utf8_to_utf16(new_engine_text);
    if (old_units.empty() || old_units.size() != new_units.size()) {
      continue;
    }
    auto payload = block.payload;
    const auto old_bytes = utf16be_text_bytes(old_engine_text);
    const auto new_bytes = utf16be_text_bytes(new_engine_text);
    if (replace_all_bytes(payload, old_bytes, new_bytes)) {
      return payload;
    }
  }
  return std::nullopt;
}

bool text_transform_overrides_psd_template(const Layer& layer) {
  const auto patchy_transform = layer_metadata_value(layer, kLayerMetadataTextTransform);
  if (!patchy_transform.has_value()) {
    return false;
  }
  const auto parsed_patchy = parse_double_array6(*patchy_transform);
  if (!parsed_patchy.has_value()) {
    return false;
  }
  const auto psd_transform = layer_metadata_value(layer, kLayerMetadataPsdTextTransform);
  if (!psd_transform.has_value()) {
    return true;
  }
  const auto parsed_psd = parse_double_array6(*psd_transform);
  if (!parsed_psd.has_value()) {
    return true;
  }
  for (std::size_t i = 0; i < parsed_patchy->size(); ++i) {
    if (std::abs((*parsed_patchy)[i] - (*parsed_psd)[i]) > 0.000001) {
      return true;
    }
  }
  return false;
}

bool layer_has_photoshop_text_source(const Layer& layer) {
  const auto source_block = layer_metadata_value(layer, kLayerMetadataTextSourceBlock);
  return source_block.has_value() && (*source_block == "TySh" || *source_block == "tySh");
}

bool should_preserve_imported_text_geometry(const Layer& layer) {
  const auto raster_status = layer_metadata_value(layer, kLayerMetadataTextRasterStatus).value_or(std::string_view{});
  if (raster_status == "patchy_raster" && layer_has_photoshop_text_source(layer)) {
    return false;
  }
  if (text_transform_overrides_psd_template(layer)) {
    return false;
  }
  if (layer_has_photoshop_text_source(layer)) {
    return true;
  }
  return raster_status != "patchy_raster" || !layer_metadata_value(layer, kLayerMetadataTextTransform).has_value();
}

double text_bounds_height(const PsdTextBoundsD& bounds) {
  return bounds.bottom - bounds.top;
}

bool finite_text_bounds(const PsdTextBoundsD& bounds) {
  return std::isfinite(bounds.left) && std::isfinite(bounds.top) && std::isfinite(bounds.right) &&
         std::isfinite(bounds.bottom);
}

void translate_text_bounds_local(PsdTextBoundsD& bounds, double dx, double dy) {
  bounds.left -= dx;
  bounds.right -= dx;
  bounds.top -= dy;
  bounds.bottom -= dy;
}

void translate_text_geometry_local(PsdTextGeometry& geometry, double dx, double dy) {
  geometry.transform[4] += geometry.transform[0] * dx + geometry.transform[2] * dy;
  geometry.transform[5] += geometry.transform[1] * dx + geometry.transform[3] * dy;
  translate_text_bounds_local(geometry.bounds, dx, dy);
  translate_text_bounds_local(geometry.bounding_box, dx, dy);
  translate_text_bounds_local(geometry.box_bounds, dx, dy);
}

double point_text_baseline_offset(const Layer& layer, const PsdTextGeometry& geometry) {
  if (finite_text_bounds(geometry.bounding_box) && geometry.bounding_box.bottom > 1.0 &&
      text_bounds_height(geometry.bounding_box) > 1.0) {
    return geometry.bounding_box.bottom;
  }
  if (finite_text_bounds(geometry.bounds) && geometry.bounds.bottom > 1.0 &&
      text_bounds_height(geometry.bounds) > 1.0) {
    return geometry.bounds.bottom;
  }
  if (const auto size = layer_metadata_value(layer, kLayerMetadataTextSize); size.has_value()) {
    return std::max(1.0, static_cast<double>(parse_int_or(*size, 1)) * 0.75);
  }
  return 1.0;
}

bool point_text_geometry_needs_baseline_anchor(const Layer& layer, const PsdTextGeometry& geometry) {
  if (layer_has_photoshop_text_source(layer) && should_preserve_imported_text_geometry(layer)) {
    return false;
  }
  if (!finite_text_bounds(geometry.bounding_box) || text_bounds_height(geometry.bounding_box) <= 1.0) {
    return false;
  }
  return geometry.bounding_box.top >= -0.5 && point_text_baseline_offset(layer, geometry) > 1.0;
}

}  // namespace

#ifdef _WIN32
std::optional<std::wstring> directwrite_localized_string(IDWriteLocalizedStrings* strings);
std::optional<std::string> directwrite_font_info_string(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id);
#endif

namespace {

#ifdef _WIN32
std::string photoshop_font_name_for_run(std::string_view family, bool bold, bool italic) {
  const auto fallback = family.empty() ? std::string("Arial") : std::string(family);
  const auto wide_family = wide_from_utf8(fallback);
  if (wide_family.empty()) {
    return fallback;
  }

  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return fallback;
  }
  Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection)) || !collection) {
    return fallback;
  }

  UINT32 family_index = 0;
  BOOL exists = FALSE;
  if (FAILED(collection->FindFamilyName(wide_family.c_str(), &family_index, &exists)) || !exists) {
    return fallback;
  }

  Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
  if (FAILED(collection->GetFontFamily(family_index, &font_family)) || !font_family) {
    return fallback;
  }
  Microsoft::WRL::ComPtr<IDWriteFont> font;
  if (FAILED(font_family->GetFirstMatchingFont(bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL,
                                               italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                               &font)) ||
      !font) {
    return fallback;
  }

  if (const auto postscript = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
      postscript.has_value()) {
    return *postscript;
  }
  if (const auto full_name = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
      full_name.has_value()) {
    return *full_name;
  }
  return fallback;
}
#else
std::string photoshop_font_name_for_run(std::string_view family, bool /*bold*/, bool /*italic*/) {
  return family.empty() ? std::string("Arial") : std::string(family);
}
#endif

int font_index_for_run(std::vector<std::string>& fonts, std::string_view family, bool bold, bool italic) {
  const auto photoshop_name = photoshop_font_name_for_run(family, bold, italic);
  for (std::size_t index = 0; index < fonts.size(); ++index) {
    if (fonts[index] == photoshop_name) {
      return static_cast<int>(index);
    }
  }
  fonts.emplace_back(photoshop_name);
  return static_cast<int>(fonts.size() - 1U);
}

std::string engine_color_values(RgbColor color) {
  const auto component = [](std::uint8_t value) {
    return std::to_string(static_cast<double>(value) / 255.0);
  };
  return "1.0 " + component(color.red) + ' ' + component(color.green) + ' ' + component(color.blue);
}

std::string engine_color_object(RgbColor color) {
  return "<< /Type 1 /Values [ " + engine_color_values(color) + " ] >>";
}

std::string engine_adjustments_object() {
  return "<< /Axis [ 1.0 0.0 1.0 ] /XY [ 0.0 0.0 ] >>";
}

std::string engine_paragraph_properties(const PsdTextParagraphRun& run) {
  std::string properties = "<< /Justification ";
  properties += std::to_string(std::clamp(run.justification, 0, 3));
  properties += " /FirstLineIndent ";
  properties += serialize_paragraph_metric(run.first_line_indent);
  properties += " /StartIndent ";
  properties += serialize_paragraph_metric(run.start_indent);
  properties += " /EndIndent ";
  properties += serialize_paragraph_metric(run.end_indent);
  properties += " /SpaceBefore ";
  properties += serialize_paragraph_metric(run.space_before);
  properties += " /SpaceAfter ";
  properties += serialize_paragraph_metric(run.space_after);
  properties +=
      " /AutoHyphenate true /HyphenatedWordSize 6 /PreHyphen 2 /PostHyphen 2 /ConsecutiveHyphens 8"
      " /Zone 36.0 /WordSpacing [ 0.8 1.0 1.33 ] /LetterSpacing [ 0.0 0.0 0.0 ]"
      " /GlyphSpacing [ 1.0 1.0 1.0 ] /AutoLeading ";
  properties += serialize_paragraph_metric(std::isfinite(run.auto_leading_fraction) &&
                                                   run.auto_leading_fraction > 0.01 &&
                                                   run.auto_leading_fraction < 10.0
                                               ? run.auto_leading_fraction
                                               : 1.2);
  properties += " /LeadingType 0 /Hanging ";
  properties += run.first_line_indent < 0.0 && run.start_indent > 0.0 ? "true" : "false";
  properties += " /Burasagari false /KinsokuOrder 0 /EveryLineComposer false >>";
  return properties;
}

std::string engine_style_sheet_data(const PsdTextStyleRun& run, int font_index) {
  std::string style = "<< /Font ";
  style += std::to_string(std::max(0, font_index));
  const auto font_size = std::max(1.0, run.size);
  style += " /FontSize ";
  style += std::to_string(font_size);
  style += " /FauxBold ";
  style += run.bold ? "true" : "false";
  style += " /FauxItalic ";
  style += run.italic ? "true" : "false";
  // A fixed-leading run must export /AutoLeading false or Photoshop ignores the value and
  // re-derives auto leading. Auto (and unspecified) runs keep the historical auto shape.
  const bool fixed_leading = !run.auto_leading && run.leading.has_value() && std::isfinite(*run.leading) &&
                             *run.leading > 0.0;
  style += fixed_leading ? " /AutoLeading false /Leading " : " /AutoLeading true /Leading ";
  style += std::to_string(fixed_leading ? *run.leading : font_size * 1.2);
  if (std::isfinite(run.tracking) && std::abs(run.tracking) > 0.0001) {
    style += " /Tracking ";
    style += std::to_string(run.tracking);
  }
  if (std::isfinite(run.horizontal_scale) && std::abs(run.horizontal_scale - 1.0) > 0.0001) {
    style += " /HorizontalScale ";
    style += std::to_string(run.horizontal_scale);
  }
  if (std::isfinite(run.vertical_scale) && std::abs(run.vertical_scale - 1.0) > 0.0001) {
    style += " /VerticalScale ";
    style += std::to_string(run.vertical_scale);
  }
  style += " /AutoKerning true /Kerning 0 /FillColor ";
  style += engine_color_object(run.color);
  style += " >>";
  return style;
}

std::string engine_font_set(std::span<const std::string> fonts) {
  std::string font_set = "[ ";
  for (std::size_t index = 0; index < fonts.size(); ++index) {
    const auto& font = fonts[index];
    font_set += "<< /Name ";
    font_set += engine_escaped_utf16_string(font.empty() ? "Arial" : font);
    font_set += " /Script 0 /FontType ";
    font_set += index == 0U ? "0" : "1";
    font_set += " /Synthetic 0 >> ";
  }
  font_set += "]";
  return font_set;
}

std::string engine_text_resources(std::span<const std::string> fonts, const PsdTextStyleRun& normal_style,
                                  int normal_font_index, int normal_justification) {
  std::string resources = "<< /KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0 ";
  resources += "/ParagraphSheetSet [ << /Name ";
  resources += engine_escaped_utf16_string("Normal RGB");
  resources += " /DefaultStyleSheet 0 /Properties ";
  PsdTextParagraphRun normal_paragraph;
  normal_paragraph.justification = normal_justification;
  resources += engine_paragraph_properties(normal_paragraph);
  resources += " >> ] /StyleSheetSet [ << /Name ";
  resources += engine_escaped_utf16_string("Normal RGB");
  resources += " /StyleSheetData ";
  resources += engine_style_sheet_data(normal_style, normal_font_index);
  resources += " >> ] /FontSet ";
  resources += engine_font_set(fonts);
  resources +=
      " /SuperscriptSize 0.583 /SuperscriptPosition 0.333 /SubscriptSize 0.583 /SubscriptPosition 0.333"
      " /SmallCapSize 0.7 >>";
  return resources;
}

std::string engine_grid_info() {
  return "/GridInfo << /GridIsOn false /ShowGrid false /GridSize 18.0 /GridLeading 22.0"
         " /GridColor << /Type 1 /Values [ 1.0 0.0 0.0 1.0 ] >>"
         " /GridLeadingFillColor << /Type 1 /Values [ 1.0 0.0 0.0 1.0 ] >>"
         " /AlignLineHeightToGridFlags false >>\n";
}

std::string engine_rendered_shape(bool boxed_text, const PsdTextBoundsD& box_bounds) {
  const auto shape_type = boxed_text ? 1 : 0;
  std::string rendered = "/Rendered << /Version 1 /Shapes << /WritingDirection 0 /Children [ << /ShapeType ";
  rendered += std::to_string(shape_type);
  rendered +=
      " /Procession 0 /Lines << /WritingDirection 0 /Children [ ] >> /Cookie << /Photoshop << /ShapeType ";
  rendered += std::to_string(shape_type);
  if (boxed_text) {
    rendered += " /BoxBounds [ ";
    rendered += std::to_string(box_bounds.left);
    rendered += ' ';
    rendered += std::to_string(box_bounds.top);
    rendered += ' ';
    rendered += std::to_string(box_bounds.right);
    rendered += ' ';
    rendered += std::to_string(box_bounds.bottom);
    rendered += " ]";
  } else {
    rendered += " /PointBase [ 0.0 0.0 ]";
  }
  rendered += " /Base << /ShapeType ";
  rendered += std::to_string(shape_type);
  rendered +=
      " /TransformPoint0 [ 1.0 0.0 ] /TransformPoint1 [ 0.0 1.0 ] /TransformPoint2 [ 0.0 0.0 ]"
      " >> >> >> >> ] >> >>\n";
  return rendered;
}

std::vector<std::uint8_t> engine_data_for_text(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                               std::span<const PsdTextParagraphRun> paragraph_runs, bool boxed_text,
                                               const PsdTextBoundsD& box_bounds, int anti_alias) {
  const auto engine_text = photoshop_engine_text(text);
  const auto engine_units = static_cast<int>(utf8_to_utf16(engine_text).size());
  std::vector<std::string> fonts{"AdobeInvisFont"};
  std::vector<int> font_indices;
  font_indices.reserve(runs.size());
  for (const auto& run : runs) {
    font_indices.push_back(font_index_for_run(fonts, run.family, run.bold, run.italic));
  }
  if (fonts.size() == 1U) {
    fonts.push_back("Arial");
  }

  std::vector<int> run_lengths;
  int covered = 0;
  for (const auto& run : runs) {
    if (run.length <= 0) {
      continue;
    }
    run_lengths.push_back(run.length);
    covered += run.length;
  }
  if (run_lengths.empty()) {
    run_lengths.push_back(engine_units);
  } else if (covered < engine_units) {
    run_lengths.back() += engine_units - covered;
  }

  std::vector<PsdTextParagraphRun> engine_paragraph_runs;
  int paragraph_covered = 0;
  for (const auto& run : paragraph_runs) {
    if (run.length <= 0) {
      continue;
    }
    engine_paragraph_runs.push_back(run);
    paragraph_covered += run.length;
  }
  if (engine_paragraph_runs.empty()) {
    PsdTextParagraphRun run;
    run.length = engine_units;
    engine_paragraph_runs.push_back(run);
  } else if (paragraph_covered < engine_units) {
    engine_paragraph_runs.back().length += engine_units - paragraph_covered;
  }

  std::string engine = "<<\n/EngineDict <<\n/Editor << /Text ";
  engine += engine_escaped_utf16_string(engine_text);
  engine += " >>\n/ParagraphRun << /DefaultRunData << /ParagraphSheet << /DefaultStyleSheet 0 /Properties << >> >> ";
  engine += "/Adjustments ";
  engine += engine_adjustments_object();
  engine += " >> /RunArray [ ";
  for (const auto& run : engine_paragraph_runs) {
    engine += "<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties ";
    engine += engine_paragraph_properties(run);
    engine += " >> /Adjustments ";
    engine += engine_adjustments_object();
    engine += " >> ";
  }
  engine += "] /RunLengthArray [ ";
  for (const auto& run : engine_paragraph_runs) {
    engine += std::to_string(run.length);
    engine += ' ';
  }
  engine += "] /IsJoinable 1 >>\n";
  engine += "/StyleRun << /DefaultRunData << /StyleSheet << /StyleSheetData << >> >> >> /RunArray [ ";
  for (std::size_t index = 0; index < std::max<std::size_t>(runs.size(), 1U); ++index) {
    PsdTextStyleRun run;
    if (!runs.empty()) {
      run = runs[index];
    }
    const auto font_index = font_indices.empty() ? 1 : font_indices[std::min(index, font_indices.size() - 1U)];
    engine += "<< /StyleSheet << /StyleSheetData ";
    engine += engine_style_sheet_data(run, font_index);
    engine += " >> >> ";
  }
  engine += "] /RunLengthArray [ ";
  for (const auto length : run_lengths) {
    engine += std::to_string(length);
    engine += ' ';
  }
  engine += "] /IsJoinable 2 >>\n";
  engine += engine_grid_info();
  engine += "/AntiAlias ";
  engine += std::to_string(std::clamp(anti_alias, 0, 16));
  engine += " /UseFractionalGlyphWidths true\n";
  engine += engine_rendered_shape(boxed_text, box_bounds);
  engine += ">>\n";
  const PsdTextStyleRun normal_style = runs.empty() ? PsdTextStyleRun{} : runs.front();
  const auto normal_font_index = font_indices.empty() ? 1 : font_indices.front();
  const auto normal_justification = engine_paragraph_runs.empty() ? 0 : engine_paragraph_runs.front().justification;
  const auto resources = engine_text_resources(fonts, normal_style, normal_font_index, normal_justification);
  engine += "/ResourceDict ";
  engine += resources;
  engine += "\n/DocumentResources ";
  engine += resources;
  engine += "\n>>";
  return std::vector<std::uint8_t>(engine.begin(), engine.end());
}

void write_text_descriptor(BigEndianWriter& writer, std::string_view text, std::span<const std::uint8_t> engine_data,
                           const PsdTextGeometry& geometry) {
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "TxLr");
  writer.write_u32(8);
  write_descriptor_item_header(writer, "Txt ", {'T', 'E', 'X', 'T'});
  write_descriptor_unicode_string(writer, text);
  write_descriptor_enum_item(writer, "textGridding", "textGridding", "None");
  write_descriptor_enum_item(writer, "Ornt", "Ornt", "Hrzn");
  write_descriptor_enum_item(writer, "AntA", "Annt", "AnCr");
  write_descriptor_object_item(writer, "bounds", geometry.bounds);
  write_descriptor_object_item(writer, "boundingBox", geometry.bounding_box);
  write_descriptor_raw_item(writer, "EngineData", engine_data);
  write_descriptor_item_header(writer, "TextIndex", {'l', 'o', 'n', 'g'});
  writer.write_u32(static_cast<std::uint32_t>(std::max(0, geometry.text_index)));
}

void write_warp_descriptor(BigEndianWriter& writer, const TextWarp* warp) {
  const bool active = warp != nullptr && !warp->style.empty();
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "warp");
  writer.write_u32(5);
  write_descriptor_enum_item(writer, "warpStyle", "warpStyle", active ? warp->style : "warpNone");
  write_descriptor_item_header(writer, "warpValue", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->value : 0.0);
  write_descriptor_item_header(writer, "warpPerspective", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->perspective : 0.0);
  write_descriptor_item_header(writer, "warpPerspectiveOther", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->perspective_other : 0.0);
  write_descriptor_enum_item(writer, "warpRotate", "Ornt",
                             active && warp->rotate == "Vrtc" ? "Vrtc" : "Hrzn");
}

PsdTextGeometry text_geometry_for_layer(const Layer& layer, const Rect& text_bounds, bool boxed_text,
                                        const TextWarp* warp) {
  PsdTextGeometry geometry;
  geometry.transform = {1.0, 0.0, 0.0, 1.0, static_cast<double>(text_bounds.x), static_cast<double>(text_bounds.y)};
  geometry.bounds =
      PsdTextBoundsD{0.0, 0.0, static_cast<double>(std::max(1, text_bounds.width)),
                     static_cast<double>(std::max(1, text_bounds.height))};
  geometry.bounding_box = geometry.bounds;
  geometry.box_bounds = geometry.bounds;
  geometry.tail_bounds = {text_bounds.x, text_bounds.y, text_bounds.x + text_bounds.width,
                          text_bounds.y + text_bounds.height};
  bool bounding_box_from_pixels = false;

  if (const auto patchy_transform = layer_metadata_value(layer, kLayerMetadataTextTransform); patchy_transform.has_value()) {
    if (const auto parsed = parse_double_array6(*patchy_transform); parsed.has_value()) {
      geometry.transform = *parsed;
    }
  } else if (const auto psd_transform = layer_metadata_value(layer, kLayerMetadataPsdTextTransform); psd_transform.has_value()) {
    if (const auto parsed = parse_double_array6(*psd_transform); parsed.has_value()) {
      geometry.transform = *parsed;
    }
  }
  if (warp != nullptr) {
    // A warped layer's pixels are the WARPED render, so neither the layer rect nor
    // the visible-pixel scan describes the text box; the warp metadata carries the
    // unwarped layout bounds in the same text-local space as the transform above.
    geometry.bounds = PsdTextBoundsD{warp->bounds_left, warp->bounds_top, warp->bounds_right,
                                     warp->bounds_bottom};
    geometry.bounding_box = geometry.bounds;
    bounding_box_from_pixels = true;
    if (!boxed_text && warp->baseline > 0.5) {
      // Photoshop anchors a point-text transform at the first-line BASELINE (its
      // own warped files store box top = -ascent): shift the top-left-origin
      // Patchy geometry there or a type re-render in Photoshop drops the text by
      // one descent (the buldge_test jump, July 2026). The shifted box top goes
      // below -0.5, which also skips the generic ink-bottom anchoring beneath.
      translate_text_geometry_local(geometry, 0.0, warp->baseline);
    }
  } else if (const auto visible = visible_pixel_local_bounds(layer.pixels()); visible.has_value()) {
    if (const auto local_bounds = visible_text_local_bounds_from_layer_pixels(layer, *visible, geometry.transform);
        local_bounds.has_value()) {
      geometry.bounding_box = *local_bounds;
      bounding_box_from_pixels = true;
    }
  }
  const auto preserve_imported_geometry = should_preserve_imported_text_geometry(layer);
  if (preserve_imported_geometry) {
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBounds); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.bounds = *parsed;
      }
    }
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBoundingBox); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.bounding_box = *parsed;
      }
    } else if (!bounding_box_from_pixels) {
      geometry.bounding_box = geometry.bounds;
    }
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBoxBounds); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.box_bounds = *parsed;
      }
    } else if (boxed_text) {
      geometry.box_bounds = PsdTextBoundsD{0.0, 0.0, geometry.bounds.right, geometry.bounds.bottom};
    }
    if (const auto tail = layer_metadata_value(layer, kLayerMetadataPsdTextTailBounds); tail.has_value()) {
      if (const auto parsed = parse_int_array4(*tail); parsed.has_value()) {
        geometry.tail_bounds = *parsed;
      }
    }
  } else if (boxed_text) {
    geometry.box_bounds = PsdTextBoundsD{0.0, 0.0, geometry.bounds.right, geometry.bounds.bottom};
  } else if (!bounding_box_from_pixels) {
    geometry.bounding_box = geometry.bounds;
  }
  if (!boxed_text && point_text_geometry_needs_baseline_anchor(layer, geometry)) {
    translate_text_geometry_local(geometry, 0.0, point_text_baseline_offset(layer, geometry));
  }
  if (const auto index = layer_metadata_value(layer, kLayerMetadataPsdTextIndex); index.has_value()) {
    geometry.text_index = std::max(0, parse_int_or(*index, 0));
  }
  return geometry;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> photoshop_type_tool_payload_for_layer(const Layer& layer,
                                                                               const Rect& bounds) {
  const auto text = layer_metadata_value(layer, kLayerMetadataText);
  if (!text.has_value() || text->empty()) {
    return std::nullopt;
  }
  if (should_preserve_imported_text_geometry(layer)) {
    if (const auto templated_payload = photoshop_type_tool_payload_from_template(layer, *text);
        templated_payload.has_value()) {
      return templated_payload;
    }
  }
  const auto runs = text_runs_for_layer(layer, *text);
  if (runs.empty()) {
    return std::nullopt;
  }
  const auto paragraph_runs = paragraph_runs_for_layer(layer, *text);
  auto text_bounds = bounds;
  const auto boxed_text = layer_metadata_value(layer, kLayerMetadataTextFlow).value_or(std::string_view{}) == "box";
  if (boxed_text) {
    if (const auto width = layer_metadata_value(layer, kLayerMetadataTextBoxWidth); width.has_value()) {
      text_bounds.width = std::max(1, parse_int_or(*width, bounds.width));
    }
    if (const auto height = layer_metadata_value(layer, kLayerMetadataTextBoxHeight); height.has_value()) {
      text_bounds.height = std::max(1, parse_int_or(*height, bounds.height));
    }
  }
  const auto warp = text_warp_from_layer(layer);
  const bool warp_active = warp.has_value() && !text_warp_is_identity(*warp);
  const auto geometry = text_geometry_for_layer(layer, text_bounds, boxed_text,
                                                warp_active ? &*warp : nullptr);
  const auto anti_alias_metadata = layer_metadata_value(layer, kLayerMetadataTextAntiAlias);
  const auto anti_alias = anti_alias_metadata.has_value() ? parse_int_or(*anti_alias_metadata, 3) : 3;
  const auto engine_data =
      engine_data_for_text(*text, runs, paragraph_runs, boxed_text, geometry.box_bounds, anti_alias);
  const auto descriptor_text = photoshop_engine_text(*text);

  BigEndianWriter writer;
  writer.write_u16(1);
  for (const auto value : geometry.transform) {
    write_f64(writer, value);
  }
  writer.write_u16(50);
  writer.write_u32(16);
  write_text_descriptor(writer, descriptor_text, engine_data, geometry);
  writer.write_u16(1);
  writer.write_u32(16);
  write_warp_descriptor(writer, warp_active ? &*warp : nullptr);
  if (warp_active) {
    // Photoshop stores the warp's reference box (the text 'bounds') as four
    // big-endian float32s in the TySh tail when warped, zeros otherwise.
    write_f32(writer, static_cast<float>(geometry.bounds.left));
    write_f32(writer, static_cast<float>(geometry.bounds.top));
    write_f32(writer, static_cast<float>(geometry.bounds.right));
    write_f32(writer, static_cast<float>(geometry.bounds.bottom));
  } else {
    for (const auto value : geometry.tail_bounds) {
      write_i32(writer, value);
    }
  }
  return writer.bytes();
}

bool should_write_generated_text_block(const EncodedLayer& encoded) {
  if (encoded.layer == nullptr || encoded.kind != EncodedLayerKind::Pixel || !layer_is_text(*encoded.layer)) {
    return false;
  }
  const auto raster_status = layer_metadata_value(*encoded.layer, kLayerMetadataTextRasterStatus).value_or(std::string_view{});
  if ((raster_status == "psd_raster_preview" || raster_status == "placeholder") &&
      !text_transform_overrides_psd_template(*encoded.layer)) {
    return false;
  }
  return layer_metadata_value(*encoded.layer, kLayerMetadataText).has_value();
}

}  // namespace patchy::psd
