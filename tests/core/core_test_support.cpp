#include "core_test_support.hpp"

namespace patchy::test {

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
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

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
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

patchy::Document make_tool_document() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  document.add_pixel_layer("Paint", solid_rgba(64, 48, 0, 0, 0, 0));
  return document;
}

patchy::EditOptions tool_options(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  patchy::EditOptions options;
  options.primary = patchy::EditColor{r, g, b, 255};
  options.secondary = patchy::EditColor{255, 255, 255, 255};
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

void write_rgb8_bmp_artifact(const std::string& name, const patchy::PixelBuffer& pixels) {
  CHECK(pixels.format().bit_depth == patchy::BitDepth::UInt8);
  CHECK(pixels.format().channels >= 3);
  const auto out_dir = std::filesystem::path("test-artifacts");
  std::filesystem::create_directories(out_dir);
  const auto path = out_dir / (name + ".bmp");
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not write test image artifact");
  }

  const auto row_stride = static_cast<std::uint32_t>(((pixels.width() * 3 + 3) / 4) * 4);
  const auto pixel_bytes = row_stride * static_cast<std::uint32_t>(pixels.height());
  const auto file_size = 14U + 40U + pixel_bytes;

  file.put('B');
  file.put('M');
  write_u32_le(file, file_size);
  write_u16_le(file, 0);
  write_u16_le(file, 0);
  write_u32_le(file, 14U + 40U);

  write_u32_le(file, 40U);
  write_u32_le(file, static_cast<std::uint32_t>(pixels.width()));
  write_u32_le(file, static_cast<std::uint32_t>(pixels.height()));
  write_u16_le(file, 1);
  write_u16_le(file, 24);
  write_u32_le(file, 0);
  write_u32_le(file, pixel_bytes);
  write_u32_le(file, 2835);
  write_u32_le(file, 2835);
  write_u32_le(file, 0);
  write_u32_le(file, 0);

  std::vector<std::uint8_t> padding(row_stride - static_cast<std::uint32_t>(pixels.width() * 3), 0);
  for (std::int32_t y = pixels.height() - 1; y >= 0; --y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      file.put(static_cast<char>(px[2]));
      file.put(static_cast<char>(px[1]));
      file.put(static_cast<char>(px[0]));
    }
    file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
  }
}

void write_bmp_artifact(const std::string& name, const patchy::Document& document) {
  write_rgb8_bmp_artifact(name, patchy::Compositor{}.flatten_rgb8(document));
}

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* found = find_layer_named(layer.children(), name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

bool close_float(float actual, float expected, float tolerance) {
  return std::abs(actual - expected) <= tolerance;
}

RgbDiffMetrics rgb_diff_metrics(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right) {
  CHECK(left.width() == right.width());
  CHECK(left.height() == right.height());
  CHECK(left.format().channels >= 3);
  CHECK(right.format().channels >= 3);

  RgbDiffMetrics metrics;
  metrics.pixels = static_cast<std::uint64_t>(left.width()) * static_cast<std::uint64_t>(left.height());
  std::uint64_t total_delta = 0;
  for (std::int32_t y = 0; y < left.height(); ++y) {
    for (std::int32_t x = 0; x < left.width(); ++x) {
      const auto* a = left.pixel(x, y);
      const auto* b = right.pixel(x, y);
      int pixel_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const auto delta = std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel]));
        total_delta += static_cast<std::uint64_t>(delta);
        pixel_delta += delta;
        metrics.max_channel_delta = std::max(metrics.max_channel_delta, delta);
      }
      if (pixel_delta > 0) {
        ++metrics.differing_pixels;
      }
    }
  }
  if (metrics.pixels > 0) {
    metrics.mean_abs_channel_delta =
        static_cast<double>(total_delta) / static_cast<double>(metrics.pixels * 3ULL);
  }
  return metrics;
}

patchy::LayerId active_tool_layer(const patchy::Document& document) {
  return document.active_layer_id().value();
}

const patchy::Layer& require_layer_named(const patchy::Document& document, const std::string& name) {
  const auto* layer = find_layer_named(document.layers(), name);
  CHECK(layer != nullptr);
  return *layer;
}

const patchy::SmartFilterStack& require_smart_filter_stack(const patchy::Document& document,
                                                           const std::string& name) {
  const auto& layer = require_layer_named(document, name);
  const auto* stack = layer.smart_filter_stack();
  CHECK(stack != nullptr);
  return *stack;
}

patchy::SmartFilterStack test_gaussian_smart_filter_stack(double radius) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::GaussianBlur;
  entry.native_name = "Gaussian Blur...";
  entry.native_class_id = "GsnB";
  entry.native_filter_id = 0x47736e42U;
  entry.parameters = patchy::GaussianBlurSmartFilter{radius};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack test_high_pass_smart_filter_stack(double radius) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::HighPass;
  entry.native_name = "High Pass...";
  entry.native_class_id = "HghP";
  entry.native_filter_id = 0x48676850U;
  entry.parameters = patchy::HighPassSmartFilter{radius};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack test_median_smart_filter_stack(double radius) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::Median;
  entry.native_name = "Median...";
  entry.native_class_id = "Mdn ";
  entry.native_filter_id = 0x4d646e20U;
  entry.parameters = patchy::MedianSmartFilter{radius};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack test_dust_and_scratches_smart_filter_stack(
    std::int32_t radius, std::int32_t threshold) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::DustAndScratches;
  entry.native_name = "Dust && Scratches...";
  entry.native_class_id = "DstS";
  entry.native_filter_id = 0x44737453U;
  entry.parameters =
      patchy::DustAndScratchesSmartFilter{radius, threshold};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack test_surface_blur_smart_filter_stack(
    double radius, std::int32_t threshold) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::SurfaceBlur;
  entry.native_name = "Surface Blur...";
  entry.native_class_id = "surfaceBlur";
  entry.native_filter_id = 854U;
  entry.parameters = patchy::SurfaceBlurSmartFilter{radius, threshold};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack
test_unsharp_mask_smart_filter_stack(double amount, double radius,
                                     std::int32_t threshold) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::UnsharpMask;
  entry.native_name = "Unsharp Mask...";
  entry.native_class_id = "UnsM";
  entry.native_filter_id = 0x556e734dU;
  entry.parameters = patchy::UnsharpMaskSmartFilter{amount, radius, threshold};
  stack.entries.push_back(std::move(entry));
  return stack;
}

patchy::SmartFilterStack
test_motion_blur_smart_filter_stack(std::int32_t angle, std::int32_t distance) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::MotionBlur;
  entry.native_name = "Motion Blur...";
  entry.native_class_id = "MtnB";
  entry.native_filter_id = 0x4d746e42U;
  entry.parameters = patchy::MotionBlurSmartFilter{angle, distance};
  stack.entries.push_back(std::move(entry));
  return stack;
}

std::uint64_t fnv1a_hash_bytes(std::span<const std::uint8_t> bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

patchy::BrushTip make_bar_brush_tip() {
  // 9x9 tip with a single opaque horizontal bar through the middle row.
  patchy::BrushTip tip;
  tip.width = 9;
  tip.height = 9;
  tip.mask.assign(81, 0);
  for (std::int32_t x = 0; x < 9; ++x) {
    tip.mask[4U * 9U + static_cast<std::size_t>(x)] = 255;
  }
  return tip;
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  CHECK(static_cast<bool>(file));
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

}  // namespace patchy::test
