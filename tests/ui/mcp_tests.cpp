#include "ui/main_window.hpp"
#include "ui/mcp_session.hpp"
#include "ui/script_engine.hpp"
#include "ui/mcp_activity.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/zoom_status_bar.hpp"
#include "test_harness.hpp"
#include "ui_test_support.hpp"
#include "ui_test_access.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QStatusBar>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
#include <set>

namespace {
using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

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
  explicit Connection(patchy::ui::MainWindow& window, bool attached = true)
      : session(window, attached, [this](const QByteArray& line) {
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
  QJsonObject request(const QString& name, const QJsonObject& args = {}) {
    return take(send("tools/call", {{"name", name}, {"arguments", args}}))["result"].toObject();
  }
  QJsonObject call(const QString& name, const QJsonObject& args = {}, bool error = false) {
    const auto result = request(name, args);
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
  CHECK(stop->isVisible() && !stop->isEnabled());
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
  CHECK(stop->isVisible() && !stop->isEnabled());
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
void ui_mcp_vector_discovery_revisions_and_previews() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  const auto info = connection.call("get_info")["structuredContent"].toObject();
  CHECK(info["capabilities"].toArray().contains("vectorShapes"));
  connection.edit(R"JS(
    var d=app.newDocument(64,64);
    var s=d.addShape('Face',{type:'ellipse',x:8,y:8,width:48,height:48},{fill:'#ffaa77'});
    d.setWorkPath(s.getShape().path);
    d.groupLayers([s],'Masked face').setVectorMask({path:s.getShape().path});
    d.activeLayer=s;
  )JS");
  auto state = connection.state();
  auto doc = state["documents"].toArray()[0].toObject();
  CHECK(doc["paths"].toArray().size() == 1);
  CHECK(!doc["workPathId"].toString().isEmpty());
  const auto group = doc["layers"].toArray().last().toObject();
  const auto shape = group["children"].toArray()[0].toObject();
  CHECK(shape["isShape"].toBool() && shape["vectorEditable"].toBool());
  CHECK(group["hasVectorMask"].toBool());
  const auto before = connection.call("get_preview")["content"].toArray()[0].toObject()["data"].toString();
  connection.edit("var d=app.activeDocument; d.activeLayer.getShape(); d.listVectorResources(); d.workPath.getPath();");
  CHECK(connection.state()["stateToken"] == state["stateToken"]);
  local_script(window, "app.activeDocument.workPath.activate();");
  connection.call("execute_script", {{"code", "app.activeDocument.activeLayer.updateShape({fill:'#bb6611'});"},
                                    {"expectedState", state["stateToken"]}}, true);
  state = connection.state();
  CHECK(state["documents"].toArray()[0].toObject()["vectorTarget"].toObject()["kind"] == "path");
  connection.edit("var s=app.activeDocument.activeLayer; var p=s.getShape().path; p.subpaths[0].anchors[0].x+=8; s.updateShape({path:p,fill:'#bb6611'});");
  CHECK(connection.state()["stateToken"] != state["stateToken"]);
  const auto after = connection.call("get_preview")["content"].toArray()[0].toObject()["data"].toString();
  CHECK(before != after);
  connection.call("undo", {{"documentId", doc["id"]}, {"expectedState", connection.state()["stateToken"]}});
  CHECK(connection.call("get_preview")["content"].toArray()[0].toObject()["data"] == before);
  connection.call("redo", {{"documentId", doc["id"]}, {"expectedState", connection.state()["stateToken"]}});
  CHECK(connection.call("get_preview")["content"].toArray()[0].toObject()["data"] == after);
  connection.disconnect();
}

class ScriptPaintObserver final : public QObject {
 public:
  explicit ScriptPaintObserver(patchy::ui::ScriptEngineHost& host) : host_(host) {}
  std::set<int> colors;
 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::Paint && host_.run_active()) {
      const auto* doc = host_.session_document_const(host_.active_session_id());
      const auto* layer = doc && doc->active_layer_id() ? doc->find_layer(*doc->active_layer_id()) : nullptr;
      if (layer && !layer->pixels().empty()) { colors.insert(layer->pixels().pixel(0, 0)[0]); }
    }
    return false;
  }
 private:
  patchy::ui::ScriptEngineHost& host_;
};

void ui_mcp_progressive_edits_and_present_keep_history() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(64,64).addLayer('Ink').fill('#ffffff');");
  auto& host = window.script_engine_host();
  auto* canvas = require_canvas(window);
  ScriptPaintObserver observer(host);
  canvas->installEventFilter(&observer);
  connection.edit(R"JS(
    var l=app.activeDocument.activeLayer, end=Date.now()+350, i=0;
    while(Date.now()<end){l.fill(i++%2?'#220000':'#dd0000');}
    l.fill('#440000'); patchy.ui.present(30);
    l.fill('#660000'); patchy.ui.present(30);
  )JS");
  CHECK(observer.colors.count(0x44) && observer.colors.count(0x66));
  CHECK(observer.colors.count(0x22) || observer.colors.count(0xdd));
  const auto state=connection.state();
  connection.call("undo", {{"documentId", state["activeDocumentId"]}, {"expectedState", state["stateToken"]}});
  const auto* restored = host.session_document_const(host.active_session_id());
  CHECK(restored->find_layer(*restored->active_layer_id())->pixels().pixel(0,0)[0] == 255);
  auto* stop = window.findChild<QPushButton*>(QStringLiteral("mcpStopButton"));
  CHECK(stop && stop->isVisible() && !stop->isEnabled());
  const QJsonValue before_read = connection.state()["stateToken"];
  connection.edit(R"JS(
    patchy.ui.present();
    for (var delay of [-1, 1001, 0.5, NaN, Infinity, '30', null]) {
      var rejected = false;
      try { patchy.ui.present(delay); } catch (e) { rejected = true; }
      if (!rejected) throw Error('invalid presentation delay accepted');
    }
  )JS");
  CHECK(connection.state()["stateToken"] == before_read);
  connection.disconnect();
}

