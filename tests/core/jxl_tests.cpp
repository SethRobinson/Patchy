#include "core/adjustment_layer.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <exception>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <span>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include "core/document.hpp"
#include "core/pixel_depth.hpp"
#include "formats/format_registry.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/metadata/metadata_containers.hpp"
// Tests build a few source files straight through libjxl (gray, premultiplied, oriented and
// PQ sources the writer never produces); the private symbol names come first, as in the
// codec module.
#include "formats/jxl_symbols.hpp"
#include "core_test_support.hpp"
#include "formats/stb/stb_image.h"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include <jxl/cms.h>
#include <jxl/encode.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace jxl = patchy::jxl;
using patchy::BitDepth;
using patchy::Document;
using patchy::PixelBuffer;
using patchy::PixelFormat;

// Deep editing is off by default in the test harness; the deep cases turn it on.
class DeepEditing {
public:
  explicit DeepEditing(bool enabled) {
    patchy::set_deep_editing_override(enabled);
  }
  ~DeepEditing() {
    patchy::set_deep_editing_override(std::nullopt);
  }
  DeepEditing(const DeepEditing&) = delete;
  DeepEditing& operator=(const DeepEditing&) = delete;
};

// A gradient with a distinct top-left pixel, so a flipped or shifted decode is caught.
[[nodiscard]] PixelBuffer gradient_rgba8(std::int32_t width, std::int32_t height, bool translucent) {
  PixelBuffer pixels(width, height, translucent ? PixelFormat::rgba8() : PixelFormat::rgb8());
  const std::size_t channels = translucent ? 4U : 3U;
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = row.data() + static_cast<std::size_t>(x) * channels;
      px[0] = static_cast<std::uint8_t>((x * 37 + 5) & 255);
      px[1] = static_cast<std::uint8_t>((y * 59 + 9) & 255);
      px[2] = static_cast<std::uint8_t>(((x + y) * 13 + 200) & 255);
      if (translucent) {
        px[3] = static_cast<std::uint8_t>(std::min(255, 40 + (x * 215) / std::max(1, width - 1)));
      }
    }
  }
  pixels.row(0)[0] = 250;
  pixels.row(0)[1] = 10;
  pixels.row(0)[2] = 20;
  return pixels;
}

[[nodiscard]] Document document_from(PixelBuffer pixels) {
  Document document(pixels.width(), pixels.height(), pixels.format());
  document.color_state().bit_depth = pixels.format().bit_depth;
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

[[nodiscard]] const PixelBuffer& top_pixels(const Document& document) {
  return document.layers().back().pixels();
}

[[nodiscard]] bool same_bytes(const PixelBuffer& a, const PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format().channels != b.format().channels ||
      a.format().bit_depth != b.format().bit_depth) {
    return false;
  }
  const auto left = std::as_const(a).data();
  const auto right = std::as_const(b).data();
  return std::equal(left.begin(), left.end(), right.begin(), right.end());
}

[[nodiscard]] std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] jxl::WriteOptions lossless() {
  jxl::WriteOptions options;
  options.lossless = true;
  return options;
}

// Encodes raw interleaved samples straight through libjxl, for sources Patchy's writer
// never produces. `color` null means sRGB.
struct RawSource {
  JxlBasicInfo info{};
  JxlPixelFormat format{};
  std::vector<std::uint8_t> samples;
  std::optional<JxlColorEncoding> color;
};

[[nodiscard]] std::vector<std::uint8_t> encode_raw(const RawSource& source) {
  auto* enc = JxlEncoderCreate(nullptr);
  CHECK(enc != nullptr);
  JxlEncoderSetCms(enc, *JxlGetDefaultCms());
  auto info = source.info;
  info.uses_original_profile = JXL_TRUE;
  CHECK(JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS);
  JxlColorEncoding color{};
  if (source.color.has_value()) {
    color = *source.color;
  } else {
    JxlColorEncodingSetToSRGB(&color, info.num_color_channels == 1 ? JXL_TRUE : JXL_FALSE);
  }
  CHECK(JxlEncoderSetColorEncoding(enc, &color) == JXL_ENC_SUCCESS);
  auto* settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
  CHECK(JxlEncoderSetFrameLossless(settings, JXL_TRUE) == JXL_ENC_SUCCESS);
  CHECK(JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, 3) == JXL_ENC_SUCCESS);
  CHECK(JxlEncoderAddImageFrame(settings, &source.format, source.samples.data(), source.samples.size()) ==
        JXL_ENC_SUCCESS);
  JxlEncoderCloseInput(enc);
  std::vector<std::uint8_t> out(4096);
  auto* next = out.data();
  auto available = out.size();
  for (;;) {
    const auto status = JxlEncoderProcessOutput(enc, &next, &available);
    if (status != JXL_ENC_NEED_MORE_OUTPUT) {
      CHECK(status == JXL_ENC_SUCCESS);
      break;
    }
    const auto used = static_cast<std::size_t>(next - out.data());
    out.resize(out.size() * 2);
    next = out.data() + used;
    available = out.size() - used;
  }
  out.resize(static_cast<std::size_t>(next - out.data()));
  JxlEncoderDestroy(enc);
  return out;
}

