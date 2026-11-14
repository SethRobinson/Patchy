#include "ui/single_instance.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QDeadlineTimer>
#include <QDir>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QThread>

#include <chrono>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {

namespace {

constexpr int kConnectTimeoutMs = 300;
// QLockFile's own timed wait backs off 100, 200, 400 ms; the lock is held for a few
// milliseconds, so the launches queued behind it ask again much sooner.
constexpr int kLockPollMs = 10;

bool connect_to_instance(QLocalSocket& socket, const QString& server_name) {
  socket.connectToServer(server_name);
  return socket.waitForConnected(kConnectTimeoutMs);
}

// The whole request, then a disconnect: the receiver decodes when the sender has left. A
// connection that sends nothing reads as a bare relaunch there (it raises the window), so
// never connect without sending.
void send_request(QLocalSocket& socket, const QStringList& request) {
  // Grant before sending: once the payload lands the receiver may activate at any moment.
  (void)allow_local_socket_server_to_take_foreground(socket);
  QByteArray payload;
  QDataStream stream(&payload, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_5_15);
  stream << request;
  socket.write(payload);
  socket.flush();
  socket.waitForBytesWritten(1000);
  socket.disconnectFromServer();
  if (socket.state() != QLocalSocket::UnconnectedState) {
    socket.waitForDisconnected(1000);
  }
}

}  // namespace

std::optional<qint64> local_socket_server_process_id(const QLocalSocket& socket) {
#ifdef Q_OS_WIN
  if (socket.state() != QLocalSocket::ConnectedState) {
    return std::nullopt;
  }
  // On Windows the descriptor is the named pipe's HANDLE.
  const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
  ULONG process_id = 0;
  if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE || GetNamedPipeServerProcessId(pipe, &process_id) == 0 ||
      process_id == 0) {
    return std::nullopt;
  }
  return static_cast<qint64>(process_id);
#else
  Q_UNUSED(socket);
  return std::nullopt;
#endif
}

bool allow_local_socket_server_to_take_foreground(const QLocalSocket& socket) {
#ifdef Q_OS_WIN
  const auto process_id = local_socket_server_process_id(socket);
  return process_id.has_value() && AllowSetForegroundWindow(static_cast<DWORD>(*process_id)) != 0;
#else
  Q_UNUSED(socket);
  return false;
#endif
}

QString single_instance_server_name() {
  if (const auto override_name = qEnvironmentVariable("PATCHY_SINGLE_INSTANCE_NAME"); !override_name.isEmpty()) {
    return override_name;
  }
  QString user = qEnvironmentVariable("USERNAME");
  if (user.isEmpty()) {
    user = qEnvironmentVariable("USER");
  }
  const auto name = QStringLiteral("Patchy-SingleInstance-") + user;
#ifdef Q_OS_LINUX
  // Inside Flatpak, QLocalServer's default socket location is the per-sandbox /tmp, so a
  // second `flatpak run` would never find the first instance's socket. $XDG_RUNTIME_DIR/
  // app/<app-id>/ is shared across sandboxes of the same app id; use an absolute socket
  // path there (QLocalServer treats a path-shaped name as the literal socket path).
  if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
    const auto runtime_dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime_dir.isEmpty()) {
      return runtime_dir + QStringLiteral("/app/") + qEnvironmentVariable("FLATPAK_ID") +
             QStringLiteral("/") + name;
    }
  }
#endif
  return name;
}

QString single_instance_lock_path(const QString& server_name) {
#ifndef Q_OS_WIN
  // A socket path (the Flatpak one): the lock must sit where every sandbox of the app sees it.
  if (QDir::isAbsolutePath(server_name)) {
    return server_name + QStringLiteral(".lock");
  }
#endif
  // A pipe or socket name. QLocalServer puts a named socket in the temp folder too.
  QString file_name = server_name;
  for (auto& character : file_name) {
    if (!character.isLetterOrNumber() && character != QLatin1Char('.') && character != QLatin1Char('-') &&
        character != QLatin1Char('_')) {
      character = QLatin1Char('_');
    }
  }
  return QDir(QDir::tempPath()).filePath(file_name + QStringLiteral(".lock"));
}

bool forward_to_single_instance(const QString& server_name, const QStringList& request) {
  QLocalSocket socket;
  if (!connect_to_instance(socket, server_name)) {
    return false;
  }
  send_request(socket, request);
  return true;
}

