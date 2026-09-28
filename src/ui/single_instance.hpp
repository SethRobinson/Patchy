#pragma once

#include <QtGlobal>

#include <optional>

class QLocalSocket;

namespace patchy::ui {

// The process id at the server end of a connected single-instance socket, or nullopt where the
// platform cannot tell (only Windows named pipes are asked).
[[nodiscard]] std::optional<qint64> local_socket_server_process_id(const QLocalSocket& socket);

// A relaunch forwards its request to the running Patchy and exits. On Windows only the process
// the user just started owns the foreground, and the foreground lock refuses the running
// instance's own SetForegroundWindow, so its window merely flashes on the taskbar. The
// forwarding process hands its foreground right to the server end before it exits; returns
// whether the grant was made. A no-op elsewhere.
bool allow_local_socket_server_to_take_foreground(const QLocalSocket& socket);

}  // namespace patchy::ui
