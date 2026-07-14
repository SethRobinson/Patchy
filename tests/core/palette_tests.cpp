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
#include "test_groups.hpp"

namespace {

using patchy::test::active_tool_layer;
using patchy::test::make_bar_brush_tip;
using patchy::test::make_tool_document;
using patchy::test::solid_rgba;
using patchy::test::tool_options;

// ---- Palette mode core (core/palette.hpp, core/palette_presets.hpp, formats/palette_io.hpp) ----

void palette_lut_snaps_within_quantization_bound_and_is_idempotent() {
  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8 != nullptr);
  // The adversarial palette places two entries inside one 5-5-5 LUT bucket.
  const std::vector<patchy::RgbColor> adversarial = {{10, 10, 10}, {12, 10, 10}, {200, 200, 200}};

  const auto check_palette = [](std::span<const patchy::RgbColor> colors) {
    patchy::PaletteLut lut;
    lut.build(colors);
    CHECK(!lut.empty());
    CHECK(lut.colors().size() == colors.size());

    for (const auto& color : colors) {
      CHECK(lut.contains(color));
      const auto snapped = lut.snap(color.red, color.green, color.blue);
      CHECK(snapped.red == color.red);
      CHECK(snapped.green == color.green);
      CHECK(snapped.blue == color.blue);
    }

    const auto distance = [](patchy::RgbColor a, patchy::RgbColor b) {
      const auto dr = static_cast<double>(a.red) - b.red;
      const auto dg = static_cast<double>(a.green) - b.green;
      const auto db = static_cast<double>(a.blue) - b.blue;
      return std::sqrt(dr * dr + dg * dg + db * db);
    };
    for (int r = 0; r < 256; r += 15) {
      for (int g = 0; g < 256; g += 15) {
        for (int b = 0; b < 256; b += 15) {
          const auto color = patchy::RgbColor{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                              static_cast<std::uint8_t>(b)};
          const auto snapped = lut.snap(color.red, color.green, color.blue);
          bool in_palette = false;
          for (const auto& entry : colors) {
            in_palette = in_palette || (entry.red == snapped.red && entry.green == snapped.green &&
                                        entry.blue == snapped.blue);
          }
          CHECK(in_palette);
          const auto best =
              colors[patchy::nearest_palette_index(color, colors)];
          // The 15-bit table may pick a neighbor of the true nearest entry, but
          // never by more than the bucket-center bound (2 * sqrt(3 * 4^2)).
          CHECK(distance(color, snapped) <= distance(color, best) + 13.87);
        }
      }
    }
  };

  check_palette(pico8->colors);
  check_palette(adversarial);

  patchy::PaletteLut empty;
  CHECK(empty.empty());
  const auto passthrough = empty.snap(12, 34, 56);
  CHECK(passthrough.red == 12);
  CHECK(passthrough.green == 34);
  CHECK(passthrough.blue == 56);
}

void palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers() {
  patchy::PaletteLut lut;
  const std::vector<patchy::RgbColor> colors = {{0, 0, 0}, {255, 255, 255}};
  lut.build(colors);
  patchy::PaletteSnapContext context;
  context.lut = &lut;

  std::array<std::uint8_t, 4> rgba{200, 190, 180, 130};
  patchy::snap_pixel_to_palette(rgba.data(), 4, context);
  CHECK(rgba[0] == 255);
  CHECK(rgba[1] == 255);
  CHECK(rgba[2] == 255);
  CHECK(rgba[3] == 255);

  rgba = {30, 20, 10, 127};
  patchy::snap_pixel_to_palette(rgba.data(), 4, context);
  CHECK(rgba[0] == 0);
  CHECK(rgba[3] == 0);

  std::array<std::uint8_t, 3> rgb{200, 190, 180};
  patchy::snap_pixel_to_palette(rgb.data(), 3, context);
  CHECK(rgb[0] == 255);

  std::array<std::uint8_t, 1> gray{99};
  patchy::snap_pixel_to_palette(gray.data(), 1, context);
  CHECK(gray[0] == 99);

  std::array<std::uint8_t, 4> untouched{1, 2, 3, 4};
  patchy::snap_pixel_to_palette(untouched.data(), 4, patchy::PaletteSnapContext{});
  CHECK(untouched[0] == 1);
  CHECK(untouched[3] == 4);
}

