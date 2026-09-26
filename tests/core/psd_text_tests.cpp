#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "psd/psd_text_runs.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_extra_data;
using patchy::test::read_u32_be_at;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::write_ascii4;
using patchy::test::write_bmp_artifact;
using patchy::test::write_pascal_padded;
using patchy::test::write_test_layer_block;

std::uint64_t read_u64_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 8U <= bytes.size());
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

double read_f64_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return std::bit_cast<double>(read_u64_be_at(bytes, offset));
}

std::array<double, 4> parse_bounds_metadata4(const std::string& text) {
  std::istringstream stream(text);
  std::array<double, 4> values{};
  for (auto& value : values) {
    stream >> value;
  }
  CHECK(static_cast<bool>(stream));
  return values;
}

std::vector<std::uint8_t> utf16be_test_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const auto ch : text) {
    bytes.push_back(0);
    bytes.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

int replace_all_ascii_same_length(std::vector<std::uint8_t>& bytes, std::string_view needle,
                                  std::string_view replacement) {
  CHECK(needle.size() == replacement.size());
  int replacements = 0;
  auto search_begin = bytes.begin();
  while (true) {
    const auto found = std::search(search_begin, bytes.end(), needle.begin(), needle.end());
    if (found == bytes.end()) {
      break;
    }
    std::copy(replacement.begin(), replacement.end(), found);
    search_begin = found + static_cast<std::ptrdiff_t>(replacement.size());
    ++replacements;
  }
  return replacements;
}

std::string engine_utf16be_literal(std::string_view text) {
  std::string literal;
  literal.push_back('(');
  literal.push_back(static_cast<char>(0xFE));
  literal.push_back(static_cast<char>(0xFF));
  for (const auto ch : text) {
    literal.push_back('\0');
    literal.push_back(ch);
  }
  literal.push_back(')');
  return literal;
}

std::vector<std::uint8_t> single_text_layer_psd(std::span<const std::uint8_t> text_payload) {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Text Layer", 4);
  write_test_layer_block(layer_extra, "TySh", text_payload);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(12);
  layer_info.write_u32(10);
  layer_info.write_u32(82);
  layer_info.write_u32(210);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 240;
  constexpr std::uint32_t height = 120;
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }
  return writer.bytes();
}

void psd_import_regenerates_large_styled_text_preview_alpha() {
  constexpr std::int32_t layer_width = 320;
  constexpr std::int32_t layer_height = 150;
  const std::string text = "Clean\nAlpha";

  patchy::Document document(420, 260, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(420, 260, 230, 220, 204));

  auto polluted = solid_rgba(layer_width, layer_height, 243, 237, 230, 0);
  for (std::int32_t y = 10; y < 48; ++y) {
    for (std::int32_t x = 0; x < layer_width; ++x) {
      polluted.pixel(x, y)[3] = 220;
    }
  }
  for (std::int32_t x = 0; x < layer_width; x += 8) {
    for (std::int32_t y = 0; y < layer_height; ++y) {
      polluted.pixel(x, y)[3] = 180;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Styled text preview", std::move(polluted));
  text_layer.set_bounds(patchy::Rect{50, 70, layer_width, layer_height});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t11\t34\t1\t0\t#f3ede6\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t11\tcenter";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "90";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "34";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#f3ede6";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 18.0F;
  shadow.spread = 70.0F;
  shadow.size = 80.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 72.0F;
  text_layer.layer_style().outer_glows.push_back(glow);

  document.add_layer(std::move(text_layer));
  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* imported = find_layer_named(read.layers(), "Styled text preview");
  CHECK(imported != nullptr);
  CHECK(imported->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");

  const auto& pixels = imported->pixels();
  std::uint64_t visible_alpha = 0;
  int tall_alpha_columns = 0;
  for (std::int32_t x = 0; x < pixels.width(); ++x) {
    int column_alpha = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++visible_alpha;
        ++column_alpha;
      }
    }
    if (column_alpha * 4 > pixels.height() * 3) {
      ++tall_alpha_columns;
    }
  }

  const auto layer_area = static_cast<std::uint64_t>(pixels.width()) * static_cast<std::uint64_t>(pixels.height());
  CHECK(visible_alpha * 4U < layer_area);
  CHECK(tall_alpha_columns == 0);
}

// A FOREIGN (Photoshop-authored) type layer with a big outer effect but a clean,
// fill-colored raster keeps Photoshop's pixels on import: substitution waits for an
// edit, exactly like Photoshop with a missing font. The same document with Patchy's
// own signature still regenerates (the control), so this pins the authorship gate.
void psd_import_keeps_clean_foreign_styled_text_raster() {
  constexpr std::int32_t layer_width = 320;
  constexpr std::int32_t layer_height = 150;

  patchy::Document document(420, 260, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(420, 260, 230, 220, 204));

  // Clean matte: fill-colored bars, no baked effect pollution.
  auto clean = solid_rgba(layer_width, layer_height, 243, 237, 230, 0);
  for (std::int32_t band = 0; band < 4; ++band) {
    for (std::int32_t y = 20 + band * 32; y < 28 + band * 32; ++y) {
      for (std::int32_t x = 16; x < layer_width - 16; ++x) {
        clean.pixel(x, y)[3] = 255;
      }
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Foreign styled text", std::move(clean));
  text_layer.set_bounds(patchy::Rect{50, 70, layer_width, layer_height});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Bars Bars";
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t9\t34\t1\t0\t#f3ede6\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t9\tcenter";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "90";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "34";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#f3ede6";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 0.0F;
  shadow.spread = 60.0F;
  shadow.size = 80.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);

  const auto reference = std::as_const(text_layer).pixels();  // copy for the post-import compare
  document.add_layer(std::move(text_layer));

  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);

  // Control: read the Patchy-signed bytes as-is; the big shadow regenerates the matte.
  const auto control = patchy::psd::DocumentIo::read(bytes);
  const auto* control_layer = find_layer_named(control.layers(), "Foreign styled text");
  CHECK(control_layer != nullptr);
  bool control_differs = false;
  for (std::int32_t y = 0; y < layer_height && !control_differs; ++y) {
    for (std::int32_t x = 0; x < layer_width; ++x) {
      if (control_layer->pixels().pixel(x, y)[3] != reference.pixel(x, y)[3]) {
        control_differs = true;
        break;
      }
    }
  }
  CHECK(control_differs);

  // Strip Patchy's engine-data signature so the block reads as Photoshop-authored.
  const std::string signature =
      "/KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0";
  auto patched = bytes;
  bool patched_any = false;
  auto it = patched.begin();
  while (true) {
    it = std::search(it, patched.end(), signature.begin(), signature.end());
    if (it == patched.end()) {
      break;
    }
    *it = static_cast<std::uint8_t>('X');  // "/XinsokuSet ..." no longer matches
    patched_any = true;
    ++it;
  }
  CHECK(patched_any);

  const auto read = patchy::psd::DocumentIo::read(patched);
  const auto* imported = find_layer_named(read.layers(), "Foreign styled text");
  CHECK(imported != nullptr);
  CHECK(imported->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");
  CHECK(imported->pixels().width() == layer_width);
  CHECK(imported->pixels().height() == layer_height);
  bool pixels_kept = imported->pixels().width() == layer_width && imported->pixels().height() == layer_height;
  for (std::int32_t y = 0; y < layer_height && pixels_kept; ++y) {
    for (std::int32_t x = 0; x < layer_width; ++x) {
      const auto* actual = imported->pixels().pixel(x, y);
      const auto* expected = reference.pixel(x, y);
      if (actual[3] != expected[3] || (expected[3] > 0U && (actual[0] != expected[0] || actual[1] != expected[1] ||
                                                            actual[2] != expected[2]))) {
        pixels_kept = false;
        break;
      }
    }
  }
  CHECK(pixels_kept);
}

void psd_writer_exports_patchy_rich_text_as_photoshop_type() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer rich_layer(document.allocate_layer_id(), "Text: Red Blue", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(rich_layer));
  layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "Red Blue";
  layer.metadata()[patchy::kLayerMetadataTextHtml] =
      "<html><body><p><span style=\"font-family:'Arial'; font-size:32px; color:#ff2020; font-weight:700;\">Red "
      "</span><span style=\"font-family:'Times New Roman'; font-size:28px; color:#2040ff; font-style:italic;\">Blue"
      "</span></p></body></html>";
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t4\t32\t1\t0\t#ff2020\tArial\n4\t4\t28\t0\t1\t#2040ff\tTimes%20New%20Roman";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t8\tcenter";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#ff2020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", {9, 9, 9}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const std::vector<std::uint8_t> raw_utf16_marker{'(', 0xFEU, 0xFFU};
  const std::vector<std::uint8_t> octal_utf16_marker{'(', '\\', '3', '7', '6', '\\', '3', '7', '7'};
  CHECK(std::search(text_payload->begin(), text_payload->end(), raw_utf16_marker.begin(), raw_utf16_marker.end()) !=
        text_payload->end());
  CHECK(std::search(text_payload->begin(), text_payload->end(), octal_utf16_marker.begin(),
                    octal_utf16_marker.end()) == text_payload->end());
  const std::string generated_payload_text(text_payload->begin(), text_payload->end());
  CHECK(generated_payload_text.find("/DefaultRunData") != std::string::npos);
  CHECK(generated_payload_text.find("/Rendered") != std::string::npos);
  CHECK(generated_payload_text.find("/DocumentResources") != std::string::npos);
  CHECK(generated_payload_text.find("/ShapeType 1") != std::string::npos);
  CHECK(generated_payload_text.find("/BoxBounds") != std::string::npos);
  CHECK(generated_payload_text.find("/PointBase") == std::string::npos);
  CHECK(generated_payload_text.find("/FontSize 32.000000") != std::string::npos);
  CHECK(generated_payload_text.find("/FontSize 28.000000") != std::string::npos);
  CHECK(text_payload->size() >= 16U);
  const auto text_bounds_offset = text_payload->size() - 16U;
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset) == 18U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 4U) == 22U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 8U) == 198U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 12U) == 86U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& round_tripped_text_layer = read.layers().back();
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataText) == "Red Blue");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextSize) == "32");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextColor) == "#ff2020");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextHtml).find("#ff2020") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextHtml).find("#2040ff") != std::string::npos);
  const auto& round_tripped_runs = round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(round_tripped_runs.find("Times%20New%20Roman") != std::string::npos ||
        round_tripped_runs.find("TimesNewRoman") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextParagraphRuns).find("center") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextFlow) == "box");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextBoxWidth) == "180");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextBoxHeight) == "64");

  bool found_generated_type = false;
  bool found_stale_type = false;
  for (const auto& block : round_tripped_text_layer.unknown_psd_blocks()) {
    if (block.key != "TySh") {
      continue;
    }
    found_stale_type = found_stale_type || block.payload == std::vector<std::uint8_t>{9, 9, 9};
    const std::string payload_text(block.payload.begin(), block.payload.end());
    // A genuinely bold or italic run rides in the FONT NAME the writer resolved, so the Faux
    // flags stay false: they are Photoshop's SYNTHETIC embolden and slant, and writing them
    // alongside a real bold/italic face made Photoshop embolden and slant it a second time.
    found_generated_type = payload_text.find("/StyleRun") != std::string::npos &&
                           payload_text.find("/ParagraphRun") != std::string::npos &&
                           payload_text.find("/DocumentResources") != std::string::npos &&
                           payload_text.find("/ShapeType 1") != std::string::npos &&
                           payload_text.find("/BoxBounds") != std::string::npos &&
                           payload_text.find("/Justification 2") != std::string::npos &&
                           payload_text.find("/FauxBold true") == std::string::npos &&
                           payload_text.find("/FauxBold false") != std::string::npos &&
                           payload_text.find("/FauxItalic true") == std::string::npos &&
                           payload_text.find("/FauxItalic false") != std::string::npos;
  }
  CHECK(found_generated_type);
  CHECK(!found_stale_type);
}

void psd_writer_preserves_imported_photoshop_text_geometry() {
  patchy::Document document(1280, 720, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1280, 720, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: THE INDIE PURSE", solid_rgba(1187, 96, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{41, 66, 1187, 96});
  layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t96\t1\t0\t#ffffff\tAdobeGothicStd-Bold";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "1292";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "541";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] =
      "1.0772238306426084 0 0 1.0772238306426084 -61.520306985688736 67.415984778757095";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] =
      "0 -16.1993408203125 1291.559814453125 524.6451416015625";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] =
      "95.5179443359375 -0.841064453125 1196.88671875 87.596435546875";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] =
      "0 0 1291.559814453125 524.6451416015625";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "0 0 0 0";
  layer.metadata()[patchy::kLayerMetadataPsdTextIndex] = "2";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 2U) - 1.0772238306426084) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - -61.520306985688736) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 67.415984778757095) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/ShapeType 1") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 1291.559814 524.645142 ]") != std::string::npos);
  CHECK(payload_text.find("/PointBase") == std::string::npos);
  CHECK(payload_text.find("/FontSize 96.000000") != std::string::npos);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 0U);
}

