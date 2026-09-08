#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>

namespace patchy::ui {
class MainWindow;

// Discovery stays available in an attached proxy while its workspace is closed.
// Both transports use the same protocol metadata and installed help resources.
QJsonArray mcp_tool_catalog();
QJsonObject mcp_initialize_result(const QJsonObject& params);
QJsonObject mcp_help_result(const QJsonObject& args);

// Startup policy for a connector-owned window. Visible workspaces permit
// normal user dialogs between requests and exit when their window is closed.
// MCP script runs still suppress prompts through RunOptions::unattended.
void configure_owned_mcp_workspace(MainWindow& window, bool visible);

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