void palette_remap_exact_color_is_lossless_and_bounded() {
  patchy::PixelBuffer pixels(8, 6, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 6; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 10;
      px[1] = 20;
      px[2] = 30;
      px[3] = static_cast<std::uint8_t>(40 + x);
    }
  }
  // A near-miss color one channel off must not be remapped.
  pixels.pixel(0, 0)[0] = 11;
  pixels.pixel(3, 2)[0] = 10;
  pixels.pixel(7, 5)[2] = 31;

  const auto dirty = patchy::remap_exact_color(pixels, patchy::RgbColor{10, 20, 30}, patchy::RgbColor{200, 100, 50});
  CHECK(!dirty.empty());
  CHECK(dirty.x == 0);
  CHECK(dirty.y == 0);
  CHECK(dirty.width == 8);
  CHECK(dirty.height == 6);
  CHECK(pixels.pixel(0, 0)[0] == 11);   // near miss untouched
  CHECK(pixels.pixel(7, 5)[2] == 31);   // near miss untouched
  CHECK(pixels.pixel(3, 2)[0] == 200);
  CHECK(pixels.pixel(3, 2)[1] == 100);
  CHECK(pixels.pixel(3, 2)[2] == 50);
  CHECK(pixels.pixel(3, 2)[3] == 43);   // alpha preserved

  CHECK(patchy::remap_exact_color(pixels, patchy::RgbColor{5, 5, 5}, patchy::RgbColor{6, 6, 6}).empty());
  CHECK(patchy::remap_exact_color(pixels, patchy::RgbColor{200, 100, 50}, patchy::RgbColor{200, 100, 50}).empty());
}

void palette_document_remap_skips_smart_object_preview_cache() {
  constexpr patchy::RgbColor kFrom{10, 20, 30};
  constexpr patchy::RgbColor kTo{200, 100, 50};
  patchy::Document document(1, 1, patchy::PixelFormat::rgba8());
  const auto ordinary_id =
      document.add_pixel_layer("Ordinary", solid_rgba(1, 1, 10, 20, 30, 123)).id();
  auto& smart_object = document.add_pixel_layer(
      "Smart Object preview", solid_rgba(1, 1, 10, 20, 30, 123));
  const auto smart_object_id = smart_object.id();
  patchy::SmartObjectPlacement placement;
  placement.uuid = "11111111-2222-3333-8444-555555555555";
  placement.transform = {0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0};
  placement.width = 1.0;
  placement.height = 1.0;
  patchy::set_layer_smart_object_metadata(
      smart_object, placement, "aaaaaaaa-bbbb-cccc-8ddd-eeeeeeeeeeee",
      "SoLd", {}, patchy::kSmartObjectRasterStatusPhotoshop);
  CHECK(patchy::layer_is_smart_object(std::as_const(smart_object)));

  patchy::remap_document_exact_color(document, kFrom, kTo);

  const auto& result = std::as_const(document);
  const auto* ordinary = result.find_layer(ordinary_id);
  const auto* cached_preview = result.find_layer(smart_object_id);
  CHECK(ordinary != nullptr && cached_preview != nullptr);
  const auto* ordinary_pixel = ordinary->pixels().pixel(0, 0);
  CHECK(ordinary_pixel[0] == kTo.red && ordinary_pixel[1] == kTo.green &&
        ordinary_pixel[2] == kTo.blue && ordinary_pixel[3] == 123);
  const auto* cached_pixel = cached_preview->pixels().pixel(0, 0);
  CHECK(cached_pixel[0] == kFrom.red && cached_pixel[1] == kFrom.green &&
        cached_pixel[2] == kFrom.blue && cached_pixel[3] == 123);
}

