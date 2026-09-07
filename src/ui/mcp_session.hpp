#pragma once

#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

namespace patchy::ui {
class MainWindow;

// Transport-independent MCP connection. Document operations run on the UI
// thread; the transport calls receive_line/disconnect on its input thread so
// cancellation still interrupts a synchronous JavaScript loop.
class McpSession {
 public:
  using Output = std::function<void(const QByteArray&)>;
  McpSession(MainWindow& window, bool attached, Output output);
  ~McpSession();
  McpSession(const McpSession&) = delete;
  McpSession& operator=(const McpSession&) = delete;
  void connect_from_any_thread();
  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool closed() const;
  void receive_line(const QByteArray& line);
  void disconnect_from_any_thread();
  // Call on UI thread, after joining the input thread, before MainWindow dies.
  void shutdown();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace patchy::ui
