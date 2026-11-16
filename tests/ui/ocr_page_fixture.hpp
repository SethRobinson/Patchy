#pragma once

// A drawn page for the text recognition tests (docs/ocr.md, "Testing"): the tests draw text
// themselves and hand the scripted fake engine the box of each drawn line, so every
// expectation is against known pixels and no test depends on a real engine.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "test_harness.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace patchy::test::ui {

inline QFont ocr_test_font(int pixel_size, bool italic = false) {
  QFont font(QStringLiteral("Arial"));
  font.setPixelSize(pixel_size);
  font.setItalic(italic);
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

// The bounding box of every pixel in `area` that differs from `background`.
inline QRect ocr_ink_box(const QImage& image, QRect area, QColor background) {
  area = area.intersected(image.rect());
  int left = area.right() + 1;
  int right = -1;
  int top = area.bottom() + 1;
  int bottom = -1;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (std::abs(pixel.red() - background.red()) + std::abs(pixel.green() - background.green()) +
              std::abs(pixel.blue() - background.blue()) >
          90) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

// A page the tests draw text on, with the box of each drawn line recorded as the fake
// engine will report it.
struct OcrTestPage {
  QImage image;
  QColor background{Qt::white};
  std::vector<patchy::ocr::FakeLine> lines;

  OcrTestPage(int width, int height, QColor paper = Qt::white)
      : image(width, height, QImage::Format_RGBA8888), background(paper) {
    image.fill(paper);
  }

  // Draws `text` with the top of its line box at (x, y). Returns the ink box.
  QRect draw(const QString& text, int x, int y, const QFont& font, QColor color = Qt::black) {
    const QFontMetrics metrics(font);
    {
      QPainter painter(&image);
      painter.setRenderHint(QPainter::TextAntialiasing);
      painter.setFont(font);
      painter.setPen(color);
      painter.drawText(x, y + metrics.ascent(), text);
    }
    // Only this line's own row: with lines set close together, a taller search would take
    // in the descenders of the line above.
    const QRect ink =
        ocr_ink_box(image, QRect(x - 20, y, metrics.horizontalAdvance(text) + 60, metrics.height()), background);
    CHECK(!ink.isEmpty());
    lines.push_back(patchy::ocr::FakeLine{
        patchy::ocr::Box{static_cast<double>(ink.x()), static_cast<double>(ink.y()),
                         static_cast<double>(ink.width()), static_cast<double>(ink.height())},
        text.toStdString(), false});
    return ink;
  }
};

// A fake engine (id "fake", English and Japanese) that reports the page's drawn lines.
inline std::shared_ptr<patchy::ocr::FakeEngine> ocr_fake_for(const OcrTestPage& page) {
  patchy::ocr::FakeEngineConfig config;
  config.languages = {{"en-US", "English (United States)"}, {"ja", "Japanese"}};
  config.lines = page.lines;
  return patchy::ocr::make_fake_engine(std::move(config));
}

// Puts `image` in the active document as a layer named "Scan" at `at`.
inline patchy::LayerId add_ocr_scan_layer(patchy::ui::MainWindow& window, const QImage& image, QPoint at) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::Layer layer(document.allocate_layer_id(), "Scan", patchy::LayerKind::Pixel);
  layer.set_pixels(patchy::ui::pixels_from_image_rgba(image));
  layer.set_bounds(patchy::Rect{at.x(), at.y(), image.width(), image.height()});
  const auto id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  return id;
}

// The first Recognized Text group at the top of the layer stack, or null.
inline const patchy::Layer* recognized_text_group(const patchy::Document& document) {
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_recognized_text_group(layer)) {
      return &layer;
    }
  }
  return nullptr;
}

}  // namespace patchy::test::ui
