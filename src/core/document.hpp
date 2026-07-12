#pragma once

#include "core/document_channel.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace patchy {

struct DocumentColorState {
  ColorMode working_mode{ColorMode::RGB};
  BitDepth bit_depth{BitDepth::UInt8};
  std::vector<std::uint8_t> embedded_icc_profile;
  std::string ocio_view;
};

struct DocumentMetadata {
  std::map<std::string, std::string> values;
  // Document-global PSD tagged blocks (the additional layer information that follows
  // the layer info), preserved verbatim — except the 'lnk*' smart-object source blocks,
  // which are parsed into `smart_objects` below (their payloads are shared_ptr-held so
  // undo snapshots share one copy instead of duplicating embedded files).
  std::vector<UnknownPsdBlock> unknown_psd_resources;
  // Smart-object sources ('lnkD'/'lnk2'/'lnk3' blocks) referenced by per-layer
  // 'PlLd'/'SoLd' blocks via uuid; see core/smart_object.hpp.
  SmartObjectStore smart_objects;
  // Pattern tiles referenced by layer-style Pattern Overlay / Bevel Texture
  // effects. Imported 'Patt'/'Pat2'/'Pat3' blocks stay raw in
  // unknown_psd_resources above AND decode here (read-only); only Authored
  // resources are written into a new pattern block on save. See
  // core/pattern_resource.hpp.
  PatternStore patterns;
  std::vector<std::uint8_t> raw_psd_global_layer_mask_info;
  std::vector<std::uint8_t> raw_psd_image_resources;
  std::optional<PixelBuffer> psd_flat_composite;
};

struct DocumentPrintSettings {
  double horizontal_ppi{300.0};
  double vertical_ppi{300.0};
};

struct DocumentIndexedPalette {
  std::vector<RgbColor> colors;
  std::uint16_t source_bit_depth{0};
};

// Palettized-editing state: present = palette mode is on and tool writes snap to
// the palette (indexed_palette above stays what it always was: import metadata).
// Every palette mutation must bump palette_revision (it keys cached PaletteLuts)
// and re-run sync_document_indexed_palette (core/palette.hpp) so indexed export
// sees the editing palette.
struct DocumentPaletteEditing {
  Palette palette;
  std::uint8_t alpha_threshold{128};
  std::uint64_t palette_revision{0};
};

enum class GuideOrientation {
  Vertical,
  Horizontal
};

struct DocumentGuide {
  GuideOrientation orientation{GuideOrientation::Vertical};
  std::int32_t position_32{0};
};

struct DocumentGridSettings {
  std::int32_t horizontal_cycle_32{576};
  std::int32_t vertical_cycle_32{576};
};

class Document {
public:
  Document() = default;
  Document(std::int32_t width, std::int32_t height, PixelFormat format);

  [[nodiscard]] std::int32_t width() const noexcept;
  [[nodiscard]] std::int32_t height() const noexcept;
  [[nodiscard]] PixelFormat format() const noexcept;
  void set_format(PixelFormat format) noexcept;
  [[nodiscard]] const DocumentColorState& color_state() const noexcept;
  [[nodiscard]] DocumentColorState& color_state() noexcept;
  [[nodiscard]] const DocumentMetadata& metadata() const noexcept;
  [[nodiscard]] DocumentMetadata& metadata() noexcept;
  [[nodiscard]] const DocumentPrintSettings& print_settings() const noexcept;
  [[nodiscard]] DocumentPrintSettings& print_settings() noexcept;
  [[nodiscard]] const std::optional<DocumentIndexedPalette>& indexed_palette() const noexcept;
  [[nodiscard]] std::optional<DocumentIndexedPalette>& indexed_palette() noexcept;
  [[nodiscard]] const std::optional<DocumentPaletteEditing>& palette_editing() const noexcept;
  [[nodiscard]] std::optional<DocumentPaletteEditing>& palette_editing() noexcept;
  [[nodiscard]] const DocumentGridSettings& grid_settings() const noexcept;
  [[nodiscard]] DocumentGridSettings& grid_settings() noexcept;
  [[nodiscard]] const std::vector<DocumentGuide>& guides() const noexcept;
  [[nodiscard]] std::vector<DocumentGuide>& guides() noexcept;
  [[nodiscard]] const std::vector<Layer>& layers() const noexcept;
  [[nodiscard]] std::vector<Layer>& layers() noexcept;
  [[nodiscard]] const std::vector<DocumentChannel>& channels() const noexcept;
  [[nodiscard]] std::vector<DocumentChannel>& channels() noexcept;
  [[nodiscard]] std::optional<LayerId> active_layer_id() const noexcept;

  Layer& add_pixel_layer(std::string name, PixelBuffer pixels);
  Layer& add_layer(Layer layer);
  [[nodiscard]] Layer* find_layer(LayerId id) noexcept;
  [[nodiscard]] const Layer* find_layer(LayerId id) const noexcept;
  DocumentChannel& add_channel(DocumentChannel channel);
  [[nodiscard]] DocumentChannel* find_channel(ChannelId id) noexcept;
  [[nodiscard]] const DocumentChannel* find_channel(ChannelId id) const noexcept;

  void set_active_layer(LayerId id);
  void clear_active_layer() noexcept;
  bool remove_layer(LayerId id);
  bool remove_channel(ChannelId id);
  bool rename_channel(ChannelId id, std::string name);
  bool reorder_channel(ChannelId id, std::size_t final_index);
  void resize_canvas(std::int32_t width, std::int32_t height);
  [[nodiscard]] LayerId allocate_layer_id() noexcept;
  [[nodiscard]] ChannelId allocate_channel_id();
  [[nodiscard]] std::string next_alpha_channel_name() const;
  [[nodiscard]] std::size_t maximum_saved_channel_count(bool includes_merged_transparency = false) const noexcept;

private:
  Layer* find_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept;
  const Layer* find_layer_recursive(const std::vector<Layer>& layers, LayerId id) const noexcept;
  bool remove_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept;
  [[nodiscard]] std::optional<LayerId> last_layer_id(const std::vector<Layer>& layers) const noexcept;

  std::int32_t width_{0};
  std::int32_t height_{0};
  PixelFormat format_{PixelFormat::rgb8()};
  DocumentColorState color_state_{};
  DocumentMetadata metadata_{};
  DocumentPrintSettings print_settings_{};
  std::optional<DocumentIndexedPalette> indexed_palette_{};
  std::optional<DocumentPaletteEditing> palette_editing_{};
  DocumentGridSettings grid_settings_{};
  std::vector<DocumentGuide> guides_{};
  std::vector<Layer> layers_{};
  std::vector<DocumentChannel> channels_{};
  std::optional<LayerId> active_layer_id_{};
  LayerId next_layer_id_{1};
  ChannelId next_channel_id_{1};
};

}  // namespace patchy
