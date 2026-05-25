#include "color/color_management.hpp"
#include "core/document.hpp"
#include "filters/filter_registry.hpp"
#include "formats/format_registry.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "core/pixel_tools.hpp"
#include "render/compositor.hpp"
#include "render/tile_cache.hpp"

#include <chrono>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using TestFn = std::function<void()>;

struct TestCase {
  std::string name;
  TestFn run;
};

void check(bool condition, const char* expression, const char* file, int line) {
  if (!condition) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " check failed: " + expression);
  }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

photoslop::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b) {
  photoslop::PixelBuffer pixels(width, height, photoslop::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = r;
      px[1] = g;
      px[2] = b;
    }
  }
  return pixels;
}

photoslop::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a) {
  photoslop::PixelBuffer pixels(width, height, photoslop::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = r;
      px[1] = g;
      px[2] = b;
      px[3] = a;
    }
  }
  return pixels;
}

photoslop::Document make_tool_document() {
  photoslop::Document document(64, 48, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  document.add_pixel_layer("Paint", solid_rgba(64, 48, 0, 0, 0, 0));
  return document;
}

photoslop::Document make_filter_document() {
  photoslop::Document document(32, 24, photoslop::PixelFormat::rgb8());
  photoslop::PixelBuffer pixels(32, 24, photoslop::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 8);
      px[1] = static_cast<std::uint8_t>(y * 10);
      px[2] = static_cast<std::uint8_t>(80 + (x + y) % 120);
    }
  }
  document.add_pixel_layer("Filter Source", std::move(pixels));
  return document;
}

photoslop::EditOptions tool_options(std::uint8_t r = 220, std::uint8_t g = 20, std::uint8_t b = 40) {
  photoslop::EditOptions options;
  options.primary = photoslop::EditColor{r, g, b, 255};
  options.secondary = photoslop::EditColor{255, 255, 255, 255};
  options.brush_size = 7;
  return options;
}

void write_u16_le(std::ofstream& file, std::uint16_t value) {
  file.put(static_cast<char>(value & 0xFFU));
  file.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write_u32_le(std::ofstream& file, std::uint32_t value) {
  file.put(static_cast<char>(value & 0xFFU));
  file.put(static_cast<char>((value >> 8U) & 0xFFU));
  file.put(static_cast<char>((value >> 16U) & 0xFFU));
  file.put(static_cast<char>((value >> 24U) & 0xFFU));
}

void write_ascii4(photoslop::psd::BigEndianWriter& writer, const char (&value)[5]) {
  for (int i = 0; i < 4; ++i) {
    writer.write_u8(static_cast<std::uint8_t>(value[i]));
  }
}

void write_pascal_padded(photoslop::psd::BigEndianWriter& writer, const std::string& value,
                         std::size_t padded_multiple) {
  const auto length = std::min<std::size_t>(value.size(), 255);
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), length));
  const auto consumed = 1 + length;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  for (std::size_t i = consumed; i < padded; ++i) {
    writer.write_u8(0);
  }
}

std::string read_pascal_padded(photoslop::psd::BigEndianReader& reader, std::size_t padded_multiple) {
  const auto start = reader.position();
  const auto length = reader.read_u8();
  auto bytes = reader.read_bytes(length);
  const auto consumed = reader.position() - start;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  if (padded > consumed) {
    reader.skip(padded - consumed);
  }
  return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> psd_raw_layer_record_names(std::span<const std::uint8_t> bytes) {
  photoslop::psd::BigEndianReader reader(bytes);
  (void)photoslop::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = reader.read_u32();
  CHECK(layer_info_length > 0);

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  std::vector<std::string> names;
  names.reserve(static_cast<std::size_t>(layer_count));

  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(4);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    const auto extra_end = reader.position() + extra_length;
    const auto mask_length = reader.read_u32();
    reader.skip(mask_length);
    const auto blending_ranges_length = reader.read_u32();
    reader.skip(blending_ranges_length);
    names.push_back(read_pascal_padded(reader, 4));
    if (reader.position() < extra_end) {
      reader.skip(extra_end - reader.position());
    }
  }

  return names;
}

void write_test_image_resource(photoslop::psd::BigEndianWriter& writer, std::uint16_t id, const std::string& name,
                               std::span<const std::uint8_t> payload) {
  write_ascii4(writer, "8BIM");
  writer.write_u16(id);
  write_pascal_padded(writer, name, 2);
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

void write_test_layer_block(photoslop::psd::BigEndianWriter& writer, const char (&key)[5],
                            std::span<const std::uint8_t> payload) {
  write_ascii4(writer, "8BIM");
  write_ascii4(writer, key);
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type, const char (&blend_mode)[5]) {
  photoslop::psd::BigEndianWriter payload;
  payload.write_u32(type);
  write_ascii4(payload, "8BIM");
  write_ascii4(payload, blend_mode);
  return payload.bytes();
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type) {
  photoslop::psd::BigEndianWriter payload;
  payload.write_u32(type);
  return payload.bytes();
}

std::vector<std::uint8_t> psd_raw_image_resources(std::span<const std::uint8_t> bytes) {
  photoslop::psd::BigEndianReader reader(bytes);
  (void)photoslop::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  return reader.read_bytes(image_resource_length);
}

std::optional<std::vector<std::uint8_t>> test_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id) {
  photoslop::psd::BigEndianReader reader(resources);
  while (reader.remaining() > 0) {
    auto signature = reader.read_bytes(4);
    CHECK(signature[0] == '8');
    CHECK(signature[1] == 'B');
    const auto resource_id = reader.read_u16();
    (void)read_pascal_padded(reader, 2);
    const auto payload_length = reader.read_u32();
    auto payload = reader.read_bytes(payload_length);
    if ((payload_length % 2U) != 0 && reader.remaining() > 0) {
      reader.skip(1);
    }
    if (resource_id == id) {
      return payload;
    }
  }
  return std::nullopt;
}

void write_bmp_artifact(const std::string& name, const photoslop::Document& document) {
  const auto out_dir = std::filesystem::path("test-artifacts");
  std::filesystem::create_directories(out_dir);
  const auto path = out_dir / (name + ".bmp");
  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not write test image artifact");
  }

  const auto row_stride = static_cast<std::uint32_t>(((flattened.width() * 3 + 3) / 4) * 4);
  const auto pixel_bytes = row_stride * static_cast<std::uint32_t>(flattened.height());
  const auto file_size = 14U + 40U + pixel_bytes;

  file.put('B');
  file.put('M');
  write_u32_le(file, file_size);
  write_u16_le(file, 0);
  write_u16_le(file, 0);
  write_u32_le(file, 14U + 40U);

  write_u32_le(file, 40U);
  write_u32_le(file, static_cast<std::uint32_t>(flattened.width()));
  write_u32_le(file, static_cast<std::uint32_t>(flattened.height()));
  write_u16_le(file, 1);
  write_u16_le(file, 24);
  write_u32_le(file, 0);
  write_u32_le(file, pixel_bytes);
  write_u32_le(file, 2835);
  write_u32_le(file, 2835);
  write_u32_le(file, 0);
  write_u32_le(file, 0);

  std::vector<std::uint8_t> padding(row_stride - static_cast<std::uint32_t>(flattened.width() * 3), 0);
  for (std::int32_t y = flattened.height() - 1; y >= 0; --y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      const auto* px = flattened.pixel(x, y);
      file.put(static_cast<char>(px[2]));
      file.put(static_cast<char>(px[1]));
      file.put(static_cast<char>(px[0]));
    }
    file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
  }
}

photoslop::LayerId active_tool_layer(const photoslop::Document& document) {
  return document.active_layer_id().value();
}

void pixel_buffer_tracks_shape_and_rows() {
  photoslop::PixelBuffer pixels(4, 3, photoslop::PixelFormat::rgba8());
  CHECK(pixels.width() == 4);
  CHECK(pixels.height() == 3);
  CHECK(pixels.byte_size() == 4U * 3U * 4U);
  CHECK(pixels.row(1).size() == 16U);
  pixels.pixel(2, 1)[0] = 77;
  CHECK(pixels.row(1)[8] == 77);
}

