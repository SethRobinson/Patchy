#pragma once

// Preferences > Metadata and Watermarks, "Saved metadata" (docs/file-info.md, "Saved
// metadata preference"): what Patchy adds of its own when saving. The settings keys and the
// mode tokens are permanent.

#include "core/document_file_info.hpp"

#include <QString>

#include <optional>

namespace patchy::ui {

enum class SaveMetadataMode {
  None,      // no save stamp, no personal info
  Basic,     // the save stamp (PSD/PSB): creator tool, dates, history entry (the default)
  Personal,  // Basic plus the user's name, copyright and contact
};

// Settings keys (never rename).
inline constexpr auto kSaveMetadataModeSettingKey = "saveMetadata/autoAdd";
inline constexpr auto kSaveMetadataNameSettingKey = "saveMetadata/authorName";
inline constexpr auto kSaveMetadataCopyrightSettingKey = "saveMetadata/copyrightNotice";
inline constexpr auto kSaveMetadataEmailSettingKey = "saveMetadata/contactEmail";
inline constexpr auto kSaveMetadataWebsiteSettingKey = "saveMetadata/contactWebsite";
inline constexpr auto kSaveMetadataAskSettingKey = "saveMetadata/askBeforeAdding";

// "none", "basic", "personal" (also the scripting values).
[[nodiscard]] QString save_metadata_mode_token(SaveMetadataMode mode);
[[nodiscard]] std::optional<SaveMetadataMode> save_metadata_mode_from_token(const QString& token);

struct PersonalSaveMetadata {
  QString name;
  QString copyright;
  QString email;
  QString website;
  bool ask{true};  // ask before adding them to a file that lacks the name
};

[[nodiscard]] SaveMetadataMode stored_save_metadata_mode();  // Basic by default
void set_stored_save_metadata_mode(SaveMetadataMode mode);
[[nodiscard]] PersonalSaveMetadata stored_personal_save_metadata();
void set_stored_personal_save_metadata(const PersonalSaveMetadata& personal);

// "© 2026 Ann Example": the copyright line the Suggest button fills in.
[[nodiscard]] QString suggested_copyright_notice(const QString& name, int year);

// True for the formats a save writes File Info metadata into: PSD, PSB, JPEG, PNG, TIFF,
// WebP, GIF, JPEG XL, AVIF (and PDF's /Info author). BMP, TGA, ICO, DDS, Proton, JPEG XR
// and SVG carry none.
[[nodiscard]] bool save_format_carries_metadata(const QString& extension);

// The document already names `name` as a creator (dc:creator, else EXIF Artist or IIM By-line).
[[nodiscard]] bool file_info_names_creator(const DocumentFileInfo& info, const QString& name);
// The creators the document names now (for the prompt).
[[nodiscard]] QStringList file_info_creators(const DocumentFileInfo& info);

// Adds the personal fields: the name beside any creators already there (never replacing
// one), and the copyright notice, email and website only where the file has none. Sets
// the copyright status to Copyrighted when it adds a notice. True when anything changed.
bool add_personal_save_metadata(DocumentFileInfo& info, const PersonalSaveMetadata& personal);

}  // namespace patchy::ui
