// Layer-record codec for the PSD reader/writer: the per-layer record read
// (bounds/channels/blend/flags/mask/blending-ranges/name and the tagged-block
// walk) and the layer-record write/encode pipeline behind write_layer_record
// and append_encoded_layers. Split out of psd_document_io.cpp as a pure move.

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

bool payload_contains_ascii(std::span<const std::uint8_t> payload, std::string_view marker) {
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  return std::search(begin, end, marker.begin(), marker.end()) != end;
}

bool payload_has_patchy_generated_text_signature(std::span<const std::uint8_t> payload) {
  return payload_contains_ascii(
      payload, "/KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0");
}

bool encoded_layer_uses_source_state(const EncodedLayer& encoded) noexcept {
  return encoded.layer != nullptr && encoded.kind != EncodedLayerKind::GroupBoundary;
}

std::string encoded_layer_name(const EncodedLayer& encoded) {
  return encoded.kind == EncodedLayerKind::GroupBoundary ? "</Layer group>" : encoded.layer->name();
}

BlendMode encoded_layer_blend_mode(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->blend_mode() : BlendMode::Normal;
}

float encoded_layer_opacity(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->opacity() : 1.0F;
}

bool encoded_layer_visible(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->visible() : true;
}

std::uint8_t encoded_layer_clipping(const EncodedLayer& encoded) noexcept {
  // Divider records (Group folders + GroupBoundary) always write clipping 0:
  // Photoshop cannot clip groups and boundary records carry no layer state.
  return encoded_layer_uses_source_state(encoded) && encoded.kind != EncodedLayerKind::Group &&
                 encoded.layer->clipped()
             ? 1U
             : 0U;
}

std::uint32_t group_section_divider_type(const Layer& layer) {
  if (!layer_group_expanded(layer)) {
    return 2U;
  }
  return 1U;
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type, BlendMode blend_mode,
                                                  bool include_blend_mode) {
  BigEndianWriter payload;
  payload.write_u32(type);
  if (include_blend_mode) {
    write_signature(payload, {'8', 'B', 'I', 'M'});
    write_signature(payload, blend_mode_key(blend_mode));
  }
  return payload.bytes();
}

// Per-layer smart object blocks reference embedded sources stored in the document-global
// 'lnk2'/'lnkD' blocks. Photoshop opens a file where those references dangle, but its
// save pipeline fails ("disk error (-1)"), so they must never be written without the data.
bool is_smart_object_reference_block(std::string_view key) {
  return key == "PlLd" || key == "plLd" || key == "SoLd" || key == "SoLE";
}

bool should_skip_layer_block(const EncodedLayer& encoded, const UnknownPsdBlock& block, bool generated_text_block,
                             bool generated_style_block) {
  if (block.key == "luni" || block.key == "plFX" || block.key == "lspf" || block.key == "lmgm" ||
      (block.key == "plAD" && encoded.kind == EncodedLayerKind::Adjustment)) {
    return true;
  }
  // A moved/transformed smart object regenerates its placed-layer blocks (see the
  // block_dirty handling at the end of write_layer_record) instead of re-emitting the
  // stale originals.
  if (encoded.layer != nullptr && is_smart_object_reference_block(block.key) &&
      layer_smart_object_block_dirty(*encoded.layer)) {
    return true;
  }
  if (encoded.kind == EncodedLayerKind::Adjustment &&
      (block.key == "levl" || block.key == "curv" || block.key == "hue2")) {
    return true;
  }
  if (generated_style_block && (block.key == "lfx2" || block.key == "lrFX" || block.key == "lmfx")) {
    return true;
  }
  if (generated_text_block && (block.key == "TySh" || block.key == "tySh")) {
    return true;
  }
  return encoded.kind == EncodedLayerKind::Group && (block.key == "lsct" || block.key == "lsdk");
}

bool layer_preserves_photoshop_layer_style(const Layer& layer) {
  return std::any_of(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                     [](const UnknownPsdBlock& block) {
                       return block.key == "lfx2" || block.key == "lrFX" || block.key == "lmfx";
                     });
}

