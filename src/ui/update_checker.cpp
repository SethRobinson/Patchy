#include "ui/update_checker.hpp"

#include "ui/host_resolve.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {
namespace {

#ifdef Q_OS_WIN
// The processor Windows itself runs on. IsWow64Process2 reports it for an emulated x64
// process too, where PROCESSOR_ARCHITECTURE says AMD64.
bool windows_machine_is_arm64() {
  USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
  USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;
  return IsWow64Process2(GetCurrentProcess(), &process_machine, &native_machine) != FALSE &&
         native_machine == IMAGE_FILE_MACHINE_ARM64;
}
#endif

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

// A file the update can install by itself, pinned by the manifest: the Windows installer
// (installer_url, installer_size, installer_sha256, installer_min_version) or the AppImage
// (appimage_install_url, appimage_install_size, appimage_install_sha256,
// appimage_min_version). Every key name is permanent. All three or none: a partial or
// malformed set leaves the update a browser download.
struct PinnedDownload {
  QUrl url;
  qint64 size{0};
  QByteArray sha256;  // lowercase hex
};

std::optional<PinnedDownload> read_pinned_download(const QJsonObject& platform_entry, const QString& current_version,
                                                   const QString& prefix, const QString& min_version_key) {
  // The remote off switch: the min-version key names the oldest running version allowed to
  // install by itself. Older copies keep the browser download, so a release whose
  // self-update turns out broken is switched back from the manifest alone, while fixed
  // versions carry on. A value that does not parse switches it off for everyone.
  const auto min_version = platform_entry.value(min_version_key);
  if (!min_version.isUndefined()) {
    const auto required = min_version.toString().trimmed();
    if (!parse_dotted_version(required).has_value() || update_version_is_newer(required, current_version)) {
      return std::nullopt;
    }
  }
  const QUrl url(platform_entry.value(prefix + QStringLiteral("_url")).toString().trimmed());
  const auto size = static_cast<qint64>(platform_entry.value(prefix + QStringLiteral("_size")).toDouble(0.0));
  const auto sha256 = platform_entry.value(prefix + QStringLiteral("_sha256")).toString().trimmed().toLower().toLatin1();
  // https only. A manifest named by PATCHY_UPDATE_MANIFEST_URL (tests, a local trial of the
  // whole update) may also use http: whoever sets that variable already chooses the manifest
  // and with it the checksum.
  const bool scheme_ok =
      url.scheme() == QStringLiteral("https") ||
      (url.scheme() == QStringLiteral("http") && qEnvironmentVariableIsSet("PATCHY_UPDATE_MANIFEST_URL"));
  // A size no installer has: the download is capped at the pinned size, so the pin itself
  // must be sane.
  constexpr qint64 kLargestInstallerBytes = qint64{2} * 1024 * 1024 * 1024;
  if (!url.isValid() || !scheme_ok || url.host().isEmpty() || size <= 0 || size > kLargestInstallerBytes ||
      sha256.size() != 64 || QByteArray::fromHex(sha256).toHex() != sha256) {
    return std::nullopt;
  }
  return PinnedDownload{url, size, sha256};
}

void read_installer_fields(const QJsonObject& platform_entry, const QString& current_version, UpdateInfo& update) {
  const auto pinned = read_pinned_download(platform_entry, current_version, QStringLiteral("installer"),
                                           QStringLiteral("installer_min_version"));
  if (pinned.has_value()) {
    update.installer_url = pinned->url;
    update.installer_size = pinned->size;
    update.installer_sha256 = pinned->sha256;
  }
}

void read_appimage_install_fields(const QJsonObject& platform_entry, const QString& current_version,
                                  UpdateInfo& update) {
  const auto pinned = read_pinned_download(platform_entry, current_version, QStringLiteral("appimage_install"),
                                           QStringLiteral("appimage_min_version"));
  if (!pinned.has_value()) {
    return;
  }
  // No publisher signature backs an AppImage, so its source is pinned too: this repository's
  // GitHub release downloads (kAppImageReleaseDownloadPrefix). A manifest chosen through
  // PATCHY_UPDATE_MANIFEST_URL (a local trial) may name any host, as for the scheme.
  if (!qEnvironmentVariableIsSet("PATCHY_UPDATE_MANIFEST_URL") &&
      !pinned->url.toString().startsWith(QString::fromLatin1(kAppImageReleaseDownloadPrefix))) {
    return;
  }
  update.appimage_install_url = pinned->url;
  update.appimage_install_size = pinned->size;
  update.appimage_install_sha256 = pinned->sha256;
}

QUrl cache_busted_manifest_url() {
  auto url = update_manifest_url();
  QUrlQuery query(url);
  query.addQueryItem(QStringLiteral("patchy_check"), QString::number(QDateTime::currentSecsSinceEpoch()));
  url.setQuery(query);
  return url;
}

}  // namespace

