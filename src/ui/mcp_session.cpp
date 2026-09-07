#include "ui/mcp_session.hpp"
#include "ui/mcp_activity.hpp"
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
#include <QUuid>
#include <QElapsedTimer>
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

namespace patchy::ui {
void configure_owned_mcp_workspace(MainWindow& window, bool visible) {
  window.set_cli_automation_mode(!visible);
  qApp->setQuitOnLastWindowClosed(visible);
}

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
         schema({{"topic", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"workflow", "api", "guide", "pixel-art", "painting", "edit-document", "reference-art", "vector-art", "edit-shape", "paths-masks"}}}}}), true),
    tool("get_state", QCoreApplication::translate("PatchyMcp", "Inspect open documents, stable IDs, layers, selections, and undo availability."), schema(), true),
    tool("execute_script", QCoreApplication::translate("PatchyMcp", "Run JavaScript in the persistent workspace. Use patchy.setResult(value) for a JSON result. Globals reset each run; documents persist. Edits form one undo step per document; errors can leave partial edits. Scripts are trusted and can access files."),
         schema({{"code", str}, {"expectedState", str}, {"name", str}, {"args", QJsonObject{{"type", "object"}, {"additionalProperties", str}}}}, {"code"}), false),
    tool("draw_strokes", QCoreApplication::translate("PatchyMcp", "Paint a batch through the native Brush or Eraser. Read get_help(api) for stroke fields and pressure behavior. Coordinates are document pixels; the batch is one undo step."),
         schema({{"documentId", str}, {"expectedState", str}, {"layerId", str}, {"strokes", QJsonObject{{"type", "array"}, {"minItems", 1}, {"maxItems", 1000}, {"items", QJsonObject{{"type", "object"}}}}}},
                {"documentId", "layerId", "strokes"}), false),
    tool("get_preview", QCoreApplication::translate("PatchyMcp", "Return a fresh canvas PNG image and coordinate metadata, or a capture of the connected Patchy window. No save path or document state changes."),
         schema({{"documentId", str}, {"target", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"canvas", "window"}}}},
                 {"options", QJsonObject{{"type", "object"}}}}), true),
    tool("undo", QCoreApplication::translate("PatchyMcp", "Undo one edit in the named document."), schema({{"documentId", str}, {"expectedState", str}}, {"documentId"}), false),
    tool("redo", QCoreApplication::translate("PatchyMcp", "Redo one edit in the named document."), schema({{"documentId", str}, {"expectedState", str}}, {"documentId"}), false)
  };
}

}  // namespace

// JSON-RPC (MCP 2025-11-25, with 2025-06-18 compatibility). The input
// thread only touches protocol state and the host's thread-safe interrupt gate.
// All document work and replies to document operations run on the Qt thread.
struct McpSession::Impl final : public QObject {
 public:
  Impl(MainWindow& window, bool attached, Output output)
      : app_(*qApp), window_(window), host_(window.script_engine_host()),
        attached_(attached), output_(std::move(output)),
        activity_(std::make_unique<McpActivity>(window, [this] { cancel_current(); })) {
    connect(&host_, &ui::ScriptEngineHost::message_emitted, this, [this](int kind, const QString& text) {
      if (script_pending_ && logs_.size() < 1000) {
        logs_.append(QJsonObject{{"level", kind == 2 ? "error" : kind == 1 ? "warning" : "info"}, {"text", text.left(16000)}});
      }
    });
    connect(&host_, &ui::ScriptEngineHost::run_state_changed, this, [this] {
      if (script_pending_ && !host_.run_active()) { finish_script(); }
    });
  }

  ~Impl() override { shutdown(); }

  void connect_from_any_thread() {
    closed_ = false;
    ready_ = false;
    disconnected_ = false;
    QMetaObject::invokeMethod(this, [this] {
      if (disconnected_) { shutdown(); return; }
      initialized_ = false;
      nonce_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
      cancelled_ = false;
      release_request();
      ready_ = true;
    }, Qt::QueuedConnection);
  }

