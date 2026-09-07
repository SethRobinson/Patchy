#pragma once
#include <QByteArray>
#include <atomic>
#include <functional>
#include <thread>

namespace patchy {
// Interruptible input: closing an attached Patchy window must let the proxy
// exit even when the MCP client still has its stdin pipe open.
class McpStdioReader {
 public:
  McpStdioReader(std::function<void(const QByteArray&)> receive, std::function<void()> eof);
  ~McpStdioReader();
 private:
  std::atomic<bool> stopping_{false};
  std::thread worker_;
};
void configure_mcp_stdio();
void write_mcp_stdout(const QByteArray& bytes);
}  // namespace patchy
