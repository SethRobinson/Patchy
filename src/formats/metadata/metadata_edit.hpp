#pragma once

#include "core/document.hpp"
#include "core/document_file_info.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Editing, stripping and save preparation for File Info (docs/file-info.md). Qt-free.
namespace patchy::metadata {

enum class CopyrightStatus : std::uint8_t { Unknown, Copyrighted, PublicDomain };

// The fields File Info edits, as Photoshop's Description and IPTC panels show them.
// Multi-value fields (authors, keywords, scene and subject codes) are lists.
struct FileInfoFields {
  // Description
  std::string title;                 // dc:title
  std::vector<std::string> authors;  // dc:creator
  std::string author_title;          // photoshop:AuthorsPosition
  std::string description;           // dc:description
  int rating{0};                     // xmp:Rating, 0..5 (-1 = rejected)
  std::string description_writer;    // photoshop:CaptionWriter
  std::vector<std::string> keywords; // dc:subject
  CopyrightStatus copyright_status{CopyrightStatus::Unknown};  // xmpRights:Marked
  std::string copyright_notice;      // dc:rights
  std::string copyright_url;         // xmpRights:WebStatement
  // IPTC
  std::string headline;              // photoshop:Headline
  std::string date_created;          // photoshop:DateCreated
  std::string city, state, country;  // photoshop:City/State/Country
  std::string sublocation;           // Iptc4xmpCore:Location
  std::string country_code;          // Iptc4xmpCore:CountryCode
  std::string credit;                // photoshop:Credit
  std::string source;                // photoshop:Source
  std::string instructions;          // photoshop:Instructions
  std::string transmission_reference;  // photoshop:TransmissionReference
  std::string usage_terms;           // xmpRights:UsageTerms
  std::string intellectual_genre;    // Iptc4xmpCore:IntellectualGenre
  std::vector<std::string> scene;    // Iptc4xmpCore:Scene
  std::vector<std::string> subject_code;  // Iptc4xmpCore:SubjectCode
  // Iptc4xmpCore:CreatorContactInfo
  std::string contact_address, contact_city, contact_region, contact_postal_code, contact_country;
  std::string contact_phone, contact_email, contact_url;
  // Read-only
  std::string created;       // xmp:CreateDate (or EXIF DateTimeOriginal)
  std::string modified;      // xmp:ModifyDate (or EXIF DateTime)
  std::string creator_tool;  // xmp:CreatorTool (or EXIF Software)

