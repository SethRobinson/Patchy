// Companion apps (docs/companions.md): several clients on the attach socket, pushed events,
// the window handle in get_info, and the Plugins > Companions menu.
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/companion_apps.hpp"
#include "ui/main_window.hpp"
#include "ui/mcp_attachment.hpp"
#include "ui/mcp_session.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QScopeGuard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMenu>
#include <QStatusBar>
#include <QUuid>

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

template <typename Predicate> bool wait_until(Predicate ready, int timeout_ms = 10000) {
  QElapsedTimer deadline;
  deadline.start();
  while (!ready() && deadline.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

// A raw JSON-RPC client on the attach socket, as a companion written in any language is.
struct SocketClient {
  QLocalSocket socket;
  QByteArray buffer;
  std::vector<QJsonObject> messages;
  int next_id{1};

  bool open(const QString& endpoint) {
    socket.connectToServer(endpoint);
    return socket.waitForConnected(2000);
  }
  void pump() {
    (void)socket.waitForReadyRead(1);
    buffer += socket.readAll();
    for (auto newline = buffer.indexOf('\n'); newline >= 0; newline = buffer.indexOf('\n')) {
      messages.push_back(QJsonDocument::fromJson(buffer.left(newline)).object());
      buffer.remove(0, newline + 1);
    }
  }
  int send(const QString& method, const QJsonObject& params) {
    const int id = next_id++;
    socket.write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}})
                     .toJson(QJsonDocument::Compact) +
                 '\n');
    socket.flush();
    return id;
  }
  // The first message matching `match`, removed from the inbox; empty after the timeout.
  template <typename Match> QJsonObject take(Match match, int timeout_ms = 10000) {
    QJsonObject found;
    (void)wait_until([&] {
      pump();
      for (auto it = messages.begin(); it != messages.end(); ++it) {
        if (match(*it)) {
          found = *it;
          messages.erase(it);
          return true;
        }
      }
      return false;
    }, timeout_ms);
    return found;
  }
  QJsonObject reply(int id, int timeout_ms = 10000) {
    return take([id](const QJsonObject& message) { return message["id"].toInt() == id; }, timeout_ms);
  }
  QJsonObject call(const QString& name, const QJsonObject& args = {}) {
    return reply(send("tools/call", {{"name", name}, {"arguments", args}}))["result"].toObject();
  }
  QJsonObject event(const QString& type, int timeout_ms = 10000) {
    return take([&type](const QJsonObject& message) {
      return message["method"] == "notifications/patchy/event" && message["params"].toObject()["type"] == type;
    }, timeout_ms)["params"].toObject();
  }
  bool initialize(const QString& name, const QJsonObject& experimental = {}, int timeout_ms = 10000) {
    QJsonObject params{{"protocolVersion", "2025-11-25"}, {"clientInfo", QJsonObject{{"name", name}, {"version", "1"}}}};
    if (!experimental.isEmpty()) { params["capabilities"] = QJsonObject{{"experimental", experimental}}; }
    return reply(send("initialize", params), timeout_ms).contains("result");
  }
  bool closed() {
    pump();
    return socket.state() != QLocalSocket::ConnectedState;
  }
};

// Short: on macOS and Linux the name becomes a socket path under TMPDIR, and sun_path holds
// about 104 bytes.
QString unique_endpoint() {
  return QStringLiteral("pct-") + QUuid::createUuid().toString(QUuid::Id128).left(10);
}

// Four clients share the window; a fifth is closed. Each has its own state token, a script
// from one makes the others wait with busy, and get_info reports the main window.
void ui_mcp_multi_client_attach_tokens_and_busy() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto endpoint = unique_endpoint();
  patchy::ui::McpAttachment attachment(window, endpoint);
  CHECK(attachment.error().isEmpty());
  std::vector<std::unique_ptr<SocketClient>> clients;
  for (int i = 0; i < 4; ++i) {
    clients.push_back(std::make_unique<SocketClient>());
    CHECK(clients.back()->open(endpoint));
    CHECK(clients.back()->initialize(QStringLiteral("Client %1").arg(i)));
  }
  SocketClient extra;
  CHECK(extra.open(endpoint));
  CHECK(wait_until([&] { return extra.closed(); }, 5000));

  auto& a = *clients[0];
  auto& b = *clients[1];
  const auto info = a.call("get_info")["structuredContent"].toObject();
  const auto main_window = info["mainWindow"].toObject();
  CHECK(main_window["geometry"].toObject()["width"].toInt() > 0);
  CHECK(info["capabilities"].toArray().contains(QStringLiteral("companionEvents")));
