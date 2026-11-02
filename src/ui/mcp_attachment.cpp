#include "ui/mcp_attachment.hpp"
#include "ui/appimage_environment.hpp"
#include "ui/mcp_line_buffer.hpp"
#include "ui/mcp_session.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace patchy::ui {
QString mcp_attachment_endpoint() {
  const auto override_name = qEnvironmentVariable("PATCHY_MCP_ENDPOINT");
  if (!override_name.isEmpty()) { return override_name; }
  auto installation = QDir(QCoreApplication::applicationDirPath()).canonicalPath();
  if (!appimage_file_path().isEmpty()) {
    // Every AppImage launch runs from a new /tmp/.mount_* directory, so the user's
    // Patchy and an assistant's `<file> patchy-mcp --attach` never share one. Any
    // Patchy AppImage of this user attaches to any other, which also keeps working
    // after the user replaces the file with a newer release.
    installation = QStringLiteral("appimage");
  }
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

// A few clients at once: an AI assistant and companion apps (docs/companions.md). Each
// connection gets one of a fixed pool of sessions, made on the UI thread at startup and
// reused after its previous client has closed. A connection beyond the pool is closed,
// never queued to start editing when another client leaves.
constexpr std::size_t kMaxAttachedClients = 4;

struct McpAttachment::Impl {
  struct Slot {
    std::mutex output_mutex;
    QByteArray output;
    std::unique_ptr<McpSession> session;
  };
  struct Client {
    std::unique_ptr<QLocalSocket> socket;
    McpLineBuffer input;
    Slot* slot{nullptr};
  };
  std::atomic<bool> stopping{false};
  QString error;
  std::array<Slot, kMaxAttachedClients> pool;
  std::unique_ptr<QThread> worker;

  Impl(MainWindow& window, const QString& endpoint) {
    for (auto& slot : pool) {
      slot.session = std::make_unique<McpSession>(window, true, [&slot](const QByteArray& bytes) {
        const std::lock_guard lock(slot.output_mutex);
        slot.output.append(bytes);
      });
    }
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
      std::vector<std::unique_ptr<Client>> clients;
      while (!stopping) {
        if (clients.empty() && !server.hasPendingConnections()) { (void)server.waitForNewConnection(25); }
        else { (void)server.waitForNewConnection(0); }
        while (server.hasPendingConnections()) {
          std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
          Slot* free = nullptr;
          for (auto& slot : pool) {
            const bool taken = std::any_of(clients.begin(), clients.end(),
                                           [&slot](const auto& client) { return client->slot == &slot; });
            if (!taken && slot.session->closed()) { free = &slot; break; }
          }
          if (free == nullptr) { socket->abort(); continue; }
          {
            const std::lock_guard lock(free->output_mutex);
            free->output.clear();
          }
          free->session->connect_from_any_thread();
          while (!stopping && !free->session->ready()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
          auto client = std::make_unique<Client>();
          client->socket = std::move(socket);
          client->slot = free;
          clients.push_back(std::move(client));
        }
        // One client waits as before; several share the same short poll.
        const int wait = clients.empty() ? 0 : std::max(1, 10 / static_cast<int>(clients.size()));
        for (auto it = clients.begin(); it != clients.end();) {
          auto& client = **it;
          if (client.socket->state() != QLocalSocket::ConnectedState) {
            client.socket->abort();
            client.slot->session->disconnect_from_any_thread();
            it = clients.erase(it);
            continue;
          }
          if (!client.socket->bytesAvailable()) { (void)client.socket->waitForReadyRead(wait); }
          client.input.append(client.socket->read(64 * 1024),
                              [&client](const QByteArray& line) { client.slot->session->receive_line(line); });
          QByteArray pending;
          {
            const std::lock_guard lock(client.slot->output_mutex);
            pending.swap(client.slot->output);
          }
          if (!pending.isEmpty()) { (void)client.socket->write(pending); }
          if (client.socket->bytesToWrite()) { (void)client.socket->waitForBytesWritten(10); }
          ++it;
        }
      }
      for (auto& client : clients) {
        client->socket->abort();
        client->slot->session->disconnect_from_any_thread();
      }
      server.close();
    }));
    worker->start();
    error = ready.get();
  }
  ~Impl() {
    stopping = true;
    worker->wait();
    for (auto& slot : pool) { slot.session->shutdown(); }
  }
};

McpAttachment::Impl* McpAttachment::active_ = nullptr;

bool McpAttachment::send_companion_command(const QString& companion_id, const QString& action) {
  if (active_ == nullptr || companion_id.isEmpty()) { return false; }
  bool sent = false;
  for (auto& slot : active_->pool) {
    if (slot.session->ready() && slot.session->companion_id() == companion_id) {
      slot.session->post_event({{"type", "companionCommand"}, {"companion", companion_id}, {"action", action}});
      sent = true;
    }
  }
  return sent;
}

bool McpAttachment::companion_attached(const QString& companion_id) {
  if (active_ == nullptr || companion_id.isEmpty()) { return false; }
  return std::any_of(active_->pool.begin(), active_->pool.end(), [&companion_id](const auto& slot) {
    return slot.session->ready() && slot.session->companion_id() == companion_id;
  });
}

McpAttachment::McpAttachment(MainWindow& window, const QString& endpoint)
    : impl_(std::make_unique<Impl>(window, endpoint)) {
  active_ = impl_.get();
}
McpAttachment::~McpAttachment() {
  if (active_ == impl_.get()) { active_ = nullptr; }
}
QString McpAttachment::error() const { return impl_->error; }
}  // namespace patchy::ui
