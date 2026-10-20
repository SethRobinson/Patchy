// Image > Adjustments stored as native Smart Filters (docs/smart-filters-adjustments.md):
// the Photoshop 2026 capture parses, renders like Photoshop's own preview, round-trips
// byte for byte and patches edited settings in Photoshop's dialog form.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_smart_filter_adjustments.hpp"
#include "psd/psd_smart_objects.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::require_layer_named;

constexpr const char* kFixture = "photoshop-smart-filter-adjustments.psd";

const patchy::UnknownPsdBlock& sold_block(const patchy::Layer& layer) {
  const auto& blocks = layer.unknown_psd_blocks();
  const auto found = std::find_if(blocks.begin(), blocks.end(),
                                  [](const patchy::UnknownPsdBlock& block) { return block.key == "SoLd"; });
  CHECK(found != blocks.end());
  return *found;
}

patchy::psd::DescriptorObject sold_descriptor(std::span<const std::uint8_t> sold) {
  patchy::psd::BigEndianReader reader(sold);
  CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
  CHECK(reader.read_u32() == 4U);
  CHECK(reader.read_u32() == 16U);
  return patchy::psd::read_descriptor(reader);
}

// The Fltr object of one filterFX entry of a SoLd descriptor.
const patchy::psd::DescriptorObject* entry_filter(const patchy::psd::DescriptorObject& descriptor,
                                                  std::size_t index = 0) {
  const auto* root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto* list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr && list->list_value.size() > index);
  const auto* entry = list->list_value[index].object_value.get();
  CHECK(entry != nullptr);
  return patchy::psd::descriptor_object(*entry, "Fltr");
}

const patchy::AdjustmentSettings& adjustment_of(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Adjustment);
  const auto* adjustment = std::get_if<patchy::AdjustmentSmartFilter>(&entry.parameters);
  CHECK(adjustment != nullptr);
  return adjustment->settings;
}

struct Difference {
  int largest{0};      // straight RGB, where Photoshop's pixel is visible
  int largest_opaque{0};  // the same over Photoshop's opaque pixels only
  int over_one{0};     // pixels with a channel more than one level off
  int alpha_misses{0};
  std::array<int, 9> worst{};  // x, y, Photoshop RGBA, Patchy RGB
};

Difference compare_with_photoshop(const patchy::FilterRenderResult& rendered, const patchy::Layer& photoshop) {
  const auto expected_bounds = photoshop.bounds();
  const auto& expected = photoshop.pixels();
  const auto sample = [](const patchy::PixelBuffer& pixels, patchy::Rect bounds, std::int32_t x, std::int32_t y) {
    std::array<int, 4> value{0, 0, 0, 0};
    if (x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height) {
      const auto* pixel = pixels.pixel(x - bounds.x, y - bounds.y);
      for (int channel = 0; channel < 4; ++channel) {
        value[static_cast<std::size_t>(channel)] = pixel[channel];
      }
    }
    return value;
  };
  Difference difference;
  const auto x0 = std::min(rendered.bounds.x, expected_bounds.x);
  const auto y0 = std::min(rendered.bounds.y, expected_bounds.y);
  const auto x1 = std::max(rendered.bounds.x + rendered.bounds.width, expected_bounds.x + expected_bounds.width);
  const auto y1 = std::max(rendered.bounds.y + rendered.bounds.height, expected_bounds.y + expected_bounds.height);
  for (std::int32_t y = y0; y < y1; ++y) {
    for (std::int32_t x = x0; x < x1; ++x) {
      const auto want = sample(expected, expected_bounds, x, y);
      const auto got = sample(rendered.pixels, rendered.bounds, x, y);
      if (want[3] != got[3]) {
        ++difference.alpha_misses;
      }
      if (want[3] == 0) {
        continue;
      }
      int largest = 0;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        largest = std::max(largest, std::abs(want[channel] - got[channel]));
      }
      if (want[3] == 255) {
        difference.largest_opaque = std::max(difference.largest_opaque, largest);
      }
      if (largest > difference.largest) {
        difference.worst = {x, y, want[0], want[1], want[2], want[3], got[0], got[1], got[2]};
      }
      difference.largest = std::max(difference.largest, largest);
      if (largest > 1) {
        if (difference.over_one < 4) {
          std::cout << "    at " << x << "," << y << " photoshop " << want[0] << "," << want[1] << ","
                    << want[2] << "," << want[3] << " patchy " << got[0] << "," << got[1] << "," << got[2]
                    << "," << got[3] << "\n";
        }
        ++difference.over_one;
      }
    }
  }
  return difference;
}

