#include "ui/single_instance.hpp"

#include <QLocalSocket>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {

std::optional<qint64> local_socket_server_process_id(const QLocalSocket& socket) {
#ifdef Q_OS_WIN
  if (socket.state() != QLocalSocket::ConnectedState) {
    return std::nullopt;
  }
  // On Windows the descriptor is the named pipe's HANDLE.
  const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
  ULONG process_id = 0;
  if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE || GetNamedPipeServerProcessId(pipe, &process_id) == 0 ||
      process_id == 0) {
    return std::nullopt;
  }
  return static_cast<qint64>(process_id);
#else
  Q_UNUSED(socket);
  return std::nullopt;
#endif
}

bool allow_local_socket_server_to_take_foreground(const QLocalSocket& socket) {
#ifdef Q_OS_WIN
  const auto process_id = local_socket_server_process_id(socket);
  return process_id.has_value() && AllowSetForegroundWindow(static_cast<DWORD>(*process_id)) != 0;
#else
  Q_UNUSED(socket);
  return false;
#endif
}

}  // namespace patchy::ui