#ifdef Q_OS_WIN
  CHECK(!main_window["nativeId"].toString().isEmpty());
#endif
  const auto token_a = a.call("get_state")["structuredContent"].toObject()["stateToken"].toString();
  const auto token_b = b.call("get_state")["structuredContent"].toObject()["stateToken"].toString();
  CHECK(!token_a.isEmpty() && token_a != token_b);

  // A script with a pending timer keeps its run active; B's requests are refused, not queued.
  const int running = a.send("tools/call", {{"name", "execute_script"},
      {"arguments", QJsonObject{{"code", "setTimeout(function () { patchy.setResult('late'); }, 700);"},
                                {"expectedState", token_a}}}});
  CHECK(wait_until([&] { return window.script_engine_host().run_active(); }, 5000));
  const auto refused = b.call("get_state");
  CHECK(refused["isError"].toBool());
  CHECK(refused["structuredContent"].toObject()["error"] == "busy");
  const auto finished = a.reply(running)["result"].toObject();
  CHECK(!finished["isError"].toBool());
  CHECK(finished["structuredContent"].toObject()["result"] == "late");
  CHECK(!b.call("get_state")["isError"].toBool());

  // A client leaving frees its place once its session has closed on the UI thread; until
  // then a newcomer is refused like any fifth client, so it retries.
  clients.pop_back();
  bool reconnected = false;
  for (int attempt = 0; attempt < 40 && !reconnected; ++attempt) {
    SocketClient late;
    reconnected = late.open(endpoint) && late.initialize(QStringLiteral("Late client"), {}, 500) &&
                  !late.call("get_info")["isError"].toBool();
  }
  CHECK(reconnected);
}

// Only a client that opted in at initialize receives events: selection, state, active
// document and closed documents, plus companionCommand from the Companions menu.
void ui_mcp_events_reach_subscribed_companions() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto endpoint = unique_endpoint();
  patchy::ui::McpAttachment attachment(window, endpoint);
  SocketClient companion;
  SocketClient assistant;
  CHECK(companion.open(endpoint));
  CHECK(assistant.open(endpoint));
  CHECK(companion.initialize(QStringLiteral("OpenFill"),
                             {{"patchy/events", true}, {"patchy/companion", "io.example.fill"}}));
  CHECK(assistant.initialize(QStringLiteral("Assistant")));

  CHECK(run_script(window, QStringLiteral("app.activeDocument.selection.selectRect(10, 20, 30, 40);")));
  const auto selected = companion.event(QStringLiteral("selectionChanged"));
  const auto bounds = selected["bounds"].toObject();
  CHECK(bounds["x"].toInt() == 10 && bounds["y"].toInt() == 20 && bounds["width"].toInt() == 30 &&
        bounds["height"].toInt() == 40);
  CHECK(!selected["documentId"].toString().isEmpty());
  const auto changed = companion.event(QStringLiteral("stateChanged"));
  CHECK(!changed["stateToken"].toString().isEmpty());

  CHECK(run_script(window, QStringLiteral("app.activeDocument.selection.deselect();")));
  CHECK(companion.event(QStringLiteral("selectionChanged"))["bounds"].isNull());

  CHECK(run_script(window, QStringLiteral("app.newDocument(64, 64);")));
  const auto activated = companion.event(QStringLiteral("activeDocumentChanged"));
  CHECK(!activated["documentId"].toString().isEmpty());
  CHECK(run_script(window, QStringLiteral("app.activeDocument.close();")));
  CHECK(companion.event(QStringLiteral("documentClosed"))["documentId"] == activated["documentId"]);

  // The Companions menu signals the running companion instead of starting another one.
  auto* menu = window.findChild<patchy::ui::CompanionMenu*>();
  CHECK(menu != nullptr);
  int launches = 0;
  menu->set_launcher_for_testing([&](const QString&, const QStringList&, const QString&,
                                     const QProcessEnvironment&) { return ++launches > 0; });
  patchy::ui::CompanionManifest manifest;
  manifest.id = QStringLiteral("io.example.fill");
  manifest.name = QStringLiteral("Example Fill");
  manifest.command = QStringLiteral("example-fill");
  CHECK(menu->activate(manifest));
  const auto command = companion.event(QStringLiteral("companionCommand"));
  CHECK(command["companion"] == "io.example.fill" && command["action"] == "show");
  CHECK(launches == 0);

  // The assistant never opted in.
  assistant.pump();
  for (const auto& message : assistant.messages) { CHECK(message["method"] != "notifications/patchy/event"); }
}