[[nodiscard]] JxlBasicInfo basic_info(std::uint32_t width, std::uint32_t height, std::uint32_t color_channels,
                                      std::uint32_t bits, bool alpha) {
  JxlBasicInfo info;
  JxlEncoderInitBasicInfo(&info);
  info.xsize = width;
  info.ysize = height;
  info.num_color_channels = color_channels;
  info.bits_per_sample = bits;
  if (alpha) {
    info.num_extra_channels = 1;
    info.alpha_bits = bits;
  }
  return info;
}

void jxl_extensions_sniff_and_registry_routing() {
  CHECK(jxl::is_jxl_extension("jxl"));
  CHECK(jxl::is_jxl_extension(".JXL"));
  CHECK(!jxl::is_jxl_extension("jxr"));
  const auto bytes = jxl::write_jxl(document_from(gradient_rgba8(6, 5, false)), lossless());
  CHECK(jxl::sniff(bytes));
  CHECK(bytes.size() > 2 && bytes[0] == 0xFF && bytes[1] == 0x0A);  // bare codestream
  // Metadata boxes need the ISOBMFF container, which sniffs too.
  auto boxed_options = lossless();
  boxed_options.xmp = {'<', 'x', '/', '>'};
  const auto boxed = jxl::write_jxl(document_from(gradient_rgba8(6, 5, false)), boxed_options);
  CHECK(jxl::sniff(boxed));
  CHECK(boxed.size() > 12 && std::memcmp(boxed.data() + 4, "JXL ", 4) == 0);
  const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  const std::vector<std::uint8_t> jxr = {'I', 'I', 0xBC, 0x01, 0, 0, 0, 0};
  CHECK(!jxl::sniff(png));
  CHECK(!jxl::sniff(jxr));
  const auto* handler = patchy::builtin_format_registry().find_by_extension("jxl");
  CHECK(handler != nullptr);
  if (handler != nullptr) {
    CHECK(handler->identifier == "patchy.formats.jxl");
    CHECK(handler->can_write());
    CHECK(handler->sniff && handler->sniff(bytes));
  }
}

void jxl_lossless_8bit_round_trips_exactly() {
  for (const bool translucent : {false, true}) {
    const auto source = gradient_rgba8(23, 17, translucent);
    const auto decoded = jxl::read_jxl(jxl::write_jxl(document_from(source), lossless()));
    CHECK(decoded.document.width() == 23);
    CHECK(decoded.document.height() == 17);
    CHECK(decoded.document.color_state().bit_depth == BitDepth::UInt8);
    CHECK(decoded.document.layers().size() == 1);
    CHECK(decoded.document.layers().front().name() == "Background");
    CHECK(same_bytes(top_pixels(decoded.document), source));
  }
  // An RGBA document whose alpha is all opaque writes no alpha channel.
  PixelBuffer opaque(4, 3, PixelFormat::rgba8());
  std::fill(opaque.data().begin(), opaque.data().end(), std::uint8_t{255});
  const auto decoded = jxl::read_jxl(jxl::write_jxl(document_from(std::move(opaque)), lossless()));
  CHECK(top_pixels(decoded.document).format().channels == 3);
}