void psd_writer_prefers_patchy_text_transform_over_imported_geometry() {
  patchy::Document document(240, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 140, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Move Me", solid_rgba(80, 32, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{12, 18, 80, 32});
  layer.metadata()[patchy::kLayerMetadataText] = "Move Me";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t7\t32\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "80";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "32";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 12 18";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 -90 200 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "90 -80 180 -20";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 200 130";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "1 2 3 4";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1.25 0 0 1.5 42 51";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 2U) - 1.25) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 26U) - 1.5) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 42.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 99.0) < 0.000001);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 12U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 18U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 92U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 50U);
}

void psd_writer_exports_point_text_with_photoshop_baseline_origin() {
  patchy::Document document(260, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(260, 140, 255, 255, 255));
  auto pixels = solid_rgba(140, 60, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 120; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Quick Ass", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 140, 60});
  layer.metadata()[patchy::kLayerMetadataText] = "Quick Ass";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t9\t72\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t9\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "140";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "60";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "72";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 40.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 98.0) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/PointBase [ 0.0 0.0 ]") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds") == std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& imported = read.layers().back();
  const auto visual_bounds =
      parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoundingBox));
  CHECK(std::abs(visual_bounds[0]) < 0.001);
  CHECK(std::abs(visual_bounds[1] + 48.0) < 0.001);
  CHECK(std::abs(visual_bounds[2] - 120.0) < 0.001);
  CHECK(std::abs(visual_bounds[3] - 0.0) < 0.001);

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*second_payload, 34U) - 40.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*second_payload, 42U) - 98.0) < 0.000001);
}

void psd_writer_maps_text_raster_bounds_into_transform_local_space() {
  patchy::Document document(360, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 220, 255, 255, 255));
  auto pixels = solid_rgba(220, 115, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < 95; ++y) {
    for (std::int32_t x = 0; x < 150; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Offset Preview", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{50, 50, 220, 115});
  layer.metadata()[patchy::kLayerMetadataText] = "Offset Preview";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t14\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t14\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "220";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 50 60";

  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 2);
  const auto& imported = read.layers().back();
  const auto box_bounds = parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoxBounds));
  CHECK(std::abs(box_bounds[0] - 0.0) < 0.001);
  CHECK(std::abs(box_bounds[1] - 0.0) < 0.001);
  CHECK(std::abs(box_bounds[2] - 220.0) < 0.001);
  CHECK(std::abs(box_bounds[3] - 80.0) < 0.001);
  const auto visual_bounds =
      parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoundingBox));
  CHECK(std::abs(visual_bounds[0] - 0.0) < 0.001);
  CHECK(std::abs(visual_bounds[1] + 10.0) < 0.001);
  CHECK(std::abs(visual_bounds[2] - 150.0) < 0.001);
  CHECK(std::abs(visual_bounds[3] - 85.0) < 0.001);
}

void psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame() {
  const std::string text = "Expanded imported text frame";

  patchy::Document document(360, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 220, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Expanded imported",
                           solid_rgba(260, 96, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{24, 30, 260, 96});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "96";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 24 30";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 24 30";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "24 30 152 70";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 24.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 30.0) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 260.000000 96.000000 ]") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 128.000000 40.000000 ]") == std::string::npos);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 24U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 30U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 284U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 126U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* round_tripped = find_layer_named(read.layers(), "Text: Expanded imported");
  CHECK(round_tripped != nullptr);
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextFlow) == "box");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextBoxWidth) == "260");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextBoxHeight) == "96");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextTransform) == "1 0 0 1 24 30");

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  const std::string second_payload_text(second_payload->begin(), second_payload->end());
  CHECK(second_payload_text.find("/BoxBounds [ 0.000000 0.000000 260.000000 96.000000 ]") !=
        std::string::npos);
  CHECK(std::abs(read_f64_be_at(*second_payload, 34U) - 24.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*second_payload, 42U) - 30.0) < 0.000001);
}

// Photoshop lays a Patchy block's first line at box top + cap height x size while Qt's raster
// has it at box top + winAscent; the renderer records the difference
// (kLayerMetadataTextBoxBaselineInset) and the writer moves the transform origin down by it with
// /BoxBounds still at the origin and the descriptor 'bounds' top at -inset. The reader puts the
// origin back on a Patchy-signed block so the frame Patchy edits reopens where it was, and the
// next save writes the identical block.
// Whether any paragraph "/AutoLeading <number>" in the engine data equals `fraction` (the writer
// prints doubles at 17 significant digits, so 1.15 lands as 1.1499999999999999).
bool payload_has_paragraph_auto_leading(const std::string& payload_text, double fraction) {
  const std::string key = "/AutoLeading ";
  for (auto at = payload_text.find(key); at != std::string::npos; at = payload_text.find(key, at + key.size())) {
    const auto value_text = payload_text.substr(at + key.size(), 32);
    if (value_text.rfind("true", 0) == 0 || value_text.rfind("false", 0) == 0) {
      continue;
    }
    if (std::abs(std::strtod(value_text.c_str(), nullptr) - fraction) < 0.0001) {
      return true;
    }
  }
  return false;
}

void psd_writer_box_text_baseline_inset_moves_box_bounds() {
  const std::string text = "Hey everyone";
  patchy::Document document(360, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 220, 255, 255, 255));
  auto pixels = solid_rgba(260, 96, 0, 0, 0, 0);
  for (std::int32_t y = 12; y < 60; ++y) {
    for (std::int32_t x = 4; x < 200; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Inset", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{24, 30, 260, 96});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "96";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 24 30";
  layer.metadata()[patchy::kLayerMetadataTextBoxBaselineInset] = "6.5";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 24.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 36.5) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 260.000000 96.000000 ]") != std::string::npos);
  // Paragraph auto leading stays Photoshop's default without a recorded pitch.
  CHECK(payload_has_paragraph_auto_leading(payload_text, 1.2));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* reopened = find_layer_named(read.layers(), "Text: Inset");
  CHECK(reopened != nullptr);
  if (reopened == nullptr) {
    return;
  }
  CHECK(reopened->metadata().at(patchy::kLayerMetadataTextFlow) == "box");
  CHECK(reopened->metadata().at(patchy::kLayerMetadataTextBoxWidth) == "260");
  CHECK(reopened->metadata().at(patchy::kLayerMetadataTextBoxHeight) == "96");
  const auto reopened_transform =
      patchy::parse_layer_affine_transform(reopened->metadata().at(patchy::kLayerMetadataTextTransform));
  CHECK(reopened_transform.has_value());
  if (reopened_transform.has_value()) {
    CHECK(std::abs((*reopened_transform)[4] - 24.0) < 1e-9);
    CHECK(std::abs((*reopened_transform)[5] - 30.0) < 1e-9);
  }
  CHECK(!reopened->metadata().contains(patchy::kLayerMetadataTextLayoutMode));
  const auto frame_bounds = parse_bounds_metadata4(reopened->metadata().at(patchy::kLayerMetadataPsdTextBounds));
  CHECK(std::abs(frame_bounds[1]) < 0.001);
  CHECK(std::abs(frame_bounds[3] - 96.0) < 0.001);
  const auto box_bounds = parse_bounds_metadata4(reopened->metadata().at(patchy::kLayerMetadataPsdTextBoxBounds));
  CHECK(std::abs(box_bounds[0]) < 0.001);
  CHECK(std::abs(box_bounds[1]) < 0.001);
  CHECK(std::abs(box_bounds[2] - 260.0) < 0.001);
  CHECK(std::abs(box_bounds[3] - 96.0) < 0.001);
  const auto inset_entry = reopened->metadata().find(patchy::kLayerMetadataTextBoxBaselineInset);
  CHECK(inset_entry != reopened->metadata().end());
  if (inset_entry != reopened->metadata().end()) {
    CHECK(std::abs(std::stod(inset_entry->second) - 6.5) < 0.000001);
  }

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  if (second_payload.has_value()) {
    CHECK(*second_payload == *text_payload);
  }
}

// The renderer's first-line baseline (kLayerMetadataTextFirstBaseline, from the raster's top
// row) anchors a point-text transform: Photoshop lays the first line ON ty, so the ink-bottom
// scan (right only for a caps-only single line) yields to it. The second write keeps it: the
// reopened layer's boundingBox top is negative, which the writer reads as "already anchored".
void psd_writer_point_text_first_baseline_beats_ink_bottom() {
  patchy::Document document(260, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(260, 140, 255, 255, 255));
  auto pixels = solid_rgba(140, 60, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < 54; ++y) {  // "Hey": ascender-to-descender ink, 54 rows
    for (std::int32_t x = 0; x < 120; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Hey", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 140, 60});
  layer.metadata()[patchy::kLayerMetadataText] = "Hey";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t3\t48\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t3\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "140";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "60";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "48";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextFirstBaseline] = "43.5";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 40.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 93.5) < 0.000001);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* reopened = find_layer_named(read.layers(), "Text: Hey");
  CHECK(reopened != nullptr);
  if (reopened == nullptr) {
    return;
  }
  const auto visual_bounds =
      parse_bounds_metadata4(reopened->metadata().at(patchy::kLayerMetadataPsdTextBoundingBox));
  CHECK(std::abs(visual_bounds[0]) < 0.001);
  CHECK(std::abs(visual_bounds[1] + 43.5) < 0.001);
  CHECK(std::abs(visual_bounds[2] - 120.0) < 0.001);
  CHECK(std::abs(visual_bounds[3] - 10.5) < 0.001);
  CHECK(!reopened->metadata().contains(patchy::kLayerMetadataTextFirstBaseline));

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  if (second_payload.has_value()) {
    CHECK(std::abs(read_f64_be_at(*second_payload, 42U) - 93.5) < 0.000001);
  }
}

// A raster grown ABOVE the text-local origin (kLayerMetadataTextRasterTop negative: glyph ink
// overshooting the line top, which CoreText's smaller ascent does to the issue 20 "M") still
// gets its transform anchored on the baseline: the ink starting above ty no longer reads as
// "already anchored". A layer whose ty already IS the baseline (raster an extra first_baseline
// higher, the reopened-layer migration) is left alone, so nothing shifts twice.
void psd_writer_point_text_grown_raster_still_anchors_baseline() {
  const auto make_document = [](double ty) {
    patchy::Document document(260, 160, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(260, 160, 255, 255, 255));
    auto pixels = solid_rgba(140, 80, 0, 0, 0, 0);
    for (std::int32_t y = 0; y < 54; ++y) {  // ink from the raster's top row down
      for (std::int32_t x = 0; x < 120; ++x) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = 32;
        pixel[1] = 32;
        pixel[2] = 32;
        pixel[3] = 255;
      }
    }
    patchy::Layer text_layer(document.allocate_layer_id(), "Text: Swash", std::move(pixels));
    auto& layer = document.add_layer(std::move(text_layer));
    layer.set_bounds(patchy::Rect{40, 50, 140, 80});  // raster top 50, 20 px above the origin at 70
    layer.metadata()[patchy::kLayerMetadataText] = "Swash";
    layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t5\t48\t0\t0\t#202020\tArial";
    layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t5\tleft";
    layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
    layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "140";
    layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
    layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
    layer.metadata()[patchy::kLayerMetadataTextSize] = "48";
    layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
    layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    layer.metadata()[patchy::kLayerMetadataTextFirstBaseline] = "43.5";
    layer.metadata()[patchy::kLayerMetadataTextRasterTop] = "-20";
    layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 40 " + std::to_string(ty);
    return document;
  };
  const auto tysh_ty = [](const patchy::Document& document) -> std::optional<double> {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
    if (!payload.has_value()) {
      return std::nullopt;
    }
    return read_f64_be_at(*payload, 42U);
  };
  // ty at the origin: the baseline anchor applies even though the ink starts 20 px above ty.
  const auto anchored = tysh_ty(make_document(70.0));
  CHECK(anchored.has_value());
  if (anchored.has_value()) {
    CHECK(std::abs(*anchored - 113.5) < 0.000001);
  }
  // ty already on the baseline (raster 63.5 above it, not 20): written as is.
  const auto kept = tysh_ty(make_document(113.5));
  CHECK(kept.has_value());
  if (kept.has_value()) {
    CHECK(std::abs(*kept - 113.5) < 0.000001);
  }
}