void document_adds_and_finds_layers() {
  photoslop::Document document(2, 2, photoslop::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Paint", solid_rgb(2, 2, 10, 20, 30));
  CHECK(layer.id() == 1);
  CHECK(document.active_layer_id().value() == layer.id());
  CHECK(document.find_layer(layer.id()) == &layer);
}

void document_removes_layers_and_updates_active_layer() {
  photoslop::Document document(2, 2, photoslop::PixelFormat::rgb8());
  auto first = document.add_pixel_layer("First", solid_rgb(2, 2, 10, 20, 30)).id();
  auto second = document.add_pixel_layer("Second", solid_rgb(2, 2, 40, 50, 60)).id();
  CHECK(document.active_layer_id().value() == second);
  CHECK(document.remove_layer(second));
  CHECK(document.find_layer(second) == nullptr);
  CHECK(document.active_layer_id().value() == first);
  CHECK(document.remove_layer(first));
  CHECK(!document.active_layer_id().has_value());
}

void compositor_flattens_visible_layers() {
  photoslop::Document document(1, 1, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 10, 20, 30));
  auto top_pixels = solid_rgb(1, 1, 110, 120, 130);
  auto& top = document.add_pixel_layer("Top", std::move(top_pixels));
  top.set_opacity(0.5F);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(0, 0);
  CHECK(px[0] == 60);
  CHECK(px[1] == 70);
  CHECK(px[2] == 80);
}

void compositor_applies_extended_blend_modes() {
  struct ExpectedBlend {
    photoslop::BlendMode mode;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
  };

  const std::vector<ExpectedBlend> expected = {
      {photoslop::BlendMode::Darken, 100, 60, 100},
      {photoslop::BlendMode::Lighten, 200, 120, 140},
      {photoslop::BlendMode::ColorDodge, 255, 156, 230},
      {photoslop::BlendMode::ColorBurn, 58, 0, 0},
      {photoslop::BlendMode::HardLight, 189, 56, 109},
      {photoslop::BlendMode::SoftLight, 134, 86, 126},
      {photoslop::BlendMode::Difference, 100, 60, 40},
      {photoslop::BlendMode::LinearBurn, 45, 0, 0},
      {photoslop::BlendMode::PinLight, 144, 120, 140},
      {photoslop::BlendMode::Saturation, 53, 120, 187},
      {photoslop::BlendMode::Luminosity, 109, 130, 151},
  };

  for (const auto& blend : expected) {
    photoslop::Document document(1, 1, photoslop::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 120, 140));
    auto& top = document.add_pixel_layer("Top", solid_rgba(1, 1, 200, 60, 100, 255));
    top.set_blend_mode(blend.mode);

    const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
    const auto* px = flattened.pixel(0, 0);
    CHECK(px[0] == blend.r);
    CHECK(px[1] == blend.g);
    CHECK(px[2] == blend.b);
  }
}