void jxl_deep_documents_round_trip_at_their_depth() {
  const DeepEditing deep(true);
  PixelBuffer sixteen(9, 7, patchy::with_bit_depth(PixelFormat::rgba8(), BitDepth::UInt16));
  auto* samples = reinterpret_cast<std::uint16_t*>(sixteen.data().data());
  for (std::size_t i = 0; i < static_cast<std::size_t>(9 * 7 * 4); ++i) {
    samples[i] = static_cast<std::uint16_t>((i * 7919U + 13U) & 0xFFFFU);
  }
  for (std::size_t p = 0; p < static_cast<std::size_t>(9 * 7); ++p) {
    samples[p * 4 + 3] = static_cast<std::uint16_t>(1000U + p * 900U);  // translucent alpha
  }
  const auto decoded16 = jxl::read_jxl(jxl::write_jxl(document_from(sixteen), lossless()));
  CHECK(decoded16.document.color_state().bit_depth == BitDepth::UInt16);
  CHECK(same_bytes(top_pixels(decoded16.document), sixteen));

  PixelBuffer floats(5, 4, patchy::with_bit_depth(PixelFormat::rgb8(), BitDepth::Float32));
  auto* values = reinterpret_cast<float*>(floats.data().data());
  for (std::size_t i = 0; i < static_cast<std::size_t>(5 * 4 * 3); ++i) {
    values[i] = static_cast<float>(i) * 0.137F;  // up to about 8, well above SDR white
  }
  const auto decoded32 = jxl::read_jxl(jxl::write_jxl(document_from(floats), lossless()));
  CHECK(decoded32.document.color_state().bit_depth == BitDepth::Float32);
  const auto& out = top_pixels(decoded32.document);
  CHECK(out.format().channels == 3);
  const auto* got = reinterpret_cast<const float*>(std::as_const(out).data().data());
  float worst = 0.0F;
  for (std::size_t i = 0; i < static_cast<std::size_t>(5 * 4 * 3); ++i) {
    worst = std::max(worst, std::abs(got[i] - values[i]));
  }
  CHECK(worst < 1e-5F);
}

void jxl_deep_files_open_at_8_bits_when_deep_editing_is_off() {
  PixelBuffer sixteen(3, 2, patchy::with_bit_depth(PixelFormat::rgb8(), BitDepth::UInt16));
  auto* samples = reinterpret_cast<std::uint16_t*>(sixteen.data().data());
  for (std::size_t i = 0; i < 18; ++i) {
    samples[i] = static_cast<std::uint16_t>(i * 3855U);  // 15 * 257 steps: exact 8-bit values
  }
  std::vector<std::uint8_t> bytes;
  {
    const DeepEditing deep(true);
    bytes = jxl::write_jxl(document_from(sixteen), lossless());
  }
  const DeepEditing off(false);
  const auto decoded = jxl::read_jxl(bytes);
  CHECK(decoded.document.color_state().bit_depth == BitDepth::UInt8);
  const auto out = std::as_const(top_pixels(decoded.document)).data();
  for (std::size_t i = 0; i < 18; ++i) {
    CHECK(out[i] == static_cast<std::uint8_t>(i * 15U));
  }
}

void jxl_lossy_quality_stays_close() {
  // A smooth photographic-style ramp; quality 90 should land within a few code values.
  PixelBuffer source(64, 48, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = source.row(y).data() + static_cast<std::size_t>(x) * 3U;
      px[0] = static_cast<std::uint8_t>(x * 4);
      px[1] = static_cast<std::uint8_t>(y * 5);
      px[2] = static_cast<std::uint8_t>(128 + (x - y) / 2);
    }
  }
  const auto document = document_from(source);
  jxl::WriteOptions options;
  options.quality = 90;
  const auto lossy = jxl::write_jxl(document, options);
  const auto exact = jxl::write_jxl(document, lossless());
  const auto decoded = jxl::read_jxl(lossy);
  const auto got = std::as_const(top_pixels(decoded.document)).data();
  const auto want = std::as_const(source).data();
  CHECK(got.size() == want.size());
  double error = 0.0;
  for (std::size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
    error += std::abs(static_cast<int>(got[i]) - static_cast<int>(want[i]));
  }
  const double mean = error / static_cast<double>(want.size());
  if (mean >= 3.0) {
    std::cout << "  jxl lossy mean error " << mean << "\n";
  }
  CHECK(mean < 3.0);
  CHECK(lossy != exact);
}

