#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Smart-object data model: a layer whose pixels are a rendered preview of a source file
// placed through a transform. The per-layer placement lives in `patchy.smart_object.*`
// metadata (parsed from the PSD 'SoLd'/'PlLd' blocks; the layer stays LayerKind::Pixel,
// mirroring how text layers work); the source payloads live in the document-level
// SmartObjectStore (parsed from the global 'lnkD'/'lnk2'/'lnk3' blocks). Payload bytes
// are shared_ptr-held so whole-Document undo snapshots share one copy.
namespace patchy {

class Document;

inline constexpr const char* kLayerMetadataSmartObject = "patchy.smart_object";  // = source uuid
inline constexpr const char* kLayerMetadataSmartObjectPlaced = "patchy.smart_object.placed";
inline constexpr const char* kLayerMetadataSmartObjectTransform = "patchy.smart_object.transform";
inline constexpr const char* kLayerMetadataSmartObjectSize = "patchy.smart_object.size";
inline constexpr const char* kLayerMetadataSmartObjectResolution = "patchy.smart_object.resolution";
inline constexpr const char* kLayerMetadataSmartObjectType = "patchy.smart_object.type";
inline constexpr const char* kLayerMetadataSmartObjectAntiAlias = "patchy.smart_object.anti_alias";
inline constexpr const char* kLayerMetadataSmartObjectSourceBlock = "patchy.smart_object.source_block";
// Why the layer's contents cannot be edited/re-rendered by Patchy: "" (editable),
// "warp", "non_affine", "filters", "external", "legacy" (PlLd-only, no SoLd), or
// "unparsed". Locked layers keep Photoshop's stored raster preview untouched.
inline constexpr const char* kLayerMetadataSmartObjectLock = "patchy.smart_object.lock";
// "psd_raster_preview" (Photoshop's baked pixels) or "patchy_raster" (re-rendered).
inline constexpr const char* kLayerMetadataSmartObjectRasterStatus = "patchy.smart_object.raster_status";
// Present when the placement diverged from the preserved SoLd/PlLd blobs (move,
// transform, replace); the PSD writer then regenerates those blocks patch-in-place.
inline constexpr const char* kLayerMetadataSmartObjectBlockDirty = "patchy.smart_object.block_dirty";

inline constexpr const char* kSmartObjectRasterStatusPhotoshop = "psd_raster_preview";
inline constexpr const char* kSmartObjectRasterStatusPatchy = "patchy_raster";

enum class SmartObjectSourceKind {
  Embedded,      // 'liFD': the file bytes are inside the document
  ExternalFile,  // 'liFE': a path reference (workflows deferred; recognized + preserved)
  Alias          // 'liFA'
};

struct SmartObjectSource {
  SmartObjectSourceKind kind{SmartObjectSourceKind::Embedded};
  std::string uuid;      // joins per-layer SoLd 'Idnt' to this element
  std::string filename;  // decoded element filename, e.g. "Art.psb"
  std::string filetype;  // 4-char OSType: "8BPB", "8BPS", "png ", "JPEG", ...
  std::string creator{"8BIM"};  // 4-char OSType (Photoshop uses "    " for placed files)
  // Embedded file bytes, verbatim (null for ExternalFile/Alias).
  std::shared_ptr<const std::vector<std::uint8_t>> file_bytes;
  // ExternalFile link data (the liFE 'ExternalFileLink' descriptor + trailer; see
  // psd_smart_objects.cpp for the pinned layout). Empty/zero for embedded sources.
  std::string external_full_path;      // "file:///D:/..." URI as Photoshop stores it
  std::string external_original_path;  // native absolute path
  std::string external_rel_path;       // relative to the owning document's folder
  std::int32_t external_link_desc_version{2};
  // The linked file's modification date exactly as PS stores it (a 16-byte struct:
  // year/month/day/hour/minute + fractional seconds) and its byte size; PS compares
  // these for staleness.
  std::int32_t external_mod_year{0};
  std::uint8_t external_mod_month{0};
  std::uint8_t external_mod_day{0};
  std::uint8_t external_mod_hour{0};
  std::uint8_t external_mod_minute{0};
  double external_mod_seconds{0.0};
  std::uint64_t external_file_size{0};
  std::string child_doc_id;       // v5+ unicode id ("adobe:docid:photoshop:...")
  double asset_mod_time{0.0};     // v6+ trailing double (0.0 in captures)
  std::uint8_t asset_lock_state{0};  // v7+
  // The element's exact original bytes (u64 length prefix through trailing pad) for
  // byte-identical re-emit while untouched. Null for Patchy-authored or edited sources.
  std::shared_ptr<const std::vector<std::uint8_t>> original_element_bytes;
  bool dirty{false};
};

struct SmartObjectLinkBlock {
  std::string key{"lnk2"};  // "lnkD"/"lnk2"/"lnk3"/"lnkE"
  bool long_length{false};  // stored with the 8B64 signature (see UnknownPsdBlock)
  // Position among ALL global tagged blocks in the source file, so the writer can
  // re-interleave store blocks with the preserved unknown blocks in file order.
  // SIZE_MAX = authored by Patchy (emitted after the preserved blocks).
  std::size_t original_global_index{SIZE_MAX};
  std::vector<SmartObjectSource> sources;
  // The block's exact original payload; emitted verbatim while no source in the block
  // is dirty and none were added/removed. Null for authored blocks.
  std::shared_ptr<const std::vector<std::uint8_t>> original_payload;
  // Set when the payload failed to parse: `sources` is empty and the payload is
  // preserved opaquely (never regenerated).
  bool opaque{false};
};

struct SmartObjectStore {
  std::vector<SmartObjectLinkBlock> blocks;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const SmartObjectSource* find(std::string_view uuid) const noexcept;
  [[nodiscard]] SmartObjectSource* find(std::string_view uuid) noexcept;
  // Adds an embedded source to the first parsed 'lnk2' block, creating one if needed.
  SmartObjectSource& add_embedded(std::string uuid, std::string filename, std::string filetype,
                                  std::shared_ptr<const std::vector<std::uint8_t>> bytes);
  // Inserts a copy of `source` unless its uuid is already present (cross-document
  // paste; matching uuids reuse the existing source, Photoshop's shared-source rule).
  void adopt(const SmartObjectSource& source);
  // Removes the element (its block then regenerates on save). Returns true when found.
  // Only for explicit swaps like Replace Contents; rasterize orphans stay (PS parity).
  bool remove(std::string_view uuid);
};

// Appends the sources referenced by `layer` (and its children) that exist in `store`.
void collect_referenced_smart_object_sources(const Layer& layer, const SmartObjectStore& store,
                                             std::vector<SmartObjectSource>& sources);

// Placement parsed from the layer's smart-object metadata.
struct SmartObjectPlacement {
  std::string uuid;
  std::array<double, 8> transform{};  // x,y of the 4 placed corners in document coords
  double width{0.0};                  // source size in pixels (SoLd 'Sz')
  double height{0.0};
  double resolution{72.0};  // dpi (SoLd 'Rslt')
  int placed_type{2};       // 0 unknown / 1 vector / 2 raster / 3 image stack
  int anti_alias{16};
};

[[nodiscard]] bool layer_is_smart_object(const Layer& layer);
[[nodiscard]] std::string smart_object_source_uuid(const Layer& layer);
[[nodiscard]] std::string smart_object_placed_uuid(const Layer& layer);
[[nodiscard]] std::string smart_object_lock_reason(const Layer& layer);
[[nodiscard]] bool layer_smart_object_block_dirty(const Layer& layer);
void mark_layer_smart_object_block_dirty(Layer& layer);
[[nodiscard]] std::optional<SmartObjectPlacement> smart_object_placement_from_layer(const Layer& layer);
void store_smart_object_placement(Layer& layer, const SmartObjectPlacement& placement);
void set_layer_smart_object_metadata(Layer& layer, const SmartObjectPlacement& placement,
                                     std::string_view placed_uuid, std::string_view source_block,
                                     std::string_view lock_reason, std::string_view raster_status);
// Removes every patchy.smart_object.* metadata key (the preserved blocks stay; used
// for dangling references so the writer's strip logic keeps handling the blobs).
void clear_layer_smart_object_metadata(Layer& layer);
// Removes the preserved SoLd/SoLE/PlLd/plLd blocks too — the rasterize / merge-target
// semantic (the layer becomes a plain pixel layer everywhere, including on resave).
void strip_layer_smart_object_data(Layer& layer);
// Document-aware rasterize/merge form: also removes the matching native FEid/
// FXid cache record once no remaining layer references the placed instance.
void strip_layer_smart_object_data(Document& document, Layer& layer);

[[nodiscard]] std::string serialize_smart_object_transform(const std::array<double, 8>& transform);
[[nodiscard]] std::optional<std::array<double, 8>> parse_smart_object_transform(std::string_view text);

// The SoLd 'warp' object: a Photoshop warp applied to the placement. Mesh points are
// CONTENT/bounds-space (they never move with the quad; E6 captures), row-major
// v_order rows of u_order columns. An empty mesh means a style-only warp.
struct SmartObjectWarp {
  std::string style{"warpNone"};  // warpStyle enum value, e.g. "warpArc"/"warpCustom"
  double value{0.0};
  double perspective{0.0};
  double perspective_other{0.0};
  std::string rotate{"Hrzn"};
  double bounds_top{0.0};
  double bounds_left{0.0};
  double bounds_bottom{0.0};
  double bounds_right{0.0};
  int u_order{4};
  int v_order{4};
  std::vector<double> mesh_xs;
  std::vector<double> mesh_ys;
  // True when the mesh was synthesized from a preset style (style-only SoLd, no
  // customEnvelopeWarp): the mesh exists for rendering only, and the writer must
  // keep the file style-only instead of injecting meshPoints Photoshop never wrote.
  bool mesh_generated{false};
};

// Layer metadata key holding a serialized SmartObjectWarp (present only when the
// placement is warped AND Patchy can re-render it; see the lock classification).
inline constexpr const char* kLayerMetadataSmartObjectWarp = "patchy.smart_object.warp";

// Scales a warp's bounds and mesh linearly (content-space) for size-changing
// replace/edit commits, consistent with the E5 linear-map rule.
[[nodiscard]] SmartObjectWarp scaled_smart_object_warp(const SmartObjectWarp& warp, double scale_x,
                                                       double scale_y);

[[nodiscard]] std::string serialize_smart_object_warp(const SmartObjectWarp& warp);
[[nodiscard]] std::optional<SmartObjectWarp> parse_smart_object_warp(std::string_view text);
[[nodiscard]] std::optional<SmartObjectWarp> smart_object_warp_from_layer(const Layer& layer);

// Fresh 8-4-4-4-12 lowercase-hex uuid for authored sources ('Idnt'), the shape
// Photoshop writes.
[[nodiscard]] std::string generate_smart_object_uuid();

// Photoshop's Replace Contents / edited-contents rescale rule (E5 COM captures, see
// docs/smart-objects.md): the content-inch to document-pixel linear map and the quad center are
// preserved and applied to the new content's pixel size and density. Same-density
// content degrades to pure pixel scaling about the center.
[[nodiscard]] SmartObjectPlacement rescaled_smart_object_placement(const SmartObjectPlacement& placement,
                                                                   double new_width, double new_height,
                                                                   double new_dpi);

}  // namespace patchy