// A Qt-natural layer's recorded line pitch (kLayerMetadataTextAutoLeading, a fraction of the
// dominant size) becomes the paragraph /AutoLeading so Photoshop's auto leading advances like
// Qt; it round-trips through the v3 paragraph column without flipping the layer into the
// Photoshop layout model. A Photoshop-layout layer keeps its own paragraph fraction.
void psd_writer_qt_natural_auto_leading_fraction() {
  const auto make_document = [](bool photoshop_layout) {
    patchy::Document document(260, 140, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(260, 140, 255, 255, 255));
    patchy::Layer text_layer(document.allocate_layer_id(), "Text: Two lines", solid_rgba(140, 90, 32, 32, 32, 255));
    auto& layer = document.add_layer(std::move(text_layer));
    layer.set_bounds(patchy::Rect{40, 50, 140, 90});
    layer.metadata()[patchy::kLayerMetadataText] = "Two\nlines";
    layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t9\t36\t0\t0\t#202020\tArial";
    layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
        photoshop_layout ? "v3\n0\t4\tleft\t0\t0\t0\t0\t0\t1.3\n4\t5\tleft\t0\t0\t0\t0\t0\t1.3" : "v1\n0\t4\tleft\n4\t5\tleft";
    layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
    layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "140";
    layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "90";
    layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
    layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
    layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
    layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    layer.metadata()[patchy::kLayerMetadataTextAutoLeading] = "1.15";
    if (photoshop_layout) {
      layer.metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
    }
    return document;
  };

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(make_document(false));
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_has_paragraph_auto_leading(payload_text, 1.15));
  // Style runs keep the auto shape: a fixed /Leading would read back as Photoshop provenance.
  CHECK(payload_text.find("/AutoLeading true /Leading 43.200000") != std::string::npos);
  CHECK(payload_text.find("/AutoLeading false") == std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* reopened = find_layer_named(read.layers(), "Text: Two lines");
  CHECK(reopened != nullptr);
  if (reopened == nullptr) {
    return;
  }
  CHECK(!reopened->metadata().contains(patchy::kLayerMetadataTextLayoutMode));
  CHECK(!reopened->metadata().contains(patchy::kLayerMetadataTextAutoLeading));
  CHECK(reopened->metadata().at(patchy::kLayerMetadataTextParagraphRuns).rfind("v3\n", 0) == 0);
  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  if (second_payload.has_value()) {
    const std::string second_text(second_payload->begin(), second_payload->end());
    CHECK(payload_has_paragraph_auto_leading(second_text, 1.15));
  }

  const auto photoshop_bytes = patchy::psd::DocumentIo::write_layered_rgb8(make_document(true));
  const auto photoshop_payload = psd_layer_block_payload(psd_layer_extra_data(photoshop_bytes, 1), "TySh");
  CHECK(photoshop_payload.has_value());
  if (photoshop_payload.has_value()) {
    const std::string photoshop_text(photoshop_payload->begin(), photoshop_payload->end());
    CHECK(payload_has_paragraph_auto_leading(photoshop_text, 1.3));
    CHECK(!payload_has_paragraph_auto_leading(photoshop_text, 1.15));
  }
}

void psd_writer_regenerates_same_length_patchy_text_without_stale_template() {
  patchy::Document source_document(240, 120, patchy::PixelFormat::rgb8());
  source_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer source_text(source_document.allocate_layer_id(), "Text: THE INDIE CURSE",
                           solid_rgba(180, 64, 0, 0, 0, 0));
  auto& source_layer = source_document.add_layer(std::move(source_text));
  source_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  source_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE CURSE";
  source_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t1\t0\t#ff2020\tArial";
  source_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  source_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  auto source_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(source_document), 1),
                              "TySh")
          .value();
  std::fill(source_payload.end() - 16, source_payload.end(), std::uint8_t{0});

  patchy::Document edited_document(240, 120, patchy::PixelFormat::rgb8());
  edited_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer edited_text(edited_document.allocate_layer_id(), "Text: THE INDIE PURSE",
                            solid_rgba(180, 64, 0, 0, 0, 0));
  auto& edited_layer = edited_document.add_layer(std::move(edited_text));
  edited_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  edited_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  edited_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t1\t0\t#ff2020\tArial";
  edited_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  edited_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  edited_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  edited_layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", source_payload});

  const auto edited_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(edited_document), 1),
                              "TySh")
          .value();
  const auto old_text = utf16be_test_bytes("THE INDIE CURSE");
  const auto new_text = utf16be_test_bytes("THE INDIE PURSE");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), old_text.begin(), old_text.end()) ==
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), new_text.begin(), new_text.end()) !=
        edited_payload.end());
  const std::string payload_text(edited_payload.begin(), edited_payload.end());
  CHECK(payload_text.find("/FontSize 32.000000") != std::string::npos);
  CHECK(payload_text.find("/AutoLeading true /Leading 38.400000") != std::string::npos);
}

void psd_writer_ignores_stale_type_template_after_patchy_text_edit() {
  patchy::Document source_document(240, 120, patchy::PixelFormat::rgb8());
  source_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer source_text(source_document.allocate_layer_id(), "Text: THE INDIE CURSE",
                           solid_rgba(180, 64, 0, 0, 0, 0));
  auto& source_layer = source_document.add_layer(std::move(source_text));
  source_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  source_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE CURSE";
  source_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t0\t0\t#ff2020\tCourier%20New";
  source_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  source_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto source_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(source_document), 1),
                              "TySh")
          .value();

  patchy::Document edited_document(240, 120, patchy::PixelFormat::rgb8());
  edited_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer edited_text(edited_document.allocate_layer_id(), "Text: THE INDIE PURSE",
                            solid_rgba(180, 64, 0, 0, 0, 0));
  auto& edited_layer = edited_document.add_layer(std::move(edited_text));
  edited_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  edited_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  edited_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t0\t0\t#ff2020\tArial";
  edited_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  edited_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  edited_layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", source_payload});

  const auto edited_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(edited_document), 1),
                              "TySh")
          .value();
  const auto old_text = utf16be_test_bytes("THE INDIE CURSE");
  const auto new_text = utf16be_test_bytes("THE INDIE PURSE");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), old_text.begin(), old_text.end()) ==
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), new_text.begin(), new_text.end()) !=
        edited_payload.end());

  const std::string payload_text(edited_payload.begin(), edited_payload.end());
  CHECK(payload_text.find("/FontSize 32.000000") != std::string::npos);
#ifdef _WIN32
  const auto arial_postscript = utf16be_test_bytes("ArialMT");
  const auto courier_family = utf16be_test_bytes("Courier");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), arial_postscript.begin(), arial_postscript.end()) !=
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), courier_family.begin(), courier_family.end()) ==
        edited_payload.end());
#endif
}

void psd_extended_blend_modes_round_trip() {
  const std::vector<patchy::BlendMode> modes = {
      patchy::BlendMode::Darken,     patchy::BlendMode::Lighten,
      patchy::BlendMode::ColorDodge, patchy::BlendMode::ColorBurn,
      patchy::BlendMode::HardLight,  patchy::BlendMode::SoftLight,
      patchy::BlendMode::Difference, patchy::BlendMode::LinearBurn,
      patchy::BlendMode::PinLight,   patchy::BlendMode::Saturation,
      patchy::BlendMode::Luminosity, patchy::BlendMode::VividLight,
      patchy::BlendMode::LinearLight, patchy::BlendMode::HardMix,
      patchy::BlendMode::DarkerColor, patchy::BlendMode::LighterColor,
      patchy::BlendMode::Dissolve,
  };

  for (const auto mode : modes) {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(2, 2, 120, 120, 120));
    auto& top = document.add_pixel_layer("Top", solid_rgba(2, 2, 200, 60, 100, 255));
    top.set_blend_mode(mode);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 2);
    CHECK(read.layers()[1].blend_mode() == mode);
  }
}

void psd_text_layer_engine_data_renders_placeholder_text() {
  const std::string text = "Patchy Text";
  const std::string engine_data =
      "/EngineData << /Editor << /Text (" + text +
      "\\r) >> /StyleRun << /StyleSheetData << /FontSize 36 /FillColor << /Type 1 /Values [ 1.0 .87059 .87059 .87059 ] "
      ">> >> >> >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Text Layer", 4);
  write_ascii4(layer_extra, "8BIM");
  write_ascii4(layer_extra, "TySh");
  layer_extra.write_u32(static_cast<std::uint32_t>(payload.size()));
  layer_extra.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    layer_extra.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(12);
  layer_info.write_u32(10);
  layer_info.write_u32(42);
  layer_info.write_u32(190);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 220;
  constexpr std::uint32_t height = 80;
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  CHECK(layer.name() == "Text Layer");
  CHECK(layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextSize) == "36");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextColor) == "#dedede");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "placeholder");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer.bounds().x == 10);
  CHECK(layer.bounds().y == 12);

  bool has_text_pixels = false;
  for (std::size_t offset = 3; offset < layer.pixels().data().size(); offset += 4) {
    if (layer.pixels().data()[offset] != 0U) {
      has_text_pixels = true;
      break;
    }
  }
  CHECK(has_text_pixels);
}

void psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style() {
  std::string raw_text = "One\rTwo";
  raw_text.push_back('\x03');
  raw_text += "Three\r";
  const auto text_literal = engine_utf16be_literal(raw_text);
  const auto font_literal = engine_utf16be_literal("Verdana-Bold");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 20 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 14 ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ 14 ] >> /AntiAlias 3 "
      "/UseFractionalGlyphWidths true /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  const auto& metadata = layer.metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == "One\nTwo\nThree");
  CHECK(metadata.at(patchy::kLayerMetadataText).find('\r') == std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataText).find('\x03') == std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Verdana");
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "20");
  CHECK(metadata.at(patchy::kLayerMetadataTextColor) == "#000000");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "false");
  CHECK(metadata.at(patchy::kLayerMetadataTextAntiAlias) == "3");
  CHECK(metadata.at(patchy::kLayerMetadataTextHtml).find("<br") != std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t13\t20\t1\t0\t#000000\tVerdana") !=
        std::string::npos);
}

void psd_text_faux_bold_stays_off_the_bold_face() {
  // Photoshop's /FauxBold synthesizes weight on the run's OWN face; it is not a request for the
  // family's bold face (the Dungeon Scroll headings: Georgia-Italic + faux bold, where resolving
  // to Georgia Bold Italic rendered a visibly different, ~14% wider typeface). It must survive
  // as its own runs-v4 column, leave bold/italic reporting the real face, and write back as
  // /FauxBold true on a NON-bold font name.
  const auto text_literal = engine_utf16be_literal("Dungeon:\r");
  const auto font_literal = engine_utf16be_literal("Georgia-Italic");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 12 "
      "/FauxBold true /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 9 ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ 9 ] >> /AntiAlias 2 "
      "/UseFractionalGlyphWidths true /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 2 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Georgia");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "false");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "true");
  const auto& runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(runs.rfind("v4", 0) == 0);
  const auto run_start = runs.find('\n');
  CHECK(run_start != std::string::npos);
  if (run_start != std::string::npos) {
    const auto line = runs.substr(run_start + 1);
    // start len size bold italic color family leading tracking hscale vscale fauxbold
    CHECK(std::count(line.begin(), line.end(), '\t') == 11);
    CHECK(!line.empty() && line.back() == '1');
    CHECK(line.find("\t0\t1\t") != std::string::npos);  // bold 0, italic 1
  }

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
  CHECK(written.has_value());
  if (written.has_value()) {
    const std::string written_text(written->begin(), written->end());
    CHECK(written_text.find("/FauxBold true") != std::string::npos);
    CHECK(written_text.find("/FauxBold false") == std::string::npos);
  }
}

void psd_text_faux_italic_stays_off_the_italic_face() {
  // The slant half of the same split: /FauxItalic synthesizes a slant on the run's own face and
  // must not resolve to the family's real Italic, which is a different typeface. It rides runs v6
  // column 13 and writes back as /FauxItalic on a NON-italic font name.
  const auto text_literal = engine_utf16be_literal("Slanted\r");
  const auto font_literal = engine_utf16be_literal("Verdana");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 18 "
      "/FauxBold false /FauxItalic true /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 8 ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ 8 ] >> /AntiAlias 2 "
      "/FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 1 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Verdana");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "false");
  const auto& runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(runs.rfind("v6", 0) == 0);
  const auto run_start = runs.find('\n');
  CHECK(run_start != std::string::npos);
  if (run_start != std::string::npos) {
    const auto line = runs.substr(run_start + 1);
    // start len size bold italic color family leading tracking hscale vscale fauxbold style fauxitalic
    CHECK(std::count(line.begin(), line.end(), '\t') == 13);
    CHECK(!line.empty() && line.back() == '1');
    CHECK(line.find("\t0\t0\t") != std::string::npos);  // bold 0, italic 0
  }

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
  CHECK(written.has_value());
  if (written.has_value()) {
    const std::string written_text(written->begin(), written->end());
    CHECK(written_text.find("/FauxItalic true") != std::string::npos);
    CHECK(written_text.find("/FauxItalic false") == std::string::npos);
  }
}