void jxl_encoder_keeps_patches_and_dots_off() {
  // docs/legal-constraints.md, JPEG XL condition 3: every encode turns both tools off.
  for (const bool lossless_encode : {false, true}) {
    jxl::WriteOptions options;
    options.lossless = lossless_encode;
    options.effort = 9;  // the efforts where libjxl would otherwise search for patches
    static_cast<void>(jxl::write_jxl(document_from(gradient_rgba8(16, 16, false)), options));
    const auto tools = jxl::last_encoder_tool_settings();
    CHECK(tools.patches == 0);
    CHECK(tools.dots == 0);
  }
  jxl::AnimationEncoder animation(4, 4, BitDepth::UInt8, 0, jxl::WriteOptions{});
  animation.add(gradient_rgba8(4, 4, true), 100);
  static_cast<void>(animation.finish());
  CHECK(jxl::last_encoder_tool_settings().patches == 0);
  CHECK(jxl::last_encoder_tool_settings().dots == 0);
}

void jxl_animation_round_trips_frames_delays_and_loops() {
  std::vector<PixelBuffer> frames;
  for (int i = 0; i < 3; ++i) {
    auto frame = gradient_rgba8(10, 8, true);
    frame.row(1)[0] = static_cast<std::uint8_t>(60 * i);
    frames.push_back(std::move(frame));
  }
  const std::uint32_t delays[] = {100, 250, 40};
  jxl::AnimationEncoder encoder(10, 8, BitDepth::UInt8, 3, lossless());
  for (int i = 0; i < 3; ++i) {
    encoder.add(frames[static_cast<std::size_t>(i)], delays[i]);
  }
  const auto bytes = encoder.finish();
  const auto result = jxl::read_jxl_detailed(bytes);
  const auto& document = result.read.document;
  CHECK(result.frame_count == 3);
  CHECK(document.layers().size() == 3);
  CHECK(document.metadata().values.at(jxl::kLoopCountMetadata) == "3");
  // Frame 1 on top (the last layer), like animated GIF and WebP imports.
  CHECK(document.layers().back().name() == "Frame 1 0.1s");
  CHECK(document.layers()[1].name() == "Frame 2 0.25s");
  CHECK(document.layers().front().name() == "Frame 3 0.04s");
  CHECK(same_bytes(document.layers().back().pixels(), frames[0]));
  CHECK(same_bytes(document.layers().front().pixels(), frames[2]));
  // A custom namer (the UI's translated one) is used as given.
  jxl::ReadOptions options;
  options.frame_name = [](int index, std::uint32_t delay) { return std::to_string(index) + "/" + std::to_string(delay); };
  const auto named = jxl::read_jxl_detailed(bytes, options);
  CHECK(named.read.document.layers().back().name() == "1/100");
}

