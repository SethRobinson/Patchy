#include "ui/ocr_recognition.hpp"

#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"
#include "ui/localization.hpp"

#include <QObject>

#include <algorithm>
#include <chrono>
#include <future>
#include <utility>

namespace patchy::ui {

namespace {

// The smallest rectangle holding every pixel of `image` that is not fully transparent.
QRect visible_rect(const QImage& image) {
  int left = image.width();
  int top = image.height();
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0U) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = y;
      }
    }
  }
  return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

ocr::Point moved(ocr::Point point, QPoint origin) {
  return ocr::Point{point.x + origin.x(), point.y + origin.y()};
}

}  // namespace

QString ocr_layer_mode_token(OcrLayerMode mode) {
  switch (mode) {
    case OcrLayerMode::None:
      return QStringLiteral("none");
    case OcrLayerMode::Replace:
      return QStringLiteral("replace");
    case OcrLayerMode::Hidden:
      return QStringLiteral("hidden");
    case OcrLayerMode::Visible:
      break;
  }
  return QStringLiteral("visible");
}

std::optional<OcrLayerMode> ocr_layer_mode_from_token(const QString& token) {
  if (token == QLatin1String("none")) {
    return OcrLayerMode::None;
  }
  if (token == QLatin1String("visible")) {
    return OcrLayerMode::Visible;
  }
  if (token == QLatin1String("replace")) {
    return OcrLayerMode::Replace;
  }
  if (token == QLatin1String("hidden")) {
    return OcrLayerMode::Hidden;
  }
  return std::nullopt;
}

std::optional<OcrSourceImage> ocr_source_image(const Document& document, std::optional<LayerId> layer,
                                               std::optional<QRect> rect, QString* error) {
  const auto fail = [error](const QString& message) -> std::optional<OcrSourceImage> {
    if (error != nullptr) {
      *error = message;
    }
    return std::nullopt;
  };
  QImage image;
  QRect area(0, 0, document.width(), document.height());
  if (layer.has_value()) {
    image = automatic_selection_layer_image(document, *layer, nullptr);
    if (image.isNull()) {
      return fail(QObject::tr("The layer has nothing visible to read text from."));
    }
    // Only where the layer has pixels: an engine reads a small layer faster than a canvas.
    area = visible_rect(image);
  } else {
    image = qimage_from_document(document, true).convertToFormat(QImage::Format_RGBA8888);
  }
  if (rect.has_value()) {
    area = area.intersected(rect->normalized());
  }
  if (image.isNull() || area.isEmpty()) {
    return fail(QObject::tr("There is nothing in that area to read text from."));
  }
  return OcrSourceImage{image.copy(area), area.topLeft()};
}

ocr::Result recognize_on_worker(const std::shared_ptr<ocr::Engine>& engine, const std::string& language,
                                const QImage& image, ocr::Cancel& cancel, const std::function<void()>& pump) {
  // The worker holds its own reference to the pixels (QImage shares them) and only reads.
  auto future = launch_async([engine, language, image, &cancel] {
    const auto recognizer = engine->open(language);
    return recognizer->recognize(ocr::ImageView{image.constBits(), image.width(), image.height(),
                                                static_cast<std::ptrdiff_t>(image.bytesPerLine()),
                                                ocr::PixelLayout::Rgba8},
                                 &cancel);
  });
  while (future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
    if (pump) {
      pump();
    }
  }
  return future.get();
}

ocr::Result recognition_in_document(ocr::Result result, QPoint origin) {
  for (auto& line : result.lines) {
    line.box.x += origin.x();
    line.box.y += origin.y();
    for (auto& corner : line.quad) {
      corner = moved(corner, origin);
    }
    for (auto& word : line.words) {
      word.box.x += origin.x();
      word.box.y += origin.y();
      for (auto& corner : word.quad) {
        corner = moved(corner, origin);
      }
    }
  }
  return result;
}

OcrChoice resolve_ocr_choice(const QString& engine_id, const QString& language, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return OcrChoice{};
  };
  const auto engines = ocr::engines();
  const auto settings = app_settings();
  std::shared_ptr<ocr::Engine> engine;
  if (!engine_id.isEmpty()) {
    engine = ocr::find_engine(engine_id.toStdString());
    if (engine == nullptr) {
      return fail(QObject::tr("There is no text recognition engine named \"%1\".").arg(engine_id));
    }
  } else {
    const auto stored = settings.value(QLatin1String(kOcrEngineSettingsKey)).toString().toStdString();
    for (const auto& candidate : engines) {
      if (candidate->unavailable() != ocr::Unavailable::None) {
        continue;
      }
      if (engine == nullptr || candidate->id() == stored) {
        engine = candidate;
      }
      if (candidate->id() == stored) {
        break;
      }
    }
    if (engine == nullptr) {
      if (engines.empty()) {
        return fail(QObject::tr("No text recognition engine is available on this system."));
      }
      return fail(QObject::tr("Text recognition is not available: %1.")
                      .arg(translate_data_text(engines.front()->unavailable_reason())));
    }
  }
  if (engine->unavailable() != ocr::Unavailable::None) {
    return fail(
        QObject::tr("Text recognition is not available: %1.").arg(translate_data_text(engine->unavailable_reason())));
  }

  const auto languages = engine->languages();
  const auto has = [&languages](const std::string& tag) {
    return std::any_of(languages.begin(), languages.end(),
                       [&tag](const ocr::Language& candidate) { return candidate.tag == tag; });
  };
  std::string tag = language.toStdString();
  if (!tag.empty()) {
    if (!has(tag)) {
      return fail(QObject::tr("The text recognition engine has no language \"%1\".").arg(language));
    }
  } else {
    tag = settings.value(QLatin1String(kOcrLanguageSettingsKey)).toString().toStdString();
    if (tag.empty() || !has(tag)) {
      tag = engine->default_language();
    }
    if (tag.empty() || !has(tag)) {
      return fail(QObject::tr("The text recognition engine has no language to read with."));
    }
  }
  return OcrChoice{std::move(engine), std::move(tag)};
}

}  // namespace patchy::ui
