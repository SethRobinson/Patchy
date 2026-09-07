#include "app/mcp_server.hpp"
#include "ui/ai_control_paths.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/background_workers.hpp"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <csignal>
#include <mutex>
#include <thread>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace patchy {
namespace {

// Shared with Help > Set up AI Control so the connector and the dialog agree on
// the installed layout.
QString kit_directory() { return ui::ai_control_skill_directory(); }

QJsonObject schema(const QJsonObject& properties = {}, const QJsonArray& required = {}) {
  return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}

QJsonArray tool_catalog() {
  const QJsonObject str{{"type", "string"}};
  const auto tool = [](const char* name, const QString& description, const QJsonObject& input, bool read) {
    return QJsonObject{{"name", QLatin1String(name)}, {"description", description}, {"inputSchema", input},
      {"annotations", QJsonObject{{"readOnlyHint", read}, {"destructiveHint", !read}, {"openWorldHint", !read}}}};
  };
  return {
    tool("get_info", QCoreApplication::translate("PatchyMcp", "Discover Patchy versions, capabilities, and the installed control skill."), schema(), true),
    tool("get_help", QCoreApplication::translate("PatchyMcp", "Read the scripting API, workflow, or a runnable example. Use before writing scripts."),
         schema({{"topic", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"workflow", "api", "guide", "pixel-art", "painting", "edit-document", "reference-art"}}}}}), true),
    tool("get_state", QCoreApplication::translate("PatchyMcp", "Inspect open documents, stable IDs, layers, selections, and undo availability."), schema(), true),
    tool("execute_script", QCoreApplication::translate("PatchyMcp", "Run JavaScript in the persistent workspace. Use patchy.setResult(value) for a JSON result. Globals reset each run; documents persist. Edits form one undo step per document; errors can leave partial edits. Scripts are trusted and can access files."),
         schema({{"code", str}, {"name", str}, {"args", QJsonObject{{"type", "object"}, {"additionalProperties", str}}}}, {"code"}), false),
    tool("draw_strokes", QCoreApplication::translate("PatchyMcp", "Paint a batch through the native Brush or Eraser. Read get_help(api) for stroke fields and pressure behavior. Coordinates are document pixels; the batch is one undo step."),
         schema({{"documentId", str}, {"layerId", str}, {"strokes", QJsonObject{{"type", "array"}, {"minItems", 1}, {"maxItems", 1000}, {"items", QJsonObject{{"type", "object"}}}}}},
                {"documentId", "layerId", "strokes"}), false),
    tool("get_preview", QCoreApplication::translate("PatchyMcp", "Return a fresh canvas PNG image and coordinate metadata, or a capture of the connector's own app window. No save path or document state changes."),
         schema({{"documentId", str}, {"target", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"canvas", "window"}}}},
                 {"options", QJsonObject{{"type", "object"}}}}), true),
    tool("undo", QCoreApplication::translate("PatchyMcp", "Undo one edit in the named document."), schema({{"documentId", str}}, {"documentId"}), false),
    tool("redo", QCoreApplication::translate("PatchyMcp", "Redo one edit in the named document."), schema({{"documentId", str}}, {"documentId"}), false)
  };
}

// STDIO JSON-RPC (MCP 2025-11-25, with 2025-06-18 compatibility). The input
// thread only touches protocol state and the host's thread-safe interrupt gate.
// All document work and replies to document operations run on the Qt thread.
class Server final : public QObject {
 public:
  Server(QApplication& app, ui::MainWindow& window)
      : app_(app), window_(window), host_(window.script_engine_host()) {
    host_.set_connector_mode(true);
    connect(&host_, &ui::ScriptEngineHost::message_emitted, this, [this](int kind, const QString& text) {
      if (logs_.size() < 1000) {
        logs_.append(QJsonObject{{"level", kind == 2 ? "error" : kind == 1 ? "warning" : "info"}, {"text", text.left(16000)}});
      }
    });
    connect(&host_, &ui::ScriptEngineHost::run_state_changed, this, [this] {
      if (script_pending_ && !host_.run_active()) { finish_script(); }
    });
  }

  ~Server() override { if (reader_.joinable()) { reader_.join(); } }

  void start() { reader_ = std::thread([this] { read_input(); }); }