void psd_text_flag_expressible_face_never_bakes_into_the_family() {
  // Bookman Old Style declares its whole family at weight 500, so the resolver's keep-the-real-
  // face rule used to turn "BookmanOldStyle-Italic" into display family "Bookman Old Style
  // Italic" - a name the font database cannot list faces for, which emptied the style picker.
  // A face the bold/italic flags CAN express must come back as the plain family plus the flag,
  // whatever weight the family declares.
  const auto text_literal = engine_utf16be_literal("Jumble\r");
  const auto font_literal = engine_utf16be_literal("BookmanOldStyle-Italic");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 16 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 7 ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ 7 ] >> /AntiAlias 2 "
      "/FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Bookman Old Style");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "false");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "true");
  const auto& runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(runs.find("\t0\t1\t#000000\tBookman%20Old%20Style") != std::string::npos);
  CHECK(runs.find("Bookman%20Old%20Style%20Italic") == std::string::npos);

  // The written FontSet must never carry the baked family-with-face name either: with the font
  // installed the italic face's PostScript name comes back, and without it the plain family
  // exports verbatim.
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
  CHECK(written.has_value());
  if (written.has_value()) {
    const auto baked_name = utf16be_test_bytes("Bookman Old Style Italic");
    CHECK(std::search(written->begin(), written->end(), baked_name.begin(), baked_name.end()) == written->end());
  }
}

void psd_text_recorded_style_resolves_the_exact_face() {
  // The style column (runs v5) names the exact face; the writer must resolve THAT face's
  // PostScript name instead of flattening it onto the family's Bold face. "Arial" with style
  // "Black" is the canonical case: the bold flag rides along for uninstalled-face fallback, and
  // flattening it wrote Arial-BoldMT (~15% narrower glyphs than Arial Black).
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer rich_layer(document.allocate_layer_id(), "Text: XY", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(rich_layer));
  layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "XY";
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v5\n0\t1\t32\t1\t0\t#202020\tArial\tauto\t0\t1\t1\t0\tBlack\n"
      "1\t1\t32\t1\t0\t#202020\tArial\tauto\t0\t1\t1\t0\t";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
#ifdef _WIN32
  // DirectWrite's weight-stretch-style family model files Arial Black under family "Arial" as
  // the weight-900 "Black" face; both stock faces resolve to their PostScript names.
  const auto black_name = utf16be_test_bytes("Arial-Black");
  const auto bold_name = utf16be_test_bytes("Arial-BoldMT");
  CHECK(std::search(text_payload->begin(), text_payload->end(), black_name.begin(), black_name.end()) !=
        text_payload->end());
  CHECK(std::search(text_payload->begin(), text_payload->end(), bold_name.begin(), bold_name.end()) !=
        text_payload->end());

  // Closing the loop: Photoshop's name for the Black face reads back as its own display family
  // ("Arial Black", the reader's >= 800 weight rule), not as a flattened Arial Bold.
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  if (read.layers().size() == 2) {
    const auto& round_tripped_runs = read.layers().back().metadata().at(patchy::kLayerMetadataTextRuns);
    CHECK(round_tripped_runs.find("Arial%20Black") != std::string::npos);
  }
#else
  // The non-Windows stub exports the display family verbatim; it must not invent a bold name.
  const auto verbatim = utf16be_test_bytes("Arial");
  CHECK(std::search(text_payload->begin(), text_payload->end(), verbatim.begin(), verbatim.end()) !=
        text_payload->end());
#endif
}

namespace {

std::vector<std::uint8_t> single_run_text_psd(std::string_view text, std::string_view postscript_name) {
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal(postscript_name);
  const auto length = std::to_string(text.size());
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 16 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ " +
      length +
      " ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ " +
      length + " ] >> /AntiAlias 2 /FontSet [ << /Name " + font_literal +
      " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());
  return single_text_layer_psd(payload);
}

// The family field and the style field of the first run line of a patchy.text.runs blob.
struct FirstRunFields {
  std::string family;
  std::string style;
};

FirstRunFields first_run_fields(const std::string& runs) {
  FirstRunFields fields;
  const auto run_start = runs.find('\n');
  if (run_start == std::string::npos) {
    return fields;
  }
  const auto line = runs.substr(run_start + 1, runs.find('\n', run_start + 1) - run_start - 1);
  std::vector<std::string> columns;
  std::size_t start = 0;
  while (true) {
    const auto tab = line.find('\t', start);
    columns.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) {
      break;
    }
    start = tab + 1;
  }
  // start len size bold italic color family leading tracking hscale vscale fauxbold style ...
  if (columns.size() > 6) {
    fields.family = columns[6];
  }
  if (columns.size() > 12) {
    fields.style = columns[12];
  }
  return fields;
}

#ifdef _WIN32
// The system font directory, the same convention tests/test_fonts.hpp registers from.
bool windows_font_file_installed(const char* file_name) {
  return std::filesystem::exists(std::filesystem::path("C:/Windows/Fonts") / file_name);
}
#endif

}  // namespace

void psd_text_gdi_family_wins_when_directwrite_renames_the_face() {
  // Issue 16 (Balmoral LET): the DirectWrite resolver stored its derived "family + face" name,
  // which Qt's Windows font database (GDI names) does not list when DirectWrite regroups a font
  // under a weight-stretch-style family. Franklin Gothic Medium is the same split on a stock
  // Windows font: DirectWrite files it as family "Franklin Gothic" + face "Medium" at weight 400,
  // GDI as family "Franklin Gothic Medium" + "Regular". The stored family has to be the GDI one,
  // with no style column (Regular is flag-expressible) and no bold flag. The suffix heuristic
  // humanizes the PostScript name to the same family, so the expectation holds everywhere; the
  // regression it guards ("Franklin Gothic" + style "Medium") only happens where DirectWrite
  // resolves the name.
  auto read = patchy::psd::DocumentIo::read(single_run_text_psd("Method\r", "FranklinGothic-Medium"));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Franklin Gothic Medium");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "false");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "false");
  const auto fields = first_run_fields(metadata.at(patchy::kLayerMetadataTextRuns));
  CHECK(fields.family == "Franklin%20Gothic%20Medium");
  CHECK(fields.style.empty());

#ifdef _WIN32
  // With the font installed the writer must find the GDI family again (DirectWrite has no family
  // of that name) and write the face's real PostScript name, not the display family verbatim.
  if (windows_font_file_installed("framd.ttf")) {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
    const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
    CHECK(written.has_value());
    if (written.has_value()) {
      const auto postscript = utf16be_test_bytes("FranklinGothic-Medium");
      CHECK(std::search(written->begin(), written->end(), postscript.begin(), postscript.end()) != written->end());
    }
  } else {
    std::cout << "[SKIP] framd.ttf is not installed (Franklin Gothic Medium PostScript round trip)\n";
  }
#endif
}

void psd_text_heavy_legacy_face_keeps_the_gdi_family_if_available() {
  // A heavy face (weight >= 800) used to return DirectWrite's FULL_NAME string early, skipping
  // the GDI-name normalization every other face gets. Futura Extra Black BT (FUTURAXK.TTF, a
  // legacy face DirectWrite names by its full name; GDI lists "Futura XBlk BT" + "Extra Black")
  // therefore came back as the family "Futura Extra Black BT", which no font database lists,
  // so an unchanged edit of a scripted poster headline re-rendered in Tahoma. With the font
  // installed the reader stores the GDI family and face (bold set: Black counts as bold for the
  // uninstalled fallback), the writer resolves that pair back to the PostScript name, and a
  // document that stored the full name as its family exports the real face as well.
#ifdef _WIN32
  if (!patchy::psd::installed_font_for_name("FuturaBT-ExtraBlack").has_value()) {
    std::cout << "[SKIP] Futura Extra Black BT is not installed (heavy legacy face round trip)\n";
    return;
  }
  const auto read = patchy::psd::DocumentIo::read(single_run_text_psd("Diorama\r", "FuturaBT-ExtraBlack"));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Futura XBlk BT");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  const auto fields = first_run_fields(metadata.at(patchy::kLayerMetadataTextRuns));
  CHECK(fields.family == "Futura%20XBlk%20BT");
  CHECK(fields.style == "Extra%20Black");

  const auto postscript = utf16be_test_bytes("FuturaBT-ExtraBlack");
  {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
    const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
    CHECK(written.has_value());
    if (written.has_value()) {
      CHECK(std::search(written->begin(), written->end(), postscript.begin(), postscript.end()) != written->end());
    }
  }

  // The full name stored by the older reader: the writer's full-name lookup exports the face
  // instead of the name verbatim, and the engine-side lookup (main_window.cpp) renders it.
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer legacy(document.allocate_layer_id(), "Text: Diorama", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(legacy));
  layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "Diorama";
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t7\t32\t1\t0\t#202020\tFutura%20Extra%20Black%20BT";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Futura Extra Black BT";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(written.has_value());
  if (written.has_value()) {
    CHECK(std::search(written->begin(), written->end(), postscript.begin(), postscript.end()) != written->end());
    const auto verbatim = utf16be_test_bytes("Futura Extra Black BT");
    CHECK(std::search(written->begin(), written->end(), verbatim.begin(), verbatim.end()) == written->end());
  }
#else
  std::cout << "[SKIP] DirectWrite-only (heavy legacy face round trip)\n";
#endif
}

// Photoshop's document-level text engine block ('Txt2') is trusted over the layers' own TySh:
// a preserved one made Photoshop read a layer Patchy had retyped in Bahnschrift Light as
// Bahnschrift Bold, silently, through the stale object the layer's TextIndex pointed at
// (September 2026 COM capture). The block stays (dropping it demotes every layer), and a
// regenerated type layer gets an index no object has, so Photoshop reads that layer from its
// TySh while untouched layers keep theirs. An untouched round trip keeps every index.
void psd_text_regenerated_layer_gets_an_index_outside_the_text_engine_block() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-tracking.psd");
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto has_text_engine_block = [](const std::vector<std::uint8_t>& bytes) {
    static constexpr std::string_view kMarker = "8BIMTxt2";
    return std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end()) != bytes.end();
  };
  const auto text_index_of = [](const std::vector<std::uint8_t>& bytes, int layer_index) -> std::optional<std::int32_t> {
    const auto payload =
        psd_layer_block_payload(psd_layer_extra_data(bytes, static_cast<std::int16_t>(layer_index)), "TySh");
    if (!payload.has_value()) {
      return std::nullopt;
    }
    static constexpr std::string_view kKey = "TextIndexlong";
    const auto it = std::search(payload->begin(), payload->end(), kKey.begin(), kKey.end());
    if (it == payload->end() || std::distance(it, payload->end()) < static_cast<std::ptrdiff_t>(kKey.size() + 4)) {
      return std::nullopt;
    }
    const auto* p = &*(it + static_cast<std::ptrdiff_t>(kKey.size()));
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                                     (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]));
  };
  std::vector<int> text_layer_indices;
  for (std::size_t i = 0; i < document.layers().size(); ++i) {
    if (patchy::layer_is_text(document.layers()[i])) {
      text_layer_indices.push_back(static_cast<int>(i));
    }
  }
  CHECK(text_layer_indices.size() == 2);
  if (text_layer_indices.size() != 2) {
    return;
  }
  const auto untouched = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(has_text_engine_block(untouched));
  const auto first_index = text_index_of(untouched, text_layer_indices[0]);
  const auto second_index = text_index_of(untouched, text_layer_indices[1]);
  CHECK(first_index.has_value() && second_index.has_value());
  if (!first_index.has_value() || !second_index.has_value()) {
    return;
  }
  CHECK(*first_index < 100000 && *second_index < 100000);

  // What a commit leaves behind on the first text layer: Patchy's own raster and runs.
  document.layers()[static_cast<std::size_t>(text_layer_indices[0])].metadata()[patchy::kLayerMetadataTextRasterStatus] =
      "patchy_raster";
  const auto retyped = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(has_text_engine_block(retyped));
  const auto regenerated_index = text_index_of(retyped, text_layer_indices[0]);
  const auto kept_index = text_index_of(retyped, text_layer_indices[1]);
  CHECK(regenerated_index.has_value() && *regenerated_index >= 100000);
  CHECK(kept_index.has_value() && kept_index == second_index);
}

