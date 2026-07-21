#include "readme_screenshot_test_support.hpp"

#include "ui_test_support.hpp"

namespace patchy::test::ui {

void show_readme_shot_window(patchy::ui::MainWindow& window) {
  window.resize(1600, 1000);
  window.show();
  QApplication::processEvents();
}

void close_untitled_start_tab(patchy::ui::MainWindow& window) {
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  if (tabs->count() > 1 && tabs->tabText(0).startsWith(QStringLiteral("Untitled"))) {
    CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
    QApplication::processEvents();
  }
}

void save_readme_shot(const std::string& name, const QImage& image) {
  ensure_artifact_dir();
  CHECK(!image.isNull());
  const auto path = QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string());
  CHECK(image.save(path));
}

void reset_readme_status_bar(patchy::ui::MainWindow& window) {
  window.statusBar()->showMessage(QStringLiteral("Ready"));
  QApplication::processEvents();
}

void draw_readme_overlay(QImage& base, const QImage& overlay, QPoint position) {
  QPainter painter(&base);
  const QRect target(position, overlay.size());
  for (int ring = 10; ring >= 1; --ring) {
    painter.fillRect(target.adjusted(-ring, -ring + 3, ring, ring + 3), QColor(0, 0, 0, 12));
  }
  painter.drawImage(position, overlay);
  painter.end();
}

void expand_layer_folder_row(QListWidget& layer_list, const QString& folder_name) {
  auto* folder_item = require_layer_item(layer_list, folder_name);
  auto* folder_widget = layer_list.itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  if (!disclosure->isChecked()) {
    disclosure->click();
    QApplication::processEvents();
    QApplication::processEvents();
  }
}

}  // namespace patchy::test::ui