void ui_script_visible_unattended_stop_and_resize_processing() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  local_script(window, "app.newDocument(32,32).addLayer('Ink').fill('#aabbcc');");
  auto& host = window.script_engine_host();
  bool saw_stop = false;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    auto* stop = window.findChild<QPushButton*>(QStringLiteral("scriptStopButton"));
    if (host.run_active() && stop && stop->isVisible() && stop->isEnabled()) {
      saw_stop = true; observer.stop();
      window.grab().save(QStringLiteral("test-artifacts/script_visible_stop.png"));
      stop->click();
    }
  });
  observer.start(10);
  patchy::ui::ScriptEngineHost::RunOptions options; options.unattended = true;
  host.run_source("app.activeDocument.activeLayer.fill('#dd0000'); patchy.ui.present(500);", options);
  until([&] {return !host.run_active();});
  CHECK(saw_stop && host.last_run_had_error());
  CHECK(window.menuBar()->isEnabled());
  CHECK(!window.findChild<QWidget*>(QStringLiteral("scriptActivity"))->isVisible());
  local_script(window, "app.activeDocument.undo();");
  auto* canvas = require_canvas(window);
  EnvironmentVariableRestorer delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", "0");
  const auto before = canvas->render_cache_diagnostics();
  bool saw_processing = false;
  bool resize_locked = false;
  QTimer processing_observer;
  QObject::connect(&processing_observer, &QTimer::timeout, &window, [&] {
    if (canvas->processing_overlay_visible()) {
      saw_processing = true;
      resize_locked = !host.automation_ready();
      processing_observer.stop();
      window.grab().save(QStringLiteral("test-artifacts/image_resize_processing.png"));
    }
  });
  processing_observer.start(10);
  // Real 100 MP resampling, the size of the reported freeze. No synthetic wait.
  accept_image_size_dialog(10000,10000);
  require_action(window, "imageSizeAction")->trigger();
  const auto after = canvas->render_cache_diagnostics();
  CHECK(after.processing_overlays_shown > before.processing_overlays_shown);
  CHECK(saw_processing && resize_locked);
  CHECK(!canvas->processing_overlay_visible() && !canvas->processing_operation_active());
  CHECK(host.session_document_const(host.active_session_id())->width() == 10000);
  local_script(window, "app.activeDocument.undo();");
  CHECK(host.session_document_const(host.active_session_id())->width() == 32);
  bool stopped_resize = false;
  QTimer cancel_resize;
  QObject::connect(&cancel_resize, &QTimer::timeout, &window, [&] {
    if (host.run_active() && canvas->processing_operation_active()) {
      stopped_resize = true;
      cancel_resize.stop();
      host.stop_active_run();
    }
  });
  cancel_resize.start(1);
  host.run_source("app.activeDocument.resizeImage(4096,4096);", options);
  until([&] {return !host.run_active();});
  CHECK(stopped_resize && host.last_run_had_error());
  CHECK(host.session_document_const(host.active_session_id())->width() == 32);
  CHECK(!canvas->processing_operation_active() && host.automation_ready());
  local_script(window, "app.activeDocument.resizeImage(96,64);");
  CHECK(host.session_document_const(host.active_session_id())->width() == 96);
  EnvironmentVariableRestorer busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  bool saw_interactive_stop = false;
  QTimer interactive_stop;
  QObject::connect(&interactive_stop, &QTimer::timeout, &window, [&] {
    const auto* panel = window.findChild<QWidget*>(QStringLiteral("scriptStopPanel"));
    if (panel && panel->isVisible()) {
      saw_interactive_stop = true;
      interactive_stop.stop();
      host.stop_active_run();
    }
  });
  interactive_stop.start(10);
  options.unattended = false;
  host.run_source("patchy.ui.present(500);", options);
  until([&] {return !host.run_active();});
  CHECK(saw_interactive_stop && host.last_run_had_error());
}