void psd_text_balmoral_let_plain_never_bakes_a_synthesized_weight_if_available() {
  // The reporter's font (issue 16): Balmoral LET is an old TrueType font whose only face is
  // "Plain" at OS/2 weight class 5, which DirectWrite reads as Medium (500) and files as family
  // "Balmoral LET Plain" + face "Medium". The reader used to store "Balmoral LET Plain Medium",
  // so editing the imported layer warned that an installed font was missing. Wherever the name
  // resolves (DirectWrite with the font installed, the suffix heuristic without it) the stored
  // family must carry neither the synthesized weight nor a style column: "Plain" is the regular
  // face and is flag-expressible.
  const auto check_layer = [](const patchy::Layer& layer) {
    const auto& metadata = layer.metadata();
    const auto family = metadata.at(patchy::kLayerMetadataTextFont);
    CHECK(family.find("Balmoral") != std::string::npos);
    CHECK(family.find("Medium") == std::string::npos);
    CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "false");
    CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "false");
    const auto fields = first_run_fields(metadata.at(patchy::kLayerMetadataTextRuns));
    CHECK(fields.family.find("Medium") == std::string::npos);
    CHECK(fields.style.empty());
    return family;
  };

  const auto read = patchy::psd::DocumentIo::read(single_run_text_psd("La methode\r", "BalmoralLetPlain"));
  CHECK(read.layers().size() == 1);
  if (read.layers().empty()) {
    return;
  }
  const auto family = check_layer(read.layers().front());
  if (family == "Balmoral LET") {
    // The font is installed: DirectWrite resolved it, so the writer must give Photoshop the same
    // PostScript name back through the WIN32-name lookup (DirectWrite knows no "Balmoral LET"
    // family of its own).
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
    const auto written = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
    CHECK(written.has_value());
    if (written.has_value()) {
      const auto postscript = utf16be_test_bytes("BalmoralLetPlain");
      CHECK(std::search(written->begin(), written->end(), postscript.begin(), postscript.end()) != written->end());
    }
  }

  // The reporter's PSD itself, when copied into local-test-fixtures: every layer set in the
  // font obeys the same rules.
  const auto path = patchy::test::local_psd_fixture_path("La methode.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  int balmoral_layers = 0;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        visit(layer.children());
        continue;
      }
      const auto font = layer.metadata().find(patchy::kLayerMetadataTextFont);
      if (font != layer.metadata().end() && font->second.find("Balmoral") != std::string::npos) {
        ++balmoral_layers;
        check_layer(layer);
      }
    }
  };
  visit(document.layers());
  CHECK(balmoral_layers > 0);
}

void psd_text_engine_data_preserves_paragraph_layout_runs() {
  const std::string first = "Speed Mode - Hold down TAB and the entire game will run faster.";
  const std::string second = "Saving your game - Find a Save Machine and use it.";
  const std::string third = "Quick state saves - F5 to save and F9 to load";
  std::string raw_text = first + "\r" + second + "\r" + third;
  raw_text.push_back('\x03');
  raw_text.push_back('\x03');
  raw_text.push_back('\r');

  const auto text_literal = engine_utf16be_literal(raw_text);
  const auto font_literal = engine_utf16be_literal("Arial-BoldMT");
  const auto paragraph_properties = [](int space_after) {
    return std::string("<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties << /Justification 0 "
                       "/FirstLineIndent -24.0 /StartIndent 24.0 /EndIndent 0.0 /SpaceBefore 0.0 /SpaceAfter ") +
           std::to_string(space_after) +
           ".0 /AutoHyphenate false /Hanging true >> >> /Adjustments << /Axis [ 1.0 0.0 1.0 ] "
           "/XY [ 0.0 0.0 ] >> >>";
  };
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size()) + 1;
  const auto third_engine_length = static_cast<int>(third.size()) + 1;
  const auto normalized_text = first + "\n" + second + "\n" + third;
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /ParagraphRun << /RunArray [ " + paragraph_properties(24) + " " + paragraph_properties(24) +
      " " + paragraph_properties(0) + " " + paragraph_properties(0) + " " + paragraph_properties(0) +
      " ] /RunLengthArray [ " + std::to_string(first_length) + ' ' + std::to_string(second_length) + ' ' +
      std::to_string(third_engine_length) + " 1 1 ] >> /StyleRun << /RunArray [ << /StyleSheet << "
      "/StyleSheetData << /Font 0 /FontSize 28 /FauxBold false /FauxItalic false "
      "/AutoLeading true /Leading 86.4 "
      "/FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> >> >> >> ] /RunLengthArray [ " +
      std::to_string(raw_text.size()) + " ] >> /AntiAlias 3 /FontSet [ << /Name " + font_literal +
      " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == normalized_text);

  const auto& paragraph_runs = metadata.at(patchy::kLayerMetadataTextParagraphRuns);
  const auto third_start = first_length + second_length;
  const std::string expected_runs =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(third_start) + '\t' + std::to_string(third.size()) + "\tleft\t-24\t24\t0\t0\t0";
  CHECK(paragraph_runs == expected_runs);
  const auto& text_runs = metadata.at(patchy::kLayerMetadataTextRuns);
  // /AutoLeading true makes the recorded /Leading 86.4 informational (Photoshop recomputes
  // auto leading as 1.2 x size; COM-verified) -- the run serializes as v3 with "auto".
  CHECK(text_runs.find("v3\n") == 0);
  CHECK(text_runs.find("\tauto\t") != std::string::npos);
  CHECK(text_runs.find("\t86.4") == std::string::npos);

  auto regenerated = read;
  regenerated.layers().front().metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto regenerated_payload = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(regenerated), 0), "TySh");
  CHECK(regenerated_payload.has_value());
  const std::string regenerated_payload_text(regenerated_payload->begin(), regenerated_payload->end());
  CHECK(regenerated_payload_text.find("/AutoLeading true /Leading 33.600000") != std::string::npos);
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto end = line.find('\t', start);
    if (end == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  return fields;
}

void psd_text_engine_normal_style_sheet_supplies_missing_run_properties() {
  // Style runs omit every property equal to the document default (ResourceDict's normal style
  // sheet). The restaurant-menu bug: dish-name runs omitted /FontSize (they used the default
  // 12.0) and the parser fell back to the first /FontSize found anywhere in the engine data
  // (7.24564 from the description runs), so the names rendered ~40% too small.
  const std::string text = "Braised Leeks\rServe with fried rice\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto title_font_literal = engine_utf16be_literal("Campanile");
  const auto body_font_literal = engine_utf16be_literal("Candara-BoldItalic");
  const auto title_length = 14;  // "Braised Leeks\r"
  const auto body_length = static_cast<int>(text.size()) - title_length;
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /ParagraphRun << /RunArray [ "
      "<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties << /Justification 0 >> >> >> "
      "<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties << /Justification 0 >> >> >> "
      "] /RunLengthArray [ " +
      std::to_string(title_length) + ' ' + std::to_string(body_length) +
      " ] >> /StyleRun << /RunArray [ "
      "<< /StyleSheet << /StyleSheetData << /Font 0 /AutoLeading false /Leading 19.43333 "
      "/Tracking -20 >> >> >> "
      "<< /StyleSheet << /StyleSheetData << /Font 1 /FontSize 7.24564 /AutoLeading false "
      "/Leading 11.1 /Tracking -20 >> >> >> "
      "] /RunLengthArray [ " +
      std::to_string(title_length) + ' ' + std::to_string(body_length) +
      " ] >> /AntiAlias 3 >> /ResourceDict << /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0 "
      "/ParagraphSheetSet [ << /Name (Normal) /Properties << /Justification 0 /AutoLeading 1.2 "
      ">> >> ] /StyleSheetSet [ << /Name (Normal) /StyleSheetData << /Font 2 /FontSize 12.0 "
      "/AutoLeading true /Leading 0.0 /Tracking 0 /FauxBold false /FauxItalic false /FillColor "
      "<< /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> >> >> ] /FontSet [ << /Name " +
      title_font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> << /Name " + body_font_literal +
      " /Script 0 /FontType 1 /Synthetic 0 >> ] >> >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  // The title run inherits the normal sheet's FontSize 12; the body run keeps its own
  // fractional size. Fixed leading and tracking ride the v3 runs.
  const auto& text_runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(text_runs.find("v3\n") == 0);
  const auto lines = split_lines(text_runs);
  CHECK(lines.size() == 3);
  if (lines.size() == 3) {
    const auto title_fields = split_tabs(lines[1]);
    CHECK(title_fields.size() == 11);
    if (title_fields.size() == 11) {
      CHECK(std::abs(std::stod(title_fields[2]) - 12.0) < 0.0001);
      CHECK(std::abs(std::stod(title_fields[7]) - 19.43333) < 0.0001);
      CHECK(std::abs(std::stod(title_fields[8]) - (-20.0)) < 0.0001);
      // No character-panel glyph scales in this data: the v3 columns carry the 1.0 defaults.
      CHECK(std::abs(std::stod(title_fields[9]) - 1.0) < 0.0001);
      CHECK(std::abs(std::stod(title_fields[10]) - 1.0) < 0.0001);
    }
    const auto body_fields = split_tabs(lines[2]);
    CHECK(body_fields.size() == 11);
    if (body_fields.size() == 11) {
      CHECK(std::abs(std::stod(body_fields[2]) - 7.24564) < 0.0001);
      CHECK(std::abs(std::stod(body_fields[7]) - 11.1) < 0.0001);
    }
  }
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "12");
  // Photoshop-authored type layers opt into the Photoshop leading model at render time.
  CHECK(metadata.at(patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
}

void psd_text_engine_auto_leading_run_serializes_auto_marker() {
  // AutoLeading true makes the recorded /Leading informational: the run must serialize as
  // "auto" so later size edits keep tracking Photoshop's 1.2 x size rule (COM-verified).
  const std::string text = "HHHH\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal("ArialMT");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 24.0 "
      "/AutoLeading true /Leading 28.8 /Tracking 0 /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 "
      "0.0 ] >> >> >> >> ] /RunLengthArray [ 5 ] >> /AntiAlias 3 >> /ResourceDict << "
      "/TheNormalStyleSheet 0 /TheNormalParagraphSheet 0 /ParagraphSheetSet [ << /Name (Normal) "
      "/Properties << /AutoLeading 1.2 >> >> ] /StyleSheetSet [ << /Name (Normal) "
      "/StyleSheetData << /Font 0 /FontSize 12.0 /AutoLeading true >> >> ] /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >> >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());
  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& text_runs = read.layers().front().metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(text_runs.find("v3\n") == 0);
  CHECK(text_runs.find("\tauto\t") != std::string::npos);
}

void psd_restaurant_menu_dishes_runs_parse_photoshop_layout_if_available() {
  // Ground truth for the whole feature: the CMYK restaurant menu whose converted text rendered
  // too small with collapsed line spacing. The 'Dishes' layer alternates default-size (12.0)
  // Campanile names with 7.24564 Candara-BoldItalic descriptions, fixed leadings 19.43333/11.1,
  // tracking -20, under a TySh transform with yy ~4.479144 (so effective sizes are ~54/32 px).
  const auto path = patchy::test::local_psd_fixture_path("restaurant-menu-inside.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  const auto read = patchy::psd::DocumentIo::read_file(path);
  const patchy::Layer* dishes = nullptr;
  std::function<void(const std::vector<patchy::Layer>&)> find_dishes =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (dishes == nullptr) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find("Braised Leeks") != std::string::npos) {
              dishes = &layer;
            }
          }
          find_dishes(layer.children());
        }
      };
  find_dishes(read.layers());
  CHECK(dishes != nullptr);
  if (dishes == nullptr) {
    return;
  }
  const auto& metadata = dishes->metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
  const auto transform = patchy::parse_layer_affine_transform(
      metadata.at(patchy::kLayerMetadataPsdTextTransform));
  CHECK(transform.has_value());
  if (transform.has_value()) {
    CHECK(std::abs((*transform)[3] - 4.479144) < 0.001);
    CHECK(std::abs((*transform)[0] - 5.863392) < 0.001);
  }
  const auto& text_runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(text_runs.find("v3\n") == 0);
  const auto lines = split_lines(text_runs);
  CHECK(lines.size() >= 3);
  bool found_title_run = false;
  bool found_body_run = false;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const auto fields = split_tabs(lines[i]);
    if (fields.size() < 9) {
      continue;
    }
    const auto size = std::stod(fields[2]);
    const auto tracking = std::stod(fields[8]);
    CHECK(std::abs(tracking - (-20.0)) < 0.0001);
    if (std::abs(size - 12.0) < 0.0001) {
      found_title_run = true;
      CHECK(std::abs(std::stod(fields[7]) - 19.43333) < 0.001);
    } else if (std::abs(size - 7.24564) < 0.0001) {
      found_body_run = true;
      CHECK(std::abs(std::stod(fields[7]) - 11.1) < 0.001);
    }
  }
  CHECK(found_title_run);
  CHECK(found_body_run);
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "12");
}

