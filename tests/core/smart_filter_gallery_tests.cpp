// Photoshop's Filter Gallery Smart Filter entry (GEfc, docs/smart-filters-native.md): the
// October 2026 capture parses into the gallery model, renders Plastic Wrap exactly as the
// Plastic Wrap filter does, round-trips byte for byte and patches edited settings in place.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_smart_filter_gallery.hpp"
#include "psd/psd_smart_objects.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::require_layer_named;

constexpr const char* kFixture = "photoshop-smart-filter-gallery.psd";

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

const patchy::psd::DescriptorObject* entry_filter(const patchy::psd::DescriptorObject& descriptor,
                                                  std::size_t index = 0) {
  const auto* root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto* list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr && list->list_value.size() > index);
  return patchy::psd::descriptor_object(*list->list_value[index].object_value, "Fltr");
}

const patchy::FilterGallerySmartFilter& gallery_of(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::FilterGallery);
  const auto* gallery = std::get_if<patchy::FilterGallerySmartFilter>(&entry.parameters);
  CHECK(gallery != nullptr);
  return *gallery;
}

const patchy::PlasticWrapSmartFilter& plastic_of(const patchy::FilterGalleryEffect& effect) {
  CHECK(effect.effect_id == "PlsW");
  const auto* plastic = std::get_if<patchy::PlasticWrapSmartFilter>(&effect.settings);
  CHECK(plastic != nullptr);
  return *plastic;
}

bool same_render(const patchy::FilterRenderResult& a, const patchy::FilterRenderResult& b) {
  if (a.bounds.x != b.bounds.x || a.bounds.y != b.bounds.y || a.bounds.width != b.bounds.width ||
      a.bounds.height != b.bounds.height) {
    return false;
  }
  const auto left = a.pixels.data();
  const auto right = b.pixels.data();
  return std::equal(left.begin(), left.end(), right.begin(), right.end());
}

