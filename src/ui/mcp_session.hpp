#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>

namespace patchy::ui {
class MainWindow;

// The companion protocol (docs/companions.md#protocol): the events, manifest keys, Companions
// menu commands and pixel-exchange script calls companion apps rely on. An addition gets a
// capability token; only a breaking change raises the maximum, and the minimum rises only
// when Patchy stops serving an old version. Reported by get_info and the initialize reply.
inline constexpr int kCompanionProtocolMin = 1;
inline constexpr int kCompanionProtocolMax = 1;

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
  // Companion events (docs/companions.md), UI thread only. A client opts in at initialize
  // with capabilities.experimental["patchy/events"]; post_event is a no-op otherwise.
  void post_event(const QJsonObject& event);
  // capabilities.experimental["patchy/companion"] from initialize, empty when absent.
  [[nodiscard]] QString companion_id() const;
  // Call on UI thread, after joining the input thread, before MainWindow dies.
  void shutdown();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace patchy::ui