void ui_mcp_slow_steps_history_and_stop() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(64,64).addLayer('Ink').fill('#ffffff');");
  auto& host = window.script_engine_host();
  auto* slow = window.findChild<QPushButton*>(QStringLiteral("mcpSlowButton"));
  CHECK(slow && slow->isVisible() && slow->isEnabled() && !slow->isChecked());
  slow->click();
  CHECK(host.slow_mode() && connection.state()["slowMode"].toBool());
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  auto* canvas = require_canvas(window);
  ScriptPaintObserver observer(host);
  canvas->installEventFilter(&observer);
  connection.edit(R"JS(
    var l=app.activeDocument.activeLayer;
    l.drawStrokes(['#220000','#440000','#660000'].map(function(color){
      return {size:16,color:color,points:[{x:0,y:0}]};
    }));
    l.opacity=50;
    app.activeDocument.addShape('Eye',{type:'ellipse',x:32,y:32,width:8,height:8});
  )JS");
  CHECK(observer.colors.count(0x22) && observer.colors.count(0x44) && observer.colors.count(0x66));
  // A property edit posts both pixel and structure dirt but is still one step.
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 5);
  window.grab().save(QStringLiteral("test-artifacts/mcp_slow_mode.png"));
  const auto undo = [&] { connection.edit("app.activeDocument.undo();"); };
  undo();
  connection.edit("if(app.activeDocument.layers.some(function(l){return l.isShape;}))throw Error('shape survived');");
  undo();
  connection.edit("if(app.activeDocument.activeLayer.opacity!==100)throw Error('opacity');");
  const auto red = [&] {
    const auto* doc = host.session_document_const(host.active_session_id());
    return doc->find_layer(*doc->active_layer_id())->pixels().pixel(0,0)[0];
  };
  undo(); CHECK(red() == 0x44);
  undo(); CHECK(red() == 0x22);
  undo(); CHECK(red() == 255);
  connection.edit("for(var i=0;i<5;i++)app.activeDocument.redo();");
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 5);
  connection.edit("app.activeDocument.undo(); app.activeDocument.undo();");

  const QJsonValue before_invalid = connection.state()["stateToken"];
  connection.call("execute_script", {{"expectedState", before_invalid}, {"code",
    "app.activeDocument.activeLayer.drawStrokes([{points:[{x:1,y:1}]},{size:-1,points:[{x:2,y:2}]}]);"}}, true);
  CHECK(connection.state()["stateToken"] == before_invalid);

  // The guarded UI must allow switching Slow during a request. Subsequent
  // normal edits share a new group; previously displayed steps stay separate.
  const auto before_toggle = MainWindowTestAccess::active_session_undo_depth(window);
  bool toggled = false;
  QTimer toggle;
  QObject::connect(&toggle, &QTimer::timeout, &window, [&] {
    if (host.run_active() && red() == 0x88) {
      toggled = true; toggle.stop(); slow->click();
    }
  });
  toggle.start(1);
  connection.edit("var l=app.activeDocument.activeLayer;l.fill('#880000');l.fill('#990000');l.fill('#aa0000');");
  toggle.stop();
  CHECK(toggled && !slow->isChecked());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before_toggle + 2);
  undo(); CHECK(red() == 0x88);
  undo(); CHECK(red() == 0x66);

  slow->click();
  const auto before_stop = MainWindowTestAccess::active_session_undo_depth(window);
  bool stopped = false;
  QTimer stop_timer;
  QObject::connect(&stop_timer, &QTimer::timeout, &window, [&] {
    if (host.run_active() && red() == 0xcc) {
      stopped = true; stop_timer.stop();
      window.findChild<QPushButton*>(QStringLiteral("mcpStopButton"))->click();
    }
  });
  stop_timer.start(1);
  connection.call("execute_script", {{"expectedState", connection.state()["stateToken"]}, {"code",
    "var l=app.activeDocument.activeLayer;l.fill('#cc0000');l.fill('#dd0000');"}}, true);
  stop_timer.stop();
  CHECK(stopped && red() == 0xcc && window.menuBar()->isEnabled());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before_stop + 1);
  undo(); CHECK(red() == 0x66);
  connection.disconnect(); connection.connect();
  CHECK(slow->isChecked() && connection.state()["slowMode"].toBool());
  local_script(window, "if(!patchy.ui.slowMode)throw Error('shared Slow'); patchy.ui.slowMode=false;");
  CHECK(!slow->isChecked());
  connection.disconnect();
}