SingleInstanceServer::SingleInstanceServer(QObject* parent) : QObject(parent) {}

SingleInstanceServer::~SingleInstanceServer() {
  close();
}

bool SingleInstanceServer::listen(const QString& server_name) {
  if (thread_ == nullptr) {
    thread_ = std::make_unique<QThread>();
    thread_->setObjectName(QStringLiteral("patchy-single-instance"));
    server_ = new QLocalServer;  // no parent: it moves to the thread, which deletes it
    server_->moveToThread(thread_.get());
    QObject::connect(thread_.get(), &QThread::finished, server_, &QObject::deleteLater);
    QObject::connect(server_, &QLocalServer::newConnection, server_,
                     [this, server = server_] { accept_connections(server); });
    thread_->start();
  }
  bool listening = false;
  QMetaObject::invokeMethod(
      server_, [server = server_, &server_name, &listening] { listening = server->listen(server_name); },
      Qt::BlockingQueuedConnection);
  listening_ = listening;
  return listening;
}

void SingleInstanceServer::close() {
  if (thread_ == nullptr) {
    return;
  }
  // The server is deleted on its thread as the thread finishes; on Unix that removes the socket.
  thread_->quit();
  thread_->wait();
  thread_.reset();
  server_ = nullptr;
  listening_ = false;
}

std::vector<QStringList> SingleInstanceServer::take_requests() {
  return std::exchange(pending_, {});
}

// Runs on the listener thread.
void SingleInstanceServer::accept_connections(QLocalServer* server) {
  while (QLocalSocket* client = server->nextPendingConnection()) {
    // Accumulate until the sender disconnects, then decode the whole payload in one shot so a
    // chunked write can't be parsed half-read.
    auto buffer = std::make_shared<QByteArray>();
    QObject::connect(client, &QLocalSocket::readyRead, client,
                     [client, buffer] { buffer->append(client->readAll()); });
    QObject::connect(client, &QLocalSocket::disconnected, client, [this, client, buffer] {
      buffer->append(client->readAll());
      QStringList request;
      QDataStream stream(buffer.get(), QIODevice::ReadOnly);
      stream.setVersion(QDataStream::Qt_5_15);
      stream >> request;
      // To the GUI thread. close() joins this thread before the object goes, so `this` is alive.
      QMetaObject::invokeMethod(
          this,
          [this, request = std::move(request)]() mutable {
            pending_.push_back(std::move(request));
            emit request_ready();
          },
          Qt::QueuedConnection);
      client->deleteLater();
    });
  }
}

SingleInstanceRole claim_single_instance(SingleInstanceServer& server, const QString& server_name,
                                         const QStringList& request, const SingleInstanceClaimOptions& options) {
  QLockFile lock(single_instance_lock_path(server_name));
  // A live owner keeps the file open (Windows) or flock()ed (Unix), so an old lock is only
  // ever removed from a process that is gone. The age rule covers a pid that cannot be judged:
  // a Flatpak sandbox has its own pid namespace, where a dead owner's pid can be a live one.
  lock.setStaleLockTime(options.stale_lock_ms);
  const QDeadlineTimer deadline{std::chrono::milliseconds(options.lock_wait_ms)};
  bool locked = lock.tryLock(0);
  while (!locked && lock.error() == QLockFile::LockFailedError && !deadline.hasExpired()) {
    QThread::msleep(kLockPollMs);
    locked = lock.tryLock(0);
  }
  // Not locked: the file cannot be made here, or its holder is stuck. Go on as Patchy did
  // before the lock existed rather than not start.

  // Whoever held the lock before may have become the instance meanwhile.
  {
    QLocalSocket socket;
    if (connect_to_instance(socket, server_name)) {
      if (locked) {
        lock.unlock();  // the next launch in line need not wait for this send
      }
      send_request(socket, request);
      return SingleInstanceRole::Forwarded;
    }
  }
  if (server.listen(server_name)) {
    return SingleInstanceRole::Primary;
  }
  // Nothing answers, yet the name is taken: a socket file a crashed instance left (Unix).
  QLocalServer::removeServer(server_name);
  return server.listen(server_name) ? SingleInstanceRole::Primary : SingleInstanceRole::Unavailable;
}

}  // namespace patchy::ui
