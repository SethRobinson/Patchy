#pragma once

#include "core/document.hpp"
#include "core/document_file_info.hpp"

#include <QString>

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

// File > File Info (docs/file-info.md): views every metadata item the document holds,
// edits the Description and IPTC fields, and stages Strip All Metadata, Remove
// Location and Remove Edit History. Nothing touches the document until OK.
struct FileInfoEdit {
  DocumentFileInfo info;
  bool drop_color_profile{false};
};

// What the Watermarks page needs from the app (docs/watermarks.md).
struct FileInfoWatermarkSupport {
  // Downloads the TrustMark watermark models with progress shown over `parent`; true when
  // they are present afterwards. Empty: the page offers no download.
  std::function<bool(QWidget* parent)> download_models;
};

// nullopt on Cancel or when nothing changed.
[[nodiscard]] std::optional<FileInfoEdit> request_file_info_edit(QWidget* parent, const Document& document,
                                                                 const QString& file_path,
                                                                 const FileInfoWatermarkSupport& watermarks = {});

// Translated name of a privacy class ("GPS location", "camera serial numbers", ...).
[[nodiscard]] QString privacy_class_label(MetadataPrivacy privacy);

// Confirms Strip All Metadata. nullopt when cancelled; otherwise whether to keep the
// color profile.
[[nodiscard]] std::optional<bool> confirm_strip_metadata(QWidget* parent);

}  // namespace patchy::ui