void jxl_metadata_boxes_and_profile_round_trip() {
  // A tiny little-endian TIFF stream and an XMP packet.
  const std::vector<std::uint8_t> exif = {'I', 'I', 42, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const std::string xmp = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"></x:xmpmeta>";
  auto options = lossless();
  options.exif = exif;
  options.xmp.assign(xmp.begin(), xmp.end());
  const auto bytes = jxl::write_jxl(document_from(gradient_rgba8(5, 5, false)), options);
  const auto metadata = jxl::read_metadata(bytes);
  CHECK(metadata.container);
  CHECK(metadata.exif == exif);
  CHECK(std::string(metadata.xmp.begin(), metadata.xmp.end()) == xmp);
  CHECK(metadata.icc.empty());  // plain sRGB carries no profile
  const auto has_box = [&](const std::string& type) {
    return std::any_of(metadata.boxes.begin(), metadata.boxes.end(), [&](const auto& box) { return box.type == type; });
  };
  CHECK(has_box("JXL ") && has_box("Exif") && has_box("xml "));
  // File Info reads the same blocks through the shared metadata scanner.
  const auto info = patchy::metadata::extract_file_info(bytes);
  CHECK(info.source_format == "JXL");
  CHECK(info.exif && *info.exif == exif);
  CHECK(info.xmp && std::string(info.xmp->begin(), info.xmp->end()) == xmp);
  CHECK(!info.structure.empty());

  // A Display P3 source: the reader keeps its pixels in P3, and the profile libjxl
  // synthesizes for it survives a write and a reread unchanged.
  RawSource p3;
  p3.info = basic_info(4, 3, 3, 8, false);
  p3.format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  p3.samples.assign(4 * 3 * 3, 0);
  for (std::size_t i = 0; i < p3.samples.size(); ++i) {
    p3.samples[i] = static_cast<std::uint8_t>(i * 7);
  }
  JxlColorEncoding display_p3{};
  JxlColorEncodingSetToSRGB(&display_p3, JXL_FALSE);
  display_p3.primaries = JXL_PRIMARIES_P3;
  p3.color = display_p3;
  const auto p3_bytes = encode_raw(p3);
  const auto p3_profile = jxl::read_metadata(p3_bytes).icc;
  CHECK(!p3_profile.empty());
  const auto p3_read = jxl::read_jxl_detailed(p3_bytes);
  CHECK(p3_read.pixels_in_source_space);
  const auto p3_pixels = std::as_const(top_pixels(p3_read.read.document)).data();
  CHECK(std::equal(p3_pixels.begin(), p3_pixels.end(), p3.samples.begin(), p3.samples.end()));
  auto document = document_from(gradient_rgba8(4, 3, false));
  document.color_state().embedded_icc_profile = p3_profile;
  CHECK(jxl::read_metadata(jxl::write_jxl(document, lossless())).icc == p3_profile);
}

void jxl_gray_premultiplied_and_oriented_sources_decode() {
  // Gray expands to RGB.
  RawSource gray;
  gray.info = basic_info(3, 2, 1, 8, false);
  gray.format = {1, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  gray.samples = {0, 40, 80, 120, 200, 255};
  const auto gray_read = jxl::read_jxl_detailed(encode_raw(gray));
  CHECK(!gray_read.pixels_in_source_space);
  const auto& gray_pixels = top_pixels(gray_read.read.document);
  CHECK(gray_pixels.format().channels == 3);
  for (std::size_t i = 0; i < 6; ++i) {
    const auto* px = std::as_const(gray_pixels).data().data() + i * 3;
    CHECK(px[0] == gray.samples[i] && px[1] == gray.samples[i] && px[2] == gray.samples[i]);
  }

  // Premultiplied alpha comes back straight.
  RawSource premultiplied;
  premultiplied.info = basic_info(2, 1, 3, 8, true);
  premultiplied.info.alpha_premultiplied = JXL_TRUE;
  premultiplied.format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  premultiplied.samples = {100, 50, 0, 128, 255, 255, 255, 255};
  const auto premultiplied_doc = jxl::read_jxl(encode_raw(premultiplied)).document;
  const auto straight = std::as_const(top_pixels(premultiplied_doc)).data();
  CHECK(straight[3] == 128);
  CHECK(std::abs(static_cast<int>(straight[0]) - 199) <= 1);  // 100 / (128 / 255)
  CHECK(std::abs(static_cast<int>(straight[1]) - 100) <= 1);
  CHECK(straight[4] == 255 && straight[7] == 255);

  // Orientation 6 (rotate 90 clockwise) is applied: a 3x2 source opens 2x3.
  RawSource oriented;
  oriented.info = basic_info(3, 2, 3, 8, false);
  oriented.info.orientation = JXL_ORIENT_ROTATE_90_CW;
  oriented.format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  oriented.samples.assign(3 * 2 * 3, 0);
  oriented.samples[0] = 255;  // red at the source's top-left
  const auto oriented_read = jxl::read_jxl_detailed(encode_raw(oriented));
  CHECK(oriented_read.orientation_applied);
  const auto& rotated = oriented_read.read.document;
  CHECK(rotated.width() == 2);
  CHECK(rotated.height() == 3);
  // Rotating clockwise moves the top-left corner to the top-right.
  CHECK(std::as_const(top_pixels(rotated)).row(0)[3] == 255);
}

void jxl_pq_sources_open_with_sdr_white_at_one() {
  // A PQ file at a 10000 nit intensity target whose one pixel encodes SDR reference white
  // (203 nits). Deep editing opens it linear with that pixel at 1.0; without deep editing
  // the highlight rolloff bakes it to 8 bits.
  constexpr double kM1 = 2610.0 / 16384.0;
  constexpr double kM2 = 2523.0 / 4096.0 * 128.0;
  constexpr double kC1 = 3424.0 / 4096.0;
  constexpr double kC2 = 2413.0 / 4096.0 * 32.0;
  constexpr double kC3 = 2392.0 / 4096.0 * 32.0;
  const double y = std::pow(203.0 / 10000.0, kM1);
  const auto pq_white = static_cast<float>(std::pow((kC1 + kC2 * y) / (1.0 + kC3 * y), kM2));
  RawSource pq;
  pq.info = basic_info(1, 1, 3, 16, false);
  pq.info.intensity_target = 10000.0F;
  pq.format = {3, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
  pq.samples.resize(3 * sizeof(float));
  const float rgb[3] = {pq_white, pq_white, pq_white};
  std::memcpy(pq.samples.data(), rgb, sizeof(rgb));
  JxlColorEncoding encoding{};
  JxlColorEncodingSetToSRGB(&encoding, JXL_FALSE);
  encoding.transfer_function = JXL_TRANSFER_FUNCTION_PQ;
  encoding.primaries = JXL_PRIMARIES_SRGB;
  pq.color = encoding;
  const auto bytes = encode_raw(pq);
  {
    const DeepEditing deep(true);
    const auto read = jxl::read_jxl_detailed(bytes);
    CHECK(!read.pixels_in_source_space);
    CHECK(read.read.document.color_state().bit_depth == BitDepth::Float32);
    const auto* value = reinterpret_cast<const float*>(std::as_const(top_pixels(read.read.document)).data().data());
    if (std::abs(value[0] - 1.0F) >= 0.02F) {
      std::cout << "  jxl pq white decoded as " << value[0] << "\n";
    }
    CHECK(std::abs(value[0] - 1.0F) < 0.02F);
  }
  const DeepEditing off(false);
  const auto read = jxl::read_jxl(bytes);
  CHECK(read.document.color_state().bit_depth == BitDepth::UInt8);
  const auto baked = std::as_const(top_pixels(read.document)).data()[0];
  CHECK(baked > 200 && baked < 245);
}

void jxl_rejects_damaged_and_truncated_files() {
  const auto valid = jxl::write_jxl(document_from(gradient_rgba8(32, 32, false)), lossless());
  const auto throws = [](std::span<const std::uint8_t> bytes) {
    try {
      static_cast<void>(jxl::read_jxl(bytes));
    } catch (const std::runtime_error&) {
      return true;
    }
    return false;
  };
  CHECK(throws(std::vector<std::uint8_t>{'n', 'o', 'p', 'e'}));
  CHECK(throws(std::span<const std::uint8_t>(valid).first(valid.size() / 2)));
  auto garbled = valid;
  for (std::size_t i = 2; i < garbled.size(); ++i) {
    garbled[i] = static_cast<std::uint8_t>(garbled[i] ^ 0xA5U);
  }
  CHECK(throws(garbled));
  // read_metadata never throws, whatever it is given.
  CHECK(jxl::read_metadata(garbled).exif.empty());
}

void jxl_decodes_conformance_files_that_use_patches() {
  // The encoder never writes patches (docs/legal-constraints.md), but files made elsewhere
  // with them must open. Both inputs are the libjxl conformance suite's CC0 "patches" cases
  // (test-fixtures/jxl, provenance in NOTICE-THIRD-PARTY.md); the PNGs are the suite's 8-bit
  // renderings of its float references.
  struct Case {
    const char* stem;
    int max_difference;
  };
  for (const auto& test_case : {Case{"conformance-patches", 3}, Case{"conformance-patches-lossless", 1}}) {
    const auto jxl_bytes =
        file_bytes(patchy::test::committed_format_fixture_path("jxl", std::string(test_case.stem) + ".jxl"));
    const auto png_bytes = file_bytes(
        patchy::test::committed_format_fixture_path("jxl", std::string(test_case.stem) + "-ref.png"));
    CHECK(!jxl_bytes.empty() && !png_bytes.empty());
    const auto read = jxl::read_jxl_detailed(jxl_bytes);
    const auto& document = read.read.document;
    int width = 0;
    int height = 0;
    int channels = 0;
    auto* reference = stbi_load_from_memory(png_bytes.data(), static_cast<int>(png_bytes.size()), &width, &height,
                                            &channels, 4);
    CHECK(reference != nullptr);
    if (reference == nullptr) {
      continue;
    }
    CHECK(document.width() == width);
    CHECK(document.height() == height);
    const auto& pixels = top_pixels(document);
    const std::size_t decoded_channels = pixels.format().channels;
    int worst = 0;
    std::uint64_t total = 0;
    for (std::int32_t y = 0; y < std::min(height, document.height()); ++y) {
      const auto row = std::as_const(pixels).row(y);
      for (std::int32_t x = 0; x < std::min(width, document.width()); ++x) {
        const auto* want = reference + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4U;
        const auto* got = row.data() + static_cast<std::size_t>(x) * decoded_channels;
        const int alpha = decoded_channels == 4 ? got[3] : 255;
        worst = std::max(worst, std::abs(alpha - want[3]));
        if (want[3] == 0) {
          continue;  // color under full transparency is not meaningful
        }
        for (int c = 0; c < 3; ++c) {
          const int difference = std::abs(static_cast<int>(got[c]) - want[c]);
          worst = std::max(worst, difference);
          total += static_cast<std::uint64_t>(difference);
        }
      }
    }
    stbi_image_free(reference);
    if (worst > test_case.max_difference) {
      std::cout << "  " << test_case.stem << ": worst difference " << worst << ", total " << total << "\n";
    }
    CHECK(worst <= test_case.max_difference);
  }
}

void jxl_writer_bytes_are_stable() {
  // Pins the default lossless writer output (effort 7, the patches and dots settings, the
  // codestream layout) by an FNV-1a hash. Re-pin only for a deliberate encoder change, such
  // as a libjxl upgrade; the failure output prints the new hash.
  struct Pin {
    const char* label;
    std::uint64_t expected;
    bool translucent;
  };
  const std::vector<Pin> pins = {
      {"lossless rgb", 0x9ab461e5683ef7ffULL, false},
      {"lossless rgba", 0x6afb6855ca0df77dULL, true},
  };
  std::vector<std::uint64_t> hashes;
  for (const auto& pin : pins) {
    const auto bytes = jxl::write_jxl(document_from(gradient_rgba8(23, 17, pin.translucent)), lossless());
    hashes.push_back(patchy::test::fnv1a_hash_bytes(bytes));
    if (hashes.back() != pin.expected) {
      std::cout << "  jxl writer hash (" << pin.label << "): 0x" << std::hex << hashes.back() << std::dec
                << " size " << bytes.size() << "\n";
    }
  }
  for (std::size_t i = 0; i < pins.size(); ++i) {
    CHECK(hashes[i] == pins[i].expected);
  }
}

void jxl_unicode_path_round_trips() {
  // Every new file-writing entry point gets a Unicode-path test (AGENTS.md).
  const auto dir = patchy::test::unicode_artifact_dir(u8"jxl");
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".jxl");
  const auto source = gradient_rgba8(5, 4, true);
  jxl::write_jxl_file(document_from(source), path, lossless());
  CHECK(std::filesystem::exists(path));
  CHECK(patchy::test::directory_holds_only(dir, {path}));
  const auto decoded = jxl::read_jxl_file(path);
  CHECK(same_bytes(top_pixels(decoded.document), source));
  CHECK(decoded.document.layers().front().name() == patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem));
}

}  // namespace

