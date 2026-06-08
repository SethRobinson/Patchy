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
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <algorithm>
#include <array>

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

  patchy::ui::MainWindow window;
  window.show();
  patchy::ui::show_startup_splash(&window, [&window](const auto& update) { window.show_update_available(update); });
  const auto files = parser.positionalArguments();
  if (!files.isEmpty()) {
    window.open_command_line_files(files);
  }
  return app.exec();
}
