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
#include <stdexcept>

namespace patchy {
namespace {
QString kit_directory() { return ui::ai_control_skill_directory(); }

// The client owns the stdio connection; the artist owns the workspace lifetime.
// Losing the latter must not destroy discovery or require restarting the client.
class AttachedProxy final : public QObject {
 public:
  AttachedProxy() {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { unavailable(); });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this] { unavailable(); });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
      if (disconnecting_) { return; }
      read_workspace();
      unavailable();
    });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
      write_workspace({{"jsonrpc", "2.0"}, {"id", "patchy-proxy-initialize"},
                       {"method", "initialize"}, {"params", initialize_params_}});
    });
    connect(&socket_, &QLocalSocket::readyRead, this, [this] { read_workspace(); });
  }
  ~AttachedProxy() override {
    deadline_.stop();
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
  }

  void receive_line(const QByteArray& line) {
    if (line.size() > 16 * 1024 * 1024) {
      rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "The request exceeds 16 MiB.")); return;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); return;
    }
    const auto message = doc.object();
    const auto method = message["method"].toString();
    const auto id = message.value("id");
    if (message["jsonrpc"] != "2.0" || method.isEmpty() ||
        (!id.isUndefined() && !id.isDouble() && !id.isString())) {
      rpc_error(QJsonValue(QJsonValue::Null), -32600, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); return;
    }
    if (id.isUndefined()) {
      if (method == "notifications/cancelled" && !pending_.isEmpty() &&
          message["params"].toObject()["requestId"] == pending_["id"]) {
        if (forwarded_) { write_workspace(message); }
        else {
          const QJsonValue cancelled_id = pending_.value("id");
          pending_ = {};
          unavailable();
          tool_reply(cancelled_id, {{"error", "cancelled"}, {"message", QCoreApplication::translate("PatchyMcp", "Operation cancelled.")}}, true);
        }
      }
      return;
    }
    if (method == "initialize") {
      if (initialized_) { rpc_error(id, -32600, QCoreApplication::translate("PatchyMcp", "The MCP connection is already initialized.")); return; }
      initialized_ = true;
      initialize_params_ = message["params"].toObject();
      reply(id, ui::mcp_initialize_result(initialize_params_));
      return;
    }
    if (method == "ping") { reply(id, {}); return; }
    if (!initialized_) { rpc_error(id, -32600, QCoreApplication::translate("PatchyMcp", "Initialize the MCP connection first.")); return; }
    if (method == "tools/list") { reply(id, {{"tools", ui::mcp_tool_catalog()}}); return; }
    if (method != "tools/call") { rpc_error(id, -32601, QCoreApplication::translate("PatchyMcp", "Unknown MCP method.")); return; }
    if (!pending_.isEmpty()) {
      tool_reply(id, {{"error", "busy"}, {"message", QCoreApplication::translate("PatchyMcp", "Another operation is running. Wait for its reply before retrying.")}}, true);
      return;
    }
    const auto params = message["params"].toObject();
    const auto name = params["name"].toString();
    try {
      const auto value = params.value("arguments");
      if (!value.isUndefined() && !value.isObject()) {
        throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Tool arguments must be an object.").toStdString());
      }
      bool known = false;
      for (const auto& tool : ui::mcp_tool_catalog()) { known = known || tool.toObject()["name"] == name; }
      if (!known) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Unknown MCP tool.").toStdString()); }
      if (name == "get_help") {
        const auto args = value.toObject();
        for (auto it = args.begin(); it != args.end(); ++it) {
          if (it.key() != "topic" || !it->isString()) {
            throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Invalid tool argument: %1").arg(it.key()).toStdString());
          }
        }
        tool_reply(id, ui::mcp_help_result(args));
        return;
      }
    } catch (const std::exception& error_message) {
      tool_reply(id, {{"error", "tool_error"}, {"message", QString::fromUtf8(error_message.what())}}, true);
      return;
    }
    pending_ = message;
    if (ready_ && socket_.state() == QLocalSocket::ConnectedState) { forward_pending(); return; }
    // Only a read starts a new attachment. An edit from an old connection must
    // never acquire a different workspace, even if document IDs are reused.
    if (name != "get_info" && name != "get_state" && name != "get_preview") { unavailable(); return; }
    workspace_input_ = {};
    deadline_.start(2000);
    socket_.connectToServer(ui::mcp_attachment_endpoint());
  }

 private:
  static void send(const QJsonObject& message) { write_mcp_stdout(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); }
  static void reply(const QJsonValue& id, const QJsonObject& result) { send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}); }
  static void rpc_error(const QJsonValue& id, int code, const QString& message) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
  }
  static void tool_reply(const QJsonValue& id, const QJsonObject& data, bool error = false) {
    reply(id, {{"isError", error}, {"structuredContent", data}, {"content", QJsonArray{QJsonObject{
      {"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}}}});
  }
  void write_workspace(const QJsonObject& message) {
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (socket_.write(bytes) != bytes.size()) { unavailable(); }
  }
  void forward_pending() {
    if (pending_.isEmpty()) { return; }
    forwarded_ = true;
    write_workspace(pending_);
  }
  void read_workspace() {
    // Replies (especially PNG previews) can exceed the request-line limit.
    // Consume each complete line before dispatch, since dispatch can abort the socket.
    workspace_input_.append(socket_.readAll());
    while (true) {
      const auto newline = workspace_input_.indexOf('\n');
      if (newline < 0) { break; }
      const auto line = workspace_input_.left(newline);
      workspace_input_.remove(0, newline + 1);
      const auto doc = QJsonDocument::fromJson(line);
      if (!doc.isObject()) { unavailable(); return; }
      const auto message = doc.object();
      if (!ready_) {
        if (message["id"] != "patchy-proxy-initialize" || !message["result"].isObject()) { unavailable(); return; }
        ready_ = true;
        deadline_.stop();
        write_workspace({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        if (ready_) { forward_pending(); }
        continue;
      }
      if (!pending_.isEmpty() && message["id"] == pending_["id"] &&
          (message.contains("result") || message.contains("error"))) {
        pending_ = {};
        forwarded_ = false;
      }
      write_mcp_stdout(line + '\n');
    }
  }
  void unavailable() {
    if (disconnecting_) { return; }
    deadline_.stop();
    ready_ = false;
    const auto pending = pending_;
    const bool uncertain = forwarded_;
    pending_ = {};
    forwarded_ = false;
    disconnecting_ = true;
    socket_.abort();
    disconnecting_ = false;
    if (!pending.isEmpty()) {
      const auto message = uncertain
        ? QCoreApplication::translate("PatchyMcp", "The Patchy workspace disconnected during this request. Changes may have been made. Do not repeat the edit automatically. Open Patchy from the same installation, call get_info, and inspect the document before continuing.")
        : QCoreApplication::translate("PatchyMcp", "The Patchy workspace is unavailable. Open Patchy from the same installation, or disconnect another attached client, then call get_info again. This MCP connection remains available. No separate workspace was created.");
      tool_reply(pending["id"], {{"error", uncertain ? "workspace_disconnected" : "workspace_unavailable"},
        {"message", message}, {"workspace", "attached"}, {"workspaceAvailable", false},
        {"liveWindowAttachment", false}, {"requiresExpectedState", true}, {"skillDirectory", kit_directory()},
        {"version", QCoreApplication::applicationVersion()}, {"retrySafe", !uncertain}}, true);
    }
  }

  QLocalSocket socket_;
  QTimer deadline_;
  QByteArray workspace_input_;
  QJsonObject initialize_params_;
  QJsonObject pending_;
  bool initialized_{false};
  bool ready_{false};
  bool forwarded_{false};
  bool disconnecting_{false};
};

int run_attached_proxy(QApplication& app) {
  AttachedProxy proxy;
  ui::McpLineBuffer input;
  McpStdioReader reader([&](const QByteArray& bytes) {
    input.append(bytes, [&](const QByteArray& line) {
      QMetaObject::invokeMethod(&proxy, [&, line] { proxy.receive_line(line); }, Qt::QueuedConnection);
    });
  }, [&] {
    QMetaObject::invokeMethod(&app, [] { QCoreApplication::exit(0); }, Qt::QueuedConnection);
  });
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
  ui::configure_owned_mcp_workspace(window, args == QStringList{QStringLiteral("--visible")});
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