void psd_restaurant_menu_dishes_leading_survives_save_round_trip_if_available() {
  // A converted (patchy_raster) layer regenerates its TySh on save; the fixed leadings,
  // tracking, and default-inherited sizes must survive Patchy's own write -> read cycle so a
  // converted document reopens with the same layout (and Photoshop honors the fixed leading,
  // which requires /AutoLeading false in the regenerated engine data).
  const auto path = patchy::test::local_psd_fixture_path("restaurant-menu-inside.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::Layer* dishes = nullptr;
  std::function<void(std::vector<patchy::Layer>&)> find_dishes = [&](std::vector<patchy::Layer>& layers) {
    for (auto& layer : layers) {
      if (dishes == nullptr) {
        if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
            it != layer.metadata().end() && it->second.find("Braised Leeks") != std::string::npos) {
          dishes = &layer;
        }
      }
      find_dishes(layer.children());
    }
  };
  find_dishes(document.layers());
  CHECK(dishes != nullptr);
  if (dishes == nullptr) {
    return;
  }
  // Simulate the converted state: commit re-renders and flips the raster status, which makes
  // the writer regenerate the type block from the runs instead of preserving the template.
  dishes->metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string written_text(written.begin(), written.end());
  CHECK(written_text.find("/AutoLeading false /Leading 19.433330") != std::string::npos);
  CHECK(written_text.find("/AutoLeading false /Leading 11.100000") != std::string::npos);
  CHECK(written_text.find("/Tracking -20 ") != std::string::npos);

  const auto reread = patchy::psd::DocumentIo::read(written);
  const patchy::Layer* reread_dishes = nullptr;
  std::function<void(const std::vector<patchy::Layer>&)> find_reread =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (reread_dishes == nullptr) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find("Braised Leeks") != std::string::npos) {
              reread_dishes = &layer;
            }
          }
          find_reread(layer.children());
        }
      };
  find_reread(reread.layers());
  CHECK(reread_dishes != nullptr);
  if (reread_dishes == nullptr) {
    return;
  }
  const auto& runs = reread_dishes->metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(runs.find("v3\n") == 0);
  // The regenerated TySh is Patchy-signed, but its fixed leading/tracking prove Photoshop
  // provenance: the layout marker must survive the reopen or the leading model switches off.
  CHECK(reread_dishes->metadata().at(patchy::kLayerMetadataTextLayoutMode) ==
        patchy::kTextLayoutModePhotoshop);
  bool reread_title = false;
  bool reread_body = false;
  for (const auto& line : split_lines(runs)) {
    const auto fields = split_tabs(line);
    if (fields.size() < 9) {
      continue;
    }
    const auto size = std::stod(fields[2]);
    if (std::abs(size - 12.0) < 0.01 && std::abs(std::stod(fields[7]) - 19.43333) < 0.001) {
      reread_title = true;
    }
    if (std::abs(size - 7.24564) < 0.01 && std::abs(std::stod(fields[7]) - 11.1) < 0.001) {
      reread_body = true;
    }
    CHECK(std::abs(std::stod(fields[8]) - (-20.0)) < 0.001);
  }
  CHECK(reread_title);
  CHECK(reread_body);
}

void psd_text_engine_data_humanizes_postscript_font_family_names() {
  const std::string text = "Metal Slug 3\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal("NotoNaskhArabic-Bold");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 113 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 13 ] >> /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Noto Naskh Arabic");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t12\t113\t1\t0\t#000000\tNoto%20Naskh%20Arabic") !=
        std::string::npos);
}

void psd_text_engine_data_resolves_hyphenated_font_family_names() {
  const std::string text = "Continue\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal("FZ-SCRIPT25");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 72 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 9 ] >> /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "FZ SCRIPT 25");
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t8\t72\t0\t0\t#000000\tFZ%20SCRIPT%2025") !=
        std::string::npos);
}

void psd_writer_emits_photoshop_text_line_breaks() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Lines", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{12, 18, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "Line One\nLine Two";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t17\t20\t1\t0\t#000000\tVerdana";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t17\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Verdana";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "20";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const auto photoshop_text = utf16be_test_bytes("Line One\rLine Two\r");
  const auto patchy_text = utf16be_test_bytes("Line One\nLine Two");
  CHECK(std::search(text_payload->begin(), text_payload->end(), photoshop_text.begin(), photoshop_text.end()) !=
        text_payload->end());
  CHECK(std::search(text_payload->begin(), text_payload->end(), patchy_text.begin(), patchy_text.end()) ==
        text_payload->end());
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/StyleRun") != std::string::npos);
  CHECK(payload_text.find("/ParagraphRun") != std::string::npos);
  CHECK(payload_text.find("/FontSet") != std::string::npos);
  CHECK(payload_text.find("/AntiAlias 3") != std::string::npos);
  CHECK(payload_text.find("/DocumentResources") != std::string::npos);
  const auto split_run_lengths = payload_text.find("/RunLengthArray [ 9 9 ]");
  CHECK(split_run_lengths != std::string::npos);
  CHECK(payload_text.find("/RunLengthArray [ 9 9 ]", split_run_lengths + 1U) != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers().back().metadata().at(patchy::kLayerMetadataText) == "Line One\nLine Two");
}

void psd_writer_emits_v2_paragraph_layout() {
  const std::string first = "Speed Mode - Hold down TAB";
  const std::string second = "faster. Good for skipping boring stuff.";
  const std::string text = first + "\n" + second;
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size());

  patchy::Document document(360, 180, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 180, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Layout", solid_rgba(260, 120, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 260, 120});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t0";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "120";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/FirstLineIndent -24") != std::string::npos);
  CHECK(payload_text.find("/StartIndent 24") != std::string::npos);
  CHECK(payload_text.find("/SpaceAfter 24") != std::string::npos);
  CHECK(payload_text.find("/Hanging true") != std::string::npos);
  CHECK(payload_text.find("/AutoLeading true /Leading 33.600000") != std::string::npos);
  CHECK(payload_text.find("/RunLengthArray [ " + std::to_string(first_length) + ' ' +
                          std::to_string(second_length + 1) + " ]") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& paragraph_runs = read.layers().back().metadata().at(patchy::kLayerMetadataTextParagraphRuns);
  CHECK(paragraph_runs.find("v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24") == 0);
}

void psd_reader_regenerates_patchy_generated_type_blocks_after_reopen() {
  const std::string first = "Speed Mode - Hold down TAB";
  const std::string second = "faster. Good for skipping boring stuff.";
  const std::string text = first + "\n" + second;
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size());

  patchy::Document document(360, 180, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 180, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Layout", solid_rgba(260, 120, 32, 32, 32, 255));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 260, 120});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t0";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "120";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  auto stale_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string leading_marker = "/AutoLeading true /Leading 33.600000";
  const std::string blank_leading(leading_marker.size(), ' ');
  CHECK(replace_all_ascii_same_length(stale_bytes, leading_marker, blank_leading) > 0);

  const auto stale_payload = psd_layer_block_payload(psd_layer_extra_data(stale_bytes, 1), "TySh");
  CHECK(stale_payload.has_value());
  const std::string stale_payload_text(stale_payload->begin(), stale_payload->end());
  CHECK(stale_payload_text.find(leading_marker) == std::string::npos);
  CHECK(stale_payload_text.find("/KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(stale_bytes);
  CHECK(read.layers().size() == 2);
  const auto& read_layer = read.layers().back();
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");

  const auto regenerated_bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto regenerated_payload = psd_layer_block_payload(psd_layer_extra_data(regenerated_bytes, 1), "TySh");
  CHECK(regenerated_payload.has_value());
  const std::string regenerated_payload_text(regenerated_payload->begin(), regenerated_payload->end());
  CHECK(regenerated_payload_text.find(leading_marker) != std::string::npos);
  CHECK(regenerated_payload_text.find("/SpaceAfter 24") != std::string::npos);

  const auto read_again = patchy::psd::DocumentIo::read(regenerated_bytes);
  CHECK(read_again.layers().size() == 2);
  const auto& read_again_layer = read_again.layers().back();
  CHECK(read_again_layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(read_again_layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  const auto regenerated_again_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read_again), 1), "TySh");
  CHECK(regenerated_again_payload.has_value());
  const std::string regenerated_again_payload_text(regenerated_again_payload->begin(), regenerated_again_payload->end());
  CHECK(regenerated_again_payload_text.find(leading_marker) != std::string::npos);
}

void psd_horror_virtualboy_imports_multiline_bold_text_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Horror VirtualBoy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  bool found_body_text = false;
  std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (const auto text = layer.metadata().find(patchy::kLayerMetadataText);
          text != layer.metadata().end() && text->second.find("Did you know") != std::string::npos) {
        found_body_text = true;
        CHECK(text->second.find('\n') != std::string::npos);
        CHECK(text->second.find('\r') == std::string::npos);
        CHECK(text->second.find('\x03') == std::string::npos);
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextFont) == "Verdana");
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextBold) == "true");
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextAntiAlias) == "3");
      }
      visit(layer.children());
    }
  };
  visit(document.layers());
  CHECK(found_body_text);
}

void psd_arduboy_real_file_renders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 2550);
  CHECK(document.height() == 3300);
  CHECK(document.layers().size() == 4);

  int text_layers = 0;
  for (const auto& layer : document.layers()) {
    if (layer.metadata().contains(patchy::kLayerMetadataText)) {
      ++text_layers;
    }
  }
  CHECK(text_layers >= 2);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::size_t non_white_pixels = 0;
  for (std::int32_t y = 0; y < flattened.height(); y += 12) {
    for (std::int32_t x = 0; x < flattened.width(); x += 12) {
      const auto* px = flattened.pixel(x, y);
      if (px[0] < 245 || px[1] < 245 || px[2] < 245) {
        ++non_white_pixels;
      }
    }
  }
  CHECK(non_white_pixels > 1000);
}

void psd_title_screen_demo_layer_styles_render_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Title Screen_demo.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 640);
  CHECK(document.height() == 480);

  int styled_layers = 0;
  int gradient_layers = 0;
  int shadow_layers = 0;
  int outer_glow_layers = 0;
  int bevel_layers = 0;
  for (const auto& layer : document.layers()) {
    if (!layer.layer_style().empty()) {
      ++styled_layers;
    }
    if (!layer.layer_style().gradient_fills.empty()) {
      ++gradient_layers;
    }
    if (!layer.layer_style().drop_shadows.empty()) {
      ++shadow_layers;
    }
    if (!layer.layer_style().outer_glows.empty()) {
      ++outer_glow_layers;
    }
    if (!layer.layer_style().bevels.empty()) {
      ++bevel_layers;
    }
  }
  CHECK(styled_layers >= 10);
  CHECK(gradient_layers >= 5);
  CHECK(shadow_layers >= 5);
  CHECK(outer_glow_layers >= 1);
  CHECK(bevel_layers >= 1);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::size_t visible_samples = 0;
  for (std::int32_t y = 0; y < flattened.height(); y += 16) {
    for (std::int32_t x = 0; x < flattened.width(); x += 16) {
      const auto* px = flattened.pixel(x, y);
      if (px[0] != 0 || px[1] != 0 || px[2] != 0) {
        ++visible_samples;
      }
    }
  }
  CHECK(visible_samples > 100);
  write_bmp_artifact("psd_title_screen_demo_layer_styles", document);
}

