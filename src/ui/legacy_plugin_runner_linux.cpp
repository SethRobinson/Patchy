#include "ui/legacy_plugin_runner.hpp"

#include "plugins/host/host_protocol.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

// Linux runs 64-bit .8bf plug-ins through the Win32 helper (patchy-8bf-host64.exe,
// cross-built with MinGW) inside Raven (https://useraven.org), which runs Windows
// code natively and answers its system calls itself: no Wine. Raven is a separate
// GPL-3.0 program Patchy only starts; it is never linked or bundled. The helper
// cannot reach a named pipe or a named mapping from inside Raven, so it connects to
// a loopback TCP port, proves itself with a per-run token, and the pixels travel in
// the stream (host_protocol.hpp, kMessagePixels). See docs/plugins.md, Linux.

namespace patchy::ui {

namespace {

using namespace patchy::legacy_host;

// Frames arriving on the socket, split from the byte stream.
struct FrameReader {
  QByteArray buffer;
  bool next(std::uint32_t& type, std::vector<std::uint8_t>& payload) {
    if (buffer.size() < 8) {
      return false;
    }
    Reader header(reinterpret_cast<const std::uint8_t*>(buffer.constData()), 8);
    const auto frame_type = header.u32();
    const auto length = header.u32();
    if (static_cast<std::uint64_t>(buffer.size()) < 8U + static_cast<std::uint64_t>(length)) {
      return false;
    }
    type = frame_type;
    payload.assign(buffer.constData() + 8, buffer.constData() + 8 + length);
    buffer.remove(0, static_cast<qsizetype>(8U + static_cast<std::uint64_t>(length)));
    return true;
  }
};

void send_frame(QTcpSocket& socket, std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  const auto bytes = frame_message(type, payload);
  socket.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
}

QString random_token() {
  std::array<quint32, 4> words{};
  QRandomGenerator::system()->fillRange(words.data(), words.size());
  QString token;
  for (const auto word : words) {
    token += QStringLiteral("%1").arg(word, 8, 16, QLatin1Char('0'));
  }
  return token;
}

QString executable_if_runnable(const QString& path) {
  const QFileInfo info(path);
  return !path.isEmpty() && info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
}

// The Windows installation Raven shows the helper: Patchy's own prefix, so the
// plug-ins never share a registry or C: drive with other Raven programs.
QString raven_prefix_path() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("raven-prefix"));
}

// Replaces a symlink (or creates it), never anything else at that path.
bool point_link(const QString& link, const QString& target) {
  const QFileInfo info(link);
  if (info.isSymLink()) {
    if (info.symLinkTarget() == QFileInfo(target).absoluteFilePath()) {
      return true;
    }
    QFile::remove(link);
  } else if (info.exists()) {
    return false;
  }
  return QFile::link(target, link);
}

}  // namespace

QString legacy_plugin_raven_executable() {
  const auto environment = QProcessEnvironment::systemEnvironment();
  if (const auto from_env = executable_if_runnable(environment.value(QStringLiteral("PATCHY_RAVEN")));
      !from_env.isEmpty()) {
    return from_env;
  }
  // The Raven Patchy bundles (cmake/patchy_raven.cmake): raven/ beside the binary in the
  // build tree, lib/patchy/raven/ in an install or the AppImage.
  const QDir app_dir(QCoreApplication::applicationDirPath());
  for (const auto& bundled : {app_dir.filePath(QStringLiteral("raven/raven")),
                              app_dir.filePath(QStringLiteral("../lib/patchy/raven/raven"))}) {
    if (const auto found = executable_if_runnable(bundled); !found.isEmpty()) {
      return found;
    }
  }
  if (const auto on_path = QStandardPaths::findExecutable(QStringLiteral("raven")); !on_path.isEmpty()) {
    return on_path;
  }
  for (const auto& candidate : {QDir::home().filePath(QStringLiteral(".local/bin/raven")),
                                QDir::home().filePath(QStringLiteral(".local/share/raven/bin/raven")),
                                QStringLiteral("/usr/local/bin/raven"), QStringLiteral("/opt/raven/bin/raven")}) {
    if (const auto found = executable_if_runnable(candidate); !found.isEmpty()) {
      return found;
    }
  }
  return {};
}

