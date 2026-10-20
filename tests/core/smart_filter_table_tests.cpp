// Table-described native Smart Filters (core/smart_filter_table.hpp,
// docs/smart-filters-pixelate-render.md): Photoshop 2026 captures parse, render
// byte for byte, round-trip and patch.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::require_layer_named;

constexpr const char *kFixture = "photoshop-smart-filter-other.psd";
constexpr const char *kRandomFixture = "photoshop-smart-filter-random.psd";

const patchy::UnknownPsdBlock &sold_block(const patchy::Layer &layer) {
  const auto &blocks = layer.unknown_psd_blocks();
  const auto found = std::find_if(
      blocks.begin(), blocks.end(),
      [](const patchy::UnknownPsdBlock &block) { return block.key == "SoLd"; });
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

// The first filterFX entry object of a SoLd descriptor.
const patchy::psd::DescriptorObject *first_entry(
    const patchy::psd::DescriptorObject &descriptor, std::size_t index = 0) {
  const auto *root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr && list->list_value.size() > index);
  return list->list_value[index].object_value.get();
}

std::vector<std::uint8_t> descriptor_bytes(
    const patchy::psd::DescriptorObject &object) {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_descriptor(writer, object);
  return writer.bytes();
}

const patchy::TableSmartFilter &table_values(const patchy::SmartFilterEntry &entry) {
  const auto *values = std::get_if<patchy::TableSmartFilter>(&entry.parameters);
  CHECK(values != nullptr);
  return *values;
}

// Photoshop's own render of the layer against Patchy's: alpha everywhere and
// straight RGB wherever Photoshop's result is visible.
void check_matches_photoshop(const patchy::FilterRenderResult &rendered,
                             const patchy::Layer &photoshop,
                             std::string_view label) {
  const auto expected_bounds = photoshop.bounds();
  const auto &expected = photoshop.pixels();
  const auto x0 = std::min(rendered.bounds.x, expected_bounds.x);
  const auto y0 = std::min(rendered.bounds.y, expected_bounds.y);
  const auto x1 = std::max(rendered.bounds.x + rendered.bounds.width,
                           expected_bounds.x + expected_bounds.width);
  const auto y1 = std::max(rendered.bounds.y + rendered.bounds.height,
                           expected_bounds.y + expected_bounds.height);
  const auto sample = [](const patchy::PixelBuffer &pixels, patchy::Rect bounds,
                         std::int32_t x, std::int32_t y) {
    std::array<std::uint8_t, 4> value{0, 0, 0, 0};
    if (x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width &&
        y < bounds.y + bounds.height) {
      const auto *pixel = pixels.pixel(x - bounds.x, y - bounds.y);
      std::copy(pixel, pixel + 4, value.begin());
    }
    return value;
  };
  int mismatches = 0;
  for (std::int32_t y = y0; y < y1; ++y) {
    for (std::int32_t x = x0; x < x1; ++x) {
      const auto want = sample(expected, expected_bounds, x, y);
      const auto got = sample(rendered.pixels, rendered.bounds, x, y);
      const bool equal = want[3] == got[3] &&
                         (want[3] == 0 || (want[0] == got[0] &&
                                           want[1] == got[1] &&
                                           want[2] == got[2]));
      if (!equal && mismatches++ < 3) {
        std::cerr << label << " differs at " << x << "," << y << ": photoshop "
                  << int(want[0]) << "," << int(want[1]) << "," << int(want[2])
                  << "," << int(want[3]) << " patchy " << int(got[0]) << ","
                  << int(got[1]) << "," << int(got[2]) << "," << int(got[3])
                  << "\n";
      }
    }
  }
  CHECK(mismatches == 0);
}

struct ExpectedLayer {
  const char *name;
  patchy::SmartFilterKind kind;
};

constexpr ExpectedLayer kSupportedLayers[]{
    {"Offset wrap 20 -7", patchy::SmartFilterKind::Offset},
    {"Offset transparent 20 -7", patchy::SmartFilterKind::Offset},
    {"Offset repeat -15 9", patchy::SmartFilterKind::Offset},
    {"Maximum 2.6 square", patchy::SmartFilterKind::Maximum},
    {"Maximum 3 no shape key", patchy::SmartFilterKind::Maximum},
    {"Minimum 1.5 square", patchy::SmartFilterKind::Minimum},
    {"Minimum 3 square", patchy::SmartFilterKind::Minimum},
    {"Custom sharpen", patchy::SmartFilterKind::Custom},
    {"Custom scale 193", patchy::SmartFilterKind::Custom},
    {"Custom negative", patchy::SmartFilterKind::Custom},
    {"Fragment", patchy::SmartFilterKind::Fragment},
    {"Fragment then Minimum 2", patchy::SmartFilterKind::Fragment},
};

