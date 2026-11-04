#include "ui/network_link_trust.hpp"

#include "support/network_path.hpp"
#include "ui/app_settings.hpp"

#include <QDir>
#include <QSettings>

#include <mutex>
#include <optional>

namespace patchy::ui {

namespace {

// Persisted settings key (never rename).
constexpr auto kAllowedLinkServersKey = "imports/allowedLinkServers";

std::mutex& trust_mutex() {
  static std::mutex mutex;
  return mutex;
}

// Loaded from settings on first use, then kept in step with every write.
std::optional<QStringList>& cached_servers() {
  static std::optional<QStringList> servers;
  return servers;
}

QStringList& servers_locked() {
  auto& cache = cached_servers();
  if (!cache.has_value()) {
    QStringList loaded;
    for (const auto& entry : app_settings().value(QLatin1String(kAllowedLinkServersKey)).toStringList()) {
      const auto server = entry.trimmed().toLower();
      if (!server.isEmpty() && !loaded.contains(server)) {
        loaded.push_back(server);
      }
    }
    cache = std::move(loaded);
  }
  return *cache;
}

void store_locked(const QStringList& servers) {
  auto settings = app_settings();
  if (servers.isEmpty()) {
    settings.remove(QLatin1String(kAllowedLinkServersKey));
  } else {
    settings.setValue(QLatin1String(kAllowedLinkServersKey), servers);
  }
}

}  // namespace

QString network_link_server(const QString& path, bool* device) {
  const auto location = classify_path_location(path.toStdString());
  if (device != nullptr) {
    *device = location.kind == PathLocationKind::Device;
  }
  return location.kind == PathLocationKind::NetworkServer ? QString::fromStdString(location.server) : QString();
}

bool network_link_path_allowed(const QString& path, const QString& document_dir, QString* blocked_server) {
  bool device = false;
  const auto server = network_link_server(path, &device);
  if (device) {
    return false;
  }
  if (server.isEmpty()) {
    return true;
  }
  if (!document_dir.isEmpty() && network_link_server(document_dir) == server) {
    return true;  // the document itself came from this server
  }
  {
    const std::scoped_lock lock(trust_mutex());
    if (servers_locked().contains(server)) {
      return true;
    }
  }
  if (blocked_server != nullptr) {
    *blocked_server = server;
  }
  return false;
}

QStringList allowed_network_link_servers() {
  const std::scoped_lock lock(trust_mutex());
  return servers_locked();
}

void allow_network_link_server(const QString& server_or_path) {
  auto server = network_link_server(server_or_path);
  if (server.isEmpty() && !server_or_path.contains(QLatin1Char('/')) && !server_or_path.contains(QLatin1Char('\\'))) {
    server = server_or_path.trimmed().toLower();  // a bare host name
  }
  if (server.isEmpty()) {
    return;
  }
  const std::scoped_lock lock(trust_mutex());
  auto& servers = servers_locked();
  if (servers.contains(server)) {
    return;
  }
  servers.push_back(server);
  store_locked(servers);
}

void set_allowed_network_link_servers(const QStringList& servers) {
  const std::scoped_lock lock(trust_mutex());
  QStringList cleaned;
  for (const auto& entry : servers) {
    const auto server = entry.trimmed().toLower();
    if (!server.isEmpty() && !cleaned.contains(server)) {
      cleaned.push_back(server);
    }
  }
  cached_servers() = cleaned;
  store_locked(cleaned);
}

QString network_link_server_display_name(const QString& server) {
  return QDir::toNativeSeparators(QStringLiteral("//") + server);
}

}  // namespace patchy::ui
