#pragma once
#include <QByteArray>
#include <atomic>
#include <functional>
#include <thread>

namespace patchy {
// Interruptible input: the reader can stop without waiting for the MCP client
// to close stdin. Client EOF disconnects the workspace and exits the connector.
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