  bool operator==(const FileInfoFields&) const = default;
};

// Reads the fields with the Metadata Working Group rules: XMP first; legacy IPTC-IIM
// when XMP lacks the value or the 1061 digest shows another program edited the IIM;
// EXIF for description, author, copyright and dates when nothing else has them.
[[nodiscard]] FileInfoFields read_fields(const DocumentFileInfo& info);

// Writes every field that differs between `before` and `after` into XMP (always) and
// into the EXIF and IPTC-IIM mirrors that already exist (MWG), recomputing the IIM
// digest and the Photoshop copyright flag/URL. Returns true when anything changed.
bool apply_fields(DocumentFileInfo& info, const FileInfoFields& before, const FileInfoFields& after);

// Strip All Metadata: everything except the color profile and the color-rendering
// chunks, unless `keep_color_profile` is false. The resolution lives in the
// document's print settings and is never touched.
void strip_metadata(Document& document, bool keep_color_profile);
// The DocumentFileInfo half of strip_metadata (File Info stages a strip on a copy;
// the caller drops the document's embedded profile itself when asked to).
void strip_file_info(DocumentFileInfo& info, bool keep_color_profile);
// Removes GPS from EXIF and XMP. Returns true when something was removed.
bool remove_location(DocumentFileInfo& info);
// Removes XMP edit history, document ancestry, derived-from links, the text-layer
// cache, Camera Raw settings, and Photoshop's auto-save and Lightroom records.
bool remove_edit_history(DocumentFileInfo& info);

// The decoder already applied the EXIF/XMP orientation to the pixels: record the
// original orientation for File Info and write "normal" back, so saves do not rotate
// the image a second time. Returns the original orientation text, or empty.
std::string bake_orientation(DocumentFileInfo& info);

// Append-only: settings and scripts store tokens, never these values, but the order is fixed.
enum class MetadataPolicy : std::uint8_t {
  All,                  // Save / Save As, Export "All"
  CopyrightAndContact,  // Export "Copyright and contact info"
  None,                 // Export "None" (the color profile and resolution stay)
  Basic,                // Export "Basic": the save stamp alone, for the file written (docs/file-info.md)
};

// What a flat-format writer embeds.
struct SaveMetadata {
  std::vector<std::uint8_t> exif;  // TIFF stream (no "Exif\0\0" prefix)
  std::string xmp;                 // complete packet
  std::vector<std::uint8_t> iptc;  // IIM datasets
  std::optional<std::array<std::uint8_t, 16>> iptc_digest;
  std::vector<FileInfoText> text;  // PNG text chunks and comments
  std::vector<FileInfoChunk> png_color_chunks;
  std::vector<std::uint8_t> icc;   // profile to embed
  [[nodiscard]] bool empty() const noexcept {
    return exif.empty() && xmp.empty() && iptc.empty() && text.empty() && png_color_chunks.empty() && icc.empty();
  }
};

// A PSD/PSB save, recorded in the document's XMP and EXIF (docs/file-info.md, "Save stamp").
struct SaveStamp {
  std::string when;           // ISO 8601 local time with its offset (xmp_date)
  std::string software;       // "Patchy 1.14"
  std::string instance_guid;  // a fresh random GUID per save, lowercase with hyphens
};

// The document as a PSD/PSB save writes it, for the parts of the stamp that describe the
// file rather than the save.
struct SaveStampDocument {
  std::string format{"application/vnd.adobe.photoshop"};  // dc:format
  int color_mode{3};  // photoshop:ColorMode, the PSD header mode (1 gray, 3 RGB, 4 CMYK, ...)
  std::int32_t width{0};
  std::int32_t height{0};
  double horizontal_ppi{72.0};
  double vertical_ppi{72.0};
  bool srgb{false};  // EXIF ColorSpace 1 (sRGB), else 65535 (uncalibrated)
};

// Records a save the way Photoshop does: xmp:ModifyDate and xmp:MetadataDate become
// stamp.when; xmp:CreateDate and xmp:CreatorTool are set only when the packet has none (a
// file another application created keeps its creator and creation date); xmpMM:History
// gains a stEvt entry ("created" on the first save, else "saved") naming stamp.software
// and the new xmpMM:InstanceID ("xmp.iid:" + guid; the first save also sets DocumentID
// and OriginalDocumentID when absent). An existing EXIF block gets IFD0 Software and
// DateTime from the stamp. Every other property stays. A packet that does not parse is
// left as it is.
// With `document` the stamp also describes the saved file as Photoshop does: dc:format and
// photoshop:ColorMode, EXIF pixel size and resolution where the block has them, no stale
// IFD1 thumbnail, and for a document without EXIF (unless Strip All Metadata ran) a new
// block: IFD0 Orientation, resolution, Software and DateTime, Exif IFD ExifVersion,
// ColorSpace and pixel size.
void stamp_file_info_for_save(DocumentFileInfo& info, const SaveStamp& stamp,
                              const SaveStampDocument* document = nullptr);

// "2026-10-20T09:41:05+09:00": `unix_seconds` (UTC) shown at `utc_offset_minutes`.
[[nodiscard]] std::string xmp_date(std::int64_t unix_seconds, int utc_offset_minutes);

// Applies the save rules (EXIF orientation "normal", pixel dimensions and resolution
// updated, the stale IFD1 thumbnail and xmp:Thumbnails dropped) and the export policy.
// Basic writes none of the document's own metadata: only `basic_stamp` (the time, software
// and a fresh id) stamped by stamp_file_info_for_save into an empty block that describes
// the output (`basic_file`: its dc:format, mode and sRGB flag; the size is the output size
// and the resolution the document's). Without a stamp Basic writes what None writes (the
// core writers never stamp).
[[nodiscard]] SaveMetadata prepare_save_metadata(const Document& document, MetadataPolicy policy,
                                                 std::int32_t output_width, std::int32_t output_height,
                                                 const SaveStamp* basic_stamp = nullptr,
                                                 const SaveStampDocument* basic_file = nullptr);

}  // namespace patchy::metadata