void ui_mcp_pause_resume_navigation_and_history() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(256,256).addLayer('Ink').fill('#ffffff');");
  auto& host = window.script_engine_host();
  auto* canvas = require_canvas(window);
  auto* pause = window.findChild<QPushButton*>(QStringLiteral("mcpPauseButton"));
  auto* label = window.findChild<QLabel*>(QStringLiteral("mcpActivityLabel"));
  auto* zoom = window.findChild<patchy::ui::ZoomPercentEdit*>();
  CHECK(pause && label && zoom && pause->isVisible() && !pause->isEnabled());
  const auto red = [&] {
    const auto* doc = host.session_document_const(host.active_session_id());
    return doc->find_layer(*doc->active_layer_id())->pixels().pixel(0,0)[0];
  };
  const auto mouse = [](QWidget* target, QEvent::Type type, QPoint position,
                        Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(position), QPointF(target->mapToGlobal(position)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
  };
  const auto navigate = [&] {
    const auto* doc = host.session_document_const(host.active_session_id());
    const auto* layer = doc->find_layer(*doc->active_layer_id());
    const auto pixel_revision = layer->pixel_revision();
    zoom->setText(QStringLiteral("400%")); zoom->setModified(true);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(zoom, &enter);
    CHECK(std::abs(canvas->zoom() - 4.0) < 0.001);
    canvas->set_wheel_zooms(true);
    const auto center = canvas->rect().center();
    QWheelEvent wheel(QPointF(center), QPointF(canvas->mapToGlobal(center)), QPoint(), QPoint(0,120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel);
    CHECK(canvas->zoom() > 4.0);
    const auto before_pan = canvas->widget_position_for_document_point(QPoint(128,128));
    mouse(canvas, QEvent::MouseButtonPress, center, Qt::MiddleButton, Qt::MiddleButton);
    mouse(canvas, QEvent::MouseMove, center + QPoint(30,20), Qt::NoButton, Qt::MiddleButton);
    mouse(canvas, QEvent::MouseButtonRelease, center + QPoint(30,20), Qt::MiddleButton, Qt::NoButton);
    CHECK(canvas->widget_position_for_document_point(QPoint(128,128)) != before_pan);
    if (!host.manual_edit_pause()) {
      mouse(canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
      mouse(canvas, QEvent::MouseMove, center + QPoint(10,10), Qt::NoButton, Qt::LeftButton);
      mouse(canvas, QEvent::MouseButtonRelease, center + QPoint(10,10), Qt::LeftButton, Qt::NoButton);
    }
    CHECK(layer->pixel_revision() == pixel_revision);
    CHECK(!canvas->pointer_gesture_active());
    CHECK(window.menuBar()->isEnabled());
    if (window.findChild<QWidget*>(QStringLiteral("windowMinimizeButton"))) {
      auto* bar = window.menuBar();
      const QPoint title(bar->width()-180, bar->height()/2);
      CHECK(!bar->actionAt(title));
      const auto before_move = window.pos();
      mouse(bar, QEvent::MouseButtonPress, title, Qt::LeftButton, Qt::LeftButton);
      mouse(bar, QEvent::MouseMove, title + QPoint(14,8), Qt::NoButton, Qt::LeftButton);
      mouse(bar, QEvent::MouseButtonRelease, title, Qt::LeftButton, Qt::NoButton);
      CHECK(window.pos() != before_move);
    }
  };
  EnvironmentVariableRestorer timeout("PATCHY_SCRIPT_TIMEOUT_MS");
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", "300");
  for (const bool slow : {false, true}) {
    host.set_slow_mode(slow);
    connection.edit("app.activeDocument.activeLayer.fill('#ffffff');");
    const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
    int phase = 0;
    std::exception_ptr error;
    QElapsedTimer hold;
    QTimer observer;
    QObject::connect(&observer, &QTimer::timeout, &window, [&] {
      try {
        if (phase == 0 && host.run_active() && red() == 0x22) {
          navigate(); // Normal and Slow playback both allow navigation.
          CHECK(pause->isEnabled()); pause->click();
          CHECK(host.paused());
          hold.start(); phase = 1;
        } else if (phase == 1) {
          CHECK(host.paused() && red() == 0x22);
          if (hold.elapsed() >= 650) {
            CHECK(host.manual_edit_pause() && pause->text() == QStringLiteral("Resume"));
            CHECK(label->text().startsWith(QStringLiteral("AI paused:")));
            navigate(); // Paused navigation must leave the document untouched.
            save_widget_artifact("mcp_paused", window);
            pause->click(); CHECK(!host.paused()); phase = 2; observer.stop();
          }
        }
      } catch (...) { error = std::current_exception(); observer.stop(); host.stop_active_run(); }
    });
    observer.start(15);
    std::jthread deadline([&](std::stop_token token) {
      for (int i=0;i<100 && !token.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!token.stop_requested()) host.interrupt_from_any_thread();
    });
    const auto result = connection.call("execute_script", {{"expectedState", connection.state()["stateToken"]},
      {"code", "var l=app.activeDocument.activeLayer;l.fill('#220000');patchy.ui.present(80);"
               "l.fill('#440000');patchy.ui.present(80);l.fill('#660000');"}}, false);
    deadline.request_stop(); observer.stop();
    if (error) std::rethrow_exception(error);
    CHECK(result["structuredContent"].toObject()["status"] == "done");
    CHECK(phase == 2 && red() == 0x66 && !host.paused());
    CHECK(!pause->isChecked() && !pause->isEnabled());
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + (slow ? 3 : 1));
  }
  connection.disconnect();
}

void ui_mcp_pause_stop_disconnect_and_cli_cleanup() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(32,32).addLayer('Ink');");
  auto& host = window.script_engine_host();
  std::jthread deadline([&](std::stop_token token) {
    for (int i=0;i<200 && !token.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!token.stop_requested()) host.interrupt_from_any_thread();
  });
  for (const bool disconnect : {false, true}) {
    bool saw_pause = false;
    QTimer stop;
    QObject::connect(&stop, &QTimer::timeout, &window, [&] {
      if (!host.paused()) return;
      saw_pause = true; stop.stop();
      if (disconnect) connection.session.disconnect_from_any_thread();
      else window.findChild<QPushButton*>(QStringLiteral("mcpStopButton"))->click();
    });
    stop.start(15);
    const auto id = connection.send("tools/call", {{"name", "execute_script"},
      {"arguments", QJsonObject{{"expectedState", connection.state()["stateToken"]},
        {"code", "app.activeDocument.activeLayer.fill('#223344');patchy.ui.paused=true;patchy.ui.present();"
                 "app.activeDocument.addLayer('Must not run');"}}}});
    if (disconnect) until([&] {return connection.session.closed();});
    else CHECK(connection.take(id)["result"].toObject()["isError"].toBool());
    CHECK(saw_pause && !host.paused() && !host.run_active());
    CHECK(host.session_document_const(host.active_session_id())->layers().size() == 2);
  }
  connection.connect();
  CHECK(!connection.state()["paused"].toBool());
  connection.disconnect();
  bool saw_cli_pause = false;
  QTimer resume;
  QObject::connect(&resume, &QTimer::timeout, &window, [&] {
    if (!host.paused()) return;
    auto* button = window.findChild<QPushButton*>(QStringLiteral("scriptPauseButton"));
    saw_cli_pause = button && button->isEnabled();
    resume.stop(); host.set_paused(false);
  });
  resume.start(15);
  local_script(window, "patchy.ui.paused=true;patchy.ui.present();app.activeDocument.activeLayer.fill('#778899');");
  CHECK(saw_cli_pause && !host.paused());
  EnvironmentVariableRestorer headless("PATCHY_HEADLESS");
  qputenv("PATCHY_HEADLESS", "1");
  patchy::ui::ScriptEngineHost::RunOptions options; options.unattended = true;
  host.run_source("patchy.ui.paused=true;", options);
  until([&]{return !host.run_active();});
  CHECK(host.last_run_had_error() && !host.paused());
}

