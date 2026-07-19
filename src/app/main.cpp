#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/stress_test.hpp"

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
#include <QProxyStyle>
#include <QRect>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>

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

// Screenshot requests ride the single-instance file list as one reserved entry. Real entries are
// absolute file paths, which can never start with this prefix nor contain newlines, so the two
// kinds cannot collide. Fields are newline-separated: prefix, output path, widget name, region.
const QString kScreenshotCommandPrefix = QStringLiteral("patchy-cmd:screenshot\n");

QString encode_screenshot_command(const QString& output_path, const QString& widget_name, const QString& region) {
  return kScreenshotCommandPrefix + output_path + QLatin1Char('\n') + widget_name + QLatin1Char('\n') + region;
}

// Parses "x,y,w,h" (as taken by --screenshot-rect); anything else yields an invalid rect,
// which save_debug_screenshot treats as "the whole widget".
QRect parse_screenshot_rect(const QString& text) {
  const auto parts = text.split(QLatin1Char(','));
  if (parts.size() != 4) {
    return {};
  }
  std::array<int, 4> values{};
  for (int i = 0; i < 4; ++i) {
    bool ok = false;
    values[static_cast<size_t>(i)] = parts[i].trimmed().toInt(&ok);
    if (!ok) {
      return {};
    }
  }
  return QRect(values[0], values[1], values[2], values[3]);
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
  QCommandLineOption stress_option(
      QStringLiteral("stress-test"),
      QCoreApplication::translate(
          "QObject", "Run the profiling stress test and exit (preset: quick, small, standard, or huge)."),
      QStringLiteral("preset"), QString());
  parser.addOption(stress_option);
  QCommandLineOption stress_report_dir_option(
      QStringLiteral("stress-report-dir"),
      QCoreApplication::translate("QObject", "Directory for stress test reports (with --stress-test)."),
      QStringLiteral("dir"));
  parser.addOption(stress_report_dir_option);
  QCommandLineOption screenshot_option(
      QStringLiteral("screenshot"),
      QCoreApplication::translate(
          "QObject", "Save a PNG of the Patchy window to <path>. With a running instance this forwards "
                     "the request and exits; otherwise the new instance captures after startup and exits."),
      QStringLiteral("path"));
  parser.addOption(screenshot_option);
  QCommandLineOption screenshot_widget_option(
      QStringLiteral("screenshot-widget"),
      QCoreApplication::translate("QObject", "Limit --screenshot to the child widget with this Qt object name."),
      QStringLiteral("name"));
  parser.addOption(screenshot_widget_option);
  QCommandLineOption screenshot_rect_option(
      QStringLiteral("screenshot-rect"),
      QCoreApplication::translate("QObject", "Limit --screenshot to this region of the captured widget."),
      QStringLiteral("x,y,w,h"));
  parser.addOption(screenshot_rect_option);
  QCommandLineOption export_option(
      QStringLiteral("export"),
      QCoreApplication::translate(
          "QObject", "Open the given file, save it to <path> (format follows the extension), and exit. "
                     "Runs unattended: prompts are suppressed and no running instance is reused."),
      QStringLiteral("path"));
  parser.addOption(export_option);
  QCommandLineOption append_text_option(
      QStringLiteral("append-text"),
      QCoreApplication::translate(
          "QObject", "With --export: append this text to every text layer, re-rendering each through "
                     "Patchy's text engine, before saving."),
      QStringLiteral("text"));
  parser.addOption(append_text_option);
  // QCommandLineParser has no optional-value options, so let a bare
  // `--stress-test` mean the default (quick) preset.
  QStringList arguments = app.arguments();
  for (auto& argument : arguments) {
    if (argument == QStringLiteral("--stress-test")) {
      argument = QStringLiteral("--stress-test=quick");
    }
  }
  parser.process(arguments);

  const bool stress_mode = parser.isSet(stress_option);
  std::optional<patchy::ui::StressPreset> stress_preset;
  if (stress_mode) {
    stress_preset = patchy::ui::stress_preset_from_string(parser.value(stress_option));
    if (!stress_preset.has_value()) {
      // GUI subsystem on Windows has no console; the exit code is the signal.
      fprintf(stderr, "Unknown stress test preset '%s' (use quick, small, standard, or huge)\n",
              parser.value(stress_option).toUtf8().constData());
      return 2;
    }
  }

  // Resolve the requested files to absolute paths now: a forwarded request runs in the receiving
  // process, whose working directory differs from this launcher's.
  QStringList files;
  for (const auto& arg : parser.positionalArguments()) {
    if (!arg.isEmpty()) {
      files.append(QFileInfo(arg).absoluteFilePath());
    }
  }

  // A screenshot request travels with the files: to a running instance when one exists, otherwise
  // this instance captures itself once startup settles (see the singleShot below).
  const bool screenshot_mode = parser.isSet(screenshot_option);
  QString screenshot_path;
  if (screenshot_mode) {
    screenshot_path = QFileInfo(parser.value(screenshot_option)).absoluteFilePath();
  }
  const QString screenshot_widget = parser.value(screenshot_widget_option);
  const QString screenshot_rect_text = parser.value(screenshot_rect_option);

  const bool export_mode = parser.isSet(export_option);
  QString export_path;
  if (export_mode) {
    export_path = QFileInfo(parser.value(export_option)).absoluteFilePath();
  }
  const QString export_append_text = parser.value(append_text_option);

  // Single-instance: if another Patchy is already running, hand it the files and exit so a double-click
  // reuses the existing window instead of spawning a new process. An env override keeps multi-instance
  // launches (and tests) possible. A stress-test or export launch opts out entirely: forwarding would
  // silently drop the run into the other instance, and this instance must not squat on the user's pipe
  // either.
  const bool single_instance_enabled =
      !qEnvironmentVariableIsSet("PATCHY_NO_SINGLE_INSTANCE") && !stress_mode && !export_mode;
  QStringList forward_payload = files;
  if (screenshot_mode) {
    forward_payload.append(encode_screenshot_command(screenshot_path, screenshot_widget, screenshot_rect_text));
  }
  if (single_instance_enabled && forward_to_running_instance(forward_payload)) {
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
          // Peel screenshot commands off the file list. A capture must not raise or focus the
          // window (that would perturb the very state being captured), so a pure-screenshot
          // request skips activation; a bare relaunch (no files, no commands) still activates.
          QStringList forwarded_files;
          bool handled_screenshot = false;
          for (const auto& entry : forwarded) {
            if (entry.startsWith(kScreenshotCommandPrefix)) {
              const auto parts = entry.split(QLatin1Char('\n'));
              if (parts.size() == 4) {
                (void)window.save_debug_screenshot(parts[1], parts[2], parse_screenshot_rect(parts[3]));
              }
              handled_screenshot = true;
            } else {
              forwarded_files.append(entry);
            }
          }
          if (!forwarded_files.isEmpty() || !handled_screenshot) {
            window.activate_for_second_instance(forwarded_files);
          }
          client->deleteLater();
        });
      });
    }
  }

  window.show();
  if (stress_mode) {
    // No update check and no file opens: run the scripted scenario as soon
    // as the event loop starts, then exit with the report's status code.
    patchy::ui::StressTestOptions stress_options;
    stress_options.preset = *stress_preset;
    stress_options.report_dir = parser.value(stress_report_dir_option);
    window.start_cli_stress_test(stress_options);
    const int stress_result = app.exec();
    patchy::ui::wait_for_tracked_background_workers();
    return stress_result;
  }
  if (export_mode) {
    // Unattended convert/export: no update check, prompts suppressed, open the
    // files synchronously, then run the deferred export and exit with its status code.
    window.set_cli_automation_mode(true);
    window.open_command_line_files(files);
    window.run_cli_export(export_path, export_append_text);
    const int export_result = app.exec();
    patchy::ui::wait_for_tracked_background_workers();
    return export_result;
  }
  // No startup splash: the start panel carries the branding, and the update-check status
  // lands on its footer (an available update still raises the update dialog).
  window.begin_startup_update_check();
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

  if (screenshot_mode) {
    // Solo capture launch: no instance was running, so this one captures itself once startup
    // (command-line file opens) has settled, then exits. Best-effort timing — a
    // slow-loading file may need the running-instance flow instead.
    QTimer::singleShot(1500, &window, [&window, screenshot_path, screenshot_widget, screenshot_rect_text] {
      const bool saved = window.save_debug_screenshot(screenshot_path, screenshot_widget,
                                                      parse_screenshot_rect(screenshot_rect_text));
      QCoreApplication::exit(saved ? 0 : 3);
    });
  }

  const int exec_result = app.exec();
  // The window (declared after `app`) is destroyed before the application object; drop
  // the handler so a late event cannot reach a dead window.
  app.file_open_handler = nullptr;
  // Detached preview/render workers capture the QCoreApplication pointer;
  // wait for them before the window and application objects are destroyed.
  patchy::ui::wait_for_tracked_background_workers();
  return exec_result;
}