QString legacy_plugin_helper_path(const std::string& architecture) {
  // Only the 64-bit helper is built for Linux (32-bit plug-ins are not supported here yet).
  const auto name = architecture == "x86" ? QStringLiteral("patchy-8bf-host32.exe") : QStringLiteral("patchy-8bf-host64.exe");
  return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

LegacyPluginRunResult run_legacy_plugin_out_of_process(const LegacyPluginRunInput& input,
                                                       const LegacyPluginRunCallbacks& callbacks) {
  LegacyPluginRunResult result;
  if (input.input == nullptr || input.output == nullptr || input.width <= 0 || input.height <= 0 ||
      (input.planes != 3 && input.planes != 4)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host was given an invalid image.");
    return result;
  }
  const std::size_t pixel_bytes = static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height) *
                                  static_cast<std::size_t>(input.planes);
  const std::size_t mask_bytes =
      input.mask != nullptr ? static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height) : 0U;
  if (input.input->size() != pixel_bytes || input.output->size() != pixel_bytes ||
      (input.mask != nullptr && input.mask->size() != mask_bytes)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host was given an invalid image.");
    return result;
  }
  if (!QFileInfo::exists(input.helper_executable)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program is missing: %1")
                         .arg(QDir::toNativeSeparators(input.helper_executable));
    return result;
  }
  const auto raven = legacy_plugin_raven_executable();
  if (raven.isEmpty()) {
    result.message = QCoreApplication::translate(
        "LegacyPluginRunner",
        "Photoshop plug-ins on Linux run inside Raven, which is not installed. Install Raven from useraven.org, or set "
        "PATCHY_RAVEN to its raven program, then try again.");
    return result;
  }
  const std::uint64_t exchange_size = static_cast<std::uint64_t>(pixel_bytes) * 2U + mask_bytes;
  if (exchange_size > kMaxStreamPixelBytes) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "Not enough memory to hand the layer to the plug-in.");
    return result;
  }

  // The prefix: the helper sits on C:, the plug-in's folder is drive P: (a symlink,
  // so plug-ins that open files beside themselves find them).
  const QString prefix = raven_prefix_path();
  const QString helper_dir = QDir(prefix).filePath(QStringLiteral("drive_c/Patchy"));
  const QString helper_in_prefix = QDir(helper_dir).filePath(QFileInfo(input.helper_executable).fileName());
  const QFileInfo plugin_info(input.plugin_path);
  if (!QDir().mkpath(helper_dir) || !point_link(helper_in_prefix, input.helper_executable) ||
      !point_link(QDir(prefix).filePath(QStringLiteral("drive_p")), plugin_info.absolutePath())) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host could not be contacted: %1")
                         .arg(QDir::toNativeSeparators(prefix));
    return result;
  }
  const QString guest_plugin_path = QStringLiteral("P:\\") + plugin_info.fileName();

  QTcpServer server;
  if (!server.listen(QHostAddress::LocalHost, 0)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host could not be contacted: %1")
                         .arg(server.errorString());
    return result;
  }
  const auto token = random_token();

  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("RAVEN_PREFIX"), prefix);
  process.setProcessEnvironment(environment);
  process.setProgram(raven);
  const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");
  process.setArguments({QStringLiteral("--run-process"), QStringLiteral("--execution=native"),
                        QStringLiteral("--timeout-ms=86400000"),
                        x11 ? QStringLiteral("--desktop=x11") : QStringLiteral("--desktop=wayland"), helper_in_prefix,
                        QStringLiteral("--"), QStringLiteral("--tcp"), QString::number(server.serverPort()), token});
  process.setProcessChannelMode(QProcess::ForwardedChannels);
  process.start();
  if (!process.waitForStarted(5000)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program could not be started: %1")
                         .arg(process.errorString());
    return result;
  }
  // Every return below ends Raven (and with it the helper and the plug-in) unless the
  // run finished and Raven already exited.
  struct ProcessGuard {
    QProcess& process;
    ~ProcessGuard() {
      if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
      }
    }
  } guard{process};

  const auto pump = [&] {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (callbacks.tick) {
      callbacks.tick();
    }
  };

  // Raven prepares a new prefix on first use, which takes longer than a start.
  QElapsedTimer timer;
  timer.start();
  while (!server.hasPendingConnections() && process.state() == QProcess::Running && timer.elapsed() < 120000) {
    server.waitForNewConnection(20);
    pump();
    if (callbacks.cancelled && callbacks.cancelled()) {
      result.status = LegacyPluginRunStatus::Cancelled;
      return result;
    }
  }
  if (!server.hasPendingConnections()) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host did not respond.");
    return result;
  }
  std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
  server.close();

  Writer hello;
  hello.u32(kProtocolVersion);
  hello.u32(static_cast<std::uint32_t>(sizeof(void*)));
  send_frame(*socket, kMessageHello, hello.data());

  RunRequest request;
  request.flags = (input.show_dialog ? kRunShowDialog : 0U) | (input.protect_alpha ? kRunProtectAlpha : 0U);
  request.parent_window = 0;  // Patchy's window is not a Win32 window inside Raven
  request.plugin_path = guest_plugin_path.toStdU16String();
  request.entry_point = input.entry_point;
  request.mapping_name.clear();  // stream transport
  request.mapping_size = exchange_size;
  request.input_offset = 0;
  request.output_offset = pixel_bytes;
  request.mask_offset = pixel_bytes * 2U;
  request.width = input.width;
  request.height = input.height;
  request.planes = input.planes;
  request.filter_case = input.filter_case;
  request.filter_top = input.filter_rect.top();
  request.filter_left = input.filter_rect.left();
  request.filter_bottom = input.filter_rect.top() + input.filter_rect.height();
  request.filter_right = input.filter_rect.left() + input.filter_rect.width();
  request.has_mask = input.mask != nullptr ? 1 : 0;
  request.case_input_handling = input.case_info.input_handling;
  request.case_output_handling = input.case_info.output_handling;
  request.case_flags1 = input.case_info.flags1;
  std::copy(input.foreground.begin(), input.foreground.end(), request.foreground);
  std::copy(input.background.begin(), input.background.end(), request.background);
  request.resolution_fixed = input.resolution_fixed;
  request.document_title = input.document_title.toStdString();
  request.parameters.assign(input.parameters.begin(), input.parameters.end());
  request.screen_max_width = input.screen_max_width;
  request.screen_max_height = input.screen_max_height;
  request.window_title = input.window_title.toStdU16String();

  const auto send_pixels = [&] {
    Writer header;
    header.u32(kMessagePixels);
    header.u32(static_cast<std::uint32_t>(exchange_size));
    socket->write(reinterpret_cast<const char*>(header.data().data()), static_cast<qint64>(header.data().size()));
    // Input, output pre-filled with the input, then the mask.
    socket->write(reinterpret_cast<const char*>(input.input->data()), static_cast<qint64>(pixel_bytes));
    socket->write(reinterpret_cast<const char*>(input.input->data()), static_cast<qint64>(pixel_bytes));
    if (input.mask != nullptr) {
      socket->write(reinterpret_cast<const char*>(input.mask->data()), static_cast<qint64>(mask_bytes));
    }
  };

  FrameReader frames;
  bool hello_seen = false;
  bool run_sent = false;
  bool cancel_sent = false;
  bool got_result = false;
  bool got_pixels = false;
  RunResult run_result;
  QElapsedTimer cancel_timer;
  timer.restart();
  const auto finished = [&] { return got_result && (run_result.status != kRunOk || got_pixels); };
  const auto read_frames = [&]() -> bool {
    frames.buffer.append(socket->readAll());
    std::uint32_t type = 0;
    std::vector<std::uint8_t> payload;
    while (frames.next(type, payload)) {
      if (type == kMessageHello) {
        Reader reader(payload.data(), payload.size());
        const auto version = reader.u32();
        (void)reader.u32();
        const auto their_token = reader.utf8();
        if (version != kProtocolVersion || !reader.ok() || QString::fromStdString(their_token) != token) {
          result.message =
              QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program does not match this Patchy build.");
          return false;
        }
        hello_seen = true;
      } else if (!hello_seen) {
        result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host did not respond.");
        return false;
      } else if (type == kMessageProgress) {
        Reader reader(payload.data(), payload.size());
        const auto done = reader.i32();
        const auto total = reader.i32();
        if (callbacks.progress) {
          callbacks.progress(done, total);
        }
      } else if (type == kMessagePhase) {
        Reader reader(payload.data(), payload.size());
        const auto selector = reader.i32();
        if (callbacks.phase) {
          auto phase = LegacyPluginPhase::Unknown;
          switch (selector) {
            case 1: phase = LegacyPluginPhase::Parameters; break;
            case 2: phase = LegacyPluginPhase::Prepare; break;
            case 3: phase = LegacyPluginPhase::Start; break;
            case 4: phase = LegacyPluginPhase::Continue; break;
            case 5: phase = LegacyPluginPhase::Finish; break;
            default: break;
          }
          callbacks.phase(phase);
        }
      } else if (type == kMessageResult) {
        if (!decode_run_result(payload.data(), payload.size(), run_result)) {
          run_result.status = kRunError;
          run_result.message.clear();
        }
        got_result = true;
      } else if (type == kMessagePixels && got_result) {
        if (payload.size() != pixel_bytes) {
          run_result.status = kRunError;
          run_result.message.clear();
        } else {
          std::memcpy(input.output->data(), payload.data(), pixel_bytes);
          got_pixels = true;
        }
      }
    }
    return true;
  };

  for (;;) {
    pump();
    socket->waitForReadyRead(10);
    if (!read_frames()) {
      return result;
    }
    if (finished()) {
      break;
    }
    if (hello_seen && !run_sent) {
      send_frame(*socket, kMessageRun, encode_run_request(request));
      send_pixels();
      run_sent = true;
    }
    if (!hello_seen && timer.elapsed() > 15000) {
      result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host did not respond.");
      return result;
    }
    if (!cancel_sent && callbacks.cancelled && callbacks.cancelled()) {
      send_frame(*socket, kMessageCancel, {});
      cancel_sent = true;
      cancel_timer.start();
    }
    if (cancel_sent && cancel_timer.elapsed() > 3000) {
      // The plug-in ignored the request: end it. The layer is untouched because
      // nothing was written back yet.
      result.status = LegacyPluginRunStatus::Cancelled;
      return result;
    }
    if (process.state() != QProcess::Running || socket->state() != QAbstractSocket::ConnectedState) {
      socket->waitForReadyRead(200);
      if (!read_frames()) {
        return result;
      }
      if (finished()) {
        break;
      }
      if (cancel_sent) {
        result.status = LegacyPluginRunStatus::Cancelled;
        return result;
      }
      process.waitForFinished(2000);
      const auto exit_code = process.exitStatus() == QProcess::CrashExit ? -1 : process.exitCode();
      result.message =
          QCoreApplication::translate("LegacyPluginRunner", "The plug-in crashed (host exit code %1).").arg(exit_code);
      return result;
    }
  }

  process.waitForFinished(3000);
  result.parameters = QByteArray(reinterpret_cast<const char*>(run_result.parameters.data()),
                                 static_cast<qsizetype>(run_result.parameters.size()));
  if (run_result.status == kRunOk) {
    result.status = LegacyPluginRunStatus::Ok;
  } else if (run_result.status == kRunCancelled) {
    result.status = LegacyPluginRunStatus::Cancelled;
  } else {
    result.status = LegacyPluginRunStatus::Error;
    result.message = run_result.message.empty()
                         ? QCoreApplication::translate("LegacyPluginRunner", "The plug-in reported an error.")
                         : QString::fromUtf8(run_result.message.c_str());
  }
  return result;
}

}  // namespace patchy::ui