struct ExpectedLayer {
  const char* name;
  patchy::AdjustmentKind kind;
  // Calibration bounds against Photoshop's preview of this capture, as measured (October
  // 2026): the adjustment-layer math's own accuracy (docs/adjustments-calibration.md,
  // docs/adjustment-kinds.md); docs/smart-filters-adjustments.md lists them.
  int largest;
  int largest_opaque;
  int over_one;
};

constexpr ExpectedLayer kLayers[]{
    {"Levels master", patchy::AdjustmentKind::Levels, 1, 1, 0},
    {"Levels output", patchy::AdjustmentKind::Levels, 0, 0, 0},
    {"Levels channels preset", patchy::AdjustmentKind::Levels, 1, 1, 0},
    {"Curves master", patchy::AdjustmentKind::Curves, 0, 0, 0},
    {"Curves channels preset", patchy::AdjustmentKind::Curves, 0, 0, 0},
    {"Hue Saturation desaturate", patchy::AdjustmentKind::HueSaturation, 2, 2, 342},
    {"Hue Saturation master", patchy::AdjustmentKind::HueSaturation, 3, 2, 379},
    {"Hue Saturation colorize", patchy::AdjustmentKind::HueSaturation, 0, 0, 0},
    {"Brightness Contrast modern", patchy::AdjustmentKind::BrightnessContrast, 0, 0, 0},
    {"Brightness Contrast no legacy key", patchy::AdjustmentKind::BrightnessContrast, 1, 1, 0},
    {"Brightness Contrast legacy", patchy::AdjustmentKind::BrightnessContrast, 1, 1, 0},
    {"Color Balance preserve", patchy::AdjustmentKind::ColorBalance, 1, 1, 0},
    {"Color Balance plain", patchy::AdjustmentKind::ColorBalance, 0, 0, 0},
    {"Channel Mixer", patchy::AdjustmentKind::ChannelMixer, 0, 0, 0},
    {"Channel Mixer monochrome", patchy::AdjustmentKind::ChannelMixer, 0, 0, 0},
    {"Exposure", patchy::AdjustmentKind::Exposure, 1, 1, 0},
    {"Posterize", patchy::AdjustmentKind::Posterize, 0, 0, 0},
    {"Threshold", patchy::AdjustmentKind::Threshold, 0, 0, 0},
    {"Photo Filter", patchy::AdjustmentKind::PhotoFilter, 5, 5, 35},
    {"Black White", patchy::AdjustmentKind::BlackWhite, 0, 0, 0},
    {"Levels then Curves", patchy::AdjustmentKind::Levels, 2, 2, 56},
    {"Gaussian then Hue Saturation", patchy::AdjustmentKind::HueSaturation, 35, 3, 619},
};

void smart_filter_adjustments_photoshop_capture_parses_and_renders() {
  const auto document = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto& source = require_layer_named(document, "Source");
  CHECK(source.smart_filter_stack() == nullptr);
  const auto canvas = patchy::Rect::from_size(document.width(), document.height());
  for (const auto& expected : kLayers) {
    const auto& layer = require_layer_named(document, expected.name);
    CHECK(patchy::layer_is_smart_object(layer));
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto* stack = layer.smart_filter_stack();
    CHECK(stack != nullptr && stack->support == patchy::SmartFilterStackSupport::Supported);
    const auto& last = stack->entries.back();
    CHECK(last.kind == patchy::SmartFilterKind::Adjustment || stack->entries.front().kind ==
                                                                  patchy::SmartFilterKind::Adjustment);
    const auto& first_adjustment = stack->entries.front().kind == patchy::SmartFilterKind::Adjustment
                                       ? stack->entries.front()
                                       : last;
    CHECK(adjustment_of(first_adjustment).kind == expected.kind);
    const auto rendered = patchy::render_smart_filter_stack(source.pixels(), source.bounds(), canvas, *stack);
    const auto difference = compare_with_photoshop(rendered, layer);
    std::cout << "  note: " << expected.name << ": largest " << difference.largest << ", over one "
              << difference.over_one << ", alpha misses " << difference.alpha_misses << ", worst at "
              << difference.worst[0] << "," << difference.worst[1] << " alpha " << difference.worst[5]
              << ", largest opaque " << difference.largest_opaque
              << "\n";
    CHECK(difference.alpha_misses == 0);
    CHECK(difference.largest <= expected.largest);
    CHECK(difference.largest_opaque <= expected.largest_opaque);
    CHECK(difference.over_one <= expected.over_one);
  }

  // The values the descriptors carry.
  const auto& levels = adjustment_of(
      require_layer_named(document, "Levels channels preset").smart_filter_stack()->entries.front());
  CHECK(levels.levels.red.black_input == 20 && levels.levels.red.white_input == 240 &&
        levels.levels.red.gamma_percent == 150);
  CHECK(levels.levels.blue.gamma_percent == 60 && levels.levels.blue.black_output == 10 &&
        levels.levels.blue.white_output == 250);
  CHECK(levels.levels.gamma_percent == 70 && levels.levels.green == patchy::LevelsRecord{});
  const auto& colorize = adjustment_of(
      require_layer_named(document, "Hue Saturation colorize").smart_filter_stack()->entries.front());
  CHECK(colorize.hue_saturation.colorize && colorize.hue_saturation.colorize_hue == 30 &&
        colorize.hue_saturation.colorize_saturation == 25);
  const auto& legacy = adjustment_of(
      require_layer_named(document, "Brightness Contrast legacy").smart_filter_stack()->entries.front());
  CHECK(legacy.brightness_contrast.use_legacy && legacy.brightness_contrast.brightness == -30 &&
        legacy.brightness_contrast.contrast == 40);
  const auto& photo = adjustment_of(
      require_layer_named(document, "Photo Filter").smart_filter_stack()->entries.front());
  CHECK(photo.photo_filter.lab_l == 6706 && photo.photo_filter.lab_a == 3200 && photo.photo_filter.lab_b == 6600 &&
        photo.photo_filter.density == 25 && photo.photo_filter.preserve_luminosity);
  const auto& photo_entry = require_layer_named(document, "Photo Filter").smart_filter_stack()->entries.front();
  CHECK(photo_entry.native_class_id == "photoFilter");
}

