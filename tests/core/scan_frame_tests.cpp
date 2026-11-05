#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "support/scan_frame.hpp"
#include "test_harness.hpp"

namespace {

using patchy::support::ScanFrameFormat;
using patchy::support::ScanFrameParameters;
using patchy::support::ScanImageAssembler;

template <typename Function>
bool throws(Function&& function) {
  try {
    function();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void scan_frame_assembles_gray_rgb_and_padded_strides() {
  // RGB, 2x2, stride padded by two bytes, delivered in uneven chunks.
  ScanImageAssembler rgb;
  rgb.begin_frame({ScanFrameFormat::Rgb, true, 8, 2, 2, 8});
  const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5, 6, 99, 99, 7, 8, 9, 10, 11, 12, 99, 99};
  rgb.append(std::span(bytes).first(3));
  rgb.append(std::span(bytes).subspan(3, 9));
  rgb.append(std::span(bytes).subspan(12));
  rgb.end_frame();
  CHECK(rgb.complete());
  const auto image = rgb.finish();
  CHECK(image.width == 2 && image.height == 2 && image.channels == 3 && image.bit_depth == 8);
  CHECK((image.samples == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));

  // Unknown line count (hand scanner): height comes from the delivered rows; a trailing
  // partial row is dropped.
  ScanImageAssembler gray;
  gray.begin_frame({ScanFrameFormat::Gray, true, 3, 3, -1, 8});
  gray.append(std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60, 70});
  gray.end_frame();
  const auto gray_image = gray.finish();
  CHECK(gray_image.height == 2 && gray_image.channels == 1);
  CHECK((gray_image.samples == std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60}));

  // Extra bytes past the announced lines are ignored.
  ScanImageAssembler padded;
  padded.begin_frame({ScanFrameFormat::Gray, true, 1, 1, 1, 8});
  padded.append(std::vector<std::uint8_t>{5, 6, 7});
  padded.end_frame();
  CHECK(padded.finish().samples == std::vector<std::uint8_t>{5});
}