void smart_filter_table_photoshop_capture_parses_and_renders_exactly() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(kFixture));
  const auto &source = require_layer_named(document, "Source");
  CHECK(source.smart_filter_stack() == nullptr);
  const auto canvas = patchy::Rect::from_size(document.width(), document.height());

  for (const auto &expected : kSupportedLayers) {
    const auto &layer = require_layer_named(document, expected.name);
    CHECK(patchy::layer_is_smart_object(layer));
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto *stack = layer.smart_filter_stack();
    CHECK(stack != nullptr &&
          stack->support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack->entries.front().kind == expected.kind);
    const auto rendered = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, *stack);
    check_matches_photoshop(rendered, layer, expected.name);
  }

  const auto &offset = require_layer_named(document, "Offset repeat -15 9");
  const auto &offset_entry = offset.smart_filter_stack()->entries.front();
  CHECK(offset_entry.native_name == "Offset...");
  CHECK(offset_entry.native_class_id == "Ofst");
  CHECK(offset_entry.native_filter_id == 0x4f667374U);
  CHECK(table_values(offset_entry).values ==
        (std::vector<patchy::SmartFilterTableValue>{
            std::int32_t{-15}, std::int32_t{9}, std::string("repeat")}));

  const auto &custom = require_layer_named(document, "Custom negative");
  const auto &custom_values =
      table_values(custom.smart_filter_stack()->entries.front());
  CHECK(std::get<std::int32_t>(custom_values.values[1]) == 7);
  CHECK(std::get<std::int32_t>(custom_values.values[2]) == -100);
  CHECK(std::get<std::vector<std::int32_t>>(custom_values.values[0])[24] == 999);

  // Photoshop's Fragment has no settings and so no Fltr object.
  const auto &fragment = require_layer_named(document, "Fragment");
  const auto &fragment_entry = fragment.smart_filter_stack()->entries.front();
  CHECK(fragment_entry.native_name == "Fragment");
  CHECK(fragment_entry.native_class_id.empty());
  CHECK(!fragment_entry.has_options);
  CHECK(table_values(fragment_entry).values.empty());

  // Preserve: Roundness anti-aliases its window and is not calibrated, so that
  // stack keeps Photoshop's preview.
  const auto &round = require_layer_named(document, "Maximum 4 roundness");
  const auto *round_stack = round.smart_filter_stack();
  CHECK(round_stack != nullptr &&
        round_stack->support == patchy::SmartFilterStackSupport::Unsupported);
  CHECK(round_stack->entries.front().kind == patchy::SmartFilterKind::Unsupported);
  CHECK(!patchy::smart_object_lock_reason(round).empty());
}

