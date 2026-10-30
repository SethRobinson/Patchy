#pragma once

#include "core/document_channel.hpp"
#include "core/document_file_info.hpp"
#include "core/document_path.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pattern_resource.hpp"
#include "core/pdf_source_page.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace patchy {

struct DocumentColorState {
  ColorMode working_mode{ColorMode::RGB};
  BitDepth bit_depth{BitDepth::UInt8};
  std::vector<std::uint8_t> embedded_icc_profile;
  std::string ocio_view;
  // 32-bit preview exposure in stops (docs/high-bit-depth.md): scales linear values
  // whenever a 32-bit composite narrows to display values. A view setting: never
  // saved, and undo keeps the current one.
  float view_exposure_stops{0.0F};
};

// Photoshop's document-wide artboard settings, the global 'artd' block (docs/artboards.md).
// Kept from a source file and written back, with its count refreshed, whenever the
// document has artboards; Patchy reads none of the behavior flags.
struct DocumentArtboardSettings {
  double origin_x{0.0};
  double origin_y{0.0};
  double auto_expand_offset_x{0.0};
  double auto_expand_offset_y{0.0};
  bool auto_expand{true};
  bool auto_nest{true};
  bool auto_position{true};
  bool shrinkwrap_on_save{true};
  RgbColor default_color{255, 255, 255};
  ArtboardBackground default_background{ArtboardBackground::White};

  friend bool operator==(const DocumentArtboardSettings&, const DocumentArtboardSettings&) = default;
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
  // Photoshop Smart Filter render caches ('FEid'/'FXid'), associated with each
  // layer's per-instance SoLd `placed` uuid. Raw record bodies use shared backing
  // storage so undo snapshots do not copy large cached planes.
  SmartFilterEffectsStore smart_filter_effects;
  // Pattern tiles referenced by layer-style Pattern Overlay / Bevel Texture
  // effects. Imported 'Patt'/'Pat2'/'Pat3' blocks stay raw in
  // unknown_psd_resources above AND decode here (read-only); only Authored
  // resources are written into a new pattern block on save. See
  // core/pattern_resource.hpp.
  PatternStore patterns;
  std::vector<std::uint8_t> raw_psd_global_layer_mask_info;
  // A 32-bit PSD's color mode data: Photoshop's HDR toning record ('hdrt', 'hdra'), which
  // Photoshop requires in every 32-bit file it opens. Kept from the source and written
  // back; documents without one get Photoshop 2026's default (docs/high-bit-depth.md).
  std::vector<std::uint8_t> raw_psd_color_mode_data;
  std::vector<std::uint8_t> raw_psd_image_resources;
  // Session-only source mode for lossless layered 16-bit CMYK/gray/Lab saves.
  // RGB remains the editing space. Ineligible/edited content saves through RGB.
  std::shared_ptr<const PsdNativeColorSpace> psd_native_color_space;
  std::optional<PixelBuffer> psd_flat_composite;
  // The encoded image an imported PDF page was made of, so an export can write it back
  // untouched while the page is unchanged. Session-only and never serialized: see
  // core/pdf_source_page.hpp.
  std::shared_ptr<const PdfSourcePage> pdf_source_page;
  // The same for a document whose pages became artboards: each untouched artboard's own
  // page image, keyed by the artboard group's layer id. Session-only, never serialized.
  std::map<LayerId, std::shared_ptr<const PdfSourcePage>> pdf_page_source_pages;
  // Present when the source had an 'artd' block or the document has artboards.
  std::optional<DocumentArtboardSettings> artboard_settings;
};

struct DocumentPrintSettings {
  // Always pixels/inch. PSD resource 1005 stores its resolutions as PPI regardless of
  // the display-unit fields (Photoshop 2026 ground truth: a byte-patched unit-2 file
  // still reports its fixed 16.16 value as PPI).
  double horizontal_ppi{300.0};
  double vertical_ppi{300.0};
  // Resource 1005 display-only unit fields, preserved across a PSD round trip.
  // Resolution units: 1 = px/inch, 2 = px/cm. Width/height units: 1 = inches,
  // 2 = cm, 3 = points, 4 = picas, 5 = columns.
  std::uint16_t horizontal_resolution_display_unit{1};
  std::uint16_t vertical_resolution_display_unit{1};
  std::uint16_t width_display_unit{1};
  std::uint16_t height_display_unit{1};
};