std::vector<patchy::test::TestCase> jxl_tests() {
  return {
      {"jxl_extensions_sniff_and_registry_routing", jxl_extensions_sniff_and_registry_routing},
      {"jxl_lossless_8bit_round_trips_exactly", jxl_lossless_8bit_round_trips_exactly},
      {"jxl_deep_documents_round_trip_at_their_depth", jxl_deep_documents_round_trip_at_their_depth},
      {"jxl_deep_files_open_at_8_bits_when_deep_editing_is_off",
       jxl_deep_files_open_at_8_bits_when_deep_editing_is_off},
      {"jxl_lossy_quality_stays_close", jxl_lossy_quality_stays_close},
      {"jxl_encoder_keeps_patches_and_dots_off", jxl_encoder_keeps_patches_and_dots_off},
      {"jxl_animation_round_trips_frames_delays_and_loops", jxl_animation_round_trips_frames_delays_and_loops},
      {"jxl_metadata_boxes_and_profile_round_trip", jxl_metadata_boxes_and_profile_round_trip},
      {"jxl_gray_premultiplied_and_oriented_sources_decode", jxl_gray_premultiplied_and_oriented_sources_decode},
      {"jxl_pq_sources_open_with_sdr_white_at_one", jxl_pq_sources_open_with_sdr_white_at_one},
      {"jxl_rejects_damaged_and_truncated_files", jxl_rejects_damaged_and_truncated_files},
      {"jxl_decodes_conformance_files_that_use_patches", jxl_decodes_conformance_files_that_use_patches},
      {"jxl_writer_bytes_are_stable", jxl_writer_bytes_are_stable},
      {"jxl_unicode_path_round_trips", jxl_unicode_path_round_trips},
  };
}