void smart_filter_table_photoshop_capture_round_trips_and_patches() {
  const auto original = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(kFixture));
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  for (const auto &expected : kSupportedLayers) {
    CHECK(sold_block(require_layer_named(clean, expected.name)).payload ==
          sold_block(require_layer_named(original, expected.name)).payload);
  }
  CHECK(sold_block(require_layer_named(clean, "Maximum 4 roundness")).payload ==
        sold_block(require_layer_named(original, "Maximum 4 roundness")).payload);

  auto edited = original;
  const auto edit = [&edited](std::string_view name, auto change) {
    auto *layer = edited.find_layer(require_layer_named(edited, name).id());
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    auto stack = *layer->smart_filter_stack();
    change(std::get<patchy::TableSmartFilter>(stack.entries.front().parameters));
    layer->set_smart_filter_stack(std::move(stack));
    patchy::mark_layer_smart_object_block_dirty(*layer);
  };
  edit("Offset wrap 20 -7", [](patchy::TableSmartFilter &values) {
    values.values[0] = std::int32_t{-33};
    values.values[2] = std::string("transparent");
  });
  edit("Maximum 3 no shape key", [](patchy::TableSmartFilter &values) {
    values.values[0] = 5.5;
  });
  edit("Custom sharpen", [](patchy::TableSmartFilter &values) {
    std::get<std::vector<std::int32_t>>(values.values[0])[12] = 13;
    values.values[1] = std::int32_t{5};
  });
  const auto reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));

  const auto &offset = require_layer_named(reread, "Offset wrap 20 -7");
  CHECK(table_values(offset.smart_filter_stack()->entries.front()).values ==
        (std::vector<patchy::SmartFilterTableValue>{
            std::int32_t{-33}, std::int32_t{-7}, std::string("transparent")}));
  const auto offset_filter = sold_descriptor(sold_block(offset).payload);
  const auto *offset_fltr =
      patchy::psd::descriptor_object(*first_entry(offset_filter), "Fltr");
  CHECK(offset_fltr != nullptr && offset_fltr->key_order.size() == 3U);
  CHECK(patchy::psd::descriptor_value(*offset_fltr, "Fl  ")->enum_value == "Bckg");

  // A Maximum Photoshop wrote without preserveShape stays without it.
  const auto &maximum = require_layer_named(reread, "Maximum 3 no shape key");
  CHECK(std::get<double>(table_values(maximum.smart_filter_stack()->entries.front())
                             .values[0]) == 5.5);
  const auto maximum_descriptor = sold_descriptor(sold_block(maximum).payload);
  const auto *maximum_fltr =
      patchy::psd::descriptor_object(*first_entry(maximum_descriptor), "Fltr");
  CHECK(maximum_fltr != nullptr && maximum_fltr->key_order.size() == 1U);

  const auto &custom = require_layer_named(reread, "Custom sharpen");
  const auto &custom_values = table_values(custom.smart_filter_stack()->entries.front());
  CHECK(std::get<std::vector<std::int32_t>>(custom_values.values[0])[12] == 13);
  CHECK(std::get<std::int32_t>(custom_values.values[1]) == 5);
  CHECK(std::get<std::int32_t>(custom_values.values[2]) == 10);

  // Unedited layers keep their bytes.
  CHECK(sold_block(require_layer_named(reread, "Fragment")).payload ==
        sold_block(require_layer_named(original, "Fragment")).payload);
}

// A fresh entry with Photoshop's values authors the same Fltr bytes Photoshop
// wrote (Maximum's capture without preserveShape aside: Patchy always writes
// the key, as Photoshop's dialog does).
void check_authored_filter_matches_photoshop(const patchy::Layer &layer) {
  const auto photoshop = sold_descriptor(sold_block(layer).payload);
  auto stack = *layer.smart_filter_stack();
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-828282828282";
  placement.transform = {0.0, 0.0, 128.0, 0.0, 128.0, 96.0, 0.0, 96.0};
  placement.width = 128.0;
  placement.height = 96.0;
  placement.resolution = 72.0;
  const auto authored = sold_descriptor(patchy::psd::author_placed_layer_sold_payload(
      placement, "dfdfdfdf-fbfb-bdbd-8444-585858585858", &stack));
  for (std::size_t index = 0; index < stack.entries.size(); ++index) {
    const auto *photoshop_entry = first_entry(photoshop, index);
    const auto *authored_entry = first_entry(authored, index);
    const auto *photoshop_filter =
        patchy::psd::descriptor_object(*photoshop_entry, "Fltr");
    const auto *authored_filter =
        patchy::psd::descriptor_object(*authored_entry, "Fltr");
    CHECK((photoshop_filter == nullptr) == (authored_filter == nullptr));
    if (photoshop_filter != nullptr && authored_filter != nullptr) {
      CHECK(descriptor_bytes(*photoshop_filter) ==
            descriptor_bytes(*authored_filter));
    }
    CHECK(patchy::psd::descriptor_value(*authored_entry, "filterID")->integer_value ==
          patchy::psd::descriptor_value(*photoshop_entry, "filterID")->integer_value);
    CHECK(patchy::psd::descriptor_value(*authored_entry, "Nm  ")->string_value ==
          patchy::psd::descriptor_value(*photoshop_entry, "Nm  ")->string_value);
  }
}

void smart_filter_table_authored_descriptors_match_photoshop() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(kFixture));
  for (const auto &expected : kSupportedLayers) {
    if (std::string_view(expected.name) != "Maximum 3 no shape key") {
      check_authored_filter_matches_photoshop(
          require_layer_named(document, expected.name));
    }
  }
}