const UnknownPsdBlock* find_layer_block(const Layer& layer, std::string_view key) {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == key) {
      return &block;
    }
  }
  return nullptr;
}

EncodedLayer encode_layer(const Layer& layer, bool large_document) {
  if (layer.kind() != LayerKind::Pixel) {
    throw std::runtime_error("Layered PSD export currently supports pixel and group layers only");
  }
  const auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 || pixels.format().channels > 4) {
    throw std::runtime_error("Layered PSD export currently supports RGB/RGBA 8-bit layers only");
  }

  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Pixel;
  encoded.bounds = layer.bounds().empty() ? Rect::from_size(pixels.width(), pixels.height()) : layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  std::vector<std::uint16_t> channel_ids{kChannelRed, kChannelGreen, kChannelBlue};
  if (pixels.format().channels >= 4) {
    channel_ids.push_back(kChannelTransparency);
  }
  if (layer.mask().has_value() && !layer.mask()->pixels.empty()) {
    const auto& mask = *layer.mask();
    if (mask.pixels.format() != PixelFormat::gray8()) {
      throw std::runtime_error("Layered PSD export requires 8-bit grayscale layer masks");
    }
    if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
      throw std::runtime_error("Layer mask bounds do not match mask pixels");
    }
    channel_ids.push_back(kChannelUserMask);
  }

  const auto pixel_count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  encoded.channels.reserve(channel_ids.size());
  for (std::size_t channel_index = 0; channel_index < channel_ids.size(); ++channel_index) {
    const auto channel_id = channel_ids[channel_index];
    if (channel_id == kChannelUserMask) {
      const auto& mask_pixels = layer.mask()->pixels;
      encoded.channels.push_back(encode_channel(channel_id, mask_pixels.width(), mask_pixels.height(),
                                                mask_pixels.data(), large_document));
    } else {
      std::vector<std::uint8_t> channel;
      channel.resize(pixel_count);
      const auto source_channel = channel_id == kChannelTransparency ? 3 : channel_index;
      for (std::size_t i = 0; i < pixel_count; ++i) {
        channel[i] = pixels.data()[i * pixels.format().channels + source_channel];
      }
      encoded.channels.push_back(encode_channel(channel_id, pixels.width(), pixels.height(), channel, large_document));
    }
  }
  return encoded;
}

EncodedLayer encode_adjustment_layer(const Layer& layer, bool large_document) {
  if (layer.kind() != LayerKind::Adjustment || !adjustment_settings_from_layer(layer).has_value()) {
    throw std::runtime_error("Adjustment layer is missing Patchy adjustment settings");
  }

  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Adjustment;
  encoded.bounds = layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  if (layer.mask().has_value() && !layer.mask()->pixels.empty()) {
    const auto& mask = *layer.mask();
    if (mask.pixels.format() != PixelFormat::gray8()) {
      throw std::runtime_error("Layered PSD export requires 8-bit grayscale layer masks");
    }
    if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
      throw std::runtime_error("Layer mask bounds do not match mask pixels");
    }
    encoded.channels.push_back(encode_channel(kChannelUserMask, mask.pixels.width(), mask.pixels.height(),
                                              mask.pixels.data(), large_document));
  }
  return encoded;
}

EncodedLayer encode_group_boundary(const Layer& layer) {
  EncodedLayer encoded;
  encoded.kind = EncodedLayerKind::GroupBoundary;
  encoded.blending_ranges = &layer.raw_psd_group_boundary_blending_ranges();
  return encoded;
}

EncodedLayer encode_group(const Layer& layer) {
  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Group;
  encoded.bounds = layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  return encoded;
}

}  // namespace

