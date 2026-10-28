#pragma once

#include "formats/metadata/metadata_privacy.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// IPTC-IIM (legacy IPTC) records and the Photoshop image-resource list ("8BIM"
// blocks) that carries them in JPEG APP13, TIFF tag 34377 and PSD files
// (docs/file-info.md). Qt-free.
namespace patchy::metadata {

struct IimDataset {
  std::uint8_t record{2};
  std::uint8_t dataset{0};
  std::vector<std::uint8_t> data;
};

struct IimBlock {
  std::vector<IimDataset> datasets;
  // Record 1:90 announced UTF-8 (ESC % G). Without it IIM text is ISO 8859-1 in
  // theory; the reader still accepts valid UTF-8 (most modern writers omit 1:90).
  bool utf8_marker{false};

  [[nodiscard]] std::vector<std::string> values(std::uint8_t record, std::uint8_t dataset) const;
  [[nodiscard]] std::string value(std::uint8_t record, std::uint8_t dataset) const;
  // Replaces every instance of the dataset (repeatable datasets take several values).
  // Values are written as UTF-8 and set the 1:90 marker.
  void set_values(std::uint8_t record, std::uint8_t dataset, const std::vector<std::string>& values);
  void erase(std::uint8_t record, std::uint8_t dataset);
  [[nodiscard]] bool empty() const noexcept;
};

// nullopt when the bytes do not start with an IIM tag marker (0x1C).
[[nodiscard]] std::optional<IimBlock> parse_iim(std::span<const std::uint8_t> bytes);
// Record 1 first (with the 1:90 marker when set), then record 2 with 2:00
// RecordVersion, then the rest in their original order.
[[nodiscard]] std::vector<std::uint8_t> write_iim(const IimBlock& block);
// Text of one dataset, transcoded to UTF-8.
[[nodiscard]] std::string iim_text(const IimBlock& block, const IimDataset& dataset);
// Specification name ("By-line", "Caption-Abstract"), or "Record:Dataset" when unknown.
[[nodiscard]] std::string iim_dataset_name(std::uint8_t record, std::uint8_t dataset);
[[nodiscard]] PrivacyClass iim_dataset_privacy(std::uint8_t record, std::uint8_t dataset) noexcept;
// Display form of a dataset's text: the date datasets (CCYYMMDD) become "CCYY-MM-DD" and the
// time datasets (HHMMSS+hhmm) "HH:MM:SS+hh:mm". Malformed values and other datasets are
// returned unchanged.
[[nodiscard]] std::string iim_display_text(std::uint8_t record, std::uint8_t dataset, std::string text);

// Photoshop resource 1061 / MWG digest: MD5 of the IIM bytes.
[[nodiscard]] std::array<std::uint8_t, 16> iim_digest(std::span<const std::uint8_t> iim_bytes);

namespace iim {
constexpr std::uint8_t kObjectName = 5;
constexpr std::uint8_t kUrgency = 10;
constexpr std::uint8_t kSubjectReference = 12;
constexpr std::uint8_t kCategory = 15;
constexpr std::uint8_t kKeywords = 25;
constexpr std::uint8_t kSpecialInstructions = 40;
constexpr std::uint8_t kDateCreated = 55;
constexpr std::uint8_t kTimeCreated = 60;
constexpr std::uint8_t kOriginatingProgram = 65;
constexpr std::uint8_t kByline = 80;
constexpr std::uint8_t kBylineTitle = 85;
constexpr std::uint8_t kCity = 90;
constexpr std::uint8_t kSublocation = 92;
constexpr std::uint8_t kProvinceState = 95;
constexpr std::uint8_t kCountryCode = 100;
constexpr std::uint8_t kCountryName = 101;
constexpr std::uint8_t kTransmissionReference = 103;
constexpr std::uint8_t kHeadline = 105;
constexpr std::uint8_t kCredit = 110;
constexpr std::uint8_t kSource = 115;
constexpr std::uint8_t kCopyrightNotice = 116;
constexpr std::uint8_t kCaption = 120;
constexpr std::uint8_t kWriterEditor = 122;
}  // namespace iim

// One block of a Photoshop image-resource list.
struct IrbResource {
  std::uint32_t signature{0x3842494DU};  // '8BIM' (also '8B64', 'MeSa', 'PHUT', 'AgHg', 'DCSR')
  std::uint16_t id{0};
  std::string name;  // Pascal name, usually empty
  std::vector<std::uint8_t> data;
};

// Parses a sequence of resource blocks; stops quietly at the first malformed one.
[[nodiscard]] std::vector<IrbResource> parse_irb(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> write_irb(const std::vector<IrbResource>& resources);

// Descriptive name of a Photoshop image resource id ("XMP metadata", "Thumbnail").
[[nodiscard]] std::string psd_resource_name(std::uint16_t id);
[[nodiscard]] PrivacyClass psd_resource_privacy(std::uint16_t id) noexcept;

namespace psd_resource {
constexpr std::uint16_t kResolutionInfo = 1005;
constexpr std::uint16_t kCaption = 1008;
constexpr std::uint16_t kIptc = 1028;
constexpr std::uint16_t kThumbnailPs4 = 1033;
constexpr std::uint16_t kCopyrightFlag = 1034;
constexpr std::uint16_t kUrl = 1035;
constexpr std::uint16_t kThumbnail = 1036;
constexpr std::uint16_t kIccProfile = 1039;
constexpr std::uint16_t kWorkflowUrl = 1051;
constexpr std::uint16_t kVersionInfo = 1057;
constexpr std::uint16_t kExifData1 = 1058;
constexpr std::uint16_t kExifData3 = 1059;
constexpr std::uint16_t kXmp = 1060;
constexpr std::uint16_t kCaptionDigest = 1061;
constexpr std::uint16_t kMacPrintInfo = 1084;
constexpr std::uint16_t kWinDevMode = 1085;
constexpr std::uint16_t kAutoSaveFilePath = 1086;
constexpr std::uint16_t kAutoSaveFormat = 1087;
constexpr std::uint16_t kLightroomWorkflow = 8000;
}  // namespace psd_resource

// Thumbnail resources (1033/1036) hold a 28-byte header followed by JFIF data.
// Returns the JPEG bytes, or empty when the header is not the JFIF format.
[[nodiscard]] std::vector<std::uint8_t> psd_thumbnail_jpeg(std::span<const std::uint8_t> resource_data);

// Latin-1 or UTF-8 bytes to UTF-8 (valid UTF-8 is kept as is).
[[nodiscard]] std::string legacy_text_to_utf8(std::span<const std::uint8_t> bytes);

}  // namespace patchy::metadata