// Crystallize, Pointillize and Mezzotint re-randomize on every Photoshop
// render (no stored seed), so their pixels are Patchy's own: the descriptors
// must still parse, author and round-trip exactly, and the renders must be
// deterministic, keep alpha and stay in Photoshop's style.
void smart_filter_table_random_filters_round_trip_and_render_in_style() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(kRandomFixture));
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto &source = require_layer_named(document, "Source");
  const auto canvas = patchy::Rect::from_size(document.width(), document.height());
  const auto source_alpha = [&](std::int32_t x, std::int32_t y) -> int {
    const auto bounds = source.bounds();
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width ||
        y >= bounds.y + bounds.height) {
      return 0;
    }
    return source.pixels().pixel(x - bounds.x, y - bounds.y)[3];
  };
  struct Case {
    const char *name;
    patchy::SmartFilterKind kind;
    double maximum_mean_difference;  // per visible channel sample vs Photoshop
  };
  // Photoshop's own re-renders differ from each other by about 41
  // (Crystallize 10) and 102 (Mezzotint fine dots) per visible channel sample
  // on the probe; Patchy's renders stay within 1.25 times that (42 and 92
  // today). Pointillize and long strokes, with no Photoshop pair measured,
  // stay within 1.25 times their October 2026 values (59 and 67).
  const Case cases[]{
      {"Crystallize 10", patchy::SmartFilterKind::Crystallize, 52.0},
      {"Pointillize 6", patchy::SmartFilterKind::Pointillize, 74.0},
      {"Mezzotint fine dots", patchy::SmartFilterKind::Mezzotint, 128.0},
      {"Mezzotint long strokes", patchy::SmartFilterKind::Mezzotint, 84.0},
  };
  for (const auto &item : cases) {
    const auto &layer = require_layer_named(document, item.name);
    const auto *stack = layer.smart_filter_stack();
    CHECK(stack != nullptr &&
          stack->support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack->entries.front().kind == item.kind);
    CHECK(sold_block(require_layer_named(clean, item.name)).payload ==
          sold_block(layer).payload);
    check_authored_filter_matches_photoshop(layer);

    const auto first = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, *stack);
    const auto second = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, *stack);
    CHECK(first.bounds.x == second.bounds.x && first.bounds.y == second.bounds.y &&
          first.bounds.width == second.bounds.width &&
          first.bounds.height == second.bounds.height);
    CHECK(std::equal(first.pixels.data().begin(), first.pixels.data().end(),
                     second.pixels.data().begin()));

    double difference = 0.0;
    std::int64_t samples = 0;
    const auto photoshop_bounds = layer.bounds();
    for (std::int32_t y = 0; y < canvas.height; ++y) {
      for (std::int32_t x = 0; x < canvas.width; ++x) {
        const auto inside = x >= first.bounds.x && y >= first.bounds.y &&
                            x < first.bounds.x + first.bounds.width &&
                            y < first.bounds.y + first.bounds.height;
        const auto *ours = inside ? first.pixels.pixel(x - first.bounds.x,
                                                        y - first.bounds.y)
                                  : nullptr;
        CHECK((ours == nullptr ? 0 : ours[3]) == source_alpha(x, y));
        if (ours == nullptr || ours[3] == 0) {
          continue;
        }
        if (item.kind == patchy::SmartFilterKind::Mezzotint) {
          for (int channel = 0; channel < 3; ++channel) {
            CHECK(ours[channel] == 0 || ours[channel] == 255);
          }
        }
        if (x < photoshop_bounds.x || y < photoshop_bounds.y ||
            x >= photoshop_bounds.x + photoshop_bounds.width ||
            y >= photoshop_bounds.y + photoshop_bounds.height) {
          continue;
        }
        const auto *theirs = layer.pixels().pixel(x - photoshop_bounds.x,
                                                  y - photoshop_bounds.y);
        for (int channel = 0; channel < 3; ++channel) {
          difference += std::abs(static_cast<int>(ours[channel]) -
                                 static_cast<int>(theirs[channel]));
          ++samples;
        }
      }
    }
    const auto mean = samples == 0 ? 0.0 : difference / static_cast<double>(samples);
    std::cout << "[INFO] " << item.name << " mean difference from Photoshop's render "
              << mean << "\n";
    CHECK(samples > 0 && mean <= item.maximum_mean_difference);
  }
}

