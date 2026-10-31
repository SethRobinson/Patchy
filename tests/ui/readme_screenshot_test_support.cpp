#include <QStatusBar>


#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QMetaObject>
#include <QPainter>
#include <QTabWidget>
#include <QToolButton>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QPoint>
#include <QString>
#include "readme_screenshot_test_support.hpp"
#include "ui_test_support.hpp"

namespace patchy::test::ui {

QImage rounded_readme_window_image(const QImage& window_image) {
  QImage rounded = window_image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QImage mask(rounded.size(), QImage::Format_ARGB32_Premultiplied);
  mask.fill(Qt::transparent);
  {
    QPainter mask_painter(&mask);
    mask_painter.setRenderHint(QPainter::Antialiasing);
    mask_painter.setPen(Qt::NoPen);
    mask_painter.setBrush(Qt::white);
    mask_painter.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(rounded.size())), kReadmeWindowCornerRadius,
                                 kReadmeWindowCornerRadius);
  }
  QPainter painter(&rounded);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  painter.drawImage(0, 0, mask);
  painter.end();
  return rounded;
}

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
  CHECK(rounded_readme_window_image(image).save(path));
}

void reset_readme_status_bar(patchy::ui::MainWindow& window) {
  window.statusBar()->showMessage(QStringLiteral("Ready"));
  QApplication::processEvents();
}

void draw_readme_overlay(QImage& base, const QImage& overlay, QPoint position) {
  QPainter painter(&base);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  const QRect target(position, overlay.size());
  for (int ring = 10; ring >= 1; --ring) {
    const QRectF halo(target.adjusted(-ring, -ring + 3, ring, ring + 3));
    painter.setBrush(QColor(0, 0, 0, 12));
    painter.drawRoundedRect(halo, kReadmeWindowCornerRadius + ring, kReadmeWindowCornerRadius + ring);
  }
  painter.drawImage(position, rounded_readme_window_image(overlay));
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

QImage readme_captioned_dialog(QWidget& dialog, const QIcon& icon) {
  constexpr int kCaption = 32;
  const auto content = dialog.grab().toImage();
  QImage framed(content.width(), content.height() + kCaption, QImage::Format_ARGB32_Premultiplied);
  const auto background = content.pixelColor(4, 4);
  framed.fill(background.darker(108));
  QPainter painter(&framed);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.drawImage(0, kCaption, content);
  painter.drawPixmap(10, (kCaption - 18) / 2, icon.pixmap(18, 18));
  QFont title(QStringLiteral("Segoe UI"));
  title.setPixelSize(15);
  painter.setFont(title);
  painter.setPen(QColor(232, 232, 232));
  painter.drawText(QRect(36, 0, framed.width() - 80, kCaption), Qt::AlignVCenter | Qt::AlignLeft,
                   dialog.windowTitle());
  painter.setPen(QPen(QColor(205, 205, 205), 1.3));
  const QPointF close(framed.width() - 22.0, kCaption / 2.0);
  painter.drawLine(close + QPointF(-5, -5), close + QPointF(5, 5));
  painter.drawLine(close + QPointF(-5, 5), close + QPointF(5, -5));
  painter.end();
  return framed;
}

}  // namespace patchy::test::ui