LayerRecord read_layer_record(BigEndianReader& reader, bool large_document,
                              const CmykColorConverter& cmyk) {
  LayerRecord record;
  const auto top = static_cast<std::int32_t>(reader.read_u32());
  const auto left = static_cast<std::int32_t>(reader.read_u32());
  const auto bottom = static_cast<std::int32_t>(reader.read_u32());
  const auto right = static_cast<std::int32_t>(reader.read_u32());
  record.bounds = Rect{left, top, right - left, bottom - top};

  const auto channel_count = reader.read_u16();
  for (std::uint16_t i = 0; i < channel_count; ++i) {
    record.channels.push_back(LayerChannelInfo{
        reader.read_u16(),
        large_document ? reader.read_u64() : static_cast<std::uint64_t>(reader.read_u32())});
  }

  const auto signature = read_signature(reader);
  if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
      signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
    throw std::runtime_error("Invalid PSD layer blend mode signature");
  }
  record.blend_mode = blend_mode_from_key(read_signature(reader));
  record.opacity = reader.read_u8();
  record.clipping = reader.read_u8() != 0;
  const auto flags = reader.read_u8();
  record.visible = (flags & 0x02U) == 0;
  reader.skip(1);  // filler

  const auto extra_length = read_section_length(reader, "layer extra data");
  const auto extra_end = reader.position() + extra_length;
  if (extra_length >= 8) {
    const auto mask_length = read_section_length(reader, "layer mask data");
    const auto mask_end = reader.position() + mask_length;
    if (mask_length >= 18U) {
      const auto mask_top = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_left = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_bottom = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_right = static_cast<std::int32_t>(reader.read_u32());
      const auto default_color = reader.read_u8();
      const auto mask_flags = reader.read_u8();
      // Flag bit 0 ("position relative to layer" in the spec) is how Photoshop persists the
      // layer/mask link toggle: 1 means the chain icon is off (unlinked).
      record.mask = LayerMaskInfo{Rect{mask_left, mask_top, mask_right - mask_left, mask_bottom - mask_top}, default_color,
                                  (mask_flags & 0x02U) != 0, (mask_flags & 0x01U) == 0};
    }
    if (reader.position() < mask_end) {
      reader.skip(mask_end - reader.position());
    }
    const auto blending_ranges_length = read_section_length(reader, "layer blending ranges");
    if (reader.position() > extra_end || blending_ranges_length > extra_end - reader.position()) {
      throw std::runtime_error("PSD layer blending ranges exceed the layer record");
    }
    record.blending_ranges = reader.read_bytes(blending_ranges_length);
    if (reader.position() < extra_end) {
      record.name = read_pascal_string(reader, 4);
    }
    while (reader.position() + 12 <= extra_end) {
      const auto block_signature = read_signature(reader);
      if (block_signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          block_signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        break;
      }
      const auto block_key = read_signature(reader);
      const auto key = key_string(block_key);
      // Photoshop's parser picks the length width BY KEY (the documented 8-byte set)
      // in PSBs; the '8B64' signature additionally marks extras like 'cinf'. Both
      // rules must apply on read: PS 2023 writes e.g. 'lnk2' as '8BIM' + u64 in
      // PSBs, and honoring the signature alone misreads the length and derails the
      // whole block walk (the 10cm-table-tent linked-SO regression).
      const bool wide_length = block_signature == std::array<char, 4>{'8', 'B', '6', '4'} ||
                               (large_document && tagged_block_length_is_u64(key));
      if (wide_length && extra_end - reader.position() < 8U) {
        break;
      }
      const auto block_length =
          wide_length ? reader.read_u64() : static_cast<std::uint64_t>(reader.read_u32());
      if (block_length > extra_end - reader.position()) {
        break;
      }
      auto payload = reader.read_bytes(static_cast<std::size_t>(block_length));
      record.additional_blocks.push_back(UnknownPsdBlock{key, payload, wide_length});
      if (key == "iOpa" && payload.size() == 4U) {
        record.fill_opacity = payload[0];
      }
      if (key == "luni") {
        if (auto unicode_name = read_unicode_string_payload(record.additional_blocks.back().payload);
            unicode_name.has_value()) {
          record.name = *unicode_name;
        }
      }
      if (key == "TySh" || key == "tySh") {
        record.text_source_block = key;
        const auto& text_payload = record.additional_blocks.back().payload;
        record.text_patchy_generated_type_block =
            record.text_patchy_generated_type_block || payload_has_patchy_generated_text_signature(text_payload);
        if (!record.text.has_value()) {
          record.text = extract_engine_data_text(text_payload);
        }
        if (!record.text_size.has_value()) {
          record.text_size = extract_engine_data_font_size(text_payload);
        }
        if (!record.text_color.has_value()) {
          record.text_color = extract_engine_data_fill_color(text_payload, cmyk);
        }
        if (!record.text_anti_alias.has_value()) {
          record.text_anti_alias = extract_engine_data_anti_alias(text_payload);
        }
        if (record.text.has_value() && !record.text_runs.has_value()) {
          if (auto runs = extract_engine_text_runs(text_payload, *record.text, record.text_size.value_or(36),
                                                   record.text_color.value_or(RgbColor{0, 0, 0}), cmyk);
              runs.has_value()) {
            if (!runs->empty()) {
              const auto& first_run = runs->front();
              record.text_font = first_run.family;
              record.text_size = std::clamp(static_cast<int>(std::lround(first_run.size)), 1, kMaxTextSizePixels);
              record.text_color = first_run.color;
              record.text_bold = first_run.bold;
              record.text_italic = first_run.italic;
            }
            record.text_runs = serialize_patchy_text_runs(*runs);
            if (auto paragraph_runs = extract_engine_paragraph_runs(text_payload, *record.text);
                paragraph_runs.has_value()) {
              record.text_paragraph_runs = serialize_patchy_paragraph_runs(*paragraph_runs);
              record.text_html = html_from_text_runs(*record.text, *runs, *paragraph_runs);
            } else {
              record.text_html = html_from_text_runs(*record.text, *runs);
            }
          }
        }
        if (record.text.has_value() && !record.text_paragraph_runs.has_value()) {
          if (auto paragraph_runs = extract_engine_paragraph_runs(text_payload, *record.text);
              paragraph_runs.has_value()) {
            record.text_paragraph_runs = serialize_patchy_paragraph_runs(*paragraph_runs);
          }
        }
        if (!record.text_box.has_value()) {
          record.text_box = extract_type_tool_text_box(text_payload);
        }
        if (!record.text_geometry.has_value()) {
          record.text_geometry = extract_type_tool_geometry(text_payload);
        }
      }
      if (key == "lmfx") {
        // Photoshop's multi-instance effects block (PS 2015.5+, written when a
        // layer stacks several instances of one effect): the payload shape is
        // identical to lfx2 (the parser reads the single keys and the *Multi
        // lists), and it is authoritative over the single-instance
        // compatibility lfx2 Photoshop writes beside it, in either block order.
        record.layer_style = parse_lfx2_layer_style(record.additional_blocks.back().payload, cmyk);
        record.layer_style_from_lmfx = true;
      } else if (key == "lfx2") {
        if (!record.layer_style_from_lmfx) {
          merge_missing_layer_style_effects(record.layer_style,
                                            parse_lfx2_layer_style(record.additional_blocks.back().payload, cmyk));
        }
      } else if (key == "lrFX") {
        if (!record.layer_style_from_lmfx) {
          merge_missing_layer_style_effects(record.layer_style, parse_lrfx_layer_style(record.additional_blocks.back().payload));
        }
      } else if (key == "plFX") {
        if (auto patchy_style = parse_patchy_layer_style(record.additional_blocks.back().payload);
            patchy_style.has_value()) {
          record.layer_style = std::move(*patchy_style);
        }
      }
      if (key == "lspf" && record.additional_blocks.back().payload.size() >= 4U) {
        BigEndianReader protection_reader(record.additional_blocks.back().payload);
        record.protection_flags = protection_reader.read_u32();
      }
      if (key == "lmgm" && !record.additional_blocks.back().payload.empty()) {
        // "Layer Mask Hides Effects" blending option (first byte is the bool).
        record.layer_mask_hides_effects = record.additional_blocks.back().payload[0] != 0;
      }
      if (key == "lsct" || key == "lsdk") {
        const auto& section_payload = record.additional_blocks.back().payload;
        if (section_payload.size() >= 4U) {
          BigEndianReader section_reader(section_payload);
          record.section_divider_type = section_reader.read_u32();
          if (section_reader.remaining() >= 8U) {
            const auto section_signature = read_signature(section_reader);
            if (section_signature == std::array<char, 4>{'8', 'B', 'I', 'M'} ||
                section_signature == std::array<char, 4>{'8', 'B', '6', '4'}) {
              record.blend_mode = blend_mode_from_key(read_signature(section_reader));
            }
          }
        }
      }
      if (key == "SoLd" || key == "SoLE") {
        if (auto info = parse_placed_layer_block(key, record.additional_blocks.back().payload); info.has_value()) {
          record.placed = std::move(*info);
          record.placed_source_block = key;
          record.placed_from_sold = true;
        } else if (!record.placed_from_sold) {
          // An unreadable SoLd wins over any PlLd fallback: the layer imports as a
          // plain preview with its blobs preserved verbatim.
          record.placed.reset();
          record.placed_parse_failed = true;
        }
      }
      if ((key == "PlLd" || key == "plLd") && !record.placed_from_sold && !record.placed_parse_failed) {
        if (auto info = parse_placed_layer_block(key, record.additional_blocks.back().payload); info.has_value()) {
          record.placed = std::move(*info);
          record.placed_source_block = key;
        }
      }
    }
  }
  if (reader.position() < extra_end) {
    reader.skip(extra_end - reader.position());
  }
  if (record.name.empty()) {
    record.name = "Layer";
  }
  return record;
}

