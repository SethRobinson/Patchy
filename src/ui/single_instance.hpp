#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QLocalServer;
class QLocalSocket;
class QThread;

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

// Per-user name of the single-instance local socket. Scoping it to the user keeps separate
// login sessions from fighting over one pipe on a shared machine. PATCHY_SINGLE_INSTANCE_NAME
// replaces it (tests and automation that must never reach the user's own Patchy).
[[nodiscard]] QString single_instance_server_name();

// The startup lock's file for a server name: beside the socket where the name is a socket
// path, in the per-user temp folder otherwise.
[[nodiscard]] QString single_instance_lock_path(const QString& server_name);

// Hands `request` (files and reserved command entries) to the instance listening on
// `server_name`. False when nothing answers there; the caller then starts its own instance.
bool forward_to_single_instance(const QString& server_name, const QStringList& request);

// The primary instance's end. It listens on its own thread, so a launch that forwards to it is
// answered at once, also while the GUI thread builds the main window or decodes a file, and the
// forwarding process can exit. Requests wait here in arrival order until the GUI thread takes
// them.
class SingleInstanceServer : public QObject {
  Q_OBJECT

 public:
  explicit SingleInstanceServer(QObject* parent = nullptr);
  ~SingleInstanceServer() override;

  // False when the name cannot be listened on (on Unix: a socket file is already there).
  bool listen(const QString& server_name);
  // Stops listening and removes the socket. Requests already received stay takeable.
  void close();
  [[nodiscard]] bool is_listening() const { return listening_; }

  // Every request received since the last call, oldest first. GUI thread only.
  [[nodiscard]] std::vector<QStringList> take_requests();

 signals:
  // On the GUI thread, once per request, after it joined the take_requests() queue.
  void request_ready();

 private:
  void accept_connections(QLocalServer* server);

  std::unique_ptr<QThread> thread_;
  QLocalServer* server_ = nullptr;  // lives on thread_, deleted when it finishes
  bool listening_ = false;
  std::vector<QStringList> pending_;
};

enum class SingleInstanceRole {
  Forwarded,    // another instance took the request: exit
  Primary,      // this process listens now
  Unavailable,  // nothing answers and listening failed: run without the single-instance socket
};

struct SingleInstanceClaimOptions {
  // How long to wait for another starting Patchy to finish its own claim.
  int lock_wait_ms = 6000;
  // A lock file older than this whose owner cannot be shown alive is a crashed launch's.
  int stale_lock_ms = 3000;
};

// Decides which of several Patchy processes started together becomes the one instance
// (GitHub issue 722). Under a per-user lock file it asks once more whether an instance answers,
// forwards `request` when one does, and otherwise makes `server` listen, so the others, which
// wait on the lock, find it. A socket nothing answers on is removed only here, with the lock
// held: without the lock a starting process could delete the socket another had just made.
// When the lock cannot be taken in time the claim goes on without it.
SingleInstanceRole claim_single_instance(SingleInstanceServer& server, const QString& server_name,
                                         const QStringList& request, const SingleInstanceClaimOptions& options = {});

}  // namespace patchy::ui
