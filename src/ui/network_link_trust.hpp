#pragma once

#include <QString>
#include <QStringList>

// Which network servers a document's links may reach (docs/smart-object-editing.md, "Network
// links"). A linked smart object or Krita file layer whose path names \\host\share is followed
// only when the host is allowed: it is on the allowed list (key `imports/allowedLinkServers`, a
// compatibility contract), or the document itself was opened from that host. Local paths and
// mapped drives are always followed. Thread-safe: open-time link checks run on the load worker.
namespace patchy::ui {

// The host a path names, ASCII lower case, or empty for a local path. `device` reports
// \\.\ and \\?\ device paths, which a document's link never reaches.
[[nodiscard]] QString network_link_server(const QString& path, bool* device = nullptr);

// Whether a link that resolves to `path` may be touched for a document in `document_dir`
// (empty for an unsaved document). When it may not, `blocked_server` (if given) gets the host;
// it stays empty for a device path.
[[nodiscard]] bool network_link_path_allowed(const QString& path, const QString& document_dir,
                                             QString* blocked_server = nullptr);

// The allowed servers, lower case, in the order they were added.
[[nodiscard]] QStringList allowed_network_link_servers();
// Adds `server` (a host or any path on it) to the allowed list and stores it. No-op for a
// local path or a server already allowed.
void allow_network_link_server(const QString& server_or_path);
// Replaces the list (tests restore it with this).
void set_allowed_network_link_servers(const QStringList& servers);

// \\host on Windows, //host elsewhere: how dialogs and notices name a server.
[[nodiscard]] QString network_link_server_display_name(const QString& server);

}  // namespace patchy::ui