// Manifests in the companions folder become menu entries; picking one with no companion
// attached starts its command with the connector endpoint in the environment.
void ui_companion_menu_lists_manifests_and_launches() {
  const StandardPathsTestMode standard_paths;
  const auto folder = patchy::ui::companions_folder_path();
  remove_test_scratch_dir(folder);
  CHECK(QDir().mkpath(folder + QStringLiteral("/bin")));
  const auto write = [&](const QString& name, const QByteArray& bytes) {
    QFile file(folder + '/' + name);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
  };
  write(QStringLiteral("bin/fill-companion.cmd"), "rem test\n");
  write(QStringLiteral("fill.json"),
        R"({"id": "io.example.fill", "name": "Example Fill", "menuLabel": "Fill & Expand",
            "command": "bin/fill-companion.cmd", "args": ["--panel", "a b"]})");
  write(QStringLiteral("broken.json"), R"({"id": "nodots", "name": "Broken", "command": "x"})");
  write(QStringLiteral("missing.json"),
        R"({"id": "io.example.missing", "name": "Missing", "command": "bin/not-there.cmd"})");

  QStringList problems;
  const auto manifests = patchy::ui::load_companion_manifests(&problems);
  CHECK(manifests.size() == 2);
  CHECK(problems.size() == 1 && problems.front().contains(QStringLiteral("broken.json")));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* companions = window.findChild<patchy::ui::CompanionMenu*>();
  CHECK(companions != nullptr);
  struct Launch {
    QString program;
    QStringList args;
    QString working_directory;
    QProcessEnvironment environment;
  };
  std::vector<Launch> launches;
  companions->set_launcher_for_testing([&](const QString& program, const QStringList& args,
                                           const QString& working_directory, const QProcessEnvironment& environment) {
    launches.push_back({program, args, working_directory, environment});
    return true;
  });
  emit companions->menu()->aboutToShow();
  auto* entry = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.fill"));
  CHECK(entry != nullptr);
  CHECK(entry->text() == QStringLiteral("Fill && Expand"));
  CHECK(companions->menu()->findChild<QAction*>(QStringLiteral("companionsNoneAction")) == nullptr);
  entry->trigger();
  CHECK(launches.size() == 1);
  CHECK(QFileInfo(launches[0].program) == QFileInfo(folder + QStringLiteral("/bin/fill-companion.cmd")));
  CHECK(launches[0].args == (QStringList{QStringLiteral("--panel"), QStringLiteral("a b")}));
  CHECK(QFileInfo(launches[0].working_directory) == QFileInfo(folder));
  CHECK(launches[0].environment.value(QStringLiteral("PATCHY_COMPANION_ID")) == QStringLiteral("io.example.fill"));
  CHECK(launches[0].environment.value(QStringLiteral("PATCHY_MCP_ENDPOINT")) == patchy::ui::mcp_attachment_endpoint());

  auto* missing = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.missing"));
  CHECK(missing != nullptr);
  missing->trigger();
  CHECK(launches.size() == 1);

  remove_test_scratch_dir(folder);
  emit companions->menu()->aboutToShow();
  CHECK(companions->menu()->findChild<QAction*>(QStringLiteral("companionsNoneAction")) != nullptr);
}

