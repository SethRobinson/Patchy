#include "ui/main_window.hpp"

#include <QApplication>
#include <QFont>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setFont(QFont(QStringLiteral("Segoe UI"), 9));
  photoslop::ui::MainWindow window;
  window.show();
  return app.exec();
}