bool update_has_installer(const UpdateInfo& update) {
  return update.installer_url.isValid() && !update.installer_url.isEmpty() && update.installer_size > 0 &&
         !update.installer_sha256.isEmpty();
}

bool update_has_appimage_install(const UpdateInfo& update) {
  return update.appimage_install_url.isValid() && !update.appimage_install_url.isEmpty() &&
         update.appimage_install_size > 0 && !update.appimage_install_sha256.isEmpty();
}

QUrl update_direct_download_url(const UpdateInfo& update, bool appimage) {
  if (appimage) {
    return update.appimage_url.isEmpty() ? QUrl(QString::fromLatin1(kAppImageDownloadUrl)) : update.appimage_url;
  }
  return update_has_installer(update) ? update.installer_url : update.download_url;
}

bool update_checks_available() {
#if defined(Q_OS_WASM) || defined(PATCHY_STORE_BUILD)
  return false;
#else
  return qEnvironmentVariableIntValue("PATCHY_NO_UPDATE_CHECK") == 0;
#endif
}

QStringList windows_update_platforms(bool build_is_arm64, bool machine_is_arm64) {
  // "windows-arm64" is a permanent manifest key beside "windows" (docs/auto-update.md).
  if (build_is_arm64) {
    return {QStringLiteral("windows-arm64")};
  }
  if (machine_is_arm64) {
    return {QStringLiteral("windows-arm64"), QStringLiteral("windows")};
  }
  return {QStringLiteral("windows")};
}

QStringList current_update_platforms() {
#if defined(Q_OS_WIN)
#if defined(Q_PROCESSOR_ARM_64)
  return windows_update_platforms(true, true);
#else
  return windows_update_platforms(false, windows_machine_is_arm64());
#endif
#elif defined(Q_OS_MACOS)
  return {QStringLiteral("macos")};
#elif defined(Q_OS_LINUX)
  return {QStringLiteral("linux")};
#else
  return {};
#endif
}

QString current_update_platform() {
  const auto platforms = current_update_platforms();
  return platforms.isEmpty() ? QString() : platforms.front();
}

