#include "ui/save_metadata_settings.hpp"

#include "formats/metadata/metadata_edit.hpp"
#include "ui/app_settings.hpp"

#include <QObject>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <array>

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
  personal.data_mining = settings.value(QLatin1String(kSaveMetadataDataMiningSettingKey)).toString().trimmed();
  personal.ask = settings.value(QLatin1String(kSaveMetadataAskSettingKey), true).toBool();
  return personal;
}

void set_stored_personal_save_metadata(const PersonalSaveMetadata& personal) {
  auto settings = app_settings();
  settings.setValue(QLatin1String(kSaveMetadataNameSettingKey), personal.name.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataCopyrightSettingKey), personal.copyright.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataEmailSettingKey), personal.email.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataWebsiteSettingKey), personal.website.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataDataMiningSettingKey), personal.data_mining.trimmed());
  settings.setValue(QLatin1String(kSaveMetadataAskSettingKey), personal.ask);
}

QString suggested_copyright_notice(const QString& name, int year) {
  return QStringLiteral("© %1 %2").arg(year).arg(name.trimmed()).trimmed();
}

QVector<DataMiningChoice> data_mining_choices() {
  // Labels follow the PLUS vocabulary's own wording (IPTC Photo Metadata, Data Mining).
  const std::array<QString, metadata::kDataMiningValues.size()> labels{
      QObject::tr("Unspecified"),
      QObject::tr("Allowed"),
      QObject::tr("Prohibited for AI/ML training"),
      QObject::tr("Prohibited for generative AI/ML training"),
      QObject::tr("Prohibited except for search engine indexing"),
      QObject::tr("Prohibited"),
      QObject::tr("Prohibited, see Other Constraints"),
      QObject::tr("Prohibited, see the embedded rights expression"),
      QObject::tr("Prohibited, see the linked rights expression"),
  };
  QVector<DataMiningChoice> choices;
  choices.push_back({QString(), QObject::tr("Not set")});
  for (std::size_t i = 0; i < labels.size(); ++i) {
    const auto uri = metadata::kDataMiningValues[i];
    choices.push_back({QString::fromLatin1(uri.data(), static_cast<qsizetype>(uri.size())), labels[i]});
  }
  return choices;
}

QString data_mining_label(const QString& uri) {
  const auto trimmed = uri.trimmed();
  if (trimmed.isEmpty()) {
    return data_mining_choices().front().label;
  }
  if (const auto index = metadata::data_mining_value_index(trimmed.toStdString())) {
    return data_mining_choices().at(static_cast<qsizetype>(*index) + 1).label;
  }
  return trimmed;
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
  if (!personal.data_mining.isEmpty() && after.data_mining.empty()) {
    after.data_mining = personal.data_mining.toStdString();
  }
  return metadata::apply_fields(info, before, after);
}

bool personal_data_mining_missing(const DocumentFileInfo& info, const PersonalSaveMetadata& personal) {
  return !personal.data_mining.isEmpty() && metadata::read_fields(info).data_mining.empty();
}

}  // namespace patchy::ui