void ui_mcp_pause_preserves_timed_brush_pixels() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(96,96).addLayer('Airbrush');");
  auto& host = window.script_engine_host();
  const QString paint = "app.activeDocument.activeLayer.drawStrokes([{size:24,softness:30,flow:12,"
      "color:'#786040',airbrush:true,points:[{x:20,y:20,timeMs:0},"
      "{x:20,y:20,timeMs:500},{x:70,y:60,timeMs:1000}]}]);";
  connection.edit(paint);
  const QJsonValue pixels = connection.call("get_preview")["content"].toArray()[0].toObject()["data"];
  connection.edit("app.activeDocument.undo();");
  connection.edit(paint);
  CHECK(connection.call("get_preview")["content"].toArray()[0].toObject()["data"] == pixels);
  connection.edit("app.activeDocument.undo();");
  bool paused = false, resumed = false;
  const auto observer = QObject::connect(&host, &patchy::ui::ScriptEngineHost::painting_progress,
                                        &window, [&](const QString&) {
    if (paused) return;
    paused = true; host.set_paused(true);
    QTimer::singleShot(80, &window, [&] {
      auto* canvas = require_canvas(window);
      QFocusEvent focus(QEvent::FocusOut, Qt::MouseFocusReason);
      QApplication::sendEvent(canvas, &focus);
      canvas->set_zoom_centered(3);
      resumed = host.paused(); host.set_paused(false);
    });
  });
  std::jthread deadline([&](std::stop_token token) {
    for (int i=0;i<100 && !token.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!token.stop_requested()) host.interrupt_from_any_thread();
  });
  connection.edit(paint);
  QObject::disconnect(observer);
  CHECK(paused && resumed && !host.paused());
  const QJsonValue after = connection.call("get_preview")["content"].toArray()[0].toObject()["data"];
  if (after != pixels) {
    QImage::fromData(QByteArray::fromBase64(pixels.toString().toLatin1())).save("test-artifacts/paused-brush-before.png");
    QImage::fromData(QByteArray::fromBase64(after.toString().toLatin1())).save("test-artifacts/paused-brush-after.png");
  }
  CHECK(after == pixels);
  connection.disconnect();
}

void ui_mcp_pause_manual_paint_has_separate_history() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(96,96).addLayer('Ink').fill('#ffffff');");
  auto& host = window.script_engine_host();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor("#00ff00"));
  canvas->set_brush_size(9); canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100); canvas->set_brush_softness(0);
  canvas->set_brush_smoothing(0); canvas->set_brush_build_up(false);
  const auto pixel = [&] {
    const auto& doc = std::as_const(MainWindowTestAccess::document(window));
    const auto* p = doc.find_layer(*doc.active_layer_id())->pixels().pixel(40,40);
    return QColor(p[0],p[1],p[2],p[3]);
  };
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  bool edited = false;
  std::exception_ptr error;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!host.manual_edit_pause()) return;
    observer.stop();
    try {
      const auto point = canvas->widget_position_for_document_point(QPoint(40,40));
      send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
      CHECK(canvas->pointer_gesture_active());
      host.set_paused(false);
      CHECK(host.paused()); // Resume must not interrupt an unfinished manual gesture.
      send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
      CHECK(!canvas->pointer_gesture_active() && pixel().green() > 240);
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 2);
      edited = true;
      host.set_paused(false);
    } catch (...) { error = std::current_exception(); host.stop_active_run(); }
  });
  observer.start(10);
  const auto result = connection.request("execute_script", {{"expectedState", connection.state()["stateToken"]},
    {"code", "var l=app.activeDocument.activeLayer;l.fill('#220000');patchy.ui.paused=true;"
             "patchy.ui.present();l.fill('#660000');"}});
  observer.stop(); if (error) std::rethrow_exception(error);
  CHECK(edited && result["structuredContent"].toObject()["status"] == "done");
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 3);
  connection.edit("app.activeDocument.undo();"); CHECK(pixel().green() > 240);
  connection.edit("app.activeDocument.undo();"); CHECK(pixel().red() == 0x22 && pixel().green() == 0);
  connection.edit("app.activeDocument.undo();"); CHECK(pixel() == QColor(Qt::white));
  connection.disconnect();
}

