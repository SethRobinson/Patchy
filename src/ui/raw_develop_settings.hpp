#pragma once

#include "formats/raw_document_io.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace patchy::ui {

// The schema, processing version and parameter tokens are persisted contracts.
struct RawDevelopSettings {
  raw::DevelopParams params;
  QJsonObject preserved;
  QByteArray original_bytes;
  QString notice;
  bool exists{false};
  bool recognized{false};
};

[[nodiscard]] QString raw_develop_settings_path(const QString& source_path);
[[nodiscard]] RawDevelopSettings load_raw_develop_settings(const QString& source_path);
// Re-reads before writing to protect a concurrently changed or unreadable sidecar.
// Returns a translated error, or empty on success. replace_unrecognized is explicit UI consent.
[[nodiscard]] QString save_raw_develop_settings(const QString& source_path,
    const raw::DevelopParams& params, const RawDevelopSettings& loaded,
    bool replace_unrecognized = false);

}  // namespace patchy::ui