void smart_filter_adjustments_photoshop_capture_round_trips_and_patches() {
  const auto original = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto clean = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(original));
  for (const auto& expected : kLayers) {
    CHECK(sold_block(require_layer_named(clean, expected.name)).payload ==
          sold_block(require_layer_named(original, expected.name)).payload);
  }

  auto edited = original;
  const auto edit = [&edited](std::string_view name, auto change) {
    auto* layer = edited.find_layer(require_layer_named(edited, name).id());
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    auto stack = *layer->smart_filter_stack();
    auto& settings = std::get<patchy::AdjustmentSmartFilter>(stack.entries.front().parameters).settings;
    change(settings);
    layer->set_smart_filter_stack(std::move(stack));
    patchy::mark_layer_smart_object_block_dirty(*layer);
  };
  // Unchanged settings on a dirty block keep the Fltr object byte for byte.
  edit("Curves channels preset", [](patchy::AdjustmentSettings&) {});
  edit("Levels master", [](patchy::AdjustmentSettings& settings) {
    settings.levels.gamma_percent = 135;
    settings.levels.green.white_input = 200;
  });
  edit("Hue Saturation master", [](patchy::AdjustmentSettings& settings) {
    settings.hue_saturation.hue_shift = -90;
  });
  edit("Exposure", [](patchy::AdjustmentSettings& settings) { settings.exposure.exposure_hundredths = -50; });
  edit("Channel Mixer monochrome", [](patchy::AdjustmentSettings& settings) {
    settings.channel_mixer.gray.constant = 12;
  });
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto settings_of = [&reread](std::string_view name) {
    const auto& layer = require_layer_named(reread, name);
    CHECK(layer.smart_filter_stack() != nullptr &&
          layer.smart_filter_stack()->support == patchy::SmartFilterStackSupport::Supported);
    return adjustment_of(layer.smart_filter_stack()->entries.front());
  };
  const auto filter_bytes = [](const patchy::Document& document, std::string_view name) {
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_descriptor(writer,
                                  *entry_filter(sold_descriptor(sold_block(require_layer_named(document, name)).payload)));
    return writer.bytes();
  };
  CHECK(filter_bytes(reread, "Curves channels preset") == filter_bytes(original, "Curves channels preset"));
  const auto levels = settings_of("Levels master");
  CHECK(levels.levels.gamma_percent == 135 && levels.levels.black_input == 8 && levels.levels.white_input == 210);
  CHECK(levels.levels.green.white_input == 200);
  CHECK(settings_of("Hue Saturation master").hue_saturation.hue_shift == -90);
  CHECK(settings_of("Hue Saturation master").hue_saturation.saturation_delta == 30);
  CHECK(settings_of("Exposure").exposure.exposure_hundredths == -50);
  CHECK(settings_of("Exposure").exposure.gamma_hundredths == 90);
  CHECK(settings_of("Channel Mixer monochrome").channel_mixer.gray.constant == 12);
  CHECK(settings_of("Channel Mixer monochrome").channel_mixer.monochrome);
  // An edit keeps the dialog's preset state and the Fltr class and name.
  const auto hue_descriptor =
      sold_descriptor(sold_block(require_layer_named(reread, "Hue Saturation master")).payload);
  const auto* hue = entry_filter(hue_descriptor);
  CHECK(hue->class_id == "HStr" && hue->name == "Hue/Saturation");
  CHECK(patchy::psd::descriptor_value(*hue, "presetKind") != nullptr);
}

