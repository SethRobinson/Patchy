#pragma once
#include <QString>
#include <memory>

namespace patchy::ui {
class MainWindow;
// Per user and installation. PATCHY_MCP_ENDPOINT selects an explicit local
// endpoint for multiple instances and isolated automation. No network listener.
[[nodiscard]] QString mcp_attachment_endpoint();

class McpAttachment {
 public:
  explicit McpAttachment(MainWindow& window, const QString& endpoint = mcp_attachment_endpoint());
  ~McpAttachment();
  [[nodiscard]] QString error() const;
  // Sends companionCommand {companion, action} to every attached client that declared
  // `companion_id` at initialize (docs/companions.md). UI thread; false when none is attached.
  static bool send_companion_command(const QString& companion_id, const QString& action);
  // True while a client that declared `companion_id` is attached. UI thread.
  [[nodiscard]] static bool companion_attached(const QString& companion_id);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  static Impl* active_;
};
}  // namespace patchy::ui