// A manifest's minProtocol/maxProtocol outside what this Patchy serves greys its entry out with
// the reason; malformed ranges are rejected; get_info and the initialize reply report the range,
// and a companion declaring an unserved range is told about in the status bar.
void ui_companion_protocol_range_is_checked() {
  const StandardPathsTestMode standard_paths;
  const auto folder = patchy::ui::companions_folder_path();
  remove_test_scratch_dir(folder);
  CHECK(QDir().mkpath(folder));
  const auto write = [&](const QString& name, const QByteArray& bytes) {
    QFile file(folder + '/' + name);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
  };
  const auto max = QByteArray::number(patchy::ui::kCompanionProtocolMax);
  write(QStringLiteral("current.json"), R"({"id": "io.example.current", "name": "Current", "command": "c.cmd",
                                            "minProtocol": 1, "maxProtocol": )" + max + "}");
  write(QStringLiteral("future.json"), R"({"id": "io.example.future", "name": "Future", "command": "f.cmd",
                                           "minProtocol": )" + QByteArray::number(patchy::ui::kCompanionProtocolMax + 1) + "}");
  write(QStringLiteral("bad.json"), R"({"id": "io.example.bad", "name": "Bad", "command": "b.cmd",
                                        "minProtocol": 3, "maxProtocol": 2})");
  write(QStringLiteral("fraction.json"), R"({"id": "io.example.fraction", "name": "Fraction", "command": "x.cmd",
                                             "minProtocol": 1.5})");
  QStringList problems;
  const auto manifests = patchy::ui::load_companion_manifests(&problems);
  CHECK(manifests.size() == 2);
  CHECK(problems.size() == 2);

  patchy::ui::MainWindow window;
  show_window(window);
  auto* companions = window.findChild<patchy::ui::CompanionMenu*>();
  CHECK(companions != nullptr);
  int launches = 0;
  companions->set_launcher_for_testing([&](const QString&, const QStringList&, const QString&,
                                           const QProcessEnvironment&) { return ++launches > 0; });
  emit companions->menu()->aboutToShow();
  auto* current = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.current"));
  auto* future = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.future"));
  CHECK(current != nullptr && current->isEnabled() && current->text() == QStringLiteral("Current"));
  CHECK(future != nullptr && !future->isEnabled() && future->text() == QStringLiteral("Future (needs a newer Patchy)"));
  CHECK(!companions->activate(manifests[0].id == QStringLiteral("io.example.future") ? manifests[0] : manifests[1]));
  CHECK(launches == 0);

  const auto endpoint = unique_endpoint();
  patchy::ui::McpAttachment attachment(window, endpoint);
  CHECK(attachment.error().isEmpty());
  SocketClient client;
  CHECK(client.open(endpoint));
  const int init = client.send("initialize", {{"protocolVersion", "2025-11-25"},
                                              {"clientInfo", QJsonObject{{"name", "Current"}, {"version", "1"}}},
                                              {"capabilities", QJsonObject{{"experimental", QJsonObject{{"patchy/companion",
                                                QJsonObject{{"id", "io.example.current"}, {"name", "Current"}, {"version", "0.3"},
                                                            {"minProtocol", 1}, {"maxProtocol", 9}}}}}}}});
  const auto reply = client.reply(init)["result"].toObject();
  const auto served = reply["capabilities"].toObject()["experimental"].toObject()["patchy/companion"].toObject();
  CHECK(served["minProtocol"].toInt() == patchy::ui::kCompanionProtocolMin);
  CHECK(served["maxProtocol"].toInt() == patchy::ui::kCompanionProtocolMax);
  const auto info = client.call("get_info")["structuredContent"].toObject()["companionProtocol"].toObject();
  CHECK(info["min"].toInt() == patchy::ui::kCompanionProtocolMin && info["max"].toInt() == patchy::ui::kCompanionProtocolMax);
  CHECK(patchy::ui::McpAttachment::companion_attached(QStringLiteral("io.example.current")));
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("Current")));

  SocketClient future_client;
  CHECK(future_client.open(endpoint));
  CHECK(future_client.initialize(QStringLiteral("Future"),
                                 {{"patchy/companion", QJsonObject{{"id", "io.example.future"}, {"name", "Future"},
                                                                   {"minProtocol", patchy::ui::kCompanionProtocolMax + 1}}}}));
  CHECK(wait_until([&] {
    return window.statusBar()->currentMessage() == QStringLiteral("Future needs a newer version of Patchy");
  }));
  remove_test_scratch_dir(folder);
}

}  // namespace

