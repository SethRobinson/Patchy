#pragma once

#include <QByteArray>
#include <QUrl>
#include <QString>

#include <functional>
#include <optional>

class QObject;

namespace patchy::ui {

struct UpdateInfo {
  QString platform;
  QString version;
  QUrl download_url;
};

enum class UpdateCheckStatus {
  UpdateAvailable,
  NoUpdateAvailable,
  UnsupportedPlatform,
  InvalidManifest,
  MissingPlatform,
  InvalidVersion,
  InvalidDownloadUrl,
  NetworkError,
};

struct UpdateCheckResult {
  UpdateCheckStatus status{UpdateCheckStatus::InvalidManifest};
  std::optional<UpdateInfo> update;
  QString platform;
  QString latest_version;
  QString detail;
  int http_status{0};
};

using UpdateCheckResultCallback = std::function<void(UpdateCheckResult)>;

[[nodiscard]] QString current_update_platform();
[[nodiscard]] QUrl update_manifest_url();
[[nodiscard]] bool update_version_is_newer(const QString& latest_version, const QString& current_version);
[[nodiscard]] UpdateCheckResult inspect_update_manifest(const QByteArray& json, const QString& platform,
                                                        const QString& current_version);
[[nodiscard]] std::optional<UpdateInfo> parse_update_manifest(const QByteArray& json, const QString& platform,
                                                              const QString& current_version);
void request_update_check(QObject* owner, QString current_version, UpdateCheckResultCallback callback);

}  // namespace patchy::ui
