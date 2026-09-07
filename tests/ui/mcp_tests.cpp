#include "ui/main_window.hpp"
#include "ui/mcp_session.hpp"
#include "ui/script_engine.hpp"
#include "ui/mcp_activity.hpp"
#include "ui/canvas_widget.hpp"
#include "test_harness.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <chrono>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace patchy::test::ui;

template <typename Predicate> void until(Predicate ready) {
  QElapsedTimer deadline;
  deadline.start();
  while (!ready() && deadline.elapsed() < 10000) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(ready());
}

struct Connection {
  std::mutex mutex;
  std::vector<QJsonObject> replies;
  patchy::ui::McpSession session;
  int next_id{1};
  explicit Connection(patchy::ui::MainWindow& window)
      : session(window, true, [this](const QByteArray& line) {
          const std::lock_guard lock(mutex);
          replies.push_back(QJsonDocument::fromJson(line).object());
        }) { connect(); }
  void connect() {
    session.connect_from_any_thread();
    until([&] { return session.ready(); });
    const auto reply = take(send("initialize", {{"protocolVersion", "2025-11-25"},
        {"clientInfo", QJsonObject{{"name", "MCP test"}, {"version", "1"}}}}));
    CHECK(reply.contains("result"));
  }
  int send(const QString& method, const QJsonObject& params) {
    const int id = next_id++;
    session.receive_line(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id},
        {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact));
    return id;
  }
  QJsonObject take(int id) {
    QJsonObject result;
    until([&] {
      const std::lock_guard lock(mutex);
      for (auto it = replies.begin(); it != replies.end(); ++it) {
        if ((*it)["id"].toInt() == id) {
          result = *it;
          replies.erase(it);
          break;
        }
      }
      return !result.isEmpty();
    });
    return result;
  }
  QJsonObject call(const QString& name, const QJsonObject& args = {}, bool error = false) {
    const auto result = take(send("tools/call", {{"name", name}, {"arguments", args}}))["result"].toObject();
    CHECK(result["isError"].toBool() == error);
    return result;
  }
  QJsonObject state() { return call("get_state")["structuredContent"].toObject(); }
  QJsonObject edit(const QString& code) {
    return call("execute_script", {{"code", code}, {"expectedState", state()["stateToken"]}});
  }
  void disconnect() {
    session.disconnect_from_any_thread();
    until([&] { return session.closed(); });
  }
};

void local_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("Local edit");
  options.unattended = true;
  CHECK(host.run_source(source, options));
  until([&] { return !host.run_active(); });
  CHECK(!host.last_run_had_error());
}

void ui_mcp_attached_state_guard_and_unsaved_history() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  local_script(window, "var d=app.newDocument(16,16); d.addLayer('Face').fill('#bb8844');");
  Connection connection(window);
  const auto info = connection.call("get_info")["structuredContent"].toObject();
  CHECK(info["liveWindowAttachment"].toBool());
  CHECK(info["requiresExpectedState"].toBool());
  CHECK(info["processId"].toString() == QString::number(QCoreApplication::applicationPid()));
  auto state = connection.state();
  const auto doc_id = state["activeDocumentId"].toString();
  CHECK(state["documents"].toArray().size() == 1);
  CHECK(state["documents"].toArray()[0].toObject()["modified"].toBool());
  auto preview = connection.call("get_preview");
  CHECK(preview["structuredContent"].toObject()["stateToken"] == state["stateToken"]);
  CHECK(connection.state()["stateToken"] == state["stateToken"]);
  const auto pixels = preview["content"].toArray()[0].toObject()["data"].toString();
  const QString fix = "app.activeDocument.activeLayer.fill('#ffddaa');";
  const auto refused = connection.call("execute_script", {{"code", fix}}, true);
  CHECK(refused["structuredContent"].toObject()["error"] == "stale_state");
  CHECK(connection.state()["stateToken"] == state["stateToken"]);

  // A real edit through the GUI's ordinary host invalidates the agent's view,
  // including a pixel-only change on the same layer with the same bounds.
  local_script(window, "app.activeDocument.activeLayer.fill('#c09060');");
  const auto stale = connection.call("execute_script", {{"code", fix}, {"expectedState", state["stateToken"]}}, true);
  CHECK(stale["structuredContent"].toObject()["error"] == "stale_state");
  const auto before_fix = connection.call("get_preview")["content"].toArray()[0].toObject()["data"].toString();
  CHECK(before_fix != pixels);
  state = connection.state();
  const auto edited = connection.call("execute_script", {{"code", fix}, {"expectedState", state["stateToken"]}});
  CHECK(edited["structuredContent"].toObject()["status"] == "done");
  CHECK(!window.script_engine_host().connector_mode());
  const auto after = connection.state();
  CHECK(after["stateToken"] != state["stateToken"]);
  connection.call("undo", {{"documentId", doc_id}, {"expectedState", after["stateToken"]}});
  CHECK(connection.call("get_preview")["content"].toArray()[0].toObject()["data"] == before_fix);

  // Tab switches are guarded even when neither document's pixels change.
  state = connection.state();
  local_script(window, "app.newDocument(8,8);");
  connection.call("execute_script", {{"code", fix}, {"expectedState", state["stateToken"]}}, true);
  local_script(window, QStringLiteral("app.getDocument('%1').activate();").arg(doc_id));
  state = connection.state();
  local_script(window, "app.documents[1].activate();");
  connection.call("execute_script", {{"code", fix}, {"expectedState", state["stateToken"]}}, true);

  state = connection.state();
  connection.disconnect();
  CHECK(window.script_engine_host().session_ids().size() == 2);
  connection.connect();
  CHECK(connection.state()["stateToken"] != state["stateToken"]);
  connection.call("execute_script", {{"code", fix}, {"expectedState", state["stateToken"]}}, true);
  connection.disconnect();
}