void palette_quantize_uses_exact_colors_then_median_cut() {
  patchy::PixelBuffer few(4, 2, patchy::PixelFormat::rgba8());
  const std::array<patchy::RgbColor, 4> wanted = {{{5, 5, 5}, {250, 0, 0}, {0, 250, 0}, {0, 0, 250}}};
  for (std::int32_t x = 0; x < 4; ++x) {
    for (std::int32_t y = 0; y < 2; ++y) {
      auto* px = few.pixel(x, y);
      px[0] = wanted[static_cast<std::size_t>(x)].red;
      px[1] = wanted[static_cast<std::size_t>(x)].green;
      px[2] = wanted[static_cast<std::size_t>(x)].blue;
      px[3] = 255;
    }
  }
  // Transparent pixels must not contribute colors.
  few.pixel(3, 1)[0] = 99;
  few.pixel(3, 1)[3] = 0;

  const auto exact = patchy::exact_palette_from_pixels(few, 256);
  CHECK(exact.has_value());
  CHECK(exact->colors.size() == 4);
  const auto quantized_few = patchy::quantize_to_palette(few, 16);
  CHECK(quantized_few.colors.size() == 4);

  patchy::PixelBuffer many(64, 1, patchy::PixelFormat::rgb8());
  for (std::int32_t x = 0; x < 64; ++x) {
    auto* px = many.pixel(x, 0);
    px[0] = static_cast<std::uint8_t>(x * 4);
    px[1] = static_cast<std::uint8_t>(255 - x * 2);
    px[2] = static_cast<std::uint8_t>(128 + x);
  }
  CHECK(!patchy::exact_palette_from_pixels(many, 16).has_value());
  const auto quantized = patchy::quantize_to_palette(many, 8);
  CHECK(quantized.colors.size() == 8);
  const auto again = patchy::quantize_to_palette(many, 8);
  CHECK(quantized.colors.size() == again.colors.size());
  for (std::size_t i = 0; i < quantized.colors.size(); ++i) {
    CHECK(patchy::palette_color_key(quantized.colors[i]) == patchy::palette_color_key(again.colors[i]));
  }
}

void palette_apply_dither_outputs_only_palette_colors_and_is_deterministic() {
  const auto* gameboy = patchy::find_builtin_palette_preset("gameboy");
  CHECK(gameboy != nullptr);
  patchy::PaletteLut lut;
  lut.build(gameboy->colors);

  const auto make_gradient = [] {
    patchy::PixelBuffer pixels(32, 32, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 32; ++y) {
      for (std::int32_t x = 0; x < 32; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(x * 8);
        px[1] = static_cast<std::uint8_t>(y * 8);
        px[2] = static_cast<std::uint8_t>((x + y) * 4);
        px[3] = static_cast<std::uint8_t>(y < 4 ? 60 : (y < 8 ? 180 : 255));
      }
    }
    return pixels;
  };

  const auto dithers = {patchy::PaletteDither::None, patchy::PaletteDither::FloydSteinberg,
                        patchy::PaletteDither::OrderedBayer4x4, patchy::PaletteDither::OrderedBayer8x8};
  patchy::PixelBuffer none_result(1, 1, patchy::PixelFormat::rgba8());
  for (const auto dither : dithers) {
    auto first = make_gradient();
    auto second = make_gradient();
    CHECK(!patchy::apply_palette_to_pixels(first, lut, dither, 128).empty());
    CHECK(!patchy::apply_palette_to_pixels(second, lut, dither, 128).empty());
    CHECK(patchy::pixels_are_palette_clean(first, lut));
    for (std::int32_t y = 0; y < 32; ++y) {
      for (std::int32_t x = 0; x < 32; ++x) {
        const auto* a = first.pixel(x, y);
        const auto* b = second.pixel(x, y);
        for (int channel = 0; channel < 4; ++channel) {
          CHECK(a[channel] == b[channel]);
        }
        CHECK(a[3] == (y < 4 ? 0 : 255));
      }
    }
    if (dither == patchy::PaletteDither::None) {
      none_result = std::move(first);
    } else if (dither == patchy::PaletteDither::FloydSteinberg) {
      // Dithering must actually change the picture relative to plain snapping.
      bool differs = false;
      for (std::int32_t y = 8; y < 32 && !differs; ++y) {
        for (std::int32_t x = 0; x < 32 && !differs; ++x) {
          const auto* a = first.pixel(x, y);
          const auto* b = none_result.pixel(x, y);
          differs = a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
        }
      }
      CHECK(differs);
    }
  }

  // A palette-clean buffer stays bit-identical through a no-dither re-apply.
  auto clean = make_gradient();
  CHECK(!patchy::apply_palette_to_pixels(clean, lut, patchy::PaletteDither::None, 128).empty());
  auto reapplied = clean;
  CHECK(patchy::apply_palette_to_pixels(reapplied, lut, patchy::PaletteDither::None, 128).empty());
}