void psd_duke_nukem_mobile_text_style_renders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 2480);
  CHECK(document.height() == 3508);

  bool found_large_text_style = false;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        visit(layer.children());
        continue;
      }
      const auto& style = layer.layer_style();
      if (!patchy::layer_is_text(layer) || style.drop_shadows.empty() || style.outer_glows.empty()) {
        continue;
      }
      const auto has_large_shadow =
          std::any_of(style.drop_shadows.begin(), style.drop_shadows.end(), [](const patchy::LayerDropShadow& shadow) {
            return shadow.enabled && shadow.size >= 200.0F && shadow.spread >= 70.0F;
          });
      const auto has_large_glow =
          std::any_of(style.outer_glows.begin(), style.outer_glows.end(), [](const patchy::LayerOuterGlow& glow) {
            return glow.enabled && glow.size >= 150.0F && glow.spread >= 99.0F;
          });
      if (has_large_shadow && has_large_glow) {
        found_large_text_style = true;
        // The stored raster is a clean text-only matte, so the import keeps
        // Photoshop's own pixels (substitution waits for an edit, like Photoshop)
        // instead of regenerating a substituted-font preview.
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");
        int tall_alpha_columns = 0;
        for (std::int32_t x = 0; x < layer.pixels().width(); ++x) {
          int column_alpha = 0;
          for (std::int32_t y = 0; y < layer.pixels().height(); ++y) {
            if (layer.pixels().pixel(x, y)[3] > 0U) {
              ++column_alpha;
            }
          }
          if (column_alpha * 4 > layer.pixels().height() * 3) {
            ++tall_alpha_columns;
          }
        }
        CHECK(tall_alpha_columns == 0);
      }
    }
  };
  visit(document.layers());
  CHECK(found_large_text_style);

  write_bmp_artifact("psd_duke_nukem_mobile_text_style", document);
}

void psd_polymega_text_keeps_photoshop_raster_if_available() {
  // Reported repro (July 2026): the file's text uses Carter One (not installed here)
  // with a big live drop shadow, and the import used to regenerate the preview with a
  // substituted font before any edit. The stored raster is the clean black text matte
  // (the white fill comes from a Color Overlay effect), so the import must keep
  // Photoshop's pixels and leave substitution to an actual edit.
  const auto path = patchy::test::local_psd_fixture_path("Polymega jump test.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const patchy::Layer* text_layer = nullptr;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        visit(layer.children());
        continue;
      }
      const auto text = layer.metadata().find(patchy::kLayerMetadataText);
      if (text != layer.metadata().end() && text->second.find("Tapping jump") != std::string::npos) {
        text_layer = &layer;
      }
    }
  };
  visit(document.layers());
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }

  // The gate this pins: a big enabled drop shadow used to force regeneration.
  const auto& shadows = text_layer->layer_style().drop_shadows;
  CHECK(!shadows.empty());
  CHECK(text_layer->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");

  // The kept matte is Photoshop's black text fill; a regenerated preview would be a
  // substituted-font render with a different ink footprint.
  const auto& pixels = text_layer->pixels();
  std::uint64_t visible = 0;
  std::uint64_t dark = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      if (px[3] <= 16U) {
        continue;
      }
      ++visible;
      if (px[0] <= 32U && px[1] <= 32U && px[2] <= 32U) {
        ++dark;
      }
    }
  }
  const auto area = static_cast<std::uint64_t>(pixels.width()) * static_cast<std::uint64_t>(pixels.height());
  CHECK(visible > area / 20U);
  CHECK(visible < area / 2U);
  CHECK(dark * 10U > visible * 9U);
}

patchy::Layer& add_vertical_text_layer_for_writer(patchy::Document& document, const patchy::Rect& bounds,
                                                  const std::string& paragraph_runs) {
  patchy::Layer text_layer(document.allocate_layer_id(), "Hello", solid_rgba(bounds.width, bounds.height, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(bounds);
  layer.metadata()[patchy::kLayerMetadataText] = "Hello";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t5\t32\t0\t0\t#000000\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = paragraph_runs;
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  return layer;
}

// Vertical type: the TySh says Ornt = Vrtc, the engine data carries Photoshop's vertical
// writing direction, and the transform translation is the first column's top centre
// recovered from the raster rect (cells plus the fixed bleed on every side).
void psd_writer_exports_vertical_type_block_and_reads_it_back() {
  patchy::Document document(240, 240, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 240, 255, 255, 255));
  // 32 px "Hello" stacked: one em wide plus 8 px bleed each side, five cells plus bleed tall.
  auto& layer = add_vertical_text_layer_for_writer(document, patchy::Rect{100, 40, 48, 176}, "v1\n0\t5\tleft");
  layer.metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("Vrtc") != std::string::npos);
  CHECK(payload_text.find("/WritingDirection 2") != std::string::npos);
  CHECK(payload_text.find("/WritingDirection 0") == std::string::npos);
  CHECK(payload_text.find("/Procession 1") != std::string::npos);
  CHECK(payload_text.find("/ParagraphDirection") == std::string::npos);
  // tx = left + width - bleed - em/2 = 100 + 48 - 8 - 16; ty = top + bleed (top-aligned).
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 124.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 48.0) < 0.000001);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& reopened = read.layers().back();
  CHECK(reopened.metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
  const auto transform = patchy::parse_layer_affine_transform(reopened.metadata().at(patchy::kLayerMetadataPsdTextTransform));
  CHECK(transform.has_value());
  if (transform.has_value()) {
    CHECK(std::abs((*transform)[4] - 124.0) < 0.000001);
    CHECK(std::abs((*transform)[5] - 48.0) < 0.000001);
  }

  // Bottom-aligned vertical text anchors at the run's bottom instead.
  patchy::Document bottom_document(240, 240, patchy::PixelFormat::rgb8());
  bottom_document.add_pixel_layer("Background", solid_rgb(240, 240, 255, 255, 255));
  auto& bottom = add_vertical_text_layer_for_writer(bottom_document, patchy::Rect{100, 40, 48, 176}, "v1\n0\t5\tright");
  bottom.metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;
  const auto bottom_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(bottom_document), 1), "TySh");
  CHECK(bottom_payload.has_value());
  if (bottom_payload.has_value()) {
    CHECK(std::abs(read_f64_be_at(*bottom_payload, 42U) - (40.0 + 176.0 - 8.0)) < 0.000001);
  }

  // A horizontal layer keeps the historical block byte for byte: Hrzn, direction 0, no
  // procession change.
  patchy::Document horizontal_document(240, 240, patchy::PixelFormat::rgb8());
  horizontal_document.add_pixel_layer("Background", solid_rgb(240, 240, 255, 255, 255));
  add_vertical_text_layer_for_writer(horizontal_document, patchy::Rect{100, 40, 96, 40}, "v1\n0\t5\tleft");
  const auto horizontal_payload = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(horizontal_document), 1), "TySh");
  CHECK(horizontal_payload.has_value());
  if (horizontal_payload.has_value()) {
    const std::string horizontal_text(horizontal_payload->begin(), horizontal_payload->end());
    CHECK(horizontal_text.find("Hrzn") != std::string::npos);
    CHECK(horizontal_text.find("/WritingDirection 0") != std::string::npos);
    CHECK(horizontal_text.find("/Procession 0") != std::string::npos);
  }
}

// Paragraph direction rides the v4 paragraph-runs column and the Middle Eastern composer's
// /ParagraphDirection key; auto paragraphs stay on the older formats and write no key.
void psd_paragraph_direction_round_trips_as_v4_column() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  add_vertical_text_layer_for_writer(document, patchy::Rect{20, 20, 96, 40}, "v4\n0\t5\tleft\t0\t0\t0\t0\t0\t1.2\trtl");

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/ParagraphDirection 1") != std::string::npos);
  CHECK(payload_text.find("Hrzn") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& runs = read.layers().back().metadata().at(patchy::kLayerMetadataTextParagraphRuns);
  CHECK(runs.rfind("v4\n", 0) == 0);
  CHECK(runs.find("\trtl") != std::string::npos);

  // Explicit left-to-right writes the key with 0; auto writes nothing.
  patchy::Document ltr_document(240, 120, patchy::PixelFormat::rgb8());
  ltr_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  add_vertical_text_layer_for_writer(ltr_document, patchy::Rect{20, 20, 96, 40}, "v4\n0\t5\tleft\t0\t0\t0\t0\t0\t1.2\tltr");
  const auto ltr_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(ltr_document), 1), "TySh");
  CHECK(ltr_payload.has_value());
  if (ltr_payload.has_value()) {
    const std::string ltr_text(ltr_payload->begin(), ltr_payload->end());
    CHECK(ltr_text.find("/ParagraphDirection 0") != std::string::npos);
  }
  patchy::Document auto_document(240, 120, patchy::PixelFormat::rgb8());
  auto_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  add_vertical_text_layer_for_writer(auto_document, patchy::Rect{20, 20, 96, 40}, "v1\n0\t5\tleft");
  const auto auto_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(auto_document), 1), "TySh");
  CHECK(auto_payload.has_value());
  if (auto_payload.has_value()) {
    const std::string auto_text(auto_payload->begin(), auto_payload->end());
    CHECK(auto_text.find("/ParagraphDirection") == std::string::npos);
    const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(auto_document));
    CHECK(reread.layers().back().metadata().at(patchy::kLayerMetadataTextParagraphRuns).rfind("v1\n", 0) == 0);
  }
}

const patchy::Layer* committed_fixture_text_layer(const patchy::Document& document) {
  const patchy::Layer* text_layer = nullptr;
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      text_layer = &layer;
    }
  }
  return text_layer;
}

// A Photoshop 2026 vertical type layer (photoshop-text-vertical-point.psd, the ps2026_vtext
// capture vt_point_ja_multi): Ornt = Vrtc imports as the orientation key with Photoshop's anchor
// (the click point) as the transform, and an unedited resave keeps the block byte for byte.
void psd_vertical_capture_imports_orientation() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-text-vertical-point.psd"));
  const auto* text_layer = committed_fixture_text_layer(document);
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }
  CHECK(text_layer->metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
  const auto transform =
      patchy::parse_layer_affine_transform(text_layer->metadata().at(patchy::kLayerMetadataPsdTextTransform));
  CHECK(transform.has_value());
  if (transform.has_value()) {
    CHECK(std::abs((*transform)[4] - 250.0) < 0.001);
    CHECK(std::abs((*transform)[5] - 40.0) < 0.001);
  }
  CHECK(text_layer->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string written(bytes.begin(), bytes.end());
  CHECK(written.find("Vrtc") != std::string::npos);
  CHECK(written.find("/WritingDirection 2") != std::string::npos);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  bool found = false;
  for (const auto& layer : reread.layers()) {
    if (patchy::layer_is_text(layer)) {
      found = true;
      CHECK(layer.metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
    }
  }
  CHECK(found);
}

// The other committed captures: a vertical box layer keeps its 150x250 frame, a run Photoshop
// captured with "Standard Vertical Roman Alignment" (/BaselineDirection 2) reads back as the
// runs v7 rotated flag, and a Middle Eastern engine Hebrew layer imports horizontal (Photoshop
// keeps paragraph direction outside TySh, so Patchy's direction stays auto and bidi decides).
void psd_vertical_box_rotated_roman_and_hebrew_captures_import() {
  {
    auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path("photoshop-text-vertical-box.psd"));
    const auto* layer = committed_fixture_text_layer(document);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      CHECK(layer->metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
      CHECK(layer->metadata().count(patchy::kLayerMetadataTextBoxWidth) == 1);
      CHECK(layer->metadata().count(patchy::kLayerMetadataTextBoxHeight) == 1);
      CHECK(std::abs(std::stod(layer->metadata().at(patchy::kLayerMetadataTextBoxWidth)) - 150.0) < 1.0);
      CHECK(std::abs(std::stod(layer->metadata().at(patchy::kLayerMetadataTextBoxHeight)) - 250.0) < 1.0);
    }
  }
  {
    auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path("photoshop-text-vertical-rotated-roman.psd"));
    const auto* layer = committed_fixture_text_layer(document);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      CHECK(layer->metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
      const auto& runs = layer->metadata().at(patchy::kLayerMetadataTextRuns);
      CHECK(runs.rfind("v7\n", 0) == 0);
      CHECK(runs.find("\t2\n") != std::string::npos || runs.substr(runs.size() - 2) == "\t2");
      const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
      const std::string written(bytes.begin(), bytes.end());
      CHECK(written.find("/BaselineDirection 2") != std::string::npos);
    }
  }
  {
    auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path("photoshop-text-rtl-hebrew.psd"));
    const auto* layer = committed_fixture_text_layer(document);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      CHECK(layer->metadata().count(patchy::kLayerMetadataTextOrientation) == 0);
      // "shalom olam" in Hebrew, UTF-8.
      CHECK(layer->metadata().at(patchy::kLayerMetadataTextHtml).find("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d") !=
            std::string::npos);
      const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
      const std::string written(bytes.begin(), bytes.end());
      CHECK(written.find("Hrzn") != std::string::npos);
      CHECK(written.find("Vrtc") == std::string::npos);
    }
  }
}