struct DocumentIndexedPalette {
  std::vector<RgbColor> colors;
  std::uint16_t source_bit_depth{0};
  std::vector<std::string> names{};
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

// position_32 is in 32nds of a pixel and may lie outside the canvas: Photoshop keeps
// guides on the pasteboard (negative, or past the far edge; GitHub issue 231).
struct DocumentGuide {
  GuideOrientation orientation{GuideOrientation::Vertical};
  std::int32_t position_32{0};
};

// How far from the canvas origin the UI places a guide, in pixels either way (the PSB
// dimension cap); files may carry any int32 position.
inline constexpr std::int32_t kMaxGuideOffsetPixels = 300000;

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
  // File > File Info metadata (EXIF, XMP, IPTC, comments; docs/file-info.md).
  [[nodiscard]] const DocumentFileInfo& file_info() const noexcept;
  [[nodiscard]] DocumentFileInfo& file_info() noexcept;
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
  [[nodiscard]] const std::vector<DocumentPath>& paths() const noexcept;
  [[nodiscard]] std::vector<DocumentPath>& paths() noexcept;
  [[nodiscard]] std::optional<LayerId> active_layer_id() const noexcept;

  Layer& add_pixel_layer(std::string name, PixelBuffer pixels);
  Layer& add_layer(Layer layer);
  [[nodiscard]] Layer* find_layer(LayerId id) noexcept;
  [[nodiscard]] const Layer* find_layer(LayerId id) const noexcept;
  DocumentChannel& add_channel(DocumentChannel channel);
  [[nodiscard]] DocumentChannel* find_channel(ChannelId id) noexcept;
  [[nodiscard]] const DocumentChannel* find_channel(ChannelId id) const noexcept;
  DocumentPath& add_path(DocumentPath path);
  [[nodiscard]] DocumentPath* find_path(DocumentPathId id) noexcept;
  [[nodiscard]] const DocumentPath* find_path(DocumentPathId id) const noexcept;
  // The (single) work path, if present.
  [[nodiscard]] DocumentPath* work_path() noexcept;
  [[nodiscard]] const DocumentPath* work_path() const noexcept;
  bool remove_path(DocumentPathId id);

  void set_active_layer(LayerId id);
  void clear_active_layer() noexcept;
  bool remove_layer(LayerId id);
  bool remove_channel(ChannelId id);
  bool rename_channel(ChannelId id, std::string name);
  bool reorder_channel(ChannelId id, std::size_t final_index);
  // Changing the size also forgets imported Photoshop slices (forget_psd_slices).
  void resize_canvas(std::int32_t width, std::int32_t height);
  [[nodiscard]] LayerId allocate_layer_id() noexcept;
  [[nodiscard]] ChannelId allocate_channel_id();
  [[nodiscard]] DocumentPathId allocate_path_id() noexcept;
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
  DocumentFileInfo file_info_{};
  std::optional<DocumentIndexedPalette> indexed_palette_{};
  std::optional<DocumentPaletteEditing> palette_editing_{};
  DocumentGridSettings grid_settings_{};
  std::vector<DocumentGuide> guides_{};
  std::vector<Layer> layers_{};
  std::vector<DocumentChannel> channels_{};
  std::vector<DocumentPath> paths_{};
  std::optional<LayerId> active_layer_id_{};
  LayerId next_layer_id_{1};
  ChannelId next_channel_id_{1};
  DocumentPathId next_path_id_{1};
};

// Photoshop slices (image resource 1050, Slices, and 1054, the slice URL list) are canvas
// rectangles kept byte for byte from the imported file. A crop, resize, rotation, flip or
// canvas size change makes them stale, so every whole-document geometry change drops them
// and Photoshop rebuilds its default slice on open (docs/ps-compat.md, "Slices").
void forget_psd_slices(Document& document);

}  // namespace patchy