// Kernel contracts independent of the capture.
void smart_filter_table_kernels_keep_their_contracts() {
  patchy::PixelBuffer pixels(9, 7, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 9; ++x) {
      auto *pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(x * 29);
      pixel[1] = static_cast<std::uint8_t>(y * 37);
      pixel[2] = static_cast<std::uint8_t>((x * y * 11) & 255);
      pixel[3] = 255;
    }
  }
  // One transparent pixel; the stack does not keep hidden color under alpha 0.
  std::fill(pixels.pixel(4, 3), pixels.pixel(4, 3) + 4, std::uint8_t{0});
  const auto bounds = patchy::Rect::from_size(9, 7);
  // Same alpha everywhere and same color wherever visible.
  const auto same_visible = [](const patchy::PixelBuffer &a,
                               const patchy::PixelBuffer &b) {
    if (a.width() != b.width() || a.height() != b.height()) {
      return false;
    }
    for (std::int32_t y = 0; y < a.height(); ++y) {
      for (std::int32_t x = 0; x < a.width(); ++x) {
        const auto *p = a.pixel(x, y);
        const auto *q = b.pixel(x, y);
        if (p[3] != q[3] ||
            (p[3] != 0 && (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]))) {
          return false;
        }
      }
    }
    return true;
  };
  const auto stack_of = [](patchy::SmartFilterKind kind,
                           std::vector<patchy::SmartFilterTableValue> values) {
    patchy::SmartFilterStack stack;
    stack.support = patchy::SmartFilterStackSupport::Supported;
    stack.mask.linked = false;
    patchy::SmartFilterEntry entry;
    entry.kind = kind;
    entry.parameters = patchy::TableSmartFilter{std::move(values)};
    stack.entries.push_back(std::move(entry));
    return stack;
  };
  // Wrapping by the whole canvas is the identity.
  const auto wrapped = patchy::render_smart_filter_stack(
      pixels, bounds,
      stack_of(patchy::SmartFilterKind::Offset,
               {std::int32_t{9}, std::int32_t{-14}, std::string("wrap")}));
  CHECK(wrapped.bounds.x == 0 && wrapped.bounds.width == 9);
  CHECK(same_visible(wrapped.pixels, pixels));
  // Radii below 1.5 round to a 3x3 window; 0.2 still renders as 1.
  const auto tiny_radius = patchy::render_smart_filter_stack(
      pixels, bounds,
      stack_of(patchy::SmartFilterKind::Minimum, {0.2, std::string("squareness")}));
  const auto one = patchy::render_smart_filter_stack(
      pixels, bounds,
      stack_of(patchy::SmartFilterKind::Minimum, {1.49, std::string("squareness")}));
  CHECK(same_visible(tiny_radius.pixels, one.pixels));
  // Minimum fills the transparent pixel (alpha takes the maximum) with the
  // window's darkest color; Maximum makes its neighbours transparent.
  CHECK(one.pixels.pixel(4, 3)[3] == 255U);
  const auto grown = patchy::render_smart_filter_stack(
      pixels, bounds,
      stack_of(patchy::SmartFilterKind::Maximum, {1.0, std::string("squareness")}));
  CHECK(grown.pixels.pixel(4 - grown.bounds.x, 3 - grown.bounds.y)[3] == 0U);
  CHECK(grown.pixels.pixel(3 - grown.bounds.x, 2 - grown.bounds.y)[3] == 0U);
  // The identity Custom kernel (center 1, scale 1, offset 0) keeps every pixel.
  std::vector<std::int32_t> identity(25, 0);
  identity[12] = 1;
  const auto same = patchy::render_smart_filter_stack(
      pixels, bounds,
      stack_of(patchy::SmartFilterKind::Custom,
               {identity, std::int32_t{1}, std::int32_t{0}}));
  CHECK(same_visible(same.pixels, pixels));
  // Out-of-range settings never render.
  bool threw = false;
  try {
    (void)patchy::render_smart_filter_stack(
        pixels, bounds,
        stack_of(patchy::SmartFilterKind::Custom,
                 {identity, std::int32_t{0}, std::int32_t{0}}));
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_table_tests() {
  return {
      {"smart_filter_table_photoshop_capture_parses_and_renders_exactly",
       smart_filter_table_photoshop_capture_parses_and_renders_exactly},
      {"smart_filter_table_photoshop_capture_round_trips_and_patches",
       smart_filter_table_photoshop_capture_round_trips_and_patches},
      {"smart_filter_table_authored_descriptors_match_photoshop",
       smart_filter_table_authored_descriptors_match_photoshop},
      {"smart_filter_table_random_filters_round_trip_and_render_in_style",
       smart_filter_table_random_filters_round_trip_and_render_in_style},
      {"smart_filter_table_kernels_keep_their_contracts",
       smart_filter_table_kernels_keep_their_contracts},
  };
}
