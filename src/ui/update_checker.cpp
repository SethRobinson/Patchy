#include "ui/update_checker.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QUrlQuery>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

std::optional<std::vector<std::int64_t>> parse_dotted_version(QString version) {
  version = version.trimmed();
  if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
    version.remove(0, 1);
  }
  if (version.isEmpty()) {
    return std::nullopt;
  }

  std::vector<std::int64_t> parts;
  for (const auto& part : version.split(QLatin1Char('.'))) {
    if (part.isEmpty()) {
      return std::nullopt;
    }
    bool ok = false;
    const auto value = part.toLongLong(&ok);
    if (!ok || value < 0) {
      return std::nullopt;
    }
    parts.push_back(value);
  }
  return parts;
}

bool is_download_url_usable(const QUrl& url) {
  if (!url.isValid() || url.isRelative()) {
    return false;
  }
  const auto scheme = url.scheme();
  return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

QUrl cache_busted_manifest_url() {
  auto url = update_manifest_url();
  QUrlQuery query(url);
  query.addQueryItem(QStringLiteral("patchy_check"), QString::number(QDateTime::currentSecsSinceEpoch()));
  url.setQuery(query);
  return url;
}

}  // namespace

QString current_update_platform() {
#if defined(Q_OS_WIN)
  return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("macos");
#else
  return {};
#endif
}

QUrl update_manifest_url() {
  return QUrl(QStringLiteral("https://raw.githubusercontent.com/SethRobinson/Patchy/main/latest_version.json"));
}

bool update_version_is_newer(const QString& latest_version, const QString& current_version) {
  const auto latest_parts = parse_dotted_version(latest_version);
  const auto current_parts = parse_dotted_version(current_version);
  if (!latest_parts.has_value() || !current_parts.has_value()) {
    return false;
  }

  const auto count = std::max(latest_parts->size(), current_parts->size());
  for (std::size_t index = 0; index < count; ++index) {
    const auto latest_part = index < latest_parts->size() ? (*latest_parts)[index] : 0;
    const auto current_part = index < current_parts->size() ? (*current_parts)[index] : 0;
    if (latest_part != current_part) {
      return latest_part > current_part;
    }
  }
  return false;
}

std::optional<UpdateInfo> parse_update_manifest(const QByteArray& json, const QString& platform,
                                                const QString& current_version) {
  return inspect_update_manifest(json, platform, current_version).update;
}

UpdateCheckResult inspect_update_manifest(const QByteArray& json, const QString& platform,
                                          const QString& current_version) {
  UpdateCheckResult result;
  result.platform = platform;

  if (platform.isEmpty()) {
    result.status = UpdateCheckStatus::UnsupportedPlatform;
    return result;
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    result.status = UpdateCheckStatus::InvalidManifest;
    result.detail = parse_error.errorString();
    return result;
  }

  const auto platforms = document.object().value(QStringLiteral("platforms")).toObject();
  if (platforms.isEmpty()) {
    result.status = UpdateCheckStatus::InvalidManifest;
    result.detail = QStringLiteral("missing platforms");
    return result;
  }
  const auto platform_entry = platforms.value(platform).toObject();
  if (platform_entry.isEmpty()) {
    result.status = UpdateCheckStatus::MissingPlatform;
    return result;
  }

  const auto latest_version = platform_entry.value(QStringLiteral("version")).toString().trimmed();
  const auto download_url = QUrl(platform_entry.value(QStringLiteral("download_url")).toString().trimmed());
  result.latest_version = latest_version;
  if (latest_version.isEmpty() || !parse_dotted_version(latest_version).has_value() ||
      !parse_dotted_version(current_version).has_value()) {
    result.status = UpdateCheckStatus::InvalidVersion;
    return result;
  }
  if (!is_download_url_usable(download_url)) {
    result.status = UpdateCheckStatus::InvalidDownloadUrl;
    return result;
  }

  if (!update_version_is_newer(latest_version, current_version)) {
    result.status = UpdateCheckStatus::NoUpdateAvailable;
    return result;
  }

  result.status = UpdateCheckStatus::UpdateAvailable;
  result.update = UpdateInfo{platform, latest_version, download_url};
  return result;
}

void request_update_check(QObject* owner, QString current_version, UpdateCheckResultCallback callback) {
  if (owner == nullptr || !callback) {
    return;
  }

  auto* manager = new QNetworkAccessManager(owner);
  QNetworkRequest request(cache_busted_manifest_url());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setRawHeader("Cache-Control", "no-cache");
  request.setRawHeader("Pragma", "no-cache");
  request.setTransferTimeout(10000);

  auto* reply = manager->get(request);
  const QPointer<QObject> owner_guard(owner);
  QObject::connect(reply, &QNetworkReply::finished, owner,
                   [reply, manager, owner_guard, current_version = std::move(current_version),
                    callback = std::move(callback)]() mutable {
                     UpdateCheckResult result;
                     if (reply->error() == QNetworkReply::NoError) {
                       result = inspect_update_manifest(reply->readAll(), current_update_platform(), current_version);
                     } else {
                       result.status = UpdateCheckStatus::NetworkError;
                       result.platform = current_update_platform();
                       result.detail = reply->errorString();
                       result.http_status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                     }
                     if (owner_guard != nullptr) {
                       callback(std::move(result));
                     }
                     reply->deleteLater();
                     manager->deleteLater();
                   });
}

}  // namespace patchy::ui