void compositor_renders_layer_style_drop_shadow_gradient_and_stroke() {
  photoslop::Document document(12, 12, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  photoslop::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(4, 4, 220, 20, 20, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(photoslop::Rect{3, 3, 4, 4});

  photoslop::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = photoslop::BlendMode::Normal;
  shadow.color = photoslop::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 2.0F;
  shadow.size = 0.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  photoslop::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = photoslop::BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient.angle_degrees = 0.0F;
  fill.gradient.color_stops.push_back(photoslop::GradientColorStop{0.0F, photoslop::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(photoslop::GradientColorStop{1.0F, photoslop::RgbColor{20, 220, 80}});
  layer.layer_style().gradient_fills.push_back(fill);

  photoslop::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = photoslop::BlendMode::Normal;
  stroke.color = photoslop::RgbColor{255, 220, 0};
  stroke.opacity = 1.0F;
  stroke.size = 1.0F;
  stroke.position = photoslop::LayerStrokePosition::Outside;
  layer.layer_style().strokes.push_back(stroke);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(8, 4);
  CHECK(shadow_px[0] < 20);
  CHECK(shadow_px[1] < 20);
  CHECK(shadow_px[2] < 20);

  const auto* left_gradient = flattened.pixel(3, 4);
  const auto* right_gradient = flattened.pixel(6, 4);
  CHECK(left_gradient[2] > left_gradient[1]);
  CHECK(right_gradient[1] > right_gradient[2]);

  const auto* stroke_px = flattened.pixel(2, 4);
  CHECK(stroke_px[0] > 240);
  CHECK(stroke_px[1] > 200);
  CHECK(stroke_px[2] < 40);
}

void compositor_renders_layer_style_bevel_emboss() {
  photoslop::Document document(12, 12, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  photoslop::Layer styled_layer(document.allocate_layer_id(), "Bevel", solid_rgba(6, 6, 120, 120, 120, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(photoslop::Rect{3, 3, 6, 6});

  photoslop::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = photoslop::BlendMode::Normal;
  bevel.highlight_color = photoslop::RgbColor{255, 255, 255};
  bevel.highlight_opacity = 1.0F;
  bevel.shadow_blend_mode = photoslop::BlendMode::Normal;
  bevel.shadow_color = photoslop::RgbColor{0, 0, 0};
  bevel.shadow_opacity = 1.0F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 3.0F;
  bevel.size = 2.0F;
  layer.layer_style().bevels.push_back(bevel);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  const auto* highlighted = flattened.pixel(3, 3);
  const auto* shadowed = flattened.pixel(8, 8);
  CHECK(highlighted[0] > 150);
  CHECK(shadowed[0] < 100);
}

void compositor_renders_layer_style_outer_glow() {
  photoslop::Document document(14, 14, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(14, 14, 255, 255, 255));

  photoslop::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(photoslop::Rect{5, 5, 4, 4});

  photoslop::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = photoslop::BlendMode::Normal;
  glow.color = photoslop::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 4.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  const auto* glow_px = flattened.pixel(4, 6);
  const auto* layer_px = flattened.pixel(6, 6);
  CHECK(glow_px[0] > 240);
  CHECK(glow_px[1] < 120);
  CHECK(glow_px[2] < 120);
  CHECK(layer_px[2] > 200);
}

void psd_flat_rgb8_round_trips() {
  photoslop::Document document(2, 1, photoslop::PixelFormat::rgb8());
  photoslop::PixelBuffer pixels(2, 1, photoslop::PixelFormat::rgb8());
  pixels.pixel(0, 0)[0] = 1;
  pixels.pixel(0, 0)[1] = 2;
  pixels.pixel(0, 0)[2] = 3;
  pixels.pixel(1, 0)[0] = 4;
  pixels.pixel(1, 0)[1] = 5;
  pixels.pixel(1, 0)[2] = 6;
  document.add_pixel_layer("Background", std::move(pixels));

  const auto bytes = photoslop::psd::DocumentIo::write_flat_rgb8(document);
  CHECK(photoslop::psd::DocumentIo::can_read(bytes));

  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 2);
  CHECK(read.height() == 1);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(1, 0);
  CHECK(px[0] == 4);
  CHECK(px[1] == 5);
  CHECK(px[2] == 6);
}

void psd_flat_rle_rgb8_reads() {
  photoslop::psd::BigEndianWriter writer;
  photoslop::psd::write_header(writer, photoslop::psd::Header{false, 3, 1, 2, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(1);
  writer.write_u16(3);
  writer.write_u16(3);
  writer.write_u16(3);
  writer.write_u8(1);
  writer.write_u8(1);
  writer.write_u8(4);
  writer.write_u8(1);
  writer.write_u8(2);
  writer.write_u8(5);
  writer.write_u8(1);
  writer.write_u8(3);
  writer.write_u8(6);

  const auto read = photoslop::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 1);
  CHECK(px0[1] == 2);
  CHECK(px0[2] == 3);
  CHECK(px1[0] == 4);
  CHECK(px1[1] == 5);
  CHECK(px1[2] == 6);
}

void psd_image_resources_round_trip_and_icc_profile_is_exposed() {
  const std::vector<std::uint8_t> resolution_payload{0, 1, 2, 3, 4};
  const std::vector<std::uint8_t> icc_payload{10, 20, 30, 40};
  photoslop::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1005, "dpi", resolution_payload);
  write_test_image_resource(resources, 1039, "", icc_payload);

  photoslop::psd::BigEndianWriter writer;
  photoslop::psd::write_header(writer, photoslop::psd::Header{false, 3, 1, 1, 8, 3});
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  writer.write_bytes(resources.bytes());
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u8(1);
  writer.write_u8(2);
  writer.write_u8(3);

  auto document = photoslop::psd::DocumentIo::read(writer.bytes());
  CHECK(document.metadata().raw_psd_image_resources == resources.bytes());
  CHECK(document.color_state().embedded_icc_profile == icc_payload);

  const auto flat_resources = psd_raw_image_resources(photoslop::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(test_image_resource_payload(flat_resources, 1005).value() == resolution_payload);
  CHECK(test_image_resource_payload(flat_resources, 1039).value() == icc_payload);

  const std::vector<std::uint8_t> replacement_icc{90, 91, 92, 93, 94};
  document.color_state().embedded_icc_profile = replacement_icc;
  const auto layered_resources = psd_raw_image_resources(photoslop::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(test_image_resource_payload(layered_resources, 1005).value() == resolution_payload);
  CHECK(test_image_resource_payload(layered_resources, 1039).value() == replacement_icc);
}

void psd_layered_rgb8_round_trips_pixel_layers() {
  photoslop::Document document(3, 2, photoslop::PixelFormat::rgb8());
  auto& background = document.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));
  background.set_opacity(1.0F);

  auto top_pixels = solid_rgba(2, 1, 200, 10, 20, 128);
  photoslop::Layer bounded_layer(document.allocate_layer_id(), "Paint", std::move(top_pixels));
  auto& top = document.add_layer(std::move(bounded_layer));
  top.set_bounds(photoslop::Rect{1, 1, 2, 1});
  top.set_opacity(0.75F);
  top.set_blend_mode(photoslop::BlendMode::Multiply);

  const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(photoslop::psd::DocumentIo::can_read(bytes));

  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 3);
  CHECK(read.height() == 2);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Paint");
  CHECK(read.layers()[1].bounds().x == 1);
  CHECK(read.layers()[1].bounds().y == 1);
  CHECK(read.layers()[1].pixels().format() == photoslop::PixelFormat::rgba8());
  CHECK(read.layers()[1].blend_mode() == photoslop::BlendMode::Multiply);
  CHECK(read.layers()[1].pixels().pixel(0, 0)[0] == 200);
  CHECK(read.layers()[1].pixels().pixel(0, 0)[3] == 128);
}

void psd_layer_masks_render_and_round_trip() {
  photoslop::Document document(4, 2, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(4, 2, 255, 255, 255));
  auto& top = document.add_pixel_layer("Masked Red", solid_rgb(4, 2, 220, 20, 20));

  photoslop::PixelBuffer mask_pixels(2, 2, photoslop::PixelFormat::gray8());
  mask_pixels.clear(255);
  top.set_mask(photoslop::LayerMask{photoslop::Rect{0, 0, 2, 2}, std::move(mask_pixels), 0, false});

  auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);

  const auto read = photoslop::psd::DocumentIo::read(photoslop::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 2);
  const auto& read_top = read.layers()[1];
  CHECK(read_top.mask().has_value());
  CHECK(read_top.mask()->bounds.x == 0);
  CHECK(read_top.mask()->bounds.width == 2);
  CHECK(read_top.mask()->default_color == 0);
  CHECK(*read_top.mask()->pixels.pixel(1, 1) == 255);

  flattened = photoslop::Compositor{}.flatten_rgb8(read);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);
}

void psd_layer_styles_round_trip_photoslop_effects() {
  photoslop::Document document(14, 14, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(14, 14, 255, 255, 255));
  photoslop::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(5, 5, 180, 40, 70, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(photoslop::Rect{4, 4, 5, 5});

  photoslop::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = photoslop::BlendMode::Multiply;
  shadow.color = photoslop::RgbColor{10, 20, 30};
  shadow.opacity = 0.6F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 4.0F;
  shadow.spread = 15.0F;
  shadow.size = 6.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  photoslop::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = photoslop::BlendMode::Screen;
  glow.color = photoslop::RgbColor{250, 230, 80};
  glow.opacity = 0.5F;
  glow.spread = 25.0F;
  glow.size = 3.0F;
  layer.layer_style().outer_glows.push_back(glow);

  photoslop::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = photoslop::BlendMode::Overlay;
  fill.opacity = 0.75F;
  fill.gradient.type = photoslop::LayerStyleGradientType::Radial;
  fill.gradient.angle_degrees = 45.0F;
  fill.gradient.scale = 0.8F;
  fill.gradient.reverse = true;
  fill.gradient.color_stops.push_back(photoslop::GradientColorStop{0.0F, photoslop::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(photoslop::GradientColorStop{1.0F, photoslop::RgbColor{20, 220, 80}});
  fill.gradient.alpha_stops.push_back(photoslop::GradientAlphaStop{0.0F, 0.25F});
  fill.gradient.alpha_stops.push_back(photoslop::GradientAlphaStop{1.0F, 1.0F});
  layer.layer_style().gradient_fills.push_back(fill);

  photoslop::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = photoslop::BlendMode::Normal;
  stroke.color = photoslop::RgbColor{255, 220, 0};
  stroke.opacity = 0.9F;
  stroke.size = 2.0F;
  stroke.position = photoslop::LayerStrokePosition::Inside;
  stroke.uses_gradient = true;
  stroke.gradient = fill.gradient;
  layer.layer_style().strokes.push_back(stroke);

  photoslop::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = photoslop::BlendMode::Screen;
  bevel.highlight_color = photoslop::RgbColor{255, 250, 220};
  bevel.highlight_opacity = 0.7F;
  bevel.shadow_blend_mode = photoslop::BlendMode::Multiply;
  bevel.shadow_color = photoslop::RgbColor{20, 15, 10};
  bevel.shadow_opacity = 0.65F;
  bevel.angle_degrees = 100.0F;
  bevel.altitude_degrees = 35.0F;
  bevel.depth = 1.5F;
  bevel.size = 4.0F;
  bevel.direction_up = false;
  layer.layer_style().bevels.push_back(bevel);

  const auto read = photoslop::psd::DocumentIo::read(photoslop::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 2);
  const auto& style = read.layers()[1].layer_style();
  CHECK(!style.empty());
  CHECK(style.drop_shadows.size() == 1);
  CHECK(style.drop_shadows.front().blend_mode == photoslop::BlendMode::Multiply);
  CHECK(style.drop_shadows.front().color.red == 10);
  CHECK(style.drop_shadows.front().opacity == 0.6F);
  CHECK(style.outer_glows.size() == 1);
  CHECK(style.outer_glows.front().color.green == 230);
  CHECK(style.gradient_fills.size() == 1);
  CHECK(style.gradient_fills.front().blend_mode == photoslop::BlendMode::Overlay);
  CHECK(style.gradient_fills.front().gradient.type == photoslop::LayerStyleGradientType::Radial);
  CHECK(style.gradient_fills.front().gradient.reverse);
  CHECK(style.gradient_fills.front().gradient.alpha_stops.size() == 2);
  CHECK(style.strokes.size() == 1);
  CHECK(style.strokes.front().position == photoslop::LayerStrokePosition::Inside);
  CHECK(style.strokes.front().uses_gradient);
  CHECK(style.bevels.size() == 1);
  CHECK(style.bevels.front().shadow_color.blue == 10);
  CHECK(!style.bevels.front().direction_up);
}

void psd_writer_uses_photoshop_bottom_to_top_layer_record_order() {
  photoslop::Document document(3, 2, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));
  document.add_pixel_layer("Middle", solid_rgba(3, 2, 80, 120, 180, 255));
  document.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));

  const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(document);
  const auto names = psd_raw_layer_record_names(bytes);

  CHECK(names.size() == 3);
  CHECK(names[0] == "Background");
  CHECK(names[1] == "Middle");
  CHECK(names[2] == "Top");

  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 3);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Middle");
  CHECK(read.layers()[2].name() == "Top");

  photoslop::Document no_background(3, 2, photoslop::PixelFormat::rgb8());
  no_background.add_pixel_layer("Bottom", solid_rgba(3, 2, 20, 40, 60, 255));
  no_background.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));

  const auto no_background_bytes = photoslop::psd::DocumentIo::write_layered_rgb8(no_background);
  const auto no_background_names = psd_raw_layer_record_names(no_background_bytes);
  CHECK(no_background_names.size() == 2);
  CHECK(no_background_names[0] == "Bottom");
  CHECK(no_background_names[1] == "Top");

  const auto no_background_read = photoslop::psd::DocumentIo::read(no_background_bytes);
  CHECK(no_background_read.layers().size() == 2);
  CHECK(no_background_read.layers()[0].name() == "Bottom");
  CHECK(no_background_read.layers()[1].name() == "Top");
}