QUrl update_manifest_url() {
  // Test and diagnostics knob: point the startup check at another manifest (a local
  // server, or an unresolvable host to hold a lookup open; see docs/platform.md).
  const auto override_url = qEnvironmentVariable("PATCHY_UPDATE_MANIFEST_URL");
  if (!override_url.isEmpty()) {
    return QUrl(override_url);
  }
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

bool startup_update_check_due(int interval_days, qint64 last_check_secs, qint64 now_secs, const QTimeZone& zone) {
  if (interval_days <= 0 || last_check_secs <= 0 || last_check_secs > now_secs) {
    return true;
  }
  const auto last_day = QDateTime::fromSecsSinceEpoch(last_check_secs, zone).date();
  const auto today = QDateTime::fromSecsSinceEpoch(now_secs, zone).date();
  return last_day.daysTo(today) >= interval_days;
}

bool startup_update_check_due(int interval_days, qint64 last_check_secs, qint64 now_secs) {
  return startup_update_check_due(interval_days, last_check_secs, now_secs, QTimeZone(QTimeZone::LocalTime));
}

bool update_prompt_wanted(const QString& version, const QString& skipped_version) {
  if (!parse_dotted_version(skipped_version).has_value()) {
    return true;
  }
  return update_version_is_newer(version, skipped_version);
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
  read_installer_fields(platform_entry, current_version, *result.update);
  read_appimage_install_fields(platform_entry, current_version, *result.update);
  if (const QUrl appimage_url(platform_entry.value(QStringLiteral("appimage_url")).toString().trimmed());
      is_download_url_usable(appimage_url)) {
    result.update->appimage_url = appimage_url;
  }
  return result;
}

UpdateCheckResult inspect_update_manifest(const QByteArray& json, const QStringList& platforms,
                                          const QString& current_version) {
  if (platforms.isEmpty()) {
    return inspect_update_manifest(json, QString(), current_version);
  }
  for (const auto& platform : platforms) {
    auto result = inspect_update_manifest(json, platform, current_version);
    if (result.status != UpdateCheckStatus::MissingPlatform) {
      return result;
    }
  }
  return inspect_update_manifest(json, platforms.front(), current_version);
}

namespace {

UpdateCheckResult network_error_result() {
  UpdateCheckResult result;
  result.status = UpdateCheckStatus::NetworkError;
  result.platform = current_update_platform();
  return result;
}

void start_manifest_request(const QPointer<QObject>& owner_guard, const QUrl& url, QString current_version,
                            UpdateCheckResultCallback callback) {
  QObject* owner = owner_guard.data();
  if (owner == nullptr) {
    return;
  }
  auto* manager = new QNetworkAccessManager(owner);
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setRawHeader("Cache-Control", "no-cache");
  request.setRawHeader("Pragma", "no-cache");
  request.setTransferTimeout(10000);

  auto* reply = manager->get(request);
  QObject::connect(reply, &QNetworkReply::finished, owner,
                   [reply, manager, owner_guard, current_version = std::move(current_version),
                    callback = std::move(callback)]() mutable {
                     UpdateCheckResult result;
                     if (reply->error() == QNetworkReply::NoError) {
                       result = inspect_update_manifest(reply->readAll(), current_update_platforms(), current_version);
                     } else {
                       result = network_error_result();
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

}  // namespace

void request_update_check(QObject* owner, QString current_version, UpdateCheckResultCallback callback) {
  if (owner == nullptr || !callback) {
    return;
  }
  const QPointer<QObject> owner_guard(owner);
  const auto url = cache_busted_manifest_url();
  // The host resolves on a detached thread first (GitHub issue 48; host_resolve.hpp).
  resolve_host_first(owner, url.host(),
                     [owner_guard, url, current_version = std::move(current_version),
                      callback = std::move(callback)](bool resolved) mutable {
                       if (owner_guard == nullptr) {
                         return;
                       }
                       if (resolved) {
                         start_manifest_request(owner_guard, url, std::move(current_version), std::move(callback));
                       } else {
                         callback(network_error_result());
                       }
                     });
}

QString update_check_status_text(const UpdateCheckResult& result) {
  switch (result.status) {
    case UpdateCheckStatus::UpdateAvailable:
      if (result.update.has_value()) {
        return QObject::tr("Update available: Patchy %1.").arg(result.update->version);
      }
      return QObject::tr("Update available.");
    case UpdateCheckStatus::NoUpdateAvailable:
      if (!result.latest_version.isEmpty()) {
        return QObject::tr("Patchy is up to date (%1).").arg(result.latest_version);
      }
      return QObject::tr("Patchy is up to date.");
    case UpdateCheckStatus::UnsupportedPlatform:
      return QObject::tr("Update checks are not supported on this platform.");
    case UpdateCheckStatus::MissingPlatform:
      if (!result.platform.isEmpty()) {
        return QObject::tr("Update check failed: no manifest entry for %1.").arg(result.platform);
      }
      return QObject::tr("Update check failed: no manifest entry for this platform.");
    case UpdateCheckStatus::InvalidManifest:
      return QObject::tr("Update check failed: invalid update manifest.");
    case UpdateCheckStatus::InvalidVersion:
      return QObject::tr("Update check failed: invalid version data.");
    case UpdateCheckStatus::InvalidDownloadUrl:
      return QObject::tr("Update check failed: invalid download URL.");
    case UpdateCheckStatus::NetworkError:
      if (result.http_status > 0 && !result.detail.isEmpty()) {
        return QObject::tr("Update check failed: HTTP %1 (%2).").arg(result.http_status).arg(result.detail);
      }
      if (result.http_status > 0) {
        return QObject::tr("Update check failed: HTTP %1.").arg(result.http_status);
      }
      if (!result.detail.isEmpty()) {
        return QObject::tr("Update check failed: %1.").arg(result.detail);
      }
      return QObject::tr("Update check failed.");
  }
  return QObject::tr("Update check failed.");
}

}  // namespace patchy::ui
