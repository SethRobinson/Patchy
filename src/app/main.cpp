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
#include <QFileOpenEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProxyStyle>
#include <QStringList>

#include <algorithm>
#include <array>
#include <functional>
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
#ifdef Q_OS_MACOS
  // Inside a .app bundle the executable lives in Contents/MacOS; the bundled fonts are
  // staged in Contents/Resources/fonts.
  load_font_directory(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/fonts")));
#endif
#ifdef Q_OS_LINUX
  // Installed layout (Flatpak / prefix installs): binary in <prefix>/bin, fonts under
  // <prefix>/share/patchy/fonts.
  load_font_directory(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../share/patchy/fonts")));
#endif
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
  const auto name = QStringLiteral("Patchy-SingleInstance-") + user;
#ifdef Q_OS_LINUX
  // Inside Flatpak, QLocalServer's default socket location is the per-sandbox /tmp, so a
  // second `flatpak run` would never find the first instance's socket. $XDG_RUNTIME_DIR/
  // app/<app-id>/ is shared across sandboxes of the same app id; use an absolute socket
  // path there (QLocalServer treats a path-shaped name as the literal socket path).
  if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
    const auto runtime_dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime_dir.isEmpty()) {
      return runtime_dir + QStringLiteral("/app/") + qEnvironmentVariable("FLATPAK_ID") +
             QStringLiteral("/") + name;
    }
  }
#endif
  return name;
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

// Wraps the platform style to override a couple of interaction hints, leaving
// all other rendering (including the app stylesheet) unchanged:
// - Sliders: a left-click anywhere on the groove jumps straight to that value
//   and begins dragging, instead of Qt's default page-step that crawls toward
//   the cursor in chunks.
// - Tool-button flyouts (DelayedPopup, e.g. the marquee/shape buttons in the
//   tool palette): open after a short Photoshop-like hold instead of Qt's much
//   longer default.
class InteractionHintsStyle : public QProxyStyle {
 public:
  using QProxyStyle::QProxyStyle;

  int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                QStyleHintReturn* return_data) const override {
    if (hint == SH_Slider_AbsoluteSetButtons) {
      return Qt::LeftButton;
    }
    if (hint == SH_ToolButton_PopupDelay) {
      return 300;  // ms; roughly Photoshop's press-and-hold flyout delay
    }
#ifdef Q_OS_MACOS
    // Form layouts consult the style: QMacStyle keeps fields at their size hint
    // and right-aligns labels (Aqua HIG), which shrinks line edits and combos to
    // slivers in dialogs designed around the roomy Windows form behavior (Brush
    // Tips manager, Layer Style pages). Pin the Windows behavior everywhere.
    if (hint == SH_FormLayoutFieldGrowthPolicy) {
      return QFormLayout::AllNonFixedFieldsGrow;
    }
    if (hint == SH_FormLayoutLabelAlignment) {
      return Qt::AlignLeft | Qt::AlignVCenter;
    }
#endif
    return QProxyStyle::styleHint(hint, option, widget, return_data);
  }
};

// macOS delivers Finder opens (double-clicked documents, dock drops) to the RUNNING
// process as QFileOpenEvent via LaunchServices -- they never arrive as argv. Files that
// arrive before the main window exists are buffered and merged into the command-line
// batch. Harmless on other platforms (the event simply never fires for files there).
class PatchyApplication : public QApplication {
 public:
  using QApplication::QApplication;

  std::function<void(const QString&)> file_open_handler;
  QStringList pending_file_opens;

 protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::FileOpen) {
      if (const auto path = static_cast<QFileOpenEvent*>(event)->file(); !path.isEmpty()) {
        if (file_open_handler) {
          file_open_handler(path);
        } else {
          pending_file_opens.append(path);
        }
      }
      return true;
    }
    return QApplication::event(event);
  }
};

QFont application_font() {
#ifdef Q_OS_WIN
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
#else
  // macOS/Linux: the platform's default UI font at its native size (San Francisco 13pt
  // on macOS; the fontconfig default on Linux). Forcing 9pt reads tiny there.
  return QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  apply_gui_scale_factor();
  PatchyApplication app(argc, argv);
#ifdef Q_OS_LINUX
  // Lets Wayland compositors match the window to its .desktop entry (taskbar icon,
  // pinning); must match packaging/linux/com.rtsoft.patchy.desktop.
  QGuiApplication::setDesktopFileName(QStringLiteral("com.rtsoft.patchy"));
#endif
  // Slider grooves snap to the click; tool flyouts open on a short hold (see the style above).
  app.setStyle(new InteractionHintsStyle);
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
  // Finder opens reuse the second-launch path (raise the window, open the file); any
  // that arrived before the window existed join the command-line batch below.
  app.file_open_handler = [&window](const QString& path) {
    window.activate_for_second_instance({path});
  };
  files += app.pending_file_opens;
  app.pending_file_opens.clear();

  if (!files.isEmpty()) {
    window.open_command_line_files(files);
  }

  const int exec_result = app.exec();
  // The window (declared after `app`) is destroyed before the application object; drop
  // the handler so a late event cannot reach a dead window.
  app.file_open_handler = nullptr;
  return exec_result;
}