void ui_mcp_pause_revalidates_deleted_stroke_target() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(96,96).addLayer('Delete me');");
  auto& host = window.script_engine_host();
  bool requested = false, deleted = false;
  std::exception_ptr error;
  const auto progress = QObject::connect(&host, &patchy::ui::ScriptEngineHost::painting_progress,
                                        &window, [&](const QString&) {
    if (!requested) { requested = true; host.set_paused(true); }
  });
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!host.manual_edit_pause()) return;
    observer.stop();
    try {
      CHECK(!require_canvas(window)->pointer_gesture_active());
      require_action(window, "layerDeleteAction")->trigger();
      CHECK(MainWindowTestAccess::document(window).layers().size() == 1);
      deleted = true; host.set_paused(false);
    } catch (...) { error = std::current_exception(); host.stop_active_run(); }
  });
  observer.start(10);
  const auto result = connection.request("execute_script", {{"expectedState", connection.state()["stateToken"]},
    {"code", "app.activeDocument.activeLayer.drawStrokes(["
             "{size:12,points:[{x:10,y:10},{x:80,y:80}]},"
             "{size:12,points:[{x:80,y:10},{x:10,y:80}]}]);"}});
  observer.stop(); QObject::disconnect(progress);
  if (error) std::rethrow_exception(error);
  CHECK(requested && deleted && result["isError"].toBool());
  CHECK(!host.run_active() && !host.paused());
  connection.edit("app.activeDocument.addLayer('Still usable').fill('#abcdef');");
  CHECK(MainWindowTestAccess::document(window).layers().size() == 2);
  connection.disconnect();
}

void ui_mcp_running_browsing_and_conflict_feedback() {
  EnvironmentVariableRestorer timeout("PATCHY_SCRIPT_TIMEOUT_MS");
  // Leave time for synchronous dialog construction/QSS polish, which precedes
  // the nested event loop. The browsing hold still exceeds this timeout.
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", "2000");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("var d=app.newDocument(96,96);for(var i=0;i<30;i++)d.addLayer('Row '+i);");
  auto& host = window.script_engine_host();
  bool browsed = false;
  std::exception_ptr error;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!host.run_active()) return;
    observer.stop();
    try {
      auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(list && list->verticalScrollBar()->maximum() > 0);
      list->verticalScrollBar()->setValue(0);
      QWheelEvent wheel(QPointF(40,40), QPointF(list->viewport()->mapToGlobal(QPoint(40,40))),
                        QPoint(), QPoint(0,-120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QApplication::sendEvent(list->viewport(), &wheel);
      CHECK(list->verticalScrollBar()->value() > 0);
      list->verticalScrollBar()->setValue(0);
      auto* row_label = list->findChild<QLabel*>(QStringLiteral("layerRowName"));
      CHECK(row_label);
      QApplication::sendEvent(row_label, &wheel);
      CHECK(list->verticalScrollBar()->value() > 0);
      auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
      CHECK(filter);
      QKeyEvent letter(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, "R");
      QApplication::sendEvent(filter, &letter); CHECK(filter->text() == "R");
      filter->clear();
      QMenu menu(&window);
      auto* deletion = require_action(window, "layerDeleteAction");
      menu.addAction(deletion); menu.setActiveAction(deletion);
      QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QApplication::sendEvent(&menu, &enter);
      CHECK(MainWindowTestAccess::document(window).layers().size() == 31);
      CHECK(window.statusBar()->currentMessage().contains("Pause automation"));
      const auto inspect_dialog = [&](QAction* action, const QString& name) {
        bool seen = false;
        std::exception_ptr dialog_error;
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, &window, [&] {
          if (auto* dialog = find_top_level_dialog(name)) {
            seen = true; closer.stop();
            try { if (name == QStringLiteral("patchyPreferencesDialog")) {
              auto* tabs = dialog->findChild<QTabWidget*>();
              CHECK(tabs && tabs->count() > 1); tabs->setCurrentIndex(1);
              QKeyEvent apply(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
              QApplication::sendEvent(dialog, &apply);
              CHECK(dialog->isVisible());
              CHECK(window.statusBar()->currentMessage().contains("Close Preferences"));
            }} catch (...) { dialog_error = std::current_exception(); }
            dialog->reject();
          }
        });
        closer.start(2400); // Browsing longer than the JS inactivity timeout is safe.
        menu.addAction(action); menu.setActiveAction(action);
        QApplication::sendEvent(&menu, &enter);
        closer.stop(); if (dialog_error) std::rethrow_exception(dialog_error); CHECK(seen);
      };
      inspect_dialog(require_action(window, "filePreferencesAction"), QStringLiteral("patchyPreferencesDialog"));
      QAction* about = nullptr;
      for (auto* action : window.findChildren<QAction*>())
        if (action->menuRole() == QAction::AboutRole) { about = action; break; }
      CHECK(about); inspect_dialog(about, QStringLiteral("patchySplashScreen"));
      browsed = true;
    } catch (...) { error = std::current_exception(); host.stop_active_run(); }
  });
  observer.start(10);
  const auto result = connection.request("execute_script", {{"expectedState", connection.state()["stateToken"]},
    {"code", "patchy.ui.present(200);app.activeDocument.activeLayer.fill('#123456');"}});
  observer.stop(); if (error) std::rethrow_exception(error);
  if (result["isError"].toBool()) throw std::runtime_error(QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString());
  CHECK(browsed && !host.run_active());
  connection.disconnect();
}