  void shutdown() {
    if (owns_run_) { host_.stop_active_run(); }
    activity_->set_disconnected();
    ready_ = false;
    closed_ = true;
  }

  void disconnect_from_any_thread() {
    ready_ = false;
    if (disconnected_.exchange(true)) { return; }
    {
      const std::lock_guard lock(request_mutex_);
      if (owns_run_) { cancelled_ = true; host_.interrupt_from_any_thread(); }
    }
    QMetaObject::invokeMethod(this, [this] {
      if (owns_run_) { host_.stop_active_run(); }
      if (!owns_run_) { shutdown(); }
    }, Qt::QueuedConnection);
  }

  void cancel_current(const QJsonValue& expected = QJsonValue(QJsonValue::Undefined)) {
    const std::lock_guard lock(request_mutex_);
    if (!busy_ || (!expected.isUndefined() && expected != active_id_)) { return; }
    cancelled_ = true;
    if (owns_run_) { host_.interrupt_from_any_thread(); }
    QMetaObject::invokeMethod(this, [this, request_id = active_id_] {
      bool stop = false;
      {
        const std::lock_guard request_lock(request_mutex_);
        stop = busy_ && active_id_ == request_id && cancelled_ && owns_run_;
      }
      if (stop) { host_.stop_active_run(); }
    }, Qt::QueuedConnection);
  }

  void send(const QJsonObject& message) {
    if (!disconnected_) { output_(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); }
  }

  QJsonObject state() const {
    auto result = host_.automation_state();
    result["stateToken"] = nonce_ + '/' + host_.automation_fingerprint();
    return result;
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
    activity_->finish_operation();
    // Let a client submit its next request as soon as it receives this reply.
    release_request();
    tool_reply(id, std::move(data), error, std::move(content));
  }