void psd_reader_tolerates_legacy_photoslop_top_to_bottom_background_files() {
  photoslop::Document legacy_file_order(3, 2, photoslop::PixelFormat::rgb8());
  legacy_file_order.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));
  legacy_file_order.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));

  const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(legacy_file_order);
  const auto names = psd_raw_layer_record_names(bytes);
  CHECK(names.size() == 2);
  CHECK(names[0] == "Top");
  CHECK(names[1] == "Background");

  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Top");
}

void psd_reader_preserves_layer_group_hierarchy() {
  auto write_empty_section_record = [](photoslop::psd::BigEndianWriter& layer_info, const std::string& name,
                                       std::uint32_t section_type, const char (&blend_mode)[5]) {
    photoslop::psd::BigEndianWriter extra;
    extra.write_u32(0);
    extra.write_u32(0);
    write_pascal_padded(extra, name, 4);
    const auto payload =
        section_type == 3U ? section_divider_payload(section_type) : section_divider_payload(section_type, blend_mode);
    write_test_layer_block(extra, "lsct", payload);

    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u16(0);
    write_ascii4(layer_info, "8BIM");
    write_ascii4(layer_info, blend_mode);
    layer_info.write_u8(255);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u32(static_cast<std::uint32_t>(extra.bytes().size()));
    layer_info.write_bytes(extra.bytes());
  };

  auto write_pixel_record = [](photoslop::psd::BigEndianWriter& layer_info, const std::string& name) {
    photoslop::psd::BigEndianWriter extra;
    extra.write_u32(0);
    extra.write_u32(0);
    write_pascal_padded(extra, name, 4);

    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(1);
    layer_info.write_u32(1);
    layer_info.write_u16(3);
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      layer_info.write_u16(channel);
      layer_info.write_u32(3);
    }
    write_ascii4(layer_info, "8BIM");
    write_ascii4(layer_info, "norm");
    layer_info.write_u8(255);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u32(static_cast<std::uint32_t>(extra.bytes().size()));
    layer_info.write_bytes(extra.bytes());
  };

  auto write_pixel_channels = [](photoslop::psd::BigEndianWriter& layer_info, std::uint8_t red, std::uint8_t green,
                                 std::uint8_t blue) {
    layer_info.write_u16(0);
    layer_info.write_u8(red);
    layer_info.write_u16(0);
    layer_info.write_u8(green);
    layer_info.write_u16(0);
    layer_info.write_u8(blue);
  };

  photoslop::psd::BigEndianWriter layer_info;
  layer_info.write_u16(4);
  write_empty_section_record(layer_info, "</Layer group>", 3, "norm");
  write_pixel_record(layer_info, "Bottom Child");
  write_pixel_record(layer_info, "Top Child");
  write_empty_section_record(layer_info, "Folder", 2, "pass");
  write_pixel_channels(layer_info, 180, 20, 20);
  write_pixel_channels(layer_info, 20, 40, 220);
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  photoslop::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  photoslop::psd::BigEndianWriter writer;
  photoslop::psd::write_header(writer, photoslop::psd::Header{false, 3, 1, 1, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  writer.write_u8(20);
  writer.write_u8(40);
  writer.write_u8(220);

  const auto read = photoslop::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto& folder = read.layers().front();
  CHECK(folder.kind() == photoslop::LayerKind::Group);
  CHECK(folder.name() == "Folder");
  CHECK(folder.blend_mode() == photoslop::BlendMode::PassThrough);
  CHECK(folder.metadata().at("photoslop.layer_group_expanded") == "false");
  CHECK(folder.children().size() == 2);
  CHECK(folder.children()[0].name() == "Bottom Child");
  CHECK(folder.children()[1].name() == "Top Child");
  CHECK(read.find_layer(folder.children()[0].id()) == &folder.children()[0]);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(read);
  const auto* px = flattened.pixel(0, 0);
  CHECK(px[0] == 20);
  CHECK(px[1] == 40);
  CHECK(px[2] == 220);
}

void psd_writer_round_trips_layer_groups() {
  photoslop::Document document(2, 2, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 255, 255, 255));

  photoslop::Layer group(document.allocate_layer_id(), "Folder", photoslop::LayerKind::Group);
  group.set_blend_mode(photoslop::BlendMode::PassThrough);
  group.metadata()["photoslop.layer_group_expanded"] = "false";
  group.add_child(photoslop::Layer(document.allocate_layer_id(), "Bottom Child",
                                   solid_rgba(2, 2, 180, 20, 20, 255)));
  group.add_child(photoslop::Layer(document.allocate_layer_id(), "Top Child",
                                   solid_rgba(2, 2, 20, 40, 220, 192)));
  document.add_layer(std::move(group));
  document.add_pixel_layer("Foreground", solid_rgba(2, 2, 10, 200, 40, 128));

  const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(document);
  const auto names = psd_raw_layer_record_names(bytes);
  CHECK(names.size() == 6);
  CHECK(names[0] == "Background");
  CHECK(names[1] == "</Layer group>");
  CHECK(names[2] == "Bottom Child");
  CHECK(names[3] == "Top Child");
  CHECK(names[4] == "Folder");
  CHECK(names[5] == "Foreground");

  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 3);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].kind() == photoslop::LayerKind::Group);
  CHECK(read.layers()[1].name() == "Folder");
  CHECK(read.layers()[1].blend_mode() == photoslop::BlendMode::PassThrough);
  CHECK(read.layers()[1].metadata().at("photoslop.layer_group_expanded") == "false");
  CHECK(read.layers()[1].children().size() == 2);
  CHECK(read.layers()[1].children()[0].name() == "Bottom Child");
  CHECK(read.layers()[1].children()[1].name() == "Top Child");
  CHECK(read.layers()[2].name() == "Foreground");

  const auto read_again = photoslop::psd::DocumentIo::read(photoslop::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers().size() == 3);
  CHECK(read_again.layers()[1].kind() == photoslop::LayerKind::Group);
  CHECK(read_again.layers()[1].children().size() == 2);
  CHECK(read_again.layers()[1].children()[1].name() == "Top Child");
}

