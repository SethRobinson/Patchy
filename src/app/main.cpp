#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include <QFont>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QStringList>

#include <algorithm>
#include <array>
#include <memory>

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

namespace {

void load_font_directory(const QDir& directory) {
  if (!directory.exists()) {
    return;
  }

  for (const auto& entry : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    load_font_directory(QDir(entry.absoluteFilePath()));
  }
  const QStringList filters = {QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc")};
  for (const auto& file : directory.entryInfoList(filters, QDir::Files)) {
    (void)QFontDatabase::addApplicationFont(file.absoluteFilePath());
  }
}

void load_bundled_fonts() {
  load_font_directory(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/fonts")));
}

// Apply the saved interface scale through Qt's QT_SCALE_FACTOR. This must run before the
// QApplication is constructed because Qt only reads the variable at construction time. An
// existing environment override (e.g. from tests/CI) is left untouched.
void apply_gui_scale_factor() {
  if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
    return;
  }
  constexpr std::array<int, 5> allowed_percents{100, 125, 150, 175, 200};
  const int stored = patchy::ui::app_settings().value(QStringLiteral("preferences/guiScalePercent"), 100).toInt();
  const int percent = std::clamp(stored, allowed_percents.front(), allowed_percents.back());
  if (percent == 100) {
    return;
  }
  qputenv("QT_SCALE_FACTOR", QByteArray::number(percent / 100.0));
}

// Per-user name for the single-instance local socket. Scoping it to the user keeps separate Windows
// login sessions from fighting over one pipe on a shared machine.
QString single_instance_server_name() {
  QString user = qEnvironmentVariable("USERNAME");
  if (user.isEmpty()) {
    user = qEnvironmentVariable("USER");
  }
  return QStringLiteral("Patchy-SingleInstance-") + user;
}

// Try to hand the file list to an already-running Patchy. Returns true if a running instance accepted
// the request (in which case this process should exit without opening its own window).
bool forward_to_running_instance(const QStringList& files) {
  QLocalSocket socket;
  socket.connectToServer(single_instance_server_name());
  if (!socket.waitForConnected(300)) {
    return false;
  }
  QByteArray payload;
  QDataStream stream(&payload, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_5_15);
  stream << files;
  socket.write(payload);
  socket.flush();
  socket.waitForBytesWritten(1000);
  socket.disconnectFromServer();
  if (socket.state() != QLocalSocket::UnconnectedState) {
    socket.waitForDisconnected(1000);
  }
  return true;
}

QFont application_font() {
  const QStringList font_files = {
      QStringLiteral("C:/Windows/Fonts/arial.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbd.ttf"),
      QStringLiteral("C:/Windows/Fonts/ariali.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbi.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuii.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuiz.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibri.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrib.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrii.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibriz.ttf"),
  };
  QString preferred_family;
  for (const auto& path : font_files) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    const auto font_id = QFontDatabase::addApplicationFont(path);
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.contains(QStringLiteral("Arial"))) {
      preferred_family = QStringLiteral("Arial");
    } else if (preferred_family.isEmpty() && !families.isEmpty()) {
      preferred_family = families.front();
    }
  }
  if (!preferred_family.isEmpty()) {
    QFont font(preferred_family);
    font.setPointSize(9);
    return font;
  }

  auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPointSize(9);
  return font;
}

}  // namespace

int main(int argc, char* argv[]) {
  apply_gui_scale_factor();
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Patchy"));
  app.setApplicationVersion(QStringLiteral(PATCHY_VERSION));
  // Keep the internal app identity for settings without letting Qt append " - Patchy" to every native window title.
  app.setApplicationDisplayName(QString());
  app.setOrganizationName(QStringLiteral("Seth A. Robinson"));
  app.setWindowIcon(patchy::ui::patchy_app_icon());
  load_bundled_fonts();
  app.setFont(application_font());
  patchy::ui::LocalizationManager::instance().load_saved_language();

  // Parse command-line arguments after translations load so option descriptions are localized.
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("QObject", "Patchy raster image editor."));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("files"),
                               QCoreApplication::translate("QObject", "Image or Photoshop files to open."),
                               QStringLiteral("[files...]"));
  parser.process(app);

  // Resolve the requested files to absolute paths now: a forwarded request runs in the receiving
  // process, whose working directory differs from this launcher's.
  QStringList files;
  for (const auto& arg : parser.positionalArguments()) {
    if (!arg.isEmpty()) {
      files.append(QFileInfo(arg).absoluteFilePath());
    }
  }

  // Single-instance: if another Patchy is already running, hand it the files and exit so a double-click
  // reuses the existing window instead of spawning a new process. An env override keeps multi-instance
  // launches (and tests) possible.
  const bool single_instance_enabled = !qEnvironmentVariableIsSet("PATCHY_NO_SINGLE_INSTANCE");
  if (single_instance_enabled && forward_to_running_instance(files)) {
    return 0;
  }

  patchy::ui::MainWindow window;

  // Become the primary instance: listen for future launches and adopt the files they forward.
  QLocalServer single_instance_server;
  if (single_instance_enabled) {
    // A previous crash can leave a stale pipe/socket that blocks listen(); clear it first.
    QLocalServer::removeServer(single_instance_server_name());
    if (single_instance_server.listen(single_instance_server_name())) {
      QObject::connect(&single_instance_server, &QLocalServer::newConnection, &window, [&single_instance_server, &window] {
        QLocalSocket* client = single_instance_server.nextPendingConnection();
        if (client == nullptr) {
          return;
        }
        // Accumulate until the sender disconnects, then decode the whole payload in one shot so a
        // chunked write can't be parsed half-read.
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(client, &QLocalSocket::readyRead, client, [client, buffer] { buffer->append(client->readAll()); });
        QObject::connect(client, &QLocalSocket::disconnected, &window, [client, buffer, &window] {
          buffer->append(client->readAll());
          QStringList forwarded;
          QDataStream stream(buffer.get(), QIODevice::ReadOnly);
          stream.setVersion(QDataStream::Qt_5_15);
          stream >> forwarded;
          window.activate_for_second_instance(forwarded);
          client->deleteLater();
        });
      });
    }
  }

  window.show();
  // Guard the splash-closed callback: if the app quits before the splash auto-closes, the window is
  // torn down first and the QPointer goes null, so we skip touching a destroyed window.
  QPointer<patchy::ui::MainWindow> window_guard(&window);
  patchy::ui::show_startup_splash(
      &window, [&window](const auto& update) { window.show_update_available(update); },
      [window_guard] {
        if (window_guard != nullptr) {
          window_guard->refresh_native_frame_after_overlay();
        }
      });
  if (!files.isEmpty()) {
    window.open_command_line_files(files);
  }
  return app.exec();
}