 private:
  void send(const QJsonObject& message) {
    if (disconnected_) { return; }
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    const std::lock_guard lock(output_mutex_);
    (void)std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
    (void)std::fflush(stdout);
  }

  void rpc_error(const QJsonValue& id, int code, const QString& message) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
  }

  void reply(const QJsonValue& id, const QJsonObject& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
  }

  void tool_reply(const QJsonValue& id, QJsonObject data, bool error = false, QJsonArray content = {}) {
    content.append(QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}});
    reply(id, {{"isError", error}, {"structuredContent", data}, {"content", content}});
  }

  void release_request() {
    const std::lock_guard lock(request_mutex_);
    active_id_ = QJsonValue();
    busy_ = false;
  }

  void complete_tool(const QJsonValue& id, QJsonObject data, bool error = false, QJsonArray content = {}) {
    // Let a client submit its next request as soon as it receives this reply.
    release_request();
    tool_reply(id, std::move(data), error, std::move(content));
  }

  void read_input() {
    QByteArray line;
    bool too_large = false;
    for (int ch; (ch = std::fgetc(stdin)) != EOF;) {
      if (ch != '\n') {
        if (line.size() < 16 * 1024 * 1024) { line.append(static_cast<char>(ch)); }
        else { too_large = true; }
        continue;
      }
      if (too_large) {
        rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "The request exceeds 16 MiB."));
        line.clear(); too_large = false; continue;
      }
      QJsonParseError parse_error;
      const auto doc = QJsonDocument::fromJson(line, &parse_error);
      line.clear();
      if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); continue;
      }
      const auto message = doc.object();
      const auto method = message["method"].toString();
      const auto id = message.value("id");
      if (message["jsonrpc"] != "2.0" || method.isEmpty() ||
          (!id.isUndefined() && !id.isDouble() && !id.isString())) {
        rpc_error(QJsonValue(QJsonValue::Null), -32600, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); continue;
      }
      if (method == "notifications/cancelled") {
        const std::lock_guard lock(request_mutex_);
        if (busy_ && message["params"].toObject()["requestId"] == active_id_) {
          cancelled_ = true;
          host_.interrupt_from_any_thread();
          QMetaObject::invokeMethod(this, [this, request_id = active_id_] {
            bool stop = false;
            {
              const std::lock_guard request_lock(request_mutex_);
              stop = busy_ && active_id_ == request_id && cancelled_;
            }
            if (stop) { host_.stop_active_run(); }
          }, Qt::QueuedConnection);
        }
        continue;
      }
      if (method == "tools/call" && !id.isUndefined()) {
        const std::lock_guard lock(request_mutex_);
        if (busy_) {
          tool_reply(id, {{"error", "busy"}, {"message", QCoreApplication::translate("PatchyMcp", "Another operation is running. Wait for its reply before retrying.")}}, true);
          continue;
        }
        busy_ = true; cancelled_ = false; active_id_ = id;
      }
      QMetaObject::invokeMethod(this, [this, message] { handle(message); }, Qt::QueuedConnection);
    }
    disconnected_ = true;
    host_.interrupt_from_any_thread();
    QMetaObject::invokeMethod(this, [this] {
      host_.stop_active_run();
      // QApplication::quit asks windows to close and can be rejected by an
      // unsaved-document prompt. This owned workspace must always disconnect;
      // stack destruction below tears down its canvases and documents in order.
      QCoreApplication::exit(0);
    }, Qt::QueuedConnection);
  }

  std::int64_t document_id(const QJsonObject& args, bool optional = false) {
    const auto value = args.value("documentId");
    if (optional && value.isUndefined() && host_.active_session_id()) { return host_.active_session_id(); }
    bool ok = false;
    const auto id = value.toString().toLongLong(&ok);
    if (!value.isString() || !ok || !host_.session_document_const(id)) {
      throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Unknown document ID.").toStdString());
    }
    return id;
  }

  void handle(const QJsonObject& message) {
    const auto method = message["method"].toString();
    const auto id = message.value("id");
    if (id.isUndefined()) { return; }
    if (method == "initialize") {
      initialized_ = true;
      const auto requested = message["params"].toObject()["protocolVersion"].toString();
      reply(id, {{"protocolVersion", requested == "2025-06-18" ? requested : QStringLiteral("2025-11-25")},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
        {"serverInfo", QJsonObject{{"name", "patchy"}, {"version", app_.applicationVersion()}}},
        {"instructions", QCoreApplication::translate("PatchyMcp", "Patchy owns an isolated persistent workspace, hidden by default or visible with --visible. It never attaches to another Patchy window. Read get_help(workflow) and get_help(api). Use document/layer IDs, batch edits, inspect get_preview, and save checkpoints before disconnecting. JS globals reset between calls. Requests are serialized; failed scripts may leave undoable edits.")}});
      return;
    }
    if (method == "ping") { reply(id, {}); return; }
    if (!initialized_) {
      if (method == "tools/call") { release_request(); }
      rpc_error(id, -32600, QCoreApplication::translate("PatchyMcp", "Initialize the MCP connection first."));
      return;
    }
    if (method == "tools/list") { reply(id, {{"tools", tool_catalog()}}); return; }
    if (method != "tools/call") { rpc_error(id, -32601, QCoreApplication::translate("PatchyMcp", "Unknown MCP method.")); return; }
    try {
      {
        const std::lock_guard lock(request_mutex_);
        if (cancelled_ || disconnected_) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Operation cancelled.").toStdString()); }
        host_.clear_external_interrupt();
      }
      const auto params = message["params"].toObject();
      const auto name = params["name"].toString();
      const auto args_value = params.value("arguments");
      if (!args_value.isUndefined() && !args_value.isObject()) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Tool arguments must be an object.").toStdString()); }
      const auto args = args_value.toObject();
      QJsonObject tool;
      for (const auto& entry : tool_catalog()) { if (entry.toObject()["name"] == name) { tool = entry.toObject(); break; } }
      if (tool.isEmpty()) { release_request(); rpc_error(id, -32602, QCoreApplication::translate("PatchyMcp", "Unknown tool.")); return; }
      const auto input = tool["inputSchema"].toObject();
      const auto properties = input["properties"].toObject();
      for (auto it = args.begin(); it != args.end(); ++it) {
        const auto type = properties[it.key()].toObject()["type"].toString();
        if (!properties.contains(it.key()) || (type == "string" && !it->isString()) ||
            (type == "object" && !it->isObject()) || (type == "array" && !it->isArray())) {
          throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Invalid tool argument: %1").arg(it.key()).toStdString());
        }
      }
      for (const auto& required : input["required"].toArray()) {
        if (!args.contains(required.toString())) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Missing tool argument: %1").arg(required.toString()).toStdString()); }
      }
      if (name == "get_info") {
        const bool offscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
        complete_tool(id, {{"version", app_.applicationVersion()}, {"apiVersion", 1}, {"mode", offscreen ? "offscreen" : "visible"},
          {"platform", QGuiApplication::platformName()}, {"windowVisible", !offscreen && window_.isVisible()},
          {"skillDirectory", kit_directory()}, {"capabilities", QJsonArray{"persistentDocuments", "javascript", "brush", "eraser", "pressure", "seededDynamics", "pixels", "preview", "undo", "redo"}},
          {"scriptTrust", "applicationPrivileges"}, {"liveWindowAttachment", false}});
      } else if (name == "get_help") {
        const auto topic = args["topic"].toString("workflow");
        const QMap<QString, QString> files{{"workflow", "references/workflow.md"}, {"api", "references/patchy.d.ts"},
          {"guide", "references/scripting-guide.md"}, {"pixel-art", "scripts/pixel-art.js"},
          {"painting", "scripts/painting.js"}, {"edit-document", "scripts/edit-document.js"},
          {"reference-art", "references/reference-art.md"}};
        if (!files.contains(topic) || kit_directory().isEmpty()) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "The requested control-kit resource is unavailable.").toStdString()); }
        QFile file(kit_directory() + '/' + files.value(topic));
        if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Could not read the control-kit resource.").toStdString()); }
        complete_tool(id, {{"topic", topic}, {"text", QString::fromUtf8(file.readAll())}});
      } else if (name == "get_state") {
        complete_tool(id, host_.automation_state());
      } else if (name == "get_preview") {
        const auto target = args["target"].toString("canvas");
        QJsonObject metadata;
        QImage image;
        if (target == "canvas") {
          image = host_.render_preview(document_id(args, true), args["options"].toObject(), &metadata);
        } else if (target == "window") {
          if (args.contains("options") || args.contains("documentId")) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Window previews do not accept document or canvas options.").toStdString()); }
          QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
          image = window_.grab().toImage();
          metadata = {{"target", "window"}, {"offscreen", QGuiApplication::platformName() == QStringLiteral("offscreen")}, {"width", image.width()}, {"height", image.height()}};
        } else { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Unknown preview target.").toStdString()); }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&buffer, "PNG")) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Could not render the preview.").toStdString()); }
        complete_tool(id, metadata, false, {QJsonObject{{"type", "image"}, {"mimeType", "image/png"}, {"data", QString::fromLatin1(png.toBase64())}}});
      } else if (name == "undo" || name == "redo") {
        const bool changed = host_.restore_session_history(document_id(args), name == "redo");
        complete_tool(id, {{"changed", changed}, {"state", host_.automation_state()}});
      } else {
        QString code = args["code"].toString();
        if (name == "draw_strokes") {
          (void)document_id(args);
          const auto quoted = [](const QJsonValue& value) {
            auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
            return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
          };
          code = "app.getDocument(" + quoted(args["documentId"]) + ").getLayer(" + quoted(args["layerId"]) + ").drawStrokes(" + quoted(args["strokes"]) + ");";
        }
        if (code.isEmpty() || code.size() > 4 * 1024 * 1024) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Script source must contain 1 to 4194304 characters.").toStdString()); }
        ui::ScriptEngineHost::RunOptions options;
        options.name = args["name"].toString(name);
        options.path = QDir::current().absoluteFilePath("patchy-agent.js");
        options.unattended = true;
        const auto script_args = args["args"].toObject();
        for (auto it = script_args.begin(); it != script_args.end(); ++it) {
          if (!it->isString()) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Script argument values must be strings.").toStdString()); }
          options.args.append(it.key() + '=' + it->toString());
        }
        logs_ = {}; script_pending_ = true; script_id_ = id;
        host_.run_source(code, std::move(options));
        return;  // run_state_changed completes the response, including timers.
      }
    } catch (const std::exception& e) {
      complete_tool(id, {{"error", cancelled_ ? "cancelled" : "operation_failed"}, {"message", QString::fromUtf8(e.what())}}, true);
    }
  }

  void finish_script() {
    script_pending_ = false;
    const bool failed = cancelled_ || host_.last_run_had_error();
    complete_tool(script_id_, {{"result", host_.last_result()}, {"logs", logs_},
        {"status", cancelled_ ? "cancelled" : failed ? "failed" : "done"},
        {"state", host_.automation_state()}}, failed);
  }

  QApplication& app_;
  ui::MainWindow& window_;
  ui::ScriptEngineHost& host_;
  std::thread reader_;
  std::mutex output_mutex_;
  std::mutex request_mutex_;
  bool busy_{false};  // guarded by request_mutex_
  QJsonValue active_id_;
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> disconnected_{false};
  bool initialized_{false};
  bool script_pending_{false};
  QJsonValue script_id_;
  QJsonArray logs_;
};
}  // namespace

int run_mcp_server(QApplication& app) {
#ifdef Q_OS_WIN
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
#else
  // A client can close both pipes before the reader observes EOF. Allow Qt's
  // orderly cleanup instead of terminating on a write to that closed pipe.
  std::signal(SIGPIPE, SIG_IGN);
#endif
  app.setQuitOnLastWindowClosed(false);
  ui::MainWindow window;
  window.set_cli_automation_mode(true);
  window.show();  // lays out text and previews; --visible also shows the workspace
  if (app.arguments().contains("--check")) {
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
  if (app.arguments().size() > 1 && app.arguments() != QStringList{app.arguments().front(), QStringLiteral("--visible")}) {
    const auto usage = QCoreApplication::translate("PatchyMcp", "Usage: patchy-mcp [--visible | --check]. Serve MCP over stdin/stdout, hidden by default; --visible opens a separate workspace window.").toUtf8();
    (void)std::fwrite(usage.constData(), 1, static_cast<std::size_t>(usage.size()), stderr);
    (void)std::fputc('\n', stderr);
    return app.arguments().contains("--help") ? 0 : 2;
  }
  Server server(app, window);
  server.start();
  const int result = app.exec();
  ui::wait_for_tracked_background_workers();
  return result;
}
}  // namespace patchy