void psd_ipad_main_v04_preserves_folders_if_available() {
  const auto path = std::filesystem::path("D:/projects/proton/RTDink/media/interface/ipad/ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = photoslop::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 768);
  CHECK(document.layers().size() == 5);
  CHECK(document.layers()[0].name() == "BG");
  CHECK(document.layers()[2].name() == "Buttons");
  CHECK(document.layers()[4].name() == "RT Soft small");

  std::function<const photoslop::Layer*(const std::vector<photoslop::Layer>&, const std::string&)> find_group =
      [&](const std::vector<photoslop::Layer>& layers, const std::string& name) -> const photoslop::Layer* {
    for (const auto& layer : layers) {
      if (layer.kind() == photoslop::LayerKind::Group && layer.name() == name) {
        return &layer;
      }
      if (const auto* found = find_group(layer.children(), name); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };

  const auto* bg = find_group(document.layers(), "BG");
  const auto* fire = find_group(document.layers(), "Fire");
  const auto* buttons = find_group(document.layers(), "Buttons");
  CHECK(bg != nullptr);
  CHECK(fire != nullptr);
  CHECK(buttons != nullptr);
  CHECK(bg->children().size() == 6);
  CHECK(fire->children().size() == 4);
  CHECK(buttons->children().size() == 14);
  CHECK(buttons->children().front().name() == "Add-on Quests");
  CHECK(buttons->children().back().name() == "Quit copy");
}

void psd_writer_preserves_layer_additional_blocks_and_long_names() {
  const std::string long_name = "Long Photoshop layer name " + std::string(280, 'X');
  const std::string text = "Editable text survives";
  const std::string engine_data =
      "/EngineData << /Editor << /Text (" + text + "\\r) >> /StyleRun << /StyleSheetData << /FontSize 42 >> >> >>";
  const auto text_payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());
  const std::vector<std::uint8_t> custom_payload{9, 8, 7, 6, 5};

  photoslop::Document document(3, 2, photoslop::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer(long_name, solid_rgba(3, 2, 20, 40, 60, 255));
  layer.unknown_psd_blocks().push_back(photoslop::UnknownPsdBlock{"zzzz", custom_payload});
  layer.unknown_psd_blocks().push_back(photoslop::UnknownPsdBlock{"TySh", text_payload});

  const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = photoslop::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().name() == long_name);
  CHECK(read.layers().front().metadata().at("photoslop.text") == text);
  CHECK(read.layers().front().metadata().at("photoslop.text.size") == "42");

  bool found_custom = false;
  bool found_text = false;
  bool found_unicode_name = false;
  for (const auto& block : read.layers().front().unknown_psd_blocks()) {
    if (block.key == "zzzz" && block.payload == custom_payload) {
      found_custom = true;
    }
    if (block.key == "TySh" && block.payload == text_payload) {
      found_text = true;
    }
    if (block.key == "luni") {
      found_unicode_name = true;
    }
  }
  CHECK(found_custom);
  CHECK(found_text);
  CHECK(found_unicode_name);

  const auto read_again = photoslop::psd::DocumentIo::read(photoslop::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers().size() == 1);
  CHECK(read_again.layers().front().name() == long_name);
  CHECK(read_again.layers().front().metadata().at("photoslop.text") == text);
}

void psd_extended_blend_modes_round_trip() {
  const std::vector<photoslop::BlendMode> modes = {
      photoslop::BlendMode::Darken,     photoslop::BlendMode::Lighten,
      photoslop::BlendMode::ColorDodge, photoslop::BlendMode::ColorBurn,
      photoslop::BlendMode::HardLight,  photoslop::BlendMode::SoftLight,
      photoslop::BlendMode::Difference, photoslop::BlendMode::LinearBurn,
      photoslop::BlendMode::PinLight,   photoslop::BlendMode::Saturation,
      photoslop::BlendMode::Luminosity,
  };

  for (const auto mode : modes) {
    photoslop::Document document(2, 2, photoslop::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(2, 2, 120, 120, 120));
    auto& top = document.add_pixel_layer("Top", solid_rgba(2, 2, 200, 60, 100, 255));
    top.set_blend_mode(mode);

    const auto bytes = photoslop::psd::DocumentIo::write_layered_rgb8(document);
    const auto read = photoslop::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 2);
    CHECK(read.layers()[1].blend_mode() == mode);
  }
}

void psd_text_layer_engine_data_renders_placeholder_text() {
  const std::string text = "Photoslop Text";
  const std::string engine_data =
      "/EngineData << /Editor << /Text (" + text + "\\r) >> /StyleRun << /StyleSheetData << /FontSize 36 >> >> >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  photoslop::psd::BigEndianWriter layer_extra;
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

  photoslop::psd::BigEndianWriter layer_info;
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

  photoslop::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 220;
  constexpr std::uint32_t height = 80;
  photoslop::psd::BigEndianWriter writer;
  photoslop::psd::write_header(writer, photoslop::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }

  const auto read = photoslop::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  CHECK(layer.name() == "Text Layer");
  CHECK(layer.metadata().at("photoslop.text") == text);
  CHECK(layer.metadata().at("photoslop.text.size") == "36");
  CHECK(layer.pixels().format() == photoslop::PixelFormat::rgba8());
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

void psd_arduboy_real_file_renders_if_available() {
  const auto path = std::filesystem::path("D:/projects/C2/MiscPrints/Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = photoslop::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 2550);
  CHECK(document.height() == 3300);
  CHECK(document.layers().size() == 4);

  int text_layers = 0;
  for (const auto& layer : document.layers()) {
    if (layer.metadata().contains("photoslop.text")) {
      ++text_layers;
    }
  }
  CHECK(text_layers >= 2);

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
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
  const auto path = std::filesystem::path("D:/projects/DungeonScroll/media/Demo/Title Screen_demo.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = photoslop::psd::DocumentIo::read_file(path);
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

  const auto flattened = photoslop::Compositor{}.flatten_rgb8(document);
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

void tool_brush_draws_color_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = photoslop::paint_brush(document, layer_id, 20, 20, tool_options(12, 140, 240), false);
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[0] == 12);
  CHECK(px[1] == 140);
  CHECK(px[2] == 240);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_brush", document);
}

void tool_brush_opacity_and_bounded_layer_expansion_work() {
  photoslop::Document document(64, 48, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  photoslop::Layer pasted(document.allocate_layer_id(), "Pasted", solid_rgba(8, 8, 0, 0, 0, 0));
  pasted.pixels().pixel(0, 0)[0] = 30;
  pasted.pixels().pixel(0, 0)[1] = 40;
  pasted.pixels().pixel(0, 0)[2] = 50;
  pasted.pixels().pixel(0, 0)[3] = 255;
  pasted.set_bounds(photoslop::Rect{10, 10, 8, 8});
  const auto layer_id = pasted.id();
  document.add_layer(std::move(pasted));

  auto options = tool_options(240, 20, 30);
  options.brush_size = 5;
  options.primary.a = 128;
  const auto dirty = photoslop::paint_brush(document, layer_id, 44, 32, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(44, 32));
  CHECK(layer->bounds().contains(10, 10));
  CHECK(layer->pixels().pixel(10 - layer->bounds().x, 10 - layer->bounds().y)[3] == 255);
  const auto* painted = layer->pixels().pixel(44 - layer->bounds().x, 32 - layer->bounds().y);
  CHECK(painted[0] == 240);
  CHECK(painted[1] == 20);
  CHECK(painted[2] == 30);
  CHECK(painted[3] >= 120);
  CHECK(painted[3] <= 136);
  write_bmp_artifact("tool_brush_expand_layer", document);
}

void tool_brush_softness_feathers_edge_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(30, 90, 240);
  options.brush_size = 21;
  options.brush_softness = 100;

  const auto dirty = photoslop::paint_brush(document, layer_id, 24, 24, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  const auto* center = pixels.pixel(24, 24);
  const auto* feather = pixels.pixel(32, 24);
  const auto* outside = pixels.pixel(35, 24);
  CHECK(center[0] == 30);
  CHECK(center[1] == 90);
  CHECK(center[2] == 240);
  CHECK(center[3] == 255);
  CHECK(feather[0] == 30);
  CHECK(feather[1] == 90);
  CHECK(feather[2] == 240);
  CHECK(feather[3] > 0);
  CHECK(feather[3] < 255);
  CHECK(outside[3] == 0);
  write_bmp_artifact("tool_soft_brush", document);
}

void tool_wide_brush_segment_is_fast_and_writes_artifact() {
  photoslop::Document document(1600, 1000, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1600, 1000, 255, 255, 255));
  photoslop::PixelBuffer pixels(1600, 1000, photoslop::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Paint", std::move(pixels)).id();

  auto options = tool_options(20, 80, 230);
  options.brush_size = 240;
  const auto started = std::chrono::steady_clock::now();
  const auto dirty = photoslop::paint_brush_segment(document, layer_id, 140, 500, 1460, 500, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(800, 500));
  const auto* center = layer->pixels().pixel(800 - layer->bounds().x, 500 - layer->bounds().y);
  CHECK(center[0] == 20);
  CHECK(center[1] == 80);
  CHECK(center[2] == 230);
  CHECK(center[3] == 255);
  write_bmp_artifact("tool_wide_brush_segment", document);
}

void tool_eraser_clears_alpha_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options();
  CHECK(!photoslop::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(!photoslop::paint_brush(document, layer_id, 20, 20, options, true).empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[3] == 0);
  write_bmp_artifact("tool_eraser", document);
}

void tool_eraser_converts_rgb_layer_to_transparency() {
  photoslop::Document document(12, 12, photoslop::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Background", solid_rgb(12, 12, 255, 255, 255)).id();
  auto options = tool_options();
  options.brush_size = 5;
  CHECK(!photoslop::paint_brush(document, layer_id, 6, 6, options, true).empty());
  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.format() == photoslop::PixelFormat::rgba8());
  CHECK(pixels.pixel(6, 6)[3] == 0);
  CHECK(pixels.pixel(0, 0)[3] == 255);
}

void tool_line_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = photoslop::draw_line(document, layer_id, 5, 5, 55, 40, tool_options(20, 180, 80), false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(30, 22)[3] > 0);
  write_bmp_artifact("tool_line", document);
}

void tool_rectangle_draws_outline_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  options.brush_size = 3;
  const auto dirty = photoslop::draw_rectangle(document, layer_id, photoslop::Rect{10, 8, 28, 20}, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 8)[3] > 0);
  write_bmp_artifact("tool_rectangle", document);
}

void tool_ellipse_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(150, 40, 220);
  options.brush_size = 3;
  const auto dirty = photoslop::draw_ellipse(document, layer_id, photoslop::Rect{12, 10, 30, 22}, options, false);
  CHECK(!dirty.empty());
  write_bmp_artifact("tool_ellipse", document);
}