  void receive_line(const QByteArray& line) {
    if (line.size() > 16 * 1024 * 1024) {
      rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "The request exceeds 16 MiB."));
      return;
    }
    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(line, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      rpc_error(QJsonValue(QJsonValue::Null), -32700, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); return;
    }
    const auto message = doc.object();
    const auto method = message["method"].toString();
    const auto id = message.value("id");
    if (message["jsonrpc"] != "2.0" || method.isEmpty() ||
        (!id.isUndefined() && !id.isDouble() && !id.isString())) {
      rpc_error(QJsonValue(QJsonValue::Null), -32600, QCoreApplication::translate("PatchyMcp", "Invalid JSON-RPC message.")); return;
    }
    if (method == "notifications/cancelled") {
      const auto request = message["params"].toObject().value("requestId");
      if (!request.isUndefined()) { cancel_current(request); }
      return;
    }
    if (method == "tools/call" && !id.isUndefined()) {
      const std::lock_guard lock(request_mutex_);
      if (busy_) {
        tool_reply(id, {{"error", "busy"}, {"message", QCoreApplication::translate("PatchyMcp", "Another operation is running. Wait for its reply before retrying.")}}, true);
        return;
      }
      busy_ = true; cancelled_ = false; active_id_ = id;
    }
    QMetaObject::invokeMethod(this, [this, message] { handle(message); }, Qt::QueuedConnection);
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
    if (disconnected_) { return; }
    const auto method = message["method"].toString();
    const auto id = message.value("id");
    if (id.isUndefined()) { return; }
    if (method == "initialize") {
      if (initialized_) { rpc_error(id, -32600, QCoreApplication::translate("PatchyMcp", "The MCP connection is already initialized.")); return; }
      initialized_ = true;
      activity_->set_connected(message["params"].toObject()["clientInfo"].toObject()["name"].toString(QStringLiteral("MCP")));
      const auto requested = message["params"].toObject()["protocolVersion"].toString();
      reply(id, {{"protocolVersion", requested == "2025-06-18" ? requested : QStringLiteral("2025-11-25")},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
        {"serverInfo", QJsonObject{{"name", "patchy"}, {"version", app_.applicationVersion()}}},
        {"instructions", QCoreApplication::translate("PatchyMcp", "Read get_info to identify the workspace: isolated, or the user's open Patchy window with --attach. Read get_help(workflow) and get_help(api). In attached mode every mutating tool requires expectedState from a fresh get_state or preview. If state is stale, inspect again before editing. Use document/layer IDs, batch edits, inspect get_preview, and save checkpoints. JS globals reset between calls. Requests are serialized; failed scripts may leave undoable edits.")}});
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
      const bool editing = name == "execute_script" || name == "draw_strokes" || name == "undo" || name == "redo";
      if (name != "get_info" && name != "get_help" && !host_.automation_ready()) {
        complete_tool(id, {{"error", "busy"}, {"message", QCoreApplication::translate("PatchyMcp", "Finish the current gesture, text edit, transform, dialog, or script before using AI control.")}}, true);
        return;
      }
      if (editing && (attached_ || args.contains("expectedState"))) {
        const auto current = state();
        if (args["expectedState"].toString().isEmpty() || args["expectedState"] != current["stateToken"]) {
          complete_tool(id, {{"error", "stale_state"}, {"message", QCoreApplication::translate("PatchyMcp", "The workspace changed or expectedState is missing. Inspect the current document and preview before retrying an edit.")}, {"state", current}}, true);
          return;
        }
      }
      const QMap<QString, QString> operations{
        {"get_info", QCoreApplication::translate("PatchyMcp", "Connection details")},
        {"get_help", QCoreApplication::translate("PatchyMcp", "Instructions")},
        {"get_state", QCoreApplication::translate("PatchyMcp", "Document state")},
        {"get_preview", QCoreApplication::translate("PatchyMcp", "Preview")},
        {"draw_strokes", QCoreApplication::translate("PatchyMcp", "Painting")},
        {"undo", QCoreApplication::translate("PatchyMcp", "Undo")},
        {"redo", QCoreApplication::translate("PatchyMcp", "Redo")},
        {"execute_script", QCoreApplication::translate("PatchyMcp", "Script")}};
      activity_->set_operation(name == "execute_script" ? args["name"].toString(operations[name]) : operations[name], editing);
      if (name == "get_info") {
        const bool offscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
        complete_tool(id, {{"version", app_.applicationVersion()}, {"apiVersion", 1}, {"mode", offscreen ? "offscreen" : "visible"},
          {"platform", QGuiApplication::platformName()}, {"windowVisible", !offscreen && window_.isVisible()},
          {"skillDirectory", kit_directory()}, {"capabilities", QJsonArray{"persistentDocuments", "javascript", "brush", "eraser", "pressure", "seededDynamics", "pixels", "preview", "undo", "redo", "vectorShapes", "vectorPaths", "vectorMasks", "vectorPaints"}},
          {"scriptTrust", "applicationPrivileges"}, {"liveWindowAttachment", attached_},
          {"workspace", attached_ ? "attached" : "isolated"}, {"requiresExpectedState", attached_},
          {"processId", QString::number(QCoreApplication::applicationPid())}});
      } else if (name == "get_help") {
        const auto topic = args["topic"].toString("workflow");
        const QMap<QString, QString> files{{"workflow", "references/workflow.md"}, {"api", "references/patchy.d.ts"},
          {"guide", "references/scripting-guide.md"}, {"pixel-art", "scripts/pixel-art.js"},
          {"painting", "scripts/painting.js"}, {"edit-document", "scripts/edit-document.js"},
          {"reference-art", "references/reference-art.md"}, {"vector-art", "scripts/vector-art.js"},
          {"edit-shape", "scripts/edit-shape.js"}, {"paths-masks", "scripts/paths-masks.js"}};
        if (!files.contains(topic) || kit_directory().isEmpty()) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "The requested control-kit resource is unavailable.").toStdString()); }
        QFile file(kit_directory() + '/' + files.value(topic));
        if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error(QCoreApplication::translate("PatchyMcp", "Could not read the control-kit resource.").toStdString()); }
        complete_tool(id, {{"topic", topic}, {"text", QString::fromUtf8(file.readAll())}});
      } else if (name == "get_state") {
        complete_tool(id, state());
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
        metadata["stateToken"] = state()["stateToken"];
        complete_tool(id, metadata, false, {QJsonObject{{"type", "image"}, {"mimeType", "image/png"}, {"data", QString::fromLatin1(png.toBase64())}}});
      } else if (name == "undo" || name == "redo") {
        const bool changed = host_.restore_session_history(document_id(args), name == "redo");
        complete_tool(id, {{"changed", changed}, {"state", state()}});
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
        previous_connector_mode_ = host_.connector_mode();
        host_.set_connector_mode(true);
        pump_clock_.start();
        host_.set_connector_progress_callback([this] {
          if (pump_clock_.elapsed() >= 50) {
            pump_clock_.restart();
            // McpActivity filters manual edits while allowing its Stop button.
            // ScriptEngineHost already prevents callback/engine reentrancy.
            QApplication::processEvents(QEventLoop::AllEvents, 8);
          }
        });
        {
          const std::lock_guard lock(request_mutex_);
          host_.clear_external_interrupt();
          owns_run_ = true;
          if (cancelled_ || disconnected_) { host_.interrupt_from_any_thread(); }
        }
        if (!host_.run_source(code, std::move(options)) && script_pending_ && !host_.run_active()) { finish_script(); }
        return;  // run_state_changed completes the response, including timers.
      }
    } catch (const std::exception& e) {
      complete_tool(id, {{"error", cancelled_ ? "cancelled" : "operation_failed"}, {"message", QString::fromUtf8(e.what())}}, true);
    }
  }

  void finish_script() {
    script_pending_ = false;
    {
      const std::lock_guard lock(request_mutex_);
      owns_run_ = false;
      // Cancellation belongs to this request, not the user's next local run.
      host_.clear_external_interrupt();
    }
    host_.set_connector_progress_callback({});
    host_.set_connector_mode(previous_connector_mode_);
    const bool failed = cancelled_ || host_.last_run_had_error();
    complete_tool(script_id_, {{"result", host_.last_result()}, {"logs", logs_},
        {"status", cancelled_ ? "cancelled" : failed ? "failed" : "done"},
        {"state", state()}}, failed);
    if (disconnected_) { shutdown(); }
  }

  QApplication& app_;
  ui::MainWindow& window_;
  ui::ScriptEngineHost& host_;
  bool attached_;
  Output output_;
  std::unique_ptr<McpActivity> activity_;
  QString nonce_;
  QElapsedTimer pump_clock_;
  bool previous_connector_mode_{false};
  std::atomic<bool> owns_run_{false};
 public:
  std::atomic<bool> ready_{false};
  std::atomic<bool> closed_{true};
 private:
  std::mutex request_mutex_;
  bool busy_{false};  // guarded by request_mutex_
  QJsonValue active_id_;
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> disconnected_{true};
  bool initialized_{false};
  bool script_pending_{false};
  QJsonValue script_id_;
  QJsonArray logs_;
};

McpSession::McpSession(MainWindow& window, bool attached, Output output)
    : impl_(std::make_unique<Impl>(window, attached, std::move(output))) {}
McpSession::~McpSession() = default;
void McpSession::connect_from_any_thread() { impl_->connect_from_any_thread(); }
bool McpSession::ready() const { return impl_->ready_; }
bool McpSession::closed() const { return impl_->closed_; }
void McpSession::receive_line(const QByteArray& line) { impl_->receive_line(line); }
void McpSession::disconnect_from_any_thread() { impl_->disconnect_from_any_thread(); }
void McpSession::shutdown() { impl_->shutdown(); }
}  // namespace patchy::ui