void smart_filter_gallery_photoshop_capture_parses_and_renders_like_plastic_wrap() {
  const auto document = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto& source = require_layer_named(document, "Source");
  const auto canvas = patchy::Rect::from_size(document.width(), document.height());
  const auto render = [&](const patchy::SmartFilterStack& stack) {
    return patchy::render_smart_filter_stack(source.pixels(), source.bounds(), canvas, stack);
  };
  const auto supported_stack = [&](std::string_view name) -> const patchy::SmartFilterStack& {
    const auto& layer = require_layer_named(document, name);
    CHECK(patchy::layer_is_smart_object(layer));
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto* stack = layer.smart_filter_stack();
    CHECK(stack != nullptr && stack->support == patchy::SmartFilterStackSupport::Supported);
    return *stack;
  };

  // One effect sits inline in the GEfc object.
  const auto& single = supported_stack("Gallery Plastic Wrap");
  const auto& single_entry = single.entries.front();
  CHECK(single_entry.native_name == "Filter Gallery...");
  CHECK(single_entry.native_class_id == "GEfc");
  CHECK(single_entry.native_filter_id == patchy::psd::kFilterGalleryFilterId);
  const auto& single_gallery = gallery_of(single_entry);
  CHECK(!single_gallery.listed && single_gallery.effects.size() == 1U);
  const auto& single_plastic = plastic_of(single_gallery.effects.front());
  CHECK(single_plastic.highlight_strength == 13 && single_plastic.detail == 9 && single_plastic.smoothness == 7);
  CHECK(patchy::smart_filter_dialog_kind(single_entry) == patchy::SmartFilterKind::PlasticWrap);
  CHECK(patchy::editable_plastic_wrap(single_entry) == &single_plastic);

  // The same effect applied from its own menu item renders identically in Patchy.
  const auto& direct = supported_stack("Direct Plastic Wrap");
  CHECK(direct.entries.front().kind == patchy::SmartFilterKind::PlasticWrap);
  CHECK(same_render(render(single), render(direct)));

  // Two or more effects sit in a GEfs list and run in order.
  const auto& listed = supported_stack("Gallery two Plastic Wraps");
  const auto& listed_gallery = gallery_of(listed.entries.front());
  CHECK(listed_gallery.listed && listed_gallery.effects.size() == 2U);
  const auto& second = plastic_of(listed_gallery.effects[1]);
  CHECK(second.highlight_strength == 5 && second.detail == 3 && second.smoothness == 12);
  CHECK(patchy::smart_filter_dialog_kind(listed.entries.front()) == patchy::SmartFilterKind::FilterGallery);
  CHECK(patchy::editable_plastic_wrap(listed.entries.front()) == nullptr);
  auto chained = direct;
  auto second_entry = direct.entries.front();
  second_entry.parameters = second;
  chained.entries.push_back(second_entry);
  CHECK(same_render(render(listed), render(chained)));

  // A gallery entry followed by another Smart Filter.
  const auto& then_gaussian = supported_stack("Gallery Plastic Wrap then Gaussian");
  CHECK(then_gaussian.entries.size() == 2U);
  CHECK(then_gaussian.entries.back().kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK(plastic_of(gallery_of(then_gaussian.entries.front()).effects.front()).highlight_strength == 20);

  // A gallery effect Patchy does not render, or settings an action left out, keep
  // Photoshop's preview.
  for (const auto* name : {"Gallery Plastic Wrap and Cutout", "Gallery Plastic Wrap defaults"}) {
    const auto& layer = require_layer_named(document, name);
    const auto* stack = layer.smart_filter_stack();
    CHECK(stack != nullptr && stack->support == patchy::SmartFilterStackSupport::Unsupported);
    CHECK(stack->entries.front().kind == patchy::SmartFilterKind::Unsupported);
    CHECK(!patchy::smart_object_lock_reason(layer).empty());
  }
}

void smart_filter_gallery_photoshop_capture_round_trips_and_patches() {
  const auto original = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto clean = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(original));
  for (const auto* name : {"Gallery Plastic Wrap", "Gallery two Plastic Wraps", "Gallery Plastic Wrap then Gaussian",
                           "Gallery Plastic Wrap and Cutout", "Gallery Plastic Wrap defaults"}) {
    CHECK(sold_block(require_layer_named(clean, name)).payload ==
          sold_block(require_layer_named(original, name)).payload);
  }

  auto edited = original;
  const auto edit = [&edited](std::string_view name, auto change) {
    auto* layer = edited.find_layer(require_layer_named(edited, name).id());
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    auto stack = *layer->smart_filter_stack();
    change(std::get<patchy::FilterGallerySmartFilter>(stack.entries.front().parameters));
    layer->set_smart_filter_stack(std::move(stack));
    patchy::mark_layer_smart_object_block_dirty(*layer);
  };
  edit("Gallery Plastic Wrap", [](patchy::FilterGallerySmartFilter& gallery) {
    std::get<patchy::PlasticWrapSmartFilter>(gallery.effects.front().settings) = {4, 2, 15};
  });
  edit("Gallery two Plastic Wraps", [](patchy::FilterGallerySmartFilter& gallery) {
    std::get<patchy::PlasticWrapSmartFilter>(gallery.effects[1].settings).detail = 14;
  });
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto gallery_now = [&reread](std::string_view name) {
    const auto& layer = require_layer_named(reread, name);
    CHECK(layer.smart_filter_stack() != nullptr &&
          layer.smart_filter_stack()->support == patchy::SmartFilterStackSupport::Supported);
    return gallery_of(layer.smart_filter_stack()->entries.front());
  };
  const auto single = gallery_now("Gallery Plastic Wrap");
  CHECK(plastic_of(single.effects.front()).highlight_strength == 4);
  CHECK(plastic_of(single.effects.front()).detail == 2);
  CHECK(plastic_of(single.effects.front()).smoothness == 15);
  const auto listed = gallery_now("Gallery two Plastic Wraps");
  CHECK(plastic_of(listed.effects[1]).detail == 14);
  CHECK(plastic_of(listed.effects[0]).detail == 9);
  // Patched in place: the keys, their order and the GEfc form stay Photoshop's.
  const auto descriptor = sold_descriptor(sold_block(require_layer_named(reread, "Gallery two Plastic Wraps")).payload);
  const auto original_descriptor =
      sold_descriptor(sold_block(require_layer_named(original, "Gallery two Plastic Wraps")).payload);
  const auto* filter = entry_filter(descriptor);
  const auto* original_filter = entry_filter(original_descriptor);
  CHECK(filter->class_id == "GEfc" && filter->name == original_filter->name);
  const auto& effects = filter->values.at("GEfs").list_value;
  const auto& original_effects = original_filter->values.at("GEfs").list_value;
  CHECK(effects.size() == 2U);
  for (std::size_t index = 0; index < effects.size(); ++index) {
    const auto& keys = effects[index].object_value->key_order;
    const auto& original_keys = original_effects[index].object_value->key_order;
    CHECK(keys.size() == original_keys.size());
    for (std::size_t key = 0; key < keys.size(); ++key) {
      CHECK(keys[key].key == original_keys[key].key && keys[key].long_form == original_keys[key].long_form);
    }
  }
}

