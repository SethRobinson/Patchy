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
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace patchy::ui
