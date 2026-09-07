#include "app/mcp_stdio.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <mutex>
#ifdef Q_OS_WIN
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace patchy {
void configure_mcp_stdio() {
#ifdef Q_OS_WIN
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
#else
  std::signal(SIGPIPE, SIG_IGN);
#endif
}
void write_mcp_stdout(const QByteArray& bytes) {
  static std::mutex mutex;
  const std::lock_guard lock(mutex);
  (void)std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
  (void)std::fflush(stdout);
}
McpStdioReader::McpStdioReader(std::function<void(const QByteArray&)> receive, std::function<void()> eof)
    : worker_([this, receive = std::move(receive), eof = std::move(eof)] {
      std::array<char, 65536> buffer{};
      while (!stopping_) {
#ifdef Q_OS_WIN
        const int fd = _fileno(stdin);
        const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        DWORD available = 0;
        const auto type = GetFileType(handle);
        if (type == FILE_TYPE_PIPE) {
          if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) { break; }
          if (!available) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        } else if (type == FILE_TYPE_DISK) {
          available = static_cast<DWORD>(buffer.size());
        } else { break; }  // MCP uses redirected pipes, not an interactive console.
        const auto count = _read(fd, buffer.data(), std::min<unsigned>(available, static_cast<unsigned>(buffer.size())));
#else
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const int result = poll(&descriptor, 1, 25);
        if (result < 0 && errno == EINTR) { continue; }
        if (result < 0) { break; }
        if (!result) { continue; }
        const auto count = read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) { continue; }
#endif
        if (count <= 0) { break; }
        receive(QByteArray(buffer.data(), static_cast<qsizetype>(count)));
      }
      if (!stopping_) { eof(); }
    }) {}
McpStdioReader::~McpStdioReader() {
  stopping_ = true;
  if (worker_.joinable()) { worker_.join(); }
}
}  // namespace patchy