void tool_filled_ellipse_uses_direct_fill_and_writes_artifact() {
  photoslop::Document document(1200, 900, photoslop::PixelFormat::rgb8());
  photoslop::PixelBuffer pixels(1200, 900, photoslop::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Filled Ellipse", std::move(pixels)).id();

  auto options = tool_options(20, 150, 240);
  options.fill_shapes = true;
  options.brush_size = 96;

  const auto started = std::chrono::steady_clock::now();
  const auto dirty = photoslop::draw_ellipse(document, layer_id, photoslop::Rect{120, 90, 900, 620}, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto& filled = document.find_layer(layer_id)->pixels();
  CHECK(filled.pixel(570, 400)[0] == 20);
  CHECK(filled.pixel(570, 400)[1] == 150);
  CHECK(filled.pixel(570, 400)[2] == 240);
  CHECK(filled.pixel(570, 400)[3] == 255);
  CHECK(filled.pixel(120, 90)[3] == 0);
  write_bmp_artifact("tool_filled_ellipse", document);
}

void tool_fill_bucket_fills_region_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = photoslop::flood_fill(document, layer_id, 10, 10, tool_options(0, 180, 210));
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(10, 10);
  CHECK(px[0] == 0);
  CHECK(px[1] == 180);
  CHECK(px[2] == 210);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_fill_bucket", document);
}

void tool_gradient_draws_foreground_to_background_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  options.secondary = photoslop::EditColor{0, 0, 255, 255};
  const auto dirty = photoslop::draw_linear_gradient(document, layer_id, 0, 0, 63, 0, options);
  CHECK(!dirty.empty());
  const auto* left = document.find_layer(layer_id)->pixels().pixel(0, 20);
  const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
  CHECK(left[0] == 255);
  CHECK(left[1] == 0);
  CHECK(left[2] == 0);
  CHECK(right[0] == 0);
  CHECK(right[1] == 0);
  CHECK(right[2] == 255);
  CHECK(right[3] == 255);
  write_bmp_artifact("tool_gradient", document);
}

void tool_fill_selection_draws_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  options.selection = photoslop::Rect{8, 8, 16, 12};
  const auto dirty = photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options);
  CHECK(dirty.x == 8);
  CHECK(dirty.y == 8);
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 0);
  write_bmp_artifact("tool_fill_selection", document);
}

void tool_clear_selection_erases_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options).empty());
  options.selection = photoslop::Rect{8, 8, 16, 12};
  const auto dirty = photoslop::clear_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 255);
  write_bmp_artifact("tool_clear_selection", document);
}

void tool_fill_clear_gradient_respect_complex_selection_mask() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(250, 20, 40);
  options.selection = photoslop::Rect{4, 4, 24, 24};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return (x >= 4 && x < 12) || (y >= 20 && y < 28); };

  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 255);

  options.selection_mask = [](std::int32_t, std::int32_t y) { return y >= 20 && y < 28; };
  CHECK(!photoslop::clear_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);

  options.primary = photoslop::EditColor{0, 0, 255, 255};
  options.secondary = photoslop::EditColor{0, 255, 0, 255};
  options.selection = photoslop::Rect{0, 0, 64, 48};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return x >= 40 && y < 20; };
  CHECK(!photoslop::draw_linear_gradient(document, layer_id, 40, 0, 63, 0, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 24)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  write_bmp_artifact("tool_complex_selection_mask_ops", document);
}

void tool_lock_transparent_pixels_preserves_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(200, 30, 40);
  options.lock_transparent_pixels = true;
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 10)[3] == 0);

  options.lock_transparent_pixels = false;
  CHECK(!photoslop::paint_brush(document, layer_id, 20, 20, options, false).empty());
  auto* painted = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(painted[0] == 200);
  CHECK(painted[3] == 255);

  options.primary = photoslop::EditColor{20, 90, 220, 255};
  options.lock_transparent_pixels = true;
  CHECK(!photoslop::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[0] == 20);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  CHECK(!photoslop::paint_brush(document, layer_id, 4, 4, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(4, 4)[3] == 0);

  CHECK(!photoslop::clear_rect(document, layer_id, photoslop::Rect{18, 18, 6, 6}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  write_bmp_artifact("tool_lock_transparency", document);
}

void tool_flip_horizontal_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 8, 48}, options).empty());
  CHECK(!photoslop::flip_layer_horizontal(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(63, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 10)[3] == 0);
  write_bmp_artifact("tool_flip_horizontal", document);
}

void tool_flip_vertical_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 0, 255);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 64, 8}, options).empty());
  CHECK(!photoslop::flip_layer_vertical(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 47)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 0)[3] == 0);
  write_bmp_artifact("tool_flip_vertical", document);
}

void document_crop_to_selection_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 180);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{12, 8, 4, 4}, options).empty());
  CHECK(photoslop::crop_document(document, photoslop::Rect{8, 6, 32, 20}));
  CHECK(document.width() == 32);
  CHECK(document.height() == 20);
  const auto* px = document.find_layer(layer_id)->pixels().pixel(4, 2);
  CHECK(px[0] == 255);
  CHECK(px[1] == 0);
  CHECK(px[2] == 180);
  CHECK(px[3] == 255);
  write_bmp_artifact("document_crop", document);
}

void document_canvas_resize_expands_layers_for_editing() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(10, 90, 220);
  CHECK(!photoslop::paint_brush(document, layer_id, 20, 20, options, false).empty());

  photoslop::resize_canvas_and_layers(document, 96, 72);
  CHECK(document.width() == 96);
  CHECK(document.height() == 72);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 0);
  CHECK(layer->bounds().y == 0);
  CHECK(layer->pixels().width() == 96);
  CHECK(layer->pixels().height() == 72);
  CHECK(layer->pixels().pixel(20, 20)[3] == 255);

  CHECK(!photoslop::paint_brush(document, layer_id, 90, 66, options, false).empty());
  CHECK(layer->pixels().pixel(90, 66)[3] == 255);
  write_bmp_artifact("document_canvas_resize", document);
}

void document_rotate_clockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 8, 6}, options).empty());
  photoslop::rotate_document_clockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 0)[3] == 0);
  write_bmp_artifact("document_rotate_clockwise", document);
}

void document_rotate_counterclockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 180, 255);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{0, 0, 8, 6}, options).empty());
  photoslop::rotate_document_counterclockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 63)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 0);
  write_bmp_artifact("document_rotate_counterclockwise", document);
}

void tool_stroke_selection_draws_border_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 20, 20);
  options.brush_size = 3;
  options.selection = photoslop::Rect{14, 10, 30, 22};
  const auto dirty = photoslop::draw_rectangle(document, layer_id, *options.selection, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(14, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 16)[3] == 0);
  write_bmp_artifact("tool_stroke_selection", document);
}

void layer_merge_visible_creates_flattened_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 120, 255);
  CHECK(!photoslop::fill_rect(document, layer_id, photoslop::Rect{4, 4, 24, 18}, options).empty());
  auto merged_pixels = photoslop::Compositor{}.flatten_rgb8(document);
  document.add_pixel_layer("Merged Visible", std::move(merged_pixels));
  CHECK(document.layers().size() == 3);
  CHECK(document.layers().back().pixels().format() == photoslop::PixelFormat::rgb8());
  CHECK(document.layers().back().pixels().pixel(5, 5)[2] == 255);
  write_bmp_artifact("layer_merge_visible", document);
}

