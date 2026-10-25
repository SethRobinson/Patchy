#include "ui/save_metadata_settings.hpp"

#include "formats/metadata/metadata_edit.hpp"
#include "ui/app_settings.hpp"

#include <QSettings>
#include <QStringList>

#include <algorithm>

namespace patchy::ui {

QString save_metadata_mode_token(SaveMetadataMode mode) {
  switch (mode) {
    case SaveMetadataMode::None:
      return QStringLiteral("none");
    case SaveMetadataMode::Basic:
      return QStringLiteral("basic");
    case SaveMetadataMode::Personal:
      return QStringLiteral("personal");
  }
  return QStringLiteral("basic");
}

std::optional<SaveMetadataMode> save_metadata_mode_from_token(const QString& token) {
  for (const auto mode : {SaveMetadataMode::None, SaveMetadataMode::Basic, SaveMetadataMode::Personal}) {
    if (save_metadata_mode_token(mode) == token) {
      return mode;
    }
  }
  return std::nullopt;
}

SaveMetadataMode stored_save_metadata_mode() {
  return save_metadata_mode_from_token(app_settings().value(QLatin1String(kSaveMetadataModeSettingKey)).toString())
      .value_or(SaveMetadataMode::Basic);
}

bool stored_show_file_info_privacy_notice() {
  return app_settings().value(QLatin1String(kFileInfoPrivacyNoticeSettingKey), true).toBool();
}

void set_stored_show_file_info_privacy_notice(bool show) {
  app_settings().setValue(QLatin1String(kFileInfoPrivacyNoticeSettingKey), show);
}

void set_stored_save_metadata_mode(SaveMetadataMode mode) {
  app_settings().setValue(QLatin1String(kSaveMetadataModeSettingKey), save_metadata_mode_token(mode));
}

PersonalSaveMetadata stored_personal_save_metadata() {
  auto settings = app_settings();
  PersonalSaveMetadata personal;
  personal.name = settings.value(QLatin1String(kSaveMetadataNameSettingKey)).toString().trimmed();
  personal.copyright = settings.value(QLatin1String(kSaveMetadataCopyrightSettingKey)).toString().trimmed();
  personal.email = settings.value(QLatin1String(kSaveMetadataEmailSettingKey)).toString().trimmed();
  personal.website = settings.value(QLatin1String(kSaveMetadataWebsiteSettingKey)).toString().trimmed();
  personal.ask = settings.value(QLatin1String(kSaveMetadataAskSettingKey), true).toBool();
  return personal;
}

void set_stored_personal_save_metadata(const PersonalSaveMetadata& personal) {
  auto settings = app_settings();
  settings.setValue(QLatin1String(kSaveMetadataNameSettingKey), personal.name.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataCopyrightSettingKey), personal.copyright.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataEmailSettingKey), personal.email.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataWebsiteSettingKey), personal.website.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataAskSettingKey), personal.ask);
}

QString suggested_copyright_notice(const QString& name, int year) {
  return QStringLiteral("© %1 %2").arg(year).arg(name.trimmed()).trimmed();
}

bool save_format_carries_metadata(const QString& extension) {
  static const QStringList formats{
      QStringLiteral("psd"),  QStringLiteral("psb"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
      QStringLiteral("jpe"),  QStringLiteral("png"),  QStringLiteral("tif"), QStringLiteral("tiff"),
      QStringLiteral("webp"), QStringLiteral("gif"),  QStringLiteral("jxl"), QStringLiteral("avif"),
      QStringLiteral("pdf")};
  return formats.contains(extension.toLower());
}

QStringList file_info_creators(const DocumentFileInfo& info) {
  QStringList creators;
  for (const auto& author : metadata::read_fields(info).authors) {
    creators.push_back(QString::fromStdString(author));
  }
  return creators;
}

bool file_info_names_creator(const DocumentFileInfo& info, const QString& name) {
  const auto wanted = name.trimmed();
  if (wanted.isEmpty()) {
    return false;
  }
  const auto creators = file_info_creators(info);
  return std::any_of(creators.begin(), creators.end(), [&wanted](const QString& creator) {
    return creator.trimmed().compare(wanted, Qt::CaseInsensitive) == 0;
  });
}

bool add_personal_save_metadata(DocumentFileInfo& info, const PersonalSaveMetadata& personal) {
  const auto before = metadata::read_fields(info);
  auto after = before;
  if (!personal.name.isEmpty() && !file_info_names_creator(info, personal.name)) {
    after.authors.push_back(personal.name.toStdString());
  }
  if (!personal.copyright.isEmpty() && after.copyright_notice.empty()) {
    after.copyright_notice = personal.copyright.toStdString();
    if (after.copyright_status == metadata::CopyrightStatus::Unknown) {
      after.copyright_status = metadata::CopyrightStatus::Copyrighted;
    }
  }
  if (!personal.email.isEmpty() && after.contact_email.empty()) {
    after.contact_email = personal.email.toStdString();
  }
  if (!personal.website.isEmpty() && after.contact_url.empty()) {
    after.contact_url = personal.website.toStdString();
  }
  return metadata::apply_fields(info, before, after);
}

}  // namespace patchy::ui