void palette_io_round_trips_all_writer_formats() {
  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8 != nullptr);
  const std::vector<patchy::RgbColor> colors(pico8->colors.begin(), pico8->colors.end());

  const auto formats = {patchy::palette_io::PaletteFileFormat::JascPal, patchy::palette_io::PaletteFileFormat::Gpl,
                        patchy::palette_io::PaletteFileFormat::Hex, patchy::palette_io::PaletteFileFormat::Act,
                        patchy::palette_io::PaletteFileFormat::Aco};
  for (const auto format : formats) {
    const auto bytes = patchy::palette_io::write_palette_bytes(colors, format, "Test Palette");
    const auto parsed = patchy::palette_io::read_palette_bytes(bytes);
    CHECK(parsed.colors.size() == colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
      CHECK(patchy::palette_color_key(parsed.colors[i]) == patchy::palette_color_key(colors[i]));
    }
    if (format == patchy::palette_io::PaletteFileFormat::Gpl) {
      CHECK(parsed.name == "Test Palette");
    }
  }

  // Lospec-style hex with prefixes, blank lines, CRLF, and 8-digit entries.
  const std::string hex_text = "#ff004d\r\n\r\n00e43680\r\n1d2b53\r\n";
  const auto hex = patchy::palette_io::read_palette_bytes(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(hex_text.data()), hex_text.size()));
  CHECK(hex.colors.size() == 3);
  CHECK(hex.colors[0].red == 0xFF);
  CHECK(hex.colors[0].blue == 0x4D);
  CHECK(hex.colors[1].green == 0xE4);

  // A hand-built single-color .ase file (one RGB block, name "A").
  const std::array<std::uint8_t, 12 + 6 + 24> ase = {
      'A', 'S', 'E', 'F', 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // header + block count 1
      0x00, 0x01, 0x00, 0x00, 0x00, 24,                                    // color block, length 24
      0x00, 0x02, 0x00, 0x41, 0x00, 0x00,                                  // name "A" + terminator
      'R', 'G', 'B', ' ',                                                  //
      0x3F, 0x80, 0x00, 0x00,                                              // 1.0f
      0x00, 0x00, 0x00, 0x00,                                              // 0.0f
      0x3F, 0x00, 0x00, 0x00,                                              // 0.5f
      0x00, 0x02,                                                          // global color type
  };
  const auto ase_parsed = patchy::palette_io::read_palette_bytes(ase);
  CHECK(ase_parsed.colors.size() == 1);
  CHECK(ase_parsed.colors[0].red == 255);
  CHECK(ase_parsed.colors[0].green == 0);
  CHECK(ase_parsed.colors[0].blue == 128);

  // .act with a 772-byte trailer carrying count + transparent index.
  auto act = patchy::palette_io::write_palette_bytes(colors, patchy::palette_io::PaletteFileFormat::Act, {});
  CHECK(act.size() == 772);
  act[770] = 0x00;
  act[771] = 0x03;  // transparent index 3
  const auto act_parsed = patchy::palette_io::read_palette_bytes(act);
  CHECK(act_parsed.colors.size() == colors.size());
  CHECK(act_parsed.transparent_index.has_value());
  CHECK(*act_parsed.transparent_index == 3);

  bool threw = false;
  try {
    const std::string garbage = "certainly not a palette";
    (void)patchy::palette_io::read_palette_bytes(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(garbage.data()), garbage.size()));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void palette_presets_are_well_formed() {
  const auto presets = patchy::builtin_palette_presets();
  CHECK(presets.size() == 13);

  const auto expect_count = [&presets](std::string_view id, std::size_t count) {
    const auto* preset = patchy::find_builtin_palette_preset(id);
    CHECK(preset != nullptr);
    CHECK(preset->colors.size() == count);
  };
  expect_count("nes", 54);
  expect_count("c64", 16);
  expect_count("gameboy", 4);
  expect_count("pico8", 16);
  expect_count("cga", 16);
  expect_count("ega64", 64);
  expect_count("vga256", 246);
  expect_count("zx_spectrum", 15);
  expect_count("msx", 15);
  expect_count("amstrad_cpc", 27);
  expect_count("dawnbringer16", 16);
  expect_count("dawnbringer32", 32);
  expect_count("dink", 256);

  for (const auto& preset : presets) {
    CHECK(preset.colors.size() >= 4);
    CHECK(preset.colors.size() <= 256);
    std::unordered_set<std::uint32_t> unique;
    for (const auto& color : preset.colors) {
      CHECK(unique.insert(patchy::palette_color_key(color)).second);  // presets carry no duplicate entries
    }
  }

  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8->colors[8].red == 0xFF);
  CHECK(pico8->colors[8].green == 0x00);
  CHECK(pico8->colors[8].blue == 0x4D);
  const auto* gameboy = patchy::find_builtin_palette_preset("gameboy");
  CHECK(gameboy->colors[3].red == 0x9B);
  // VGA default DAC: EGA white bit-replicates to full white, the first wheel
  // entry after the 16 EGA colors + 14-step gray ramp is pure blue.
  const auto* vga = patchy::find_builtin_palette_preset("vga256");
  CHECK(patchy::palette_color_key(vga->colors[0]) == 0x000000U);
  CHECK(patchy::palette_color_key(vga->colors[15]) == 0xFFFFFFU);
  CHECK(patchy::palette_color_key(vga->colors[30]) == 0x0000FFU);
  // Dink Smallwood: engine index order (white first, black last, the pure-green
  // sprite key near the end).
  const auto* dink = patchy::find_builtin_palette_preset("dink");
  CHECK(patchy::palette_color_key(dink->colors[0]) == 0xFFFFFFU);
  CHECK(patchy::palette_color_key(dink->colors[252]) == 0x00FF00U);
  CHECK(patchy::palette_color_key(dink->colors[255]) == 0x000000U);
  CHECK(patchy::find_builtin_palette_preset("not_a_preset") == nullptr);
}

