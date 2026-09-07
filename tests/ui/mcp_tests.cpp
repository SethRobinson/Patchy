#include "ui/main_window.hpp"
#include "ui/mcp_session.hpp"
#include "ui/script_engine.hpp"
#include "ui/mcp_activity.hpp"
#include "ui/canvas_widget.hpp"
#include "test_harness.hpp"
#include "ui_test_support.hpp"
#include "ui_test_access.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTimer>
#include <chrono>
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
          {"ui_script_visible_unattended_stop_and_resize_processing", ui_script_visible_unattended_stop_and_resize_processing},
          {"ui_mcp_visible_idle_save_prompts_and_window_close", ui_mcp_visible_idle_save_prompts_and_window_close},
          {"ui_mcp_hidden_workspace_keeps_unattended_policy", ui_mcp_hidden_workspace_keeps_unattended_policy}};
}