void scan_frame_expands_line_art_and_keeps_16_bit_host_order() {
  ScanImageAssembler line_art;
  line_art.begin_frame({ScanFrameFormat::Gray, true, 1, 3, 1, 1});
  line_art.append(std::vector<std::uint8_t>{0b1010'0000});
  line_art.end_frame();
  CHECK((line_art.finish().samples == std::vector<std::uint8_t>{0, 255, 0}));

  // One-bit color: 1 is full intensity, not black.
  ScanImageAssembler color_line_art;
  color_line_art.begin_frame({ScanFrameFormat::Rgb, true, 1, 2, 1, 1});
  color_line_art.append(std::vector<std::uint8_t>{0b1010'1000});
  color_line_art.end_frame();
  CHECK((color_line_art.finish().samples == std::vector<std::uint8_t>{255, 0, 255, 0, 255, 0}));

  ScanImageAssembler deep;
  deep.begin_frame({ScanFrameFormat::Gray, true, 4, 2, 1, 16});
  const std::uint16_t samples[2]{0x1234, 0xFEDC};
  std::vector<std::uint8_t> bytes(4);
  std::memcpy(bytes.data(), samples, 4);
  deep.append(bytes);
  deep.end_frame();
  const auto image = deep.finish();
  CHECK(image.bit_depth == 16);
  std::uint16_t out[2]{};
  std::memcpy(out, image.samples.data(), 4);
  CHECK(out[0] == 0x1234 && out[1] == 0xFEDC);
}

void scan_frame_merges_three_pass_color() {
  ScanImageAssembler passes;
  passes.begin_frame({ScanFrameFormat::Green, false, 2, 2, 1, 8});
  passes.append(std::vector<std::uint8_t>{20, 21});
  passes.end_frame();
  passes.begin_frame({ScanFrameFormat::Red, false, 2, 2, 1, 8});
  passes.append(std::vector<std::uint8_t>{10, 11});
  passes.end_frame();
  CHECK(!passes.complete());
  passes.begin_frame({ScanFrameFormat::Blue, true, 2, 2, 1, 8});
  passes.append(std::vector<std::uint8_t>{30, 31});
  passes.end_frame();
  CHECK((passes.finish().samples == std::vector<std::uint8_t>{10, 20, 30, 11, 21, 31}));
}

void scan_frame_rejects_malformed_frames() {
  CHECK(throws([] { ScanImageAssembler().begin_frame({ScanFrameFormat::Gray, true, 1, 1, 1, 4}); }));
  CHECK(throws([] { ScanImageAssembler().begin_frame({ScanFrameFormat::Rgb, true, 5, 2, 1, 8}); }));
  CHECK(throws([] { ScanImageAssembler().begin_frame({ScanFrameFormat::Gray, true, 1, 0, 1, 8}); }));
  CHECK(throws([] { ScanImageAssembler().begin_frame({static_cast<ScanFrameFormat>(9), true, 1, 1, 1, 8}); }));
  CHECK(throws([] { ScanImageAssembler().begin_frame({ScanFrameFormat::Gray, false, 1, 1, 1, 8}); }));
  CHECK(throws([] {
    ScanImageAssembler short_frame;
    short_frame.begin_frame({ScanFrameFormat::Gray, true, 1, 1, 3, 8});
    short_frame.append(std::vector<std::uint8_t>{1});
    short_frame.end_frame();
  }));
  CHECK(throws([] {
    ScanImageAssembler empty;
    empty.begin_frame({ScanFrameFormat::Gray, true, 1, 1, -1, 8});
    empty.end_frame();
  }));
  CHECK(throws([] {
    ScanImageAssembler mismatched;
    mismatched.begin_frame({ScanFrameFormat::Red, false, 2, 2, 1, 8});
    mismatched.append(std::vector<std::uint8_t>{1, 2});
    mismatched.end_frame();
    mismatched.begin_frame({ScanFrameFormat::Green, false, 3, 3, 1, 8});
  }));
  CHECK(throws([] {
    ScanImageAssembler early;
    early.begin_frame({ScanFrameFormat::Red, true, 1, 1, 1, 8});
    early.append(std::vector<std::uint8_t>{1});
    early.end_frame();
  }));
  CHECK(throws([] { (void)ScanImageAssembler().finish(); }));
}

void scan_frame_resolution_words_convert_to_dpi() {
  CHECK(patchy::support::sane_resolution_to_dpi(300, false) == 300.0);
  CHECK(patchy::support::sane_resolution_to_dpi(600 << 16, true) == 600.0);
  CHECK(patchy::support::sane_resolution_to_dpi((150 << 16) + (1 << 15), true) == 150.5);
  CHECK(patchy::support::sane_resolution_to_dpi(-5, false) == 0.0);
}

void scan_frame_failures_map_to_actionable_causes() {
  using patchy::support::AcquireFailureCause;
  using patchy::support::classify_gphoto_failure;
  using patchy::support::classify_sane_failure;
  // An I/O error means "unreachable" for network backends and "connection lost" for USB.
  CHECK(classify_sane_failure(9, "airscan:e0:Lexmark") == AcquireFailureCause::NetworkUnreachable);
  CHECK(classify_sane_failure(9, "escl:https://10.0.0.5:443") == AcquireFailureCause::NetworkUnreachable);
  CHECK(classify_sane_failure(9, "genesys:libusb:001:004") == AcquireFailureCause::ConnectionLost);
  CHECK(classify_sane_failure(3, "x") == AcquireFailureCause::Busy);
  CHECK(classify_sane_failure(6, "x") == AcquireFailureCause::Jammed);
  CHECK(classify_sane_failure(7, "x") == AcquireFailureCause::FeederEmpty);
  CHECK(classify_sane_failure(8, "x") == AcquireFailureCause::CoverOpen);
  CHECK(classify_sane_failure(11, "x") == AcquireFailureCause::AccessDenied);
  CHECK(classify_sane_failure(4, "x") == AcquireFailureCause::UnsupportedSettings);
  CHECK(classify_sane_failure(10, "x") == AcquireFailureCause::OutOfMemory);
  CHECK(classify_sane_failure(42, "x") == AcquireFailureCause::Unknown);
  CHECK(classify_gphoto_failure(-53) == AcquireFailureCause::CameraClaimed);
  CHECK(classify_gphoto_failure(-110) == AcquireFailureCause::CameraClaimed);
  CHECK(classify_gphoto_failure(-105) == AcquireFailureCause::CameraNotSupported);
  CHECK(classify_gphoto_failure(-52) == AcquireFailureCause::ConnectionLost);
  CHECK(classify_gphoto_failure(-31) == AcquireFailureCause::AccessDenied);
  CHECK(classify_gphoto_failure(-108) == AcquireFailureCause::ImageMissing);
  CHECK(classify_gphoto_failure(-1) == AcquireFailureCause::Unknown);
  CHECK(patchy::support::network_scanner_address("Lexmark mc3224dwe (eSCL) - https ip=192.168.8.3") ==
        "192.168.8.3");
  CHECK(patchy::support::network_scanner_address("Canon ip=fe80::1 x") == "fe80::1");
  CHECK(patchy::support::network_scanner_address("Canon LiDE 300").empty());
  CHECK(patchy::support::network_scanner_address("bad ip=<script>").empty());
}

}  // namespace

std::vector<patchy::test::TestCase> scan_frame_tests() {
  return {
      {"scan_frame_assembles_gray_rgb_and_padded_strides", scan_frame_assembles_gray_rgb_and_padded_strides},
      {"scan_frame_expands_line_art_and_keeps_16_bit_host_order",
       scan_frame_expands_line_art_and_keeps_16_bit_host_order},
      {"scan_frame_merges_three_pass_color", scan_frame_merges_three_pass_color},
      {"scan_frame_rejects_malformed_frames", scan_frame_rejects_malformed_frames},
      {"scan_frame_resolution_words_convert_to_dpi", scan_frame_resolution_words_convert_to_dpi},
      {"scan_frame_failures_map_to_actionable_causes", scan_frame_failures_map_to_actionable_causes},
  };
}