void tool_writes_snap_to_palette_when_constrained() {
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::PaletteLut lut;
  lut.build(preset->colors);
  patchy::PaletteSnapContext snap;
  snap.lut = &lut;

  const auto check_layer_clean = [&lut](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(patchy::pixels_are_palette_clean(layer->pixels(), lut));
  };
  const auto snapped_options = [&snap](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto options = tool_options(r, g, b);
    options.palette_snap = &snap;
    return options;
  };

  {  // Soft procedural brush: hard rim, palette colors, 0/255 alpha only.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(211, 32, 41);
    options.brush_size = 15;
    options.brush_softness = 80;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 44.0, 30.0, options, false).empty());
    check_layer_clean(document, layer_id);
    CHECK(document.find_layer(layer_id)->pixels().pixel(30, 25)[3] == 255);
  }
  {  // Painting with an exact palette color keeps exactly that color.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(0xFF, 0x00, 0x4D);  // PICO-8 entry 8
    options.brush_size = 9;
    CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
    const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
    CHECK(px[0] == 0xFF);
    CHECK(px[1] == 0x00);
    CHECK(px[2] == 0x4D);
    CHECK(px[3] == 255);
    check_layer_clean(document, layer_id);
  }
  {  // Bitmap tip stroke through the stateful spacing overload.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    const auto tip = make_bar_brush_tip();
    const auto mips = patchy::build_brush_tip_mips(tip);
    const auto scaled = patchy::make_scaled_brush_tip(mips, 9);
    auto options = snapped_options(30, 160, 220);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 40.0, 28.0, options, false, state).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Anti-aliased shapes: filled rounded rect and ellipse outline.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto fill = snapped_options(255, 120, 0);
    fill.fill_shapes = true;
    fill.shape_corner_radius = 6;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{8, 6, 30, 22}, fill, false).empty());
    auto outline = snapped_options(150, 40, 220);
    outline.brush_size = 3;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{30, 20, 30, 22}, outline, false).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Feathered fill inside a selection hardens at the coverage threshold.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(20, 90, 200);
    options.selection = patchy::Rect{10, 8, 30, 24};
    options.fill_softness_feather = 4.0;
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{10, 8, 30, 24}, options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Flood fill on the 3-channel background.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    CHECK(!patchy::flood_fill(document, background_id, 5, 5, snapped_options(0, 180, 210)).empty());
    check_layer_clean(document, background_id);
  }
  {  // Gradient with off-palette, alpha-varying stops posterizes to the palette.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Linear;
    gradient.opacity = 0.9F;
    gradient.stops = {{0.0F, patchy::EditColor{255, 0, 0, 255}},
                      {0.5F, patchy::EditColor{40, 200, 60, 128}},
                      {1.0F, patchy::EditColor{20, 40, 255, 0}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 47, snapped_options(0, 0, 0), gradient).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Soft erase keeps alpha hard; clear honors the same rule.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, snapped_options(0xFF, 0xF1, 0xE8)).empty());
    auto erase_options = snapped_options(0, 0, 0);
    erase_options.brush_size = 14;
    erase_options.brush_softness = 60;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 12.0, 40.0, 30.0, erase_options, true).empty());
    check_layer_clean(document, layer_id);
    auto clear_options = snapped_options(0, 0, 0);
    clear_options.selection = patchy::Rect{6, 30, 20, 12};
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, clear_options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Smudge drags palette content and still lands on palette colors.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 32, 48}, snapped_options(0xFF, 0xA3, 0x00)).empty());
    auto options = snapped_options(0, 0, 0);
    options.brush_size = 13;
    CHECK(!patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // The transparency lock is honored: pixels the blend refuses are never
     // snapped, even though their RGB is off-palette.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
    px[0] = 7;
    px[1] = 8;
    px[2] = 9;
    px[3] = 0;
    auto options = snapped_options(0xFF, 0x00, 0x4D);
    options.lock_transparent_pixels = true;
    options.brush_size = 9;
    (void)patchy::paint_brush(document, layer_id, 20, 20, options, false);
    const auto* after = document.find_layer(layer_id)->pixels().pixel(20, 20);
    CHECK(after[0] == 7);
    CHECK(after[1] == 8);
    CHECK(after[2] == 9);
    CHECK(after[3] == 0);
  }
}