// get_state reports each layer's raster mask (bounds and settings), so a companion can find
// the layers it inserted; a layer without one reports null.
void ui_mcp_state_reports_layer_masks() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var masked = doc.addLayer('Masked');
    masked.fillRect(0, 0, 32, 32, '#ff0000');
    masked.setLayerMask({x: 4, y: 6, width: 8, height: 2, data: new Uint8Array(16).fill(255).buffer, density: 50,
                         feather: 2});
    doc.addLayer('Plain').fillRect(0, 0, 8, 8, '#0000ff');
  )JS")));
  const auto endpoint = unique_endpoint();
  patchy::ui::McpAttachment attachment(window, endpoint);
  CHECK(attachment.error().isEmpty());
  SocketClient client;
  CHECK(client.open(endpoint));
  CHECK(client.initialize(QStringLiteral("Mask reader")));
  const auto state = client.call("get_state")["structuredContent"].toObject();
  QJsonObject masked;
  QJsonObject plain;
  const auto active = state["activeDocumentId"].toString();
  for (const auto& document : state["documents"].toArray()) {
    if (document.toObject()["id"].toString() != active) { continue; }
    for (const auto& layer : document.toObject()["layers"].toArray()) {
      const auto object = layer.toObject();
      if (object["name"] == "Masked") { masked = object; }
      if (object["name"] == "Plain") { plain = object; }
    }
  }
  CHECK(masked["hasLayerMask"].toBool());
  const auto mask = masked["layerMask"].toObject();
  CHECK(mask["bounds"].toObject() == (QJsonObject{{"x", 4}, {"y", 6}, {"width", 8}, {"height", 2}}));
  CHECK(mask["enabled"].toBool() && mask["defaultColor"].toInt() == 0 && mask["feather"].toDouble() == 2.0);
  CHECK(std::abs(mask["density"].toDouble() - 50.0) < 0.5);
  CHECK(!plain.isEmpty() && !plain["hasLayerMask"].toBool() && plain["layerMask"].isNull());
}

// While a companion is attached its Quit entry appears and sends companionCommand "quit";
// a companion that is not attached has none.
void ui_companion_menu_quits_attached_companion() {
  const StandardPathsTestMode standard_paths;
  const auto folder = patchy::ui::companions_folder_path();
  remove_test_scratch_dir(folder);
  CHECK(QDir().mkpath(folder));
  const auto write = [&](const QString& name, const QByteArray& bytes) {
    QFile file(folder + '/' + name);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
  };
  write(QStringLiteral("fill.json"), R"({"id": "io.example.fill", "name": "Fill & Co", "command": "fill.cmd"})");
  write(QStringLiteral("other.json"), R"({"id": "io.example.other", "name": "Other", "command": "other.cmd"})");

  patchy::ui::MainWindow window;
  show_window(window);
  auto* companions = window.findChild<patchy::ui::CompanionMenu*>();
  CHECK(companions != nullptr);
  const auto quit_entry = [&](const QString& id) {
    emit companions->menu()->aboutToShow();
    return companions->menu()->findChild<QAction*>(QStringLiteral("companionQuitAction_") + id);
  };
  CHECK(quit_entry(QStringLiteral("io.example.fill")) == nullptr);

  const auto endpoint = unique_endpoint();
  patchy::ui::McpAttachment attachment(window, endpoint);
  CHECK(attachment.error().isEmpty());
  SocketClient companion;
  CHECK(companion.open(endpoint));
  CHECK(companion.initialize(QStringLiteral("Fill"), {{"patchy/companion", "io.example.fill"}}));
  CHECK(wait_until([&] { return patchy::ui::McpAttachment::companion_attached(QStringLiteral("io.example.fill")); }));
  CHECK(quit_entry(QStringLiteral("io.example.other")) == nullptr);
  auto* quit = quit_entry(QStringLiteral("io.example.fill"));
  CHECK(quit != nullptr && quit->text() == QStringLiteral("Quit Fill && Co"));
  quit->trigger();
  const auto command = companion.event(QStringLiteral("companionCommand"));
  CHECK(command["companion"] == "io.example.fill" && command["action"] == "quit");

  companion.socket.disconnectFromServer();
  CHECK(wait_until([&] { return !patchy::ui::McpAttachment::companion_attached(QStringLiteral("io.example.fill")); }));
  CHECK(quit_entry(QStringLiteral("io.example.fill")) == nullptr);

  // Inside a Flatpak the entry cannot start the companion: it says so until the companion attaches.
  companions->set_can_launch_for_testing(false);
  int launches = 0;
  companions->set_launcher_for_testing([&](const QString&, const QStringList&, const QString&,
                                           const QProcessEnvironment&) { return ++launches > 0; });
  emit companions->menu()->aboutToShow();
  auto* entry = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.fill"));
  CHECK(entry != nullptr && !entry->isEnabled() && entry->text() == QStringLiteral("Fill && Co (start it outside Patchy)"));
  CHECK(!companions->activate(patchy::ui::load_companion_manifests().front()));
  CHECK(launches == 0);
  SocketClient again;
  CHECK(again.open(endpoint));
  CHECK(again.initialize(QStringLiteral("Fill"), {{"patchy/companion", "io.example.fill"}}));
  CHECK(wait_until([&] { return patchy::ui::McpAttachment::companion_attached(QStringLiteral("io.example.fill")); }));
  emit companions->menu()->aboutToShow();
  entry = companions->menu()->findChild<QAction*>(QStringLiteral("companionAction_io.example.fill"));
  CHECK(entry != nullptr && entry->isEnabled() && entry->text() == QStringLiteral("Fill && Co"));
  CHECK(quit_entry(QStringLiteral("io.example.fill")) != nullptr);
  remove_test_scratch_dir(folder);
}

