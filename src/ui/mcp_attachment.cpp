#include "ui/mcp_attachment.hpp"
#include "ui/mcp_line_buffer.hpp"
#include "ui/mcp_session.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

namespace patchy::ui {
QString mcp_attachment_endpoint() {
  const auto override_name = qEnvironmentVariable("PATCHY_MCP_ENDPOINT");
  if (!override_name.isEmpty()) { return override_name; }
  auto installation = QDir(QCoreApplication::applicationDirPath()).canonicalPath();
#ifdef Q_OS_WIN
  installation = installation.toLower();
#endif
  const auto identity = (QDir::homePath() + '\n' + installation).toUtf8();
  const auto name = QStringLiteral("PatchyMcp-") + QString::fromLatin1(
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(32));
#ifdef Q_OS_LINUX
  const auto flatpak_id = qEnvironmentVariable("FLATPAK_ID");
  if (!flatpak_id.isEmpty()) {
    // Each Flatpak invocation has private /tmp, but this per-app runtime directory
    // is shared across its sandboxes and restricted to the current user.
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/app/") + flatpak_id + '/' + name;
  }
#endif
  return name;
}

struct McpAttachment::Impl {
  std::atomic<bool> stopping{false};
  std::mutex output_mutex;
  QByteArray output;
  QString error;
  McpSession session;
  std::unique_ptr<QThread> worker;

  Impl(MainWindow& window, const QString& endpoint)
      : session(window, true, [this](const QByteArray& bytes) {
          const std::lock_guard lock(output_mutex);
          output.append(bytes);
        }) {
    std::promise<QString> started;
    auto ready = started.get_future();
    worker.reset(QThread::create([this, endpoint, started = std::move(started)]() mutable {
      // All socket operations, including writes, stay on this thread. It keeps
      // processing cancellation even when the document thread is evaluating JS.
      QLocalServer server;
      server.setSocketOptions(QLocalServer::UserAccessOption);
      if (!server.listen(endpoint)) {
        QLocalSocket probe;
        probe.connectToServer(endpoint);
        if (!probe.waitForConnected(200)) {
          QLocalServer::removeServer(endpoint);  // stale endpoint only
          (void)server.listen(endpoint);
        }
      }
      started.set_value(server.isListening() ? QString() : server.errorString());
      if (!server.isListening()) { return; }
      while (!stopping) {
        if (!server.hasPendingConnections()) { (void)server.waitForNewConnection(25); }
        if (!server.hasPendingConnections()) { continue; }
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        {
          const std::lock_guard lock(output_mutex);
          output.clear();
        }
        session.connect_from_any_thread();
        while (!stopping && !session.ready()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        McpLineBuffer input;
        while (!stopping && socket->state() == QLocalSocket::ConnectedState) {
          // One attached assistant per workspace. Do not queue another client
          // to begin editing automatically when this one leaves.
          (void)server.waitForNewConnection(0);
          while (server.hasPendingConnections()) {
            std::unique_ptr<QLocalSocket> extra(server.nextPendingConnection());
            extra->abort();
          }
          if (!socket->bytesAvailable()) { (void)socket->waitForReadyRead(10); }
          input.append(socket->read(64 * 1024), [this](const QByteArray& line) { session.receive_line(line); });
          QByteArray pending;
          {
            const std::lock_guard lock(output_mutex);
            pending.swap(output);
          }
          if (!pending.isEmpty()) { (void)socket->write(pending); }
          if (socket->bytesToWrite()) { (void)socket->waitForBytesWritten(10); }
        }
        socket->abort();
        session.disconnect_from_any_thread();
        while (!stopping && !session.closed()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
      }
      server.close();
    }));
    worker->start();
    error = ready.get();
  }
  ~Impl() {
    stopping = true;
    worker->wait();
    session.shutdown();
  }
};

McpAttachment::McpAttachment(MainWindow& window, const QString& endpoint)
    : impl_(std::make_unique<Impl>(window, endpoint)) {}
McpAttachment::~McpAttachment() = default;
QString McpAttachment::error() const { return impl_->error; }
}  // namespace patchy::ui