void smart_filter_gallery_fails_closed_outside_the_model() {
  const auto original = patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(kFixture));
  const auto parse = [](const patchy::psd::DescriptorObject& filter, std::uint32_t filter_id) {
    patchy::SmartFilterEntry entry;
    entry.native_filter_id = filter_id;
    bool supported = true;
    CHECK(patchy::psd::parse_filter_gallery_smart_filter(&filter, entry, supported));
    return supported && entry.kind == patchy::SmartFilterKind::FilterGallery;
  };
  const auto descriptor = sold_descriptor(sold_block(require_layer_named(original, "Gallery Plastic Wrap")).payload);
  const auto& single = *entry_filter(descriptor);
  CHECK(parse(single, patchy::psd::kFilterGalleryFilterId));
  CHECK(!parse(single, 0x506C7357U));  // a GEfc object under another filterID

  auto extra = single;  // a key the effect does not have
  patchy::psd::DescriptorValue value;
  value.type = patchy::psd::DescriptorValue::Type::Integer;
  value.integer_value = 3;
  extra.values["NmbL"] = value;
  extra.key_order.push_back({"NmbL", false});
  CHECK(!parse(extra, patchy::psd::kFilterGalleryFilterId));
  auto out_of_range = single;
  out_of_range.values["Hghl"].integer_value = 21;
  CHECK(!parse(out_of_range, patchy::psd::kFilterGalleryFilterId));

  // The writer never changes which effects run.
  auto gallery = gallery_of(require_layer_named(original, "Gallery Plastic Wrap").smart_filter_stack()->entries.front());
  patchy::FilterGalleryEffect added;
  added.effect_id = "PlsW";
  added.settings = patchy::PlasticWrapSmartFilter{1, 1, 1};
  gallery.effects.push_back(std::move(added));
  auto writable = single;
  CHECK(!patchy::psd::write_filter_gallery_smart_filter(writable, gallery));

  patchy::psd::DescriptorObject plastic;
  plastic.class_id = "PlsW";
  patchy::SmartFilterEntry entry;
  bool supported = true;
  CHECK(!patchy::psd::parse_filter_gallery_smart_filter(&plastic, entry, supported));
  CHECK(!patchy::psd::parse_filter_gallery_smart_filter(nullptr, entry, supported));
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_gallery_tests() {
  return {
      {"smart_filter_gallery_photoshop_capture_parses_and_renders_like_plastic_wrap",
       smart_filter_gallery_photoshop_capture_parses_and_renders_like_plastic_wrap},
      {"smart_filter_gallery_photoshop_capture_round_trips_and_patches",
       smart_filter_gallery_photoshop_capture_round_trips_and_patches},
      {"smart_filter_gallery_fails_closed_outside_the_model", smart_filter_gallery_fails_closed_outside_the_model},
  };
}