void bmp_palette_mode_doc_exports_exact_document_palette() {
  patchy::Document document(8, 4, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  document.palette_editing() = editing;
  patchy::sync_document_indexed_palette(document);

  patchy::PixelBuffer pixels(8, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  // One off-palette pixel, standing in for live layer-style output that the
  // flatten picks up; Exact export must snap it instead of failing.
  auto* off_palette = pixels.pixel(5, 2);
  off_palette[0] = 250;
  off_palette[1] = 10;
  off_palette[2] = 10;
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::bmp::WriteOptions options;
  options.encoding = patchy::bmp::BmpEncoding::Indexed2;
  options.palette_mode = patchy::bmp::BmpPaletteMode::Exact;
  const auto bytes = patchy::bmp::DocumentIo::write(document, options);
  const auto read = patchy::bmp::DocumentIo::read(bytes);
  CHECK(read.indexed_palette().has_value());
  CHECK(read.indexed_palette()->colors.size() == 4);
  for (std::size_t index = 0; index < 4; ++index) {
    // The file carries the DOCUMENT palette, in order.
    CHECK(patchy::palette_color_key(read.indexed_palette()->colors[index]) ==
          patchy::palette_color_key(preset->colors[index]));
  }
  patchy::PaletteLut lut;
  lut.build(preset->colors);
  const auto* snapped = read.layers().front().pixels().pixel(5, 2);
  CHECK(lut.contains(patchy::RgbColor{snapped[0], snapped[1], snapped[2]}));
}

void psd_round_trips_palette_resource() {
  auto document = make_tool_document();
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.alpha_threshold = 96;
  document.palette_editing() = editing;
  patchy::sync_document_indexed_palette(document);

  // The layered writer carries the palette resource; the mode flag survives.
  const auto layered = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(layered);
  CHECK(reread.palette_editing().has_value());
  CHECK(reread.palette_editing()->palette.colors.size() == 4);
  CHECK(reread.palette_editing()->alpha_threshold == 96);
  CHECK(reread.palette_editing()->palette.colors[3].red == 0x9B);
  CHECK(reread.indexed_palette().has_value());

  // An attached palette without the editing mode also travels, mode off.
  document.palette_editing().reset();
  const auto rgb_reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(!rgb_reread.palette_editing().has_value());
  CHECK(rgb_reread.indexed_palette().has_value());
  CHECK(rgb_reread.indexed_palette()->colors.size() == 4);

  // No palette anywhere: no resource, nothing restored.
  auto plain = make_tool_document();
  const auto plain_reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_flat_rgb8(plain));
  CHECK(!plain_reread.palette_editing().has_value());
  CHECK(!plain_reread.indexed_palette().has_value());

  // A corrupt payload is ignored: the file opens as plain RGB. Flip the magic in
  // the written bytes and re-read.
  auto corrupted = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::array<std::uint8_t, 4> magic = {'P', 't', 'c', 'P'};
  for (std::size_t offset = 0; offset + magic.size() <= corrupted.size(); ++offset) {
    if (std::equal(magic.begin(), magic.end(), corrupted.begin() + static_cast<std::ptrdiff_t>(offset))) {
      corrupted[offset] = 'X';
      break;
    }
  }
  const auto corrupt_reread = patchy::psd::DocumentIo::read(corrupted);
  CHECK(!corrupt_reread.palette_editing().has_value());
}

void document_palette_editing_copies_and_syncs_indexed_mirror() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  CHECK(!document.palette_editing().has_value());

  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}};
  editing.alpha_threshold = 100;
  editing.palette_revision = 7;
  document.palette_editing() = editing;

  // Undo snapshots copy the whole Document; palette state must ride along.
  patchy::Document snapshot = document;
  document.palette_editing()->palette.colors.push_back({0, 255, 0});
  document.palette_editing()->palette_revision = 8;
  CHECK(snapshot.palette_editing().has_value());
  CHECK(snapshot.palette_editing()->palette.colors.size() == 3);
  CHECK(snapshot.palette_editing()->palette_revision == 7);
  CHECK(document.palette_editing()->palette.colors.size() == 4);

  patchy::sync_document_indexed_palette(document);
  CHECK(document.indexed_palette().has_value());
  CHECK(document.indexed_palette()->colors.size() == 4);
  CHECK(document.indexed_palette()->source_bit_depth == 2);

  document.palette_editing()->palette.colors.resize(17, patchy::RgbColor{1, 2, 3});
  patchy::sync_document_indexed_palette(document);
  CHECK(document.indexed_palette()->source_bit_depth == 8);

  patchy::Document untouched(2, 2, patchy::PixelFormat::rgb8());
  untouched.indexed_palette() = patchy::DocumentIndexedPalette{{{9, 9, 9}}, 8};
  patchy::sync_document_indexed_palette(untouched);  // no editing state: mirror keeps import metadata
  CHECK(untouched.indexed_palette()->colors.size() == 1);
  CHECK(untouched.indexed_palette()->colors[0].red == 9);
}

}  // namespace

