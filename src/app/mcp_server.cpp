#include "app/mcp_server.hpp"
#include "app/mcp_stdio.hpp"
#include "ui/ai_control_paths.hpp"
#include "ui/background_workers.hpp"
#include "ui/main_window.hpp"
#include "ui/mcp_attachment.hpp"
#include "ui/mcp_line_buffer.hpp"
#include "ui/mcp_session.hpp"
#include "ui/script_engine.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <cstdio>
#include <mutex>

namespace patchy {
namespace {
QString kit_directory() { return ui::ai_control_skill_directory(); }

int run_attached_proxy(QApplication& app) {
  QLocalSocket socket;
  socket.connectToServer(ui::mcp_attachment_endpoint());
  if (!socket.waitForConnected(1500)) {
    const auto message = QCoreApplication::translate("PatchyMcp", "No running Patchy workspace is available for attachment. Open Patchy from the same installation, then reconnect. No separate workspace was created.").toUtf8();
    (void)std::fprintf(stderr, "%s\n", message.constData());
    return 2;
  }
  QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] { write_mcp_stdout(socket.readAll()); });
  QObject::connect(&socket, &QLocalSocket::disconnected, &app, [&] {
    write_mcp_stdout(socket.readAll());
    QCoreApplication::exit(0);
  });
  std::mutex input_mutex;
  QByteArray pending;
  McpStdioReader reader([&](const QByteArray& bytes) {
    const std::lock_guard lock(input_mutex);
    pending.append(bytes);
  }, [&] {
    QMetaObject::invokeMethod(&app, [&] { socket.abort(); QCoreApplication::exit(0); }, Qt::QueuedConnection);
  });
  QTimer flush;
  QObject::connect(&flush, &QTimer::timeout, &app, [&] {
    QByteArray bytes;
    { const std::lock_guard lock(input_mutex); bytes.swap(pending); }
    if (!bytes.isEmpty()) { (void)socket.write(bytes); }
  });
  flush.start(5);
  return app.exec();
}
}  // namespace

int run_mcp_server(QApplication& app) {
  configure_mcp_stdio();
  app.setQuitOnLastWindowClosed(false);
  const auto args = app.arguments().mid(1);
  if (args == QStringList{QStringLiteral("--attach")}) { return run_attached_proxy(app); }
  if (!args.isEmpty() && args != QStringList{QStringLiteral("--visible")} && args != QStringList{QStringLiteral("--check")}) {
    const auto usage = QCoreApplication::translate("PatchyMcp", "Usage: patchy-mcp [--attach | --visible | --check]. Default: hidden workspace. --visible: separate window. --attach: the running Patchy workspace.").toUtf8();
    (void)std::fprintf(stderr, "%s\n", usage.constData());
    return args == QStringList{QStringLiteral("--help")} ? 0 : 2;
  }
  ui::MainWindow window;
  window.set_cli_automation_mode(true);
  window.show();
  if (args == QStringList{QStringLiteral("--check")}) {
    auto& host = window.script_engine_host();
    host.set_connector_mode(true);
    ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("connector-check");
    options.unattended = true;
    (void)host.run_source(QStringLiteral("var d=app.newDocument(16,16); d.addLayer('Ink').drawStrokes([{points:[{x:2,y:2},{x:12,y:12}]}]);"), std::move(options));
    while (host.run_active()) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
    QJsonObject metadata;
    bool ok = !host.last_run_had_error() && !kit_directory().isEmpty();
    try {
      ok = ok && !host.render_preview(host.active_session_id(), {}, &metadata).isNull();
    } catch (...) { ok = false; }
    for (const auto& file : {"references/workflow.md", "references/patchy.d.ts", "references/scripting-guide.md", "scripts/pixel-art.js"}) {
      ok = ok && QFileInfo::exists(kit_directory() + '/' + QLatin1String(file));
    }
    const auto report = QJsonDocument(QJsonObject{{"ok", ok}, {"version", app.applicationVersion()},
        {"skillDirectory", kit_directory()}, {"preview", metadata}}).toJson(QJsonDocument::Compact);
    (void)std::fwrite(report.constData(), 1, static_cast<std::size_t>(report.size()), stdout);
    (void)std::fputc('\n', stdout);
    ui::wait_for_tracked_background_workers();
    return ok ? 0 : 2;
  }

  ui::McpSession session(window, false, write_mcp_stdout);
  session.connect_from_any_thread();
  while (!session.ready()) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
  int result = 0;
  {
    ui::McpLineBuffer input;
    McpStdioReader reader([&](const QByteArray& bytes) {
      input.append(bytes, [&](const QByteArray& line) { session.receive_line(line); });
    }, [&] { session.disconnect_from_any_thread(); });
    QTimer lifecycle;
    QObject::connect(&lifecycle, &QTimer::timeout, &app, [&] {
      if (session.closed()) { QCoreApplication::exit(0); }
    });
    lifecycle.start(10);
    result = app.exec();
  }
  session.shutdown();
  ui::wait_for_tracked_background_workers();
  return result;
}
}  // namespace patchy