void filters_register_and_apply() {
  photoslop::FilterRegistry registry;
  photoslop::register_builtin_filters(registry);
  CHECK(registry.find("photoslop.filters.invert") != nullptr);
  CHECK(registry.find("photoslop.filters.brightness_plus") != nullptr);
  CHECK(registry.find("photoslop.filters.contrast_plus") != nullptr);
  CHECK(registry.find("photoslop.filters.grayscale") != nullptr);
  CHECK(registry.find("photoslop.filters.desaturate") != nullptr);
  CHECK(registry.find("photoslop.filters.auto_contrast") != nullptr);
  CHECK(registry.find("photoslop.filters.sepia") != nullptr);
  CHECK(registry.find("photoslop.filters.threshold") != nullptr);
  CHECK(registry.find("photoslop.filters.posterize") != nullptr);
  CHECK(registry.find("photoslop.filters.box_blur") != nullptr);
  CHECK(registry.find("photoslop.filters.sharpen") != nullptr);
  CHECK(registry.find("photoslop.filters.gaussian_blur") != nullptr);
  CHECK(registry.find("photoslop.filters.edge_detect") != nullptr);
  CHECK(registry.find("photoslop.filters.emboss") != nullptr);
  CHECK(registry.find("photoslop.filters.pixelate") != nullptr);
  CHECK(registry.find("photoslop.filters.film_grain") != nullptr);
  CHECK(registry.find("photoslop.filters.vignette") != nullptr);

  auto pixels = solid_rgb(1, 1, 1, 2, 3);
  registry.apply("photoslop.filters.invert", pixels);
  const auto* px = pixels.pixel(0, 0);
  CHECK(px[0] == 254);
  CHECK(px[1] == 253);
  CHECK(px[2] == 252);
}

void filters_builtin_effects_apply_and_write_artifacts() {
  photoslop::FilterRegistry registry;
  photoslop::register_builtin_filters(registry);

  const std::vector<std::pair<std::string, std::string>> filters = {
      {"photoslop.filters.brightness_plus", "filter_brightness"},
      {"photoslop.filters.contrast_plus", "filter_contrast"},
      {"photoslop.filters.grayscale", "filter_grayscale"},
      {"photoslop.filters.desaturate", "filter_desaturate"},
      {"photoslop.filters.auto_contrast", "filter_auto_contrast"},
      {"photoslop.filters.sepia", "filter_sepia"},
      {"photoslop.filters.threshold", "filter_threshold"},
      {"photoslop.filters.posterize", "filter_posterize"},
      {"photoslop.filters.box_blur", "filter_box_blur"},
      {"photoslop.filters.sharpen", "filter_sharpen"},
      {"photoslop.filters.gaussian_blur", "filter_gaussian_blur"},
      {"photoslop.filters.edge_detect", "filter_edge_detect"},
      {"photoslop.filters.emboss", "filter_emboss"},
      {"photoslop.filters.pixelate", "filter_pixelate"},
      {"photoslop.filters.film_grain", "filter_film_grain"},
      {"photoslop.filters.vignette", "filter_vignette"},
  };

  for (const auto& [identifier, artifact_name] : filters) {
    auto document = make_filter_document();
    auto& pixels = document.layers().front().pixels();
    registry.apply(identifier, pixels);
    CHECK(!pixels.empty());
    write_bmp_artifact(artifact_name, document);
  }

  auto brightness = make_filter_document();
  registry.apply("photoslop.filters.brightness_plus", brightness.layers().front().pixels());
  const auto* bright_px = brightness.layers().front().pixels().pixel(0, 0);
  CHECK(bright_px[0] == 24);
  CHECK(bright_px[1] == 24);
  CHECK(bright_px[2] == 104);

  auto threshold = make_filter_document();
  registry.apply("photoslop.filters.threshold", threshold.layers().front().pixels());
  const auto* threshold_px = threshold.layers().front().pixels().pixel(0, 0);
  CHECK(threshold_px[0] == 0);
  CHECK(threshold_px[1] == 0);
  CHECK(threshold_px[2] == 0);

  auto desaturate = make_filter_document();
  registry.apply("photoslop.filters.desaturate", desaturate.layers().front().pixels());
  const auto* desaturated_px = desaturate.layers().front().pixels().pixel(3, 2);
  CHECK(desaturated_px[0] == desaturated_px[1]);
  CHECK(desaturated_px[1] == desaturated_px[2]);

  auto auto_contrast = make_filter_document();
  registry.apply("photoslop.filters.auto_contrast", auto_contrast.layers().front().pixels());
  const auto* low_px = auto_contrast.layers().front().pixels().pixel(0, 0);
  const auto* high_px = auto_contrast.layers().front().pixels().pixel(31, 23);
  CHECK(low_px[0] == 0);
  CHECK(low_px[1] == 0);
  CHECK(low_px[2] == 0);
  CHECK(high_px[0] == 255);
  CHECK(high_px[1] == 255);
  CHECK(high_px[2] == 255);

  auto pin_blur = photoslop::PixelBuffer(5, 5, photoslop::PixelFormat::rgb8());
  pin_blur.pixel(2, 2)[0] = 255;
  pin_blur.pixel(2, 2)[1] = 255;
  pin_blur.pixel(2, 2)[2] = 255;
  registry.apply("photoslop.filters.gaussian_blur", pin_blur);
  CHECK(pin_blur.pixel(2, 2)[0] > pin_blur.pixel(1, 2)[0]);
  CHECK(pin_blur.pixel(1, 2)[0] > 0);
  CHECK(pin_blur.pixel(2, 2)[0] < 255);

  auto edge = solid_rgb(3, 3, 0, 0, 0);
  for (std::int32_t y = 0; y < edge.height(); ++y) {
    for (std::int32_t x = 1; x < edge.width(); ++x) {
      auto* px = edge.pixel(x, y);
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
    }
  }
  registry.apply("photoslop.filters.edge_detect", edge);
  CHECK(edge.pixel(1, 1)[0] == 255);
  CHECK(edge.pixel(1, 1)[1] == 255);
  CHECK(edge.pixel(1, 1)[2] == 255);

  auto relief = solid_rgb(3, 3, 100, 110, 120);
  registry.apply("photoslop.filters.emboss", relief);
  CHECK(relief.pixel(1, 1)[0] == 128);
  CHECK(relief.pixel(1, 1)[1] == 128);
  CHECK(relief.pixel(1, 1)[2] == 128);

  auto pixelated = make_filter_document();
  registry.apply("photoslop.filters.pixelate", pixelated.layers().front().pixels());
  const auto* pixelated_px = pixelated.layers().front().pixels().pixel(0, 0);
  CHECK(pixelated_px[0] == 12);
  CHECK(pixelated_px[1] == 15);
  CHECK(pixelated_px[2] == 83);

  auto grain_a = solid_rgb(2, 2, 128, 128, 128);
  auto grain_b = solid_rgb(2, 2, 128, 128, 128);
  registry.apply("photoslop.filters.film_grain", grain_a);
  registry.apply("photoslop.filters.film_grain", grain_b);
  bool grain_changed = false;
  for (std::int32_t y = 0; y < grain_a.height(); ++y) {
    for (std::int32_t x = 0; x < grain_a.width(); ++x) {
      const auto* a = grain_a.pixel(x, y);
      const auto* b = grain_b.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        CHECK(a[channel] == b[channel]);
        grain_changed = grain_changed || a[channel] != 128;
      }
    }
  }
  CHECK(grain_changed);

  auto vignetted = solid_rgb(5, 5, 255, 255, 255);
  registry.apply("photoslop.filters.vignette", vignetted);
  CHECK(vignetted.pixel(2, 2)[0] == 255);
  CHECK(vignetted.pixel(0, 0)[0] < 130);
}

void format_registry_finds_psd() {
  photoslop::FormatRegistry registry;
  photoslop::register_builtin_formats(registry);
  CHECK(registry.find_by_extension(".psd") != nullptr);
  CHECK(registry.find_by_extension("PSB") != nullptr);
}