// Inside an AppImage a companion is handed the .AppImage file as both PATCHY_EXECUTABLE and
// PATCHY_MCP (run with the patchy-mcp argument), never paths in the per-launch mount.
void ui_companion_environment_names_the_appimage() {
  patchy::ui::CompanionManifest manifest;
  manifest.id = QStringLiteral("io.example.fill");
  const auto outside = patchy::ui::companion_environment(manifest);
  CHECK(outside.value(QStringLiteral("PATCHY_COMPANION_ID")) == manifest.id);
  CHECK(QFileInfo(outside.value(QStringLiteral("PATCHY_EXECUTABLE"))) == QFileInfo(QCoreApplication::applicationFilePath()));
#ifdef Q_OS_LINUX
  const auto saved_appimage = qgetenv("APPIMAGE");
  const auto saved_appdir = qgetenv("APPDIR");
  const auto restore = qScopeGuard([&] {
    saved_appimage.isNull() ? qunsetenv("APPIMAGE") : qputenv("APPIMAGE", saved_appimage);
    saved_appdir.isNull() ? qunsetenv("APPDIR") : qputenv("APPDIR", saved_appdir);
  });
  // A real AppImage runs usr/bin/patchy below its mount, so the mount is a parent folder.
  qputenv("APPDIR", QFileInfo(QCoreApplication::applicationDirPath()).absolutePath().toUtf8());
  qputenv("APPIMAGE", "/opt/apps/Patchy-x86_64.AppImage");
  const auto inside = patchy::ui::companion_environment(manifest);
  CHECK(inside.value(QStringLiteral("PATCHY_EXECUTABLE")) == QStringLiteral("/opt/apps/Patchy-x86_64.AppImage"));
  CHECK(inside.value(QStringLiteral("PATCHY_MCP")) == QStringLiteral("/opt/apps/Patchy-x86_64.AppImage"));
#endif
}

std::vector<patchy::test::TestCase> companion_tests() {
  return {
      {"ui_mcp_multi_client_attach_tokens_and_busy", ui_mcp_multi_client_attach_tokens_and_busy},
      {"ui_mcp_events_reach_subscribed_companions", ui_mcp_events_reach_subscribed_companions},
      {"ui_companion_menu_lists_manifests_and_launches", ui_companion_menu_lists_manifests_and_launches},
      {"ui_mcp_state_reports_layer_masks", ui_mcp_state_reports_layer_masks},
      {"ui_companion_menu_quits_attached_companion", ui_companion_menu_quits_attached_companion},
      {"ui_companion_protocol_range_is_checked", ui_companion_protocol_range_is_checked},
      {"ui_companion_environment_names_the_appimage", ui_companion_environment_names_the_appimage},
  };
}
