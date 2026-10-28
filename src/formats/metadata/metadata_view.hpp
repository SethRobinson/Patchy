#pragma once

#include "core/document_file_info.hpp"
#include "formats/metadata/metadata_privacy.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Read-only views of a document's metadata for File Info and the scripting API
// (docs/file-info.md). Everything here is derived from DocumentFileInfo (plus the
// document's kept ICC profile); nothing is cached.
namespace patchy::metadata {

// One row of File Info's All Metadata list. `group` is the container ("EXIF IFD0",
// "XMP dc", "IPTC-IIM", "PNG text"), `name` the specification identifier.
struct MetadataEntry {
  std::string group;
  std::string name;
  std::string value;
  std::string source;  // where it came from ("APP1 Exif", "iTXt", "8BIM 1060")
  PrivacyClass privacy{PrivacyClass::None};
};

// Every item the document holds, grouped by container, in a stable order.
// `kept_icc_profile` is the profile saves will write (may be empty).
[[nodiscard]] std::vector<MetadataEntry> describe_file_info(const DocumentFileInfo& info,
                                                            std::span<const std::uint8_t> kept_icc_profile);

// ICC profile header and descriptive tags.
struct IccSummary {
  std::string description;
  std::string copyright;
  std::string manufacturer;  // 'dmnd' text, else the header signature
  std::string model;         // 'dmdd' text, else the header signature
  std::string version;       // "4.3"
  std::string device_class;  // "Display", "Output", ...
  std::string color_space;   // "RGB", "GRAY", "CMYK"
  std::string connection_space;  // "XYZ", "Lab"
  std::string creator;
  std::string created;       // "2024-03-01 12:00:00"
  std::string rendering_intent;
  std::size_t size{0};
};
[[nodiscard]] std::optional<IccSummary> summarize_icc(std::span<const std::uint8_t> profile);

// Curated camera fields (EXIF first, XMP exif:/tiff:/aux: mirrors as fallback).
struct CameraSummary {
  std::string make, model, lens, body_serial, lens_serial, owner;
  std::string exposure_time, f_number, iso, focal_length, focal_length_35mm, exposure_bias;
  std::string exposure_program, metering_mode, flash, white_balance;
  std::string date_original, date_digitized, offset_time, orientation, software;
  std::optional<double> latitude, longitude, altitude;
  std::string gps_direction, gps_timestamp;
  bool has_maker_note{false};
  [[nodiscard]] bool has_location() const noexcept { return latitude.has_value() && longitude.has_value(); }
};
[[nodiscard]] CameraSummary summarize_camera(const DocumentFileInfo& info);

struct HistoryEvent {
  std::string action, when, software_agent, changed, instance_id;
};
struct EditHistorySummary {
  std::string document_id, instance_id, original_document_id, derived_from;
  std::vector<HistoryEvent> events;
  std::size_t ancestor_count{0};
  std::size_t ancestor_bytes{0};
  std::vector<std::pair<std::string, std::string>> text_layers;  // name, text
  bool has_camera_raw_settings{false};
  [[nodiscard]] bool empty() const noexcept {
    return document_id.empty() && instance_id.empty() && original_document_id.empty() && derived_from.empty() &&
           events.empty() && ancestor_count == 0 && text_layers.empty() && !has_camera_raw_settings;
  }
};
[[nodiscard]] EditHistorySummary summarize_edit_history(const DocumentFileInfo& info);

// Generator metadata: Automatic1111/Forge "parameters", ComfyUI "prompt"/"workflow",
// InvokeAI, NovelAI, IPTC DigitalSourceType, C2PA actions.
struct AiGenerationSummary {
  std::string tool;  // best guess ("Automatic1111/Forge", "ComfyUI", ...)
  std::string prompt;
  std::string negative_prompt;
  std::string settings;
  std::string workflow;  // raw JSON (ComfyUI prompt/workflow, InvokeAI)
  std::string digital_source_type;
  std::vector<std::pair<std::string, std::string>> raw;  // every text chunk that contributed
  [[nodiscard]] bool present() const noexcept {
    return !tool.empty() || !prompt.empty() || !workflow.empty() || !digital_source_type.empty() || !raw.empty();
  }
};
[[nodiscard]] AiGenerationSummary summarize_ai_generation(const DocumentFileInfo& info);
// True for PNG text keys that generators use for prompts and workflows.
[[nodiscard]] bool is_ai_generation_text_key(std::string_view key) noexcept;

struct EmbeddedThumbnail {
  std::string source;  // "EXIF IFD1", "Photoshop resource 1036", "XMP xmp:Thumbnails"
  std::vector<std::uint8_t> bytes;  // encoded image (JPEG)
};
[[nodiscard]] std::vector<EmbeddedThumbnail> embedded_thumbnails(const DocumentFileInfo& info);

// Count of items per privacy class (None excluded), for File Info's summary banner.
[[nodiscard]] std::map<PrivacyClass, std::size_t> privacy_summary(const std::vector<MetadataEntry>& entries);

[[nodiscard]] std::vector<std::uint8_t> decode_base64(std::string_view text);

// The XMP packet as the Raw XMP page shows and copies it: the padding (whitespace and NULs
// that let tools edit in place) is dropped from the end and from before a closing
// <?xpacket end=...?>, which stays on its own line.
[[nodiscard]] std::string xmp_display_text(std::span<const std::uint8_t> packet);

}  // namespace patchy::metadata