void write_layer_record(BigEndianWriter& writer, const EncodedLayer& encoded, bool strip_smart_object_blocks,
                        bool large_document) {
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y + encoded.bounds.height));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x + encoded.bounds.width));
  writer.write_u16(static_cast<std::uint16_t>(encoded.channels.size()));

  for (const auto& channel : encoded.channels) {
    writer.write_u16(channel.id);
    if (large_document) {
      writer.write_u64(channel.data.size() + 2);
    } else {
      writer.write_u32(checked_u32(channel.data.size() + 2, "layer channel data length"));
    }
  }

  write_signature(writer, {'8', 'B', 'I', 'M'});
  write_signature(writer, blend_mode_key(encoded_layer_blend_mode(encoded)));
  writer.write_u8(
      static_cast<std::uint8_t>(std::clamp(std::lround(encoded_layer_opacity(encoded) * 255.0F), 0L, 255L)));
  writer.write_u8(encoded_layer_clipping(encoded));
  // Bit 3 marks the record as Photoshop 5.0+. Without it Photoshop falls back to legacy
  // layer semantics — most visibly, an unlinked layer mask's rectangle gets treated as
  // relative to the layer, scrambling masked layers that carry effects.
  std::uint8_t record_flags = 0x08U;
  if (!encoded_layer_visible(encoded)) {
    record_flags |= 0x02U;
  }
  writer.write_u8(record_flags);
  writer.write_u8(0);

  BigEndianWriter extra;
  if (encoded.layer != nullptr &&
      (encoded.kind == EncodedLayerKind::Pixel || encoded.kind == EncodedLayerKind::Adjustment) &&
      encoded.layer->mask().has_value()) {
    const auto& mask = *encoded.layer->mask();
    BigEndianWriter mask_data;
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.y));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.x));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.y + mask.bounds.height));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.x + mask.bounds.width));
    mask_data.write_u8(mask.default_color);
    // Bit 0 set = mask unlinked from the layer (Photoshop's chain toggle), bit 1 = mask disabled.
    std::uint8_t mask_flags = 0;
    if (!layer_mask_linked(*encoded.layer)) {
      mask_flags |= 0x01U;
    }
    if (mask.disabled) {
      mask_flags |= 0x02U;
    }
    mask_data.write_u8(mask_flags);
    mask_data.write_u16(0);
    write_length_prefixed_block(extra, mask_data.bytes());
  } else {
    extra.write_u32(0);  // layer mask data
  }
  if (encoded.blending_ranges != nullptr) {
    write_length_prefixed_block(extra, *encoded.blending_ranges);
  } else {
    extra.write_u32(0);  // layer blending ranges
  }
  const auto name = encoded_layer_name(encoded);
  write_pascal_string(extra, name, 4);
  auto unicode_name = unicode_string_payload(name);
  write_additional_layer_block(extra, {'l', 'u', 'n', 'i'}, unicode_name, large_document);

  if (encoded.kind == EncodedLayerKind::GroupBoundary) {
    const auto payload = section_divider_payload(3U, BlendMode::Normal, false);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload, large_document);
  } else if (encoded.kind == EncodedLayerKind::Group) {
    const auto payload =
        section_divider_payload(group_section_divider_type(*encoded.layer), encoded.layer->blend_mode(), true);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload, large_document);
  }

  bool generated_style_payload = false;
  // LayerStyle::empty() is a rendering predicate, so a style containing only a
  // disabled Satin is "empty" visually but still has a native record to save.
  if (encoded.layer != nullptr &&
      (!encoded.layer->layer_style().empty() || !encoded.layer->layer_style().satins.empty()) &&
      !layer_preserves_photoshop_layer_style(*encoded.layer)) {
    const auto payload = photoshop_lfx2_layer_style_payload(encoded.layer->layer_style());
    write_additional_layer_block(extra, {'l', 'f', 'x', '2'}, payload, large_document);
    generated_style_payload = true;
  }

  if (encoded.layer != nullptr && encoded.kind == EncodedLayerKind::Adjustment) {
    const auto settings = adjustment_settings_from_layer(*encoded.layer);
    if (settings.has_value() && settings->kind == AdjustmentKind::Levels) {
      write_additional_layer_block(extra, kPhotoshopLevelsAdjustmentBlockKey,
                                   photoshop_levels_payload(settings->levels), large_document);
    }
    if (settings.has_value() && settings->kind == AdjustmentKind::Curves) {
      write_additional_layer_block(
          extra, kPhotoshopCurvesAdjustmentBlockKey,
          photoshop_curves_payload(settings->curves, find_layer_block(*encoded.layer, "curv")),
          large_document);
    }
    if (settings.has_value() && settings->kind == AdjustmentKind::HueSaturation) {
      write_additional_layer_block(
          extra, kPhotoshopHueSaturationBlockKey,
          photoshop_hue2_payload(settings->hue_saturation, find_layer_block(*encoded.layer, "hue2")),
          large_document);
    }
    // Native Curves carries the complete Patchy point model. Writing plAD next
    // to curv makes Photoshop report "unknown data" on every open, even though
    // it recognizes the native adjustment. Legacy plAD remains readable and is
    // migrated to curv on save; malformed native layers stay opaque above and
    // therefore retain both raw blocks.
    if (!settings.has_value() || settings->kind != AdjustmentKind::Curves) {
      const auto payload = patchy_adjustment_payload(*encoded.layer);
      if (!payload.empty()) {
        write_additional_layer_block(extra, kPatchyAdjustmentBlockKey, payload, large_document);
      }
    }
  }

  const auto generated_text_payload = should_write_generated_text_block(encoded)
                                          ? photoshop_type_tool_payload_for_layer(*encoded.layer, encoded.bounds)
                                          : std::optional<std::vector<std::uint8_t>>{};
  if (generated_text_payload.has_value()) {
    write_additional_layer_block(extra, {'T', 'y', 'S', 'h'}, *generated_text_payload, large_document);
  }

  if (encoded.layer != nullptr) {
    const auto fill_opacity_byte = static_cast<std::uint8_t>(
        std::clamp(std::lround(encoded.layer->fill_opacity() * 255.0F), 0L, 255L));
    bool wrote_fill_opacity = false;
    const auto protection_flags = layer_lock_flags(*encoded.layer) &
                                  (kPsdProtectTransparency | kPsdProtectComposite | kPsdProtectPosition);
    if (protection_flags != 0U) {
      BigEndianWriter protection;
      protection.write_u32(protection_flags);
      write_additional_layer_block(extra, {'l', 's', 'p', 'f'}, protection.bytes(), large_document);
    }

    if (encoded.layer->layer_style().layer_mask_hides_effects) {
      // "Layer Mask Hides Effects" blending option; absence means off.
      BigEndianWriter mask_hides;
      mask_hides.write_u8(1);
      mask_hides.write_u8(0);
      mask_hides.write_u16(0);
      write_additional_layer_block(extra, {'l', 'm', 'g', 'm'}, mask_hides.bytes(), large_document);
    }

    for (const auto& block : encoded.layer->unknown_psd_blocks()) {
      if (should_skip_layer_block(encoded, block, generated_text_payload.has_value(), generated_style_payload)) {
        continue;
      }
      if (block.key == "iOpa" && block.payload.size() == 4U) {
        if (!wrote_fill_opacity && fill_opacity_byte != 255U) {
          auto payload = block.payload;
          payload[0] = fill_opacity_byte;
          write_additional_layer_block(extra, {'i', 'O', 'p', 'a'}, payload, large_document,
                                       block.long_length);
          wrote_fill_opacity = true;
        }
        continue;
      }
      if (strip_smart_object_blocks && is_smart_object_reference_block(block.key)) {
        continue;
      }
      if (auto key = block_key_from_string(block.key); key.has_value()) {
        write_additional_layer_block(extra, *key, block.payload, large_document, block.long_length);
      }
    }
    if (!wrote_fill_opacity && fill_opacity_byte != 255U) {
      const std::array<std::uint8_t, 4> payload{fill_opacity_byte, 0U, 0U, 0U};
      write_additional_layer_block(extra, {'i', 'O', 'p', 'a'}, payload, large_document);
    }

    // A dirty smart-object placement (moved/transformed since import) re-emits its
    // placed-layer blocks with the current quad patched in; unmodeled descriptor
    // fields survive because the regeneration patches the ORIGINAL payload. A failed
    // regeneration falls back to the original bytes (stale quad beats a broken block).
    if (!strip_smart_object_blocks && layer_smart_object_block_dirty(*encoded.layer)) {
      const auto placement = smart_object_placement_from_layer(*encoded.layer);
      const auto warp = smart_object_warp_from_layer(*encoded.layer);
      for (const auto& block : encoded.layer->unknown_psd_blocks()) {
        if (!is_smart_object_reference_block(block.key)) {
          continue;
        }
        const auto key = block_key_from_string(block.key);
        if (!key.has_value()) {
          continue;
        }
        std::optional<std::vector<std::uint8_t>> regenerated;
        if (placement.has_value()) {
          regenerated = regenerate_placed_layer_payload(
              block.key, block.payload, *placement, warp.has_value() ? &*warp : nullptr,
              smart_object_placed_uuid(*encoded.layer),
              [&] {
                const auto* stack = encoded.layer->smart_filter_stack();
                const auto action =
                    stack == nullptr
                        ? SmartFilterDescriptorAction::Remove
                        : stack->support == SmartFilterStackSupport::Supported
                              ? SmartFilterDescriptorAction::Replace
                              : SmartFilterDescriptorAction::Preserve;
                return SmartFilterDescriptorEdit{action, stack};
              }());
        }
        if (regenerated.has_value()) {
          write_additional_layer_block(extra, *key, *regenerated, large_document, block.long_length);
        } else {
          write_additional_layer_block(extra, *key, block.payload, large_document, block.long_length);
        }
      }
    }
  }
  if ((extra.bytes().size() % 2U) != 0) {
    extra.write_u8(0);
  }
  write_length_prefixed_block(writer, extra.bytes());
}

void append_encoded_layers(const Layer& layer, std::vector<EncodedLayer>& encoded_layers, bool large_document) {
  if (layer.kind() == LayerKind::Pixel) {
    encoded_layers.push_back(encode_layer(layer, large_document));
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    encoded_layers.push_back(encode_adjustment_layer(layer, large_document));
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    encoded_layers.push_back(encode_group_boundary(layer));
    for (const auto& child : layer.children()) {
      append_encoded_layers(child, encoded_layers, large_document);
    }
    encoded_layers.push_back(encode_group(layer));
    return;
  }

  throw std::runtime_error("Layered PSD export currently supports pixel, adjustment, and group layers only");
}

}  // namespace patchy::psd