void ui_mcp_pause_can_close_document_and_resume_async_safely() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(32,32).addLayer('Ink').fill('#123456');");
  auto& host = window.script_engine_host();
  bool closed = false, prompted = false;
  std::exception_ptr error;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!host.manual_edit_pause()) return;
    observer.stop();
    try {
      QTimer answer;
      QObject::connect(&answer, &QTimer::timeout, &window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")))) {
          prompted = true; answer.stop(); box->done(QMessageBox::No);
        }
      });
      answer.start(10);
      require_action(window, "fileCloseAction")->trigger();
      answer.stop();
      closed = host.session_ids().empty();
      host.set_paused(false);
    } catch (...) { error = std::current_exception(); host.stop_active_run(); }
  });
  observer.start(10);
  const auto result = connection.request("execute_script", {{"expectedState", connection.state()["stateToken"]},
    {"code", "var old=app.activeDocument;setTimeout(function(){old.addLayer('Gone');},40);"
             "patchy.ui.paused=true;"}});
  observer.stop(); if (error) std::rethrow_exception(error);
  CHECK(closed && prompted && result["isError"].toBool() && !host.paused());
  connection.edit("app.newDocument(16,16).addShape('Still editable',"
                  "{type:'ellipse',x:2,y:2,width:10,height:10});");
  CHECK(host.session_ids().size() == 1);
  connection.disconnect();
}

void ui_mcp_pause_manual_move_preserves_native_shape_on_resume() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(96,96).addShape('Movable',"
                  "{type:'rectangle',x:10,y:10,width:30,height:30});");
  auto& host = window.script_engine_host();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_auto_select_layer(false); canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  canvas->set_zoom(2);
  bool moved = false;
  std::exception_ptr error;
  QTimer observer;
  QObject::connect(&observer, &QTimer::timeout, &window, [&] {
    if (!host.manual_edit_pause()) return;
    observer.stop();
    try {
      const auto start = canvas->widget_position_for_document_point(QPoint(25,25));
      const auto end = canvas->widget_position_for_document_point(QPoint(37,33));
      send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
      const auto& doc = std::as_const(MainWindowTestAccess::document(window));
      const auto* layer = doc.find_layer(*doc.active_layer_id());
      CHECK(layer->bounds().x == 22 && layer->bounds().y == 18);
      moved = true; host.set_paused(false);
    } catch (...) { error = std::current_exception(); host.stop_active_run(); }
  });
  observer.start(10);
  const auto result = connection.request("execute_script", {{"expectedState", connection.state()["stateToken"]},
    {"code", "var s=app.activeDocument.activeLayer;patchy.ui.paused=true;patchy.ui.present();"
                  "s.updateShape({fill:{type:'solid',color:'#123456'}});"
                  "if(s.x!==22||s.y!==18||!s.isShape)throw Error('manual move lost');"}});
  observer.stop(); if (error) std::rethrow_exception(error);
  CHECK(moved && !result["isError"].toBool() && !host.paused());
  connection.disconnect();
}

void ui_mcp_layer_rows_stay_bounded_during_long_script() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  Connection connection(window);
  connection.edit("app.newDocument(96,96);");
  auto& host = window.script_engine_host();
  bool bounded = true;
  int observations = 0;
  const auto observer = QObject::connect(&host, &patchy::ui::ScriptEngineHost::message_emitted,
                                       &window, [&](int, const QString& text) {
    if (!text.startsWith(QStringLiteral("rows:"))) return;
    const auto rows = window.findChildren<QWidget*>(QStringLiteral("layerRowWidget")).size();
    const auto* doc = host.session_document_const(host.active_session_id());
    bounded = bounded && rows <= static_cast<qsizetype>(doc->layers().size()) + 2;
    ++observations;
  });
  connection.edit("for(var i=0;i<32;i++){app.activeDocument.addShape('Detail '+i,"
                  "{type:'rectangle',x:i,y:i,width:8,height:8});patchy.ui.present();console.log('rows:'+i);}");
  QObject::disconnect(observer);
  CHECK(observations == 32 && bounded);
  connection.disconnect();
}