void ui_mcp_activity_stop_input_lock_and_local_scripts() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  local_script(window, "app.newDocument(64,64).addLayer('Face').fill('#f6c58c');");
  Connection connection(window);
  auto* indicator = window.findChild<patchy::ui::McpActivity*>(QStringLiteral("mcpActivity"));
  auto* label = window.findChild<QLabel*>(QStringLiteral("mcpActivityLabel"));
  auto* stop = window.findChild<QPushButton*>(QStringLiteral("mcpStopButton"));
  CHECK(indicator && label && stop);
  CHECK(indicator->isVisible());
  CHECK(label->text() == QStringLiteral("AI connected"));
  CHECK(!stop->isVisible());
  save_widget_artifact("mcp_connected", window);
  const auto state = connection.state();
  bool saw_working = false;
  bool close_blocked = false;
  bool read_busy = false;
  std::exception_ptr observer_error;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!indicator->working()) { return; }
    observer.stop();
    try {
    saw_working = label->text().startsWith(QStringLiteral("AI editing:")) && stop->isVisible();
    QCloseEvent close;
    QApplication::sendEvent(&window, &close);
    close_blocked = !close.isAccepted();
    save_widget_artifact("mcp_editing", window);
    const auto request = connection.send("tools/call", {{"name", "get_state"}});
    // Busy is answered by the protocol reader immediately, with no nested wait.
    const auto result = connection.take(request)["result"].toObject();
    read_busy = result["isError"].toBool() && result["structuredContent"].toObject()["error"] == "busy";
    } catch (...) { observer_error = std::current_exception(); }
    stop->click();
  });
  observer.start(20);
  // A failed Stop implementation must fail this test, not leave a forever
  // loop that continually feeds the normal inactivity watchdog.
  std::jthread deadline([&](std::stop_token stop_token) {
    for (int i = 0; i < 100 && !stop_token.stop_requested(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!stop_token.stop_requested()) { window.script_engine_host().interrupt_from_any_thread(); }
  });
  const auto result = connection.call("execute_script", {
      {"code", "app.activeDocument.addLayer('Correction').fill('#ffd8a8'); while(true){console.log('working');}"},
      {"name", "Fix face"}, {"expectedState", state["stateToken"]}}, true);
  deadline.request_stop();
  if (observer_error) { std::rethrow_exception(observer_error); }
  CHECK(saw_working && close_blocked && read_busy);
  CHECK(result["structuredContent"].toObject()["status"] == "cancelled");
  CHECK(!indicator->working());
  CHECK(!stop->isVisible());
  CHECK(!window.script_engine_host().connector_mode());
  connection.call("undo", {{"documentId", state["activeDocumentId"]}, {"expectedState", connection.state()["stateToken"]}});
  CHECK(connection.state()["documents"].toArray()[0].toObject()["layers"].toArray().size() == 2);

  // An idle connected assistant must not change or stop a user's local script.
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.unattended = true;
  auto& host = window.script_engine_host();
  CHECK(host.run_source("setInterval(function(){},100);", options));
  CHECK(!host.connector_mode());
  connection.call("get_state", {}, true);
  connection.disconnect();
  CHECK(host.run_active());
  CHECK(!indicator->isVisible());
  host.stop_active_run();
  until([&] { return !host.run_active(); });
}

void ui_mcp_attached_cancellation_interrupts_tight_loop() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  const int id = connection.send("tools/call", {{"name", "execute_script"},
      {"arguments", QJsonObject{{"code", "app.newDocument(8,8); while(true){}"},
                                {"expectedState", connection.state()["stateToken"]}}}});
  std::jthread cancel([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    connection.session.receive_line(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"}, {"params", QJsonObject{{"requestId", id}}}}).toJson());
  });
  const auto response = connection.take(id);
  cancel.join();
  CHECK(response["result"].toObject()["isError"].toBool());
  CHECK(connection.state()["documents"].toArray().size() == 1);
  connection.disconnect();
  CHECK(window.script_engine_host().session_ids().size() == 1);
}
}  // namespace

std::vector<patchy::test::TestCase> mcp_tests() {
  return {{"ui_mcp_attached_state_guard_and_unsaved_history", ui_mcp_attached_state_guard_and_unsaved_history},
          {"ui_mcp_activity_stop_input_lock_and_local_scripts", ui_mcp_activity_stop_input_lock_and_local_scripts},
          {"ui_mcp_attached_cancellation_interrupts_tight_loop", ui_mcp_attached_cancellation_interrupts_tight_loop}};
}