void smart_filter_adjustments_fail_closed_outside_the_model() {
  const auto original = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto parse = [](const patchy::psd::DescriptorObject& filter, std::uint32_t filter_id) {
    patchy::SmartFilterEntry entry;
    entry.native_filter_id = filter_id;
    bool supported = true;
    const bool handled = patchy::psd::parse_adjustment_smart_filter(&filter, entry, supported);
    CHECK(handled);
    return supported && entry.kind == patchy::SmartFilterKind::Adjustment;
  };
  const auto levels_descriptor =
      sold_descriptor(sold_block(require_layer_named(original, "Levels master")).payload);
  auto levels = *entry_filter(levels_descriptor);
  constexpr std::uint32_t kLevelsId = 0x4c766c73U;
  CHECK(parse(levels, kLevelsId));
  CHECK(!parse(levels, 0x43727673U));  // a Levels Fltr under another filterID

  // Levels Auto computes its values in Photoshop: not modeled.
  auto automatic = levels;
  patchy::psd::DescriptorValue on;
  on.type = patchy::psd::DescriptorValue::Type::Bool;
  on.bool_value = true;
  automatic.values["Auto"] = on;
  automatic.key_order.push_back({"Auto", false});
  CHECK(!parse(automatic, kLevelsId));

  // An inverted output range, a gamma off Patchy's range, a CMYK channel record.
  auto record = *levels.values.at("Adjs").list_value.front().object_value;
  const auto with_record = [&levels](const patchy::psd::DescriptorObject& changed) {
    auto copy = levels;
    copy.values["Adjs"].list_value.front().object_value =
        std::make_shared<patchy::psd::DescriptorObject>(changed);
    return copy;
  };
  auto gamma = record;
  gamma.values["Gmm "].double_value = 0.05;
  CHECK(!parse(with_record(gamma), kLevelsId));
  auto inverted = record;
  patchy::psd::DescriptorValue output;
  output.type = patchy::psd::DescriptorValue::Type::List;
  for (const int value : {240, 10}) {
    patchy::psd::DescriptorValue item;
    item.type = patchy::psd::DescriptorValue::Type::Integer;
    item.integer_value = value;
    output.list_value.push_back(item);
  }
  inverted.values["Otpt"] = output;
  inverted.key_order.push_back({"Otpt", false});
  CHECK(!parse(with_record(inverted), kLevelsId));
  auto black = record;
  black.values["Chnl"].reference_items.front().id_b = "Blck";
  CHECK(!parse(with_record(black), kLevelsId));

  // A per-hue-range band record (more than one Hst2) stays unmodeled.
  const auto hue_descriptor =
      sold_descriptor(sold_block(require_layer_named(original, "Hue Saturation master")).payload);
  auto hue = *entry_filter(hue_descriptor);
  constexpr std::uint32_t kHueId = 0x48537472U;
  CHECK(parse(hue, kHueId));
  auto banded = hue;
  banded.values["Adjs"].list_value.push_back(banded.values["Adjs"].list_value.front());
  CHECK(!parse(banded, kHueId));

  // A band edit cannot be written in the recorded form; the writer refuses it.
  auto settings = adjustment_of(require_layer_named(original, "Hue Saturation master").smart_filter_stack()->entries.front());
  settings.hue_saturation.bands[0].hue_shift = 20;
  auto writable = hue;
  CHECK(!patchy::psd::write_adjustment_smart_filter(writable, settings));

  // A filter that is not an adjustment command is left to the other readers.
  patchy::psd::DescriptorObject gaussian;
  gaussian.class_id = "GsnB";
  patchy::SmartFilterEntry entry;
  bool supported = true;
  CHECK(!patchy::psd::parse_adjustment_smart_filter(&gaussian, entry, supported));
  CHECK(!patchy::psd::parse_adjustment_smart_filter(nullptr, entry, supported));
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_adjustment_tests() {
  return {
      {"smart_filter_adjustments_photoshop_capture_parses_and_renders",
       smart_filter_adjustments_photoshop_capture_parses_and_renders},
      {"smart_filter_adjustments_photoshop_capture_round_trips_and_patches",
       smart_filter_adjustments_photoshop_capture_round_trips_and_patches},
      {"smart_filter_adjustments_fail_closed_outside_the_model",
       smart_filter_adjustments_fail_closed_outside_the_model},
  };
}