void ui_mcp_visible_idle_save_prompts_and_window_close() {
  const bool previous_quit = qApp->quitOnLastWindowClosed();
  const auto restore_quit = qScopeGuard([previous_quit] { qApp->setQuitOnLastWindowClosed(previous_quit); });
  patchy::ui::MainWindow window;
  patchy::ui::configure_owned_mcp_workspace(window, true);
  show_window_empty(window);
  Connection connection(window, false);
  CHECK(qApp->quitOnLastWindowClosed());
  CHECK(!window.unattended_automation());
  const auto path = QFileInfo(QStringLiteral("test-artifacts/mcp-visible-close.psd")).absoluteFilePath();
  const auto path_json = QString::fromUtf8(QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact)) + "[0]";
  connection.edit("var d=app.newDocument(24,24); d.addLayer('Painting').fill('#112233');"
                  "if(!d.saveAs(" + path_json + ")) throw Error('save');");
  connection.edit("app.activeDocument.activeLayer.fill('#445566');");
  auto& host = window.script_engine_host();
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs && tabs->count() == 1);

  // Requests remain unattended, even though idle manual actions are interactive.
  bool request_unattended = false;
  QTimer request_observer;
  QObject::connect(&request_observer, &QTimer::timeout, &window, [&] {
    if (host.run_active()) { request_unattended = window.unattended_automation(); }
  });
  request_observer.start(5);
  connection.edit("patchy.ui.present(60);");
  request_observer.stop();
  CHECK(request_unattended && !window.unattended_automation());

  const auto answer_close = [&](QMessageBox::StandardButton answer, bool whole_window) {
    bool seen = false;
    bool busy_during_prompt = false;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, &window, [&] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
      if (!box) { return; }
      seen = true;
      busy_during_prompt = !host.automation_ready();
      dismiss.stop();
      if (!whole_window && answer == QMessageBox::Cancel) {
        box->grab().save(QStringLiteral("test-artifacts/mcp_visible_save_prompt.png"));
      }
      if (auto* button = box->button(answer)) { button->click(); }
      else { box->reject(); }
    });
    dismiss.start(5);
    if (whole_window) { window.close(); }
    else { CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0))); }
    dismiss.stop();
    CHECK(seen && busy_during_prompt);
  };

  // The reported tab-X failure: Cancel preserves work, Discard closes it.
  const auto before_cancel = connection.state();
  answer_close(QMessageBox::Cancel, false);
  CHECK(tabs->count() == 1 && window.isVisible());
  CHECK(connection.state()["stateToken"] == before_cancel["stateToken"]);
  answer_close(QMessageBox::No, false);
  CHECK(host.session_ids().empty());
  connection.edit("app.open(" + path_json + ");");
  const auto active_red = [&] {
    const auto* doc = host.session_document_const(host.active_session_id());
    return doc->find_layer(*doc->active_layer_id())->pixels().pixel(0,0)[0];
  };
  CHECK(active_red() == 0x11); // Discard did not overwrite the saved file.
  connection.edit("app.activeDocument.activeLayer.fill('#667788');");
  answer_close(QMessageBox::Yes, false);
  CHECK(host.session_ids().empty());
  connection.edit("app.open(" + path_json + ");");
  CHECK(active_red() == 0x66); // Save persisted the edit before closing.
  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  CHECK(host.session_ids().empty()); // Unchanged documents still close directly.

  connection.edit("app.newDocument(24,24).addLayer('Unsaved').fill('#8899aa');");
  answer_close(QMessageBox::Cancel, true);
  CHECK(window.isVisible() && host.session_ids().size() == 1);
  answer_close(QMessageBox::No, true);
  CHECK(!window.isVisible());
  connection.disconnect();
}

void ui_mcp_hidden_workspace_keeps_unattended_policy() {
  const bool previous_quit = qApp->quitOnLastWindowClosed();
  const auto restore_quit = qScopeGuard([previous_quit] { qApp->setQuitOnLastWindowClosed(previous_quit); });
  patchy::ui::MainWindow window;
  patchy::ui::configure_owned_mcp_workspace(window, false);
  show_window_empty(window);
  Connection connection(window, false);
  CHECK(window.unattended_automation() && !qApp->quitOnLastWindowClosed());
  connection.edit("app.newDocument(24,24).addLayer('Unsaved').fill('#112233');");
  require_action(window, "fileCloseAction")->trigger();
  CHECK(window.script_engine_host().session_ids().size() == 1);
  CHECK(!find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
  connection.edit("app.activeDocument.close();");
  CHECK(window.script_engine_host().session_ids().empty());
  connection.disconnect();
}
}  // namespace

std::vector<patchy::test::TestCase> mcp_tests() {
  return {{"ui_mcp_attached_state_guard_and_unsaved_history", ui_mcp_attached_state_guard_and_unsaved_history},
          {"ui_mcp_activity_stop_input_lock_and_local_scripts", ui_mcp_activity_stop_input_lock_and_local_scripts},
          {"ui_mcp_attached_cancellation_interrupts_tight_loop", ui_mcp_attached_cancellation_interrupts_tight_loop},
          {"ui_mcp_vector_discovery_revisions_and_previews", ui_mcp_vector_discovery_revisions_and_previews},
          {"ui_mcp_progressive_edits_and_present_keep_history", ui_mcp_progressive_edits_and_present_keep_history},
          {"ui_mcp_slow_steps_history_and_stop", ui_mcp_slow_steps_history_and_stop},
          {"ui_mcp_pause_resume_navigation_and_history", ui_mcp_pause_resume_navigation_and_history},
          {"ui_mcp_pause_stop_disconnect_and_cli_cleanup", ui_mcp_pause_stop_disconnect_and_cli_cleanup},
          {"ui_mcp_pause_preserves_timed_brush_pixels", ui_mcp_pause_preserves_timed_brush_pixels},
          {"ui_mcp_pause_manual_paint_has_separate_history", ui_mcp_pause_manual_paint_has_separate_history},
          {"ui_mcp_pause_revalidates_deleted_stroke_target", ui_mcp_pause_revalidates_deleted_stroke_target},
          {"ui_mcp_running_browsing_and_conflict_feedback", ui_mcp_running_browsing_and_conflict_feedback},
          {"ui_mcp_pause_can_close_document_and_resume_async_safely", ui_mcp_pause_can_close_document_and_resume_async_safely},
          {"ui_mcp_pause_manual_move_preserves_native_shape_on_resume", ui_mcp_pause_manual_move_preserves_native_shape_on_resume},
          {"ui_mcp_layer_rows_stay_bounded_during_long_script", ui_mcp_layer_rows_stay_bounded_during_long_script},
          {"ui_script_visible_unattended_stop_and_resize_processing", ui_script_visible_unattended_stop_and_resize_processing},
          {"ui_mcp_visible_idle_save_prompts_and_window_close", ui_mcp_visible_idle_save_prompts_and_window_close},
          {"ui_mcp_hidden_workspace_keeps_unattended_policy", ui_mcp_hidden_workspace_keeps_unattended_policy}};
}