void plugin_host_and_legacy_probe_work() {
  photoslop::PluginHost host;
  host.register_plugin({PHOTOSLOP_PLUGIN_FILTER, "com.photoslop.test", "Test", 1, 0, 0, {}});
  CHECK(host.find("com.photoslop.test") != nullptr);
  CHECK(host.plugins_by_kind(PHOTOSLOP_PLUGIN_FILTER).size() == 1);

  const auto fixture = std::filesystem::path("test_sample_filter.8bf");
  {
    std::vector<std::uint8_t> bytes(512, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = 0x80;
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
#if defined(_M_IX86) || defined(__i386__)
    bytes[0x84] = 0x4c;
    bytes[0x85] = 0x01;
#elif defined(_M_ARM64) || defined(__aarch64__)
    bytes[0x84] = 0x64;
    bytes[0x85] = 0xaa;
#else
    bytes[0x84] = 0x64;
    bytes[0x85] = 0x86;
#endif
    std::ofstream output(fixture, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  photoslop::LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(fixture);
  CHECK(probe.supported);
  CHECK(probe.kind == photoslop::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(!probe.architecture.empty());
  std::filesystem::remove(fixture);

#ifdef PHOTOSLOP_SOURCE_DIR
  const auto real_plugin =
      std::filesystem::path(PHOTOSLOP_SOURCE_DIR) / "test-fixtures" / "photoshop-plugins" / "Greyscale64.8bf";
  CHECK(std::filesystem::exists(real_plugin));
  const auto real_probe = adapter.probe(real_plugin);
  CHECK(real_probe.kind == photoslop::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(real_probe.architecture == "x64");
#endif
}

void tile_cache_stores_and_invalidates() {
  photoslop::TileCache cache(128);
  photoslop::TileKey key{0, 0, 0};
  cache.put(key, solid_rgb(2, 2, 9, 8, 7));
  CHECK(cache.find(key).has_value());
  cache.invalidate(key);
  CHECK(!cache.find(key).has_value());
}

void color_manager_assigns_profiles() {
  photoslop::Document document(1, 1, photoslop::PixelFormat::rgb8());
  photoslop::ColorManager manager;
  manager.assign_icc_profile(document, {1, 2, 3});
  CHECK(document.color_state().embedded_icc_profile.size() == 3);
}

}  // namespace

int main() {
  const std::vector<TestCase> tests = {
      {"pixel_buffer_tracks_shape_and_rows", pixel_buffer_tracks_shape_and_rows},
      {"document_adds_and_finds_layers", document_adds_and_finds_layers},
      {"document_removes_layers_and_updates_active_layer", document_removes_layers_and_updates_active_layer},
      {"compositor_flattens_visible_layers", compositor_flattens_visible_layers},
      {"compositor_applies_extended_blend_modes", compositor_applies_extended_blend_modes},
      {"compositor_renders_layer_style_drop_shadow_gradient_and_stroke",
       compositor_renders_layer_style_drop_shadow_gradient_and_stroke},
      {"compositor_renders_layer_style_bevel_emboss", compositor_renders_layer_style_bevel_emboss},
      {"compositor_renders_layer_style_outer_glow", compositor_renders_layer_style_outer_glow},
      {"psd_flat_rgb8_round_trips", psd_flat_rgb8_round_trips},
      {"psd_flat_rle_rgb8_reads", psd_flat_rle_rgb8_reads},
      {"psd_image_resources_round_trip_and_icc_profile_is_exposed",
       psd_image_resources_round_trip_and_icc_profile_is_exposed},
      {"psd_layered_rgb8_round_trips_pixel_layers", psd_layered_rgb8_round_trips_pixel_layers},
      {"psd_layer_masks_render_and_round_trip", psd_layer_masks_render_and_round_trip},
      {"psd_layer_styles_round_trip_photoslop_effects", psd_layer_styles_round_trip_photoslop_effects},
      {"psd_writer_uses_photoshop_bottom_to_top_layer_record_order",
       psd_writer_uses_photoshop_bottom_to_top_layer_record_order},
      {"psd_reader_tolerates_legacy_photoslop_top_to_bottom_background_files",
       psd_reader_tolerates_legacy_photoslop_top_to_bottom_background_files},
      {"psd_reader_preserves_layer_group_hierarchy", psd_reader_preserves_layer_group_hierarchy},
      {"psd_writer_round_trips_layer_groups", psd_writer_round_trips_layer_groups},
      {"psd_ipad_main_v04_preserves_folders_if_available", psd_ipad_main_v04_preserves_folders_if_available},
      {"psd_writer_preserves_layer_additional_blocks_and_long_names",
       psd_writer_preserves_layer_additional_blocks_and_long_names},
      {"psd_extended_blend_modes_round_trip", psd_extended_blend_modes_round_trip},
      {"psd_text_layer_engine_data_renders_placeholder_text",
       psd_text_layer_engine_data_renders_placeholder_text},
      {"psd_arduboy_real_file_renders_if_available", psd_arduboy_real_file_renders_if_available},
      {"psd_title_screen_demo_layer_styles_render_if_available",
       psd_title_screen_demo_layer_styles_render_if_available},
      {"tool_brush_draws_color_and_writes_artifact", tool_brush_draws_color_and_writes_artifact},
      {"tool_brush_opacity_and_bounded_layer_expansion_work",
       tool_brush_opacity_and_bounded_layer_expansion_work},
      {"tool_brush_softness_feathers_edge_alpha", tool_brush_softness_feathers_edge_alpha},
      {"tool_wide_brush_segment_is_fast_and_writes_artifact",
       tool_wide_brush_segment_is_fast_and_writes_artifact},
      {"tool_eraser_clears_alpha_and_writes_artifact", tool_eraser_clears_alpha_and_writes_artifact},
      {"tool_eraser_converts_rgb_layer_to_transparency", tool_eraser_converts_rgb_layer_to_transparency},
      {"tool_line_draws_and_writes_artifact", tool_line_draws_and_writes_artifact},
      {"tool_rectangle_draws_outline_and_writes_artifact", tool_rectangle_draws_outline_and_writes_artifact},
      {"tool_ellipse_draws_and_writes_artifact", tool_ellipse_draws_and_writes_artifact},
      {"tool_filled_ellipse_uses_direct_fill_and_writes_artifact",
       tool_filled_ellipse_uses_direct_fill_and_writes_artifact},
      {"tool_fill_bucket_fills_region_and_writes_artifact", tool_fill_bucket_fills_region_and_writes_artifact},
      {"tool_gradient_draws_foreground_to_background_and_writes_artifact",
       tool_gradient_draws_foreground_to_background_and_writes_artifact},
      {"tool_fill_selection_draws_only_selection_and_writes_artifact", tool_fill_selection_draws_only_selection_and_writes_artifact},
      {"tool_clear_selection_erases_only_selection_and_writes_artifact", tool_clear_selection_erases_only_selection_and_writes_artifact},
      {"tool_fill_clear_gradient_respect_complex_selection_mask",
       tool_fill_clear_gradient_respect_complex_selection_mask},
      {"tool_lock_transparent_pixels_preserves_alpha", tool_lock_transparent_pixels_preserves_alpha},
      {"tool_flip_horizontal_changes_pixels_and_writes_artifact", tool_flip_horizontal_changes_pixels_and_writes_artifact},
      {"tool_flip_vertical_changes_pixels_and_writes_artifact", tool_flip_vertical_changes_pixels_and_writes_artifact},
      {"document_crop_to_selection_changes_canvas_and_writes_artifact",
       document_crop_to_selection_changes_canvas_and_writes_artifact},
      {"document_canvas_resize_expands_layers_for_editing", document_canvas_resize_expands_layers_for_editing},
      {"document_rotate_clockwise_changes_canvas_and_writes_artifact",
       document_rotate_clockwise_changes_canvas_and_writes_artifact},
      {"document_rotate_counterclockwise_changes_canvas_and_writes_artifact",
       document_rotate_counterclockwise_changes_canvas_and_writes_artifact},
      {"tool_stroke_selection_draws_border_and_writes_artifact", tool_stroke_selection_draws_border_and_writes_artifact},
      {"layer_merge_visible_creates_flattened_artifact", layer_merge_visible_creates_flattened_artifact},
      {"filters_register_and_apply", filters_register_and_apply},
      {"filters_builtin_effects_apply_and_write_artifacts", filters_builtin_effects_apply_and_write_artifacts},
      {"format_registry_finds_psd", format_registry_finds_psd},
      {"plugin_host_and_legacy_probe_work", plugin_host_and_legacy_probe_work},
      {"tile_cache_stores_and_invalidates", tile_cache_stores_and_invalidates},
      {"color_manager_assigns_profiles", color_manager_assigns_profiles},
  };

  int failures = 0;
  for (const auto& test : tests) {
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    }
  }

  return failures == 0 ? 0 : 1;
}
