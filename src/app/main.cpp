#include "ui/action_icons.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

namespace {

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
  app.setApplicationDisplayName(QStringLiteral("Patchy"));
  app.setOrganizationName(QStringLiteral("Seth A. Robinson"));
  app.setWindowIcon(patchy::ui::patchy_app_icon());
  app.setFont(application_font());
  patchy::ui::MainWindow window;
  window.show();
  patchy::ui::show_startup_splash();
  return app.exec();
}