std::vector<patchy::test::TestCase> palette_tests() {
  return {
      {"palette_lut_snaps_within_quantization_bound_and_is_idempotent",
       palette_lut_snaps_within_quantization_bound_and_is_idempotent},
      {"palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers",
       palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers},
      {"palette_remap_exact_color_is_lossless_and_bounded", palette_remap_exact_color_is_lossless_and_bounded},
      {"palette_document_remap_skips_smart_object_preview_cache",
       palette_document_remap_skips_smart_object_preview_cache},
      {"palette_quantize_uses_exact_colors_then_median_cut", palette_quantize_uses_exact_colors_then_median_cut},
      {"palette_apply_dither_outputs_only_palette_colors_and_is_deterministic",
       palette_apply_dither_outputs_only_palette_colors_and_is_deterministic},
      {"palette_io_round_trips_all_writer_formats", palette_io_round_trips_all_writer_formats},
      {"palette_presets_are_well_formed", palette_presets_are_well_formed},
      {"tool_writes_snap_to_palette_when_constrained", tool_writes_snap_to_palette_when_constrained},
      {"bmp_palette_mode_doc_exports_exact_document_palette",
       bmp_palette_mode_doc_exports_exact_document_palette},
      {"psd_round_trips_palette_resource", psd_round_trips_palette_resource},
      {"document_palette_editing_copies_and_syncs_indexed_mirror",
       document_palette_editing_copies_and_syncs_indexed_mirror},
  };
}