// Photoshop's type engine fails to re-lay out a run whose /Tracking is a negative float
// ("result would be too big", September 2026 bisect); tracking is written as an integer.
void psd_writer_writes_tracking_as_an_integer() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  auto& layer = add_vertical_text_layer_for_writer(document, patchy::Rect{20, 20, 96, 40}, "v1\n0\t5\tleft");
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v3\n0\t5\t32\t0\t0\t#000000\tArial\tauto\t-305\t1\t1";
  layer.metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  if (!text_payload.has_value()) {
    return;
  }
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/Tracking -305 ") != std::string::npos);
  CHECK(payload_text.find("/Tracking -305.") == std::string::npos);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().back().metadata().at(patchy::kLayerMetadataTextRuns).find("\t-305\t") != std::string::npos);
}

// The user file that failed in Photoshop (vertical box text, tracking -305): a Patchy re-save
// writes the integer tracking. The artifact is kept for the Photoshop re-render check by COM.
void psd_vertical_tracking_bug_file_resaves_with_integer_tracking_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("vtext-bugs/vertical_text_test_from_patchy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string written(bytes.begin(), bytes.end());
  CHECK(written.find("/Tracking -305 ") != std::string::npos);
  CHECK(written.find("/Tracking -305.") == std::string::npos);
  CHECK(written.find("Vrtc") != std::string::npos);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream out("test-artifacts/vertical_tracking_check.psd", std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Vertical type writes Photoshop's /BaselineDirection: 1 (upright Roman, PS's own default)
// unless the run carries the rotated flag (runs v7 column 14 = 2); horizontal type writes no
// key. A run without the key re-laid out ROTATED in Photoshop (September 2026).
void psd_writer_writes_baseline_direction_for_vertical_type() {
  const auto payload_for = [](const std::string& runs, bool vertical) {
    patchy::Document document(240, 240, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(240, 240, 255, 255, 255));
    auto& layer = add_vertical_text_layer_for_writer(document, patchy::Rect{100, 40, 48, 176}, "v1\n0\t5\tleft");
    layer.metadata()[patchy::kLayerMetadataTextRuns] = runs;
    if (vertical) {
      layer.metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;
    }
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
    CHECK(payload.has_value());
    return std::pair(payload.has_value() ? std::string(payload->begin(), payload->end()) : std::string(), bytes);
  };
  const auto [upright, upright_bytes] = payload_for("v1\n0\t5\t32\t0\t0\t#000000\tArial", true);
  CHECK(upright.find("/BaselineDirection 1") != std::string::npos);
  CHECK(upright.find("/BaselineDirection 2") == std::string::npos);
  // v7: ... faux-bold style faux-italic rotated
  const auto [rotated, rotated_bytes] =
      payload_for("v7\n0\t5\t32\t0\t0\t#000000\tArial\tauto\t0\t1\t1\t0\t\t0\t2", true);
  CHECK(rotated.find("/BaselineDirection 2") != std::string::npos);
  CHECK(rotated.find("/BaselineDirection 1") == std::string::npos);
  const auto [horizontal, horizontal_bytes] = payload_for("v1\n0\t5\t32\t0\t0\t#000000\tArial", false);
  CHECK(horizontal.find("/BaselineDirection") == std::string::npos);
  // Read back: the rotated run keeps its v7 column, the upright one stays on the older format.
  const auto reread = patchy::psd::DocumentIo::read(rotated_bytes);
  const auto& runs = reread.layers().back().metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(runs.rfind("v7\n", 0) == 0);
  CHECK(runs.find("\t2") != std::string::npos);
  const auto reread_upright = patchy::psd::DocumentIo::read(upright_bytes);
  CHECK(reread_upright.layers().back().metadata().at(patchy::kLayerMetadataTextRuns).rfind("v7\n", 0) != 0);
}

// photoshop-text-anchor-{whole,half}.psd: Photoshop 27.9 point text "Hg" (Arial 48 px, Sharp)
// placed at x 100.0 and x 100.5. The TySh keeps the fractional anchor, and Photoshop's own
// raster sits at the ROUNDED anchor: the half layer's record rect starts one column later
// (its glyph rows are otherwise byte-identical to the whole layer's; docs/text-render-calibration.md).
void psd_text_anchor_captures_keep_fractional_transform() {
  const auto text_layer_of = [](const patchy::Document& document) -> const patchy::Layer* {
    for (const auto& layer : document.layers()) {
      if (layer.metadata().contains(patchy::kLayerMetadataPsdTextTransform)) {
        return &layer;
      }
    }
    return nullptr;
  };
  const auto whole =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-text-anchor-whole.psd"));
  const auto half =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-text-anchor-half.psd"));
  const auto* whole_layer = text_layer_of(whole);
  const auto* half_layer = text_layer_of(half);
  CHECK(whole_layer != nullptr);
  CHECK(half_layer != nullptr);
  if (whole_layer == nullptr || half_layer == nullptr) {
    return;
  }
  const auto whole_transform =
      patchy::parse_layer_affine_transform(whole_layer->metadata().at(patchy::kLayerMetadataPsdTextTransform));
  const auto half_transform =
      patchy::parse_layer_affine_transform(half_layer->metadata().at(patchy::kLayerMetadataPsdTextTransform));
  CHECK(whole_transform.has_value());
  CHECK(half_transform.has_value());
  if (!whole_transform.has_value() || !half_transform.has_value()) {
    return;
  }
  // Photoshop's own file holds 99.99999999999999 for the "100" anchor (its position setter
  // works in percent of the document), which still rounds to the 100 the raster sits at.
  CHECK(std::abs((*whole_transform)[4] - 100.0) < 1e-9);
  CHECK(std::abs((*whole_transform)[5] - 100.0) < 1e-9);
  CHECK(std::abs((*half_transform)[4] - 100.5) < 1e-9);
  CHECK(std::abs((*half_transform)[5] - 100.0) < 1e-9);
  CHECK(whole_layer->bounds().x == 103);
  CHECK(whole_layer->bounds().y == 65);
  CHECK(half_layer->bounds().x == 104);
  CHECK(half_layer->bounds().y == 65);
}

}  // namespace

std::vector<patchy::test::TestCase> psd_text_tests() {
  return {
      {"psd_text_anchor_captures_keep_fractional_transform", psd_text_anchor_captures_keep_fractional_transform},
      {"psd_writer_box_text_baseline_inset_moves_box_bounds", psd_writer_box_text_baseline_inset_moves_box_bounds},
      {"psd_writer_point_text_first_baseline_beats_ink_bottom", psd_writer_point_text_first_baseline_beats_ink_bottom},
      {"psd_writer_point_text_grown_raster_still_anchors_baseline",
       psd_writer_point_text_grown_raster_still_anchors_baseline},
      {"psd_writer_qt_natural_auto_leading_fraction", psd_writer_qt_natural_auto_leading_fraction},
      {"psd_writer_writes_baseline_direction_for_vertical_type", psd_writer_writes_baseline_direction_for_vertical_type},
      {"psd_writer_writes_tracking_as_an_integer", psd_writer_writes_tracking_as_an_integer},
      {"psd_vertical_tracking_bug_file_resaves_with_integer_tracking_if_available",
       psd_vertical_tracking_bug_file_resaves_with_integer_tracking_if_available},
      {"psd_writer_exports_vertical_type_block_and_reads_it_back", psd_writer_exports_vertical_type_block_and_reads_it_back},
      {"psd_paragraph_direction_round_trips_as_v4_column", psd_paragraph_direction_round_trips_as_v4_column},
      {"psd_vertical_capture_imports_orientation", psd_vertical_capture_imports_orientation},
      {"psd_vertical_box_rotated_roman_and_hebrew_captures_import", psd_vertical_box_rotated_roman_and_hebrew_captures_import},
      {"psd_import_regenerates_large_styled_text_preview_alpha",
       psd_import_regenerates_large_styled_text_preview_alpha},
      {"psd_import_keeps_clean_foreign_styled_text_raster",
       psd_import_keeps_clean_foreign_styled_text_raster},
      {"psd_writer_exports_patchy_rich_text_as_photoshop_type",
       psd_writer_exports_patchy_rich_text_as_photoshop_type},
      {"psd_writer_preserves_imported_photoshop_text_geometry",
       psd_writer_preserves_imported_photoshop_text_geometry},
      {"psd_writer_prefers_patchy_text_transform_over_imported_geometry",
       psd_writer_prefers_patchy_text_transform_over_imported_geometry},
      {"psd_writer_exports_point_text_with_photoshop_baseline_origin",
       psd_writer_exports_point_text_with_photoshop_baseline_origin},
      {"psd_writer_maps_text_raster_bounds_into_transform_local_space",
       psd_writer_maps_text_raster_bounds_into_transform_local_space},
      {"psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame",
       psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame},
      {"psd_writer_regenerates_same_length_patchy_text_without_stale_template",
       psd_writer_regenerates_same_length_patchy_text_without_stale_template},
      {"psd_writer_ignores_stale_type_template_after_patchy_text_edit",
       psd_writer_ignores_stale_type_template_after_patchy_text_edit},
      {"psd_extended_blend_modes_round_trip", psd_extended_blend_modes_round_trip},
      {"psd_text_layer_engine_data_renders_placeholder_text",
       psd_text_layer_engine_data_renders_placeholder_text},
      {"psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style",
       psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style},
      {"psd_text_faux_bold_stays_off_the_bold_face", psd_text_faux_bold_stays_off_the_bold_face},
      {"psd_text_faux_italic_stays_off_the_italic_face", psd_text_faux_italic_stays_off_the_italic_face},
      {"psd_text_flag_expressible_face_never_bakes_into_the_family",
       psd_text_flag_expressible_face_never_bakes_into_the_family},
      {"psd_text_recorded_style_resolves_the_exact_face", psd_text_recorded_style_resolves_the_exact_face},
      {"psd_text_gdi_family_wins_when_directwrite_renames_the_face",
       psd_text_gdi_family_wins_when_directwrite_renames_the_face},
      {"psd_text_heavy_legacy_face_keeps_the_gdi_family_if_available",
       psd_text_heavy_legacy_face_keeps_the_gdi_family_if_available},
      {"psd_text_regenerated_layer_gets_an_index_outside_the_text_engine_block",
       psd_text_regenerated_layer_gets_an_index_outside_the_text_engine_block},
      {"psd_text_balmoral_let_plain_never_bakes_a_synthesized_weight_if_available",
       psd_text_balmoral_let_plain_never_bakes_a_synthesized_weight_if_available},
      {"psd_text_engine_data_preserves_paragraph_layout_runs",
       psd_text_engine_data_preserves_paragraph_layout_runs},
      {"psd_text_engine_normal_style_sheet_supplies_missing_run_properties",
       psd_text_engine_normal_style_sheet_supplies_missing_run_properties},
      {"psd_text_engine_auto_leading_run_serializes_auto_marker",
       psd_text_engine_auto_leading_run_serializes_auto_marker},
      {"psd_restaurant_menu_dishes_runs_parse_photoshop_layout_if_available",
       psd_restaurant_menu_dishes_runs_parse_photoshop_layout_if_available},
      {"psd_restaurant_menu_dishes_leading_survives_save_round_trip_if_available",
       psd_restaurant_menu_dishes_leading_survives_save_round_trip_if_available},
      {"psd_text_engine_data_humanizes_postscript_font_family_names",
       psd_text_engine_data_humanizes_postscript_font_family_names},
      {"psd_text_engine_data_resolves_hyphenated_font_family_names",
       psd_text_engine_data_resolves_hyphenated_font_family_names},
      {"psd_writer_emits_photoshop_text_line_breaks",
       psd_writer_emits_photoshop_text_line_breaks},
      {"psd_writer_emits_v2_paragraph_layout",
       psd_writer_emits_v2_paragraph_layout},
      {"psd_reader_regenerates_patchy_generated_type_blocks_after_reopen",
       psd_reader_regenerates_patchy_generated_type_blocks_after_reopen},
      {"psd_horror_virtualboy_imports_multiline_bold_text_if_available",
       psd_horror_virtualboy_imports_multiline_bold_text_if_available},
      {"psd_arduboy_real_file_renders_if_available", psd_arduboy_real_file_renders_if_available},
      {"psd_title_screen_demo_layer_styles_render_if_available",
       psd_title_screen_demo_layer_styles_render_if_available},
      {"psd_duke_nukem_mobile_text_style_renders_if_available",
       psd_duke_nukem_mobile_text_style_renders_if_available},
      {"psd_polymega_text_keeps_photoshop_raster_if_available",
       psd_polymega_text_keeps_photoshop_raster_if_available},
  };
}
