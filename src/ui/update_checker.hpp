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

using UpdateCheckCallback = std::function<void(std::optional<UpdateInfo>)>;

[[nodiscard]] QString current_update_platform();
[[nodiscard]] QUrl update_manifest_url();
[[nodiscard]] bool update_version_is_newer(const QString& latest_version, const QString& current_version);
[[nodiscard]] std::optional<UpdateInfo> parse_update_manifest(const QByteArray& json, const QString& platform,
                                                              const QString& current_version);
void request_latest_update(QObject* owner, QString current_version, UpdateCheckCallback callback);

}  // namespace patchy::ui
