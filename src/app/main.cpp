#include "ui/action_icons.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

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
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Patchy"));
  // Keep the internal app identity for settings without letting Qt append " - Patchy" to every native window title.
  app.setApplicationDisplayName(QString());
  app.setOrganizationName(QStringLiteral("Seth A. Robinson"));
  app.setWindowIcon(patchy::ui::patchy_app_icon());
  load_bundled_fonts();
  app.setFont(application_font());
  patchy::ui::LocalizationManager::instance().load_saved_language();
  patchy::ui::MainWindow window;
  window.show();
  patchy::ui::show_startup_splash(&window, [&window](const auto& update) { window.show_update_available(update); });
  return app.exec();
}
