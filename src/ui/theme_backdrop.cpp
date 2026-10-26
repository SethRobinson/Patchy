#include "ui/theme_backdrop.hpp"

#include "ui/theme_package.hpp"
#include "ui/theme_palette.hpp"

#include <QBrush>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

// Everything derived from the active backdrop, rebuilt when theme_generation()
// moves. Stretched copies are kept per target size (a canvas and the start panel
// can differ) and capped so a run of window resizes cannot grow the cache.
struct BackdropCache {
  int generation = -1;
  QPixmap tile;
  QHash<QString, QPixmap> stretched;
  int gutter_generation = -1;
  QString gutter_url;
};

BackdropCache& backdrop_cache() {
  static BackdropCache cache;
  return cache;
}

const ThemeAssets* active_backdrop_assets() {
  const auto* assets = active_theme_assets();
  return assets != nullptr && !assets->backdrop.isNull() ? assets : nullptr;
}

BackdropCache& current_cache(const ThemeAssets& assets) {
  auto& cache = backdrop_cache();
  if (cache.generation != theme_generation()) {
    cache.generation = theme_generation();
    cache.tile = QPixmap::fromImage(assets.backdrop);
    cache.stretched.clear();
  }
  return cache;
}

[[nodiscard]] int channel_step(int value, int delta) { return std::clamp(value + delta, 0, 255); }

}  // namespace

bool theme_backdrop_active() { return active_backdrop_assets() != nullptr; }

bool paint_theme_backdrop(QPainter& painter, const QRect& area, const QWidget& widget, const QColor& fallback) {
  const auto* assets = active_backdrop_assets();
  if (assets == nullptr) {
    return false;
  }
  if (area.isEmpty()) {
    return true;
  }
  auto& cache = current_cache(*assets);
  painter.fillRect(area, fallback);
  switch (assets->options.backdrop_mode) {
    case ThemeBackdropMode::Tile: {
      // Anchored to the top-level window: the pattern stays put while the document
      // pans, and every canvas in one window lines up with its neighbours.
      const auto origin = widget.mapTo(widget.window(), QPoint(0, 0));
      painter.save();
      painter.setBrushOrigin(-origin);
      painter.fillRect(area, QBrush(cache.tile));
      painter.restore();
      break;
    }
    case ThemeBackdropMode::Stretch: {
      const qreal ratio = widget.devicePixelRatioF();
      const QSize device_size(qRound(widget.width() * ratio), qRound(widget.height() * ratio));
      if (device_size.isEmpty()) {
        break;
      }
      const auto key = QStringLiteral("%1x%2").arg(device_size.width()).arg(device_size.height());
      auto found = cache.stretched.find(key);
      if (found == cache.stretched.end()) {
        if (cache.stretched.size() >= 4) {
          cache.stretched.clear();
        }
        found = cache.stretched.insert(
            key, QPixmap::fromImage(assets->backdrop.scaled(device_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
      }
      const QRectF source(area.x() * ratio, area.y() * ratio, area.width() * ratio, area.height() * ratio);
      painter.drawPixmap(QRectF(area), found.value(), source);
      break;
    }
    case ThemeBackdropMode::Center: {
      const auto size = cache.tile.size();
      const QPoint top_left((widget.width() - size.width()) / 2, (widget.height() - size.height()) / 2);
      painter.save();
      painter.setClipRect(area);
      painter.drawPixmap(top_left, cache.tile);
      painter.restore();
      break;
    }
  }
  return true;
}

QString theme_cache_png_url(const QImage& image, const QString& prefix) {
  QByteArray png;
  {
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
      return {};
    }
  }
  const auto digest = QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex().left(20);
  const QDir directory(QDir::temp().filePath(QStringLiteral("patchy-theme-cache")));
  if (!QDir().mkpath(directory.path())) {
    return {};
  }
  const auto path = directory.filePath(QStringLiteral("%1-%2.png").arg(prefix, QString::fromLatin1(digest)));
  if (!QFileInfo::exists(path)) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
      return {};
    }
  }
  // QSS takes a bare url() path; quote one holding anything a bare one cannot.
  static const QRegularExpression bare_path(QStringLiteral("^[A-Za-z0-9_./:~-]+$"));
  return bare_path.match(path).hasMatch() ? path : QStringLiteral("\"%1\"").arg(path);
}

QString themed_scroll_gutter_url() {
  if (!has_active_custom_palette()) {
    return {};
  }
  auto& cache = backdrop_cache();
  if (cache.gutter_generation == theme_generation()) {
    return cache.gutter_url;
  }
  cache.gutter_generation = theme_generation();
  cache.gutter_url.clear();

  const auto track = theme().canvas_scrollbar_track;
  QImage texture;
  const auto* assets = active_backdrop_assets();
  if (assets != nullptr && assets->options.backdrop_mode == ThemeBackdropMode::Tile) {
    // The tile itself, made opaque over the track so the gutter never shows the
    // window through it.
    texture = QImage(assets->backdrop.size(), QImage::Format_ARGB32_Premultiplied);
    texture.fill(track);
    QPainter painter(&texture);
    painter.drawImage(0, 0, assets->backdrop);
  } else {
    if (track.rgb() == theme(active_color_scheme()).canvas_scrollbar_track.rgb()) {
      return {};  // the authored scroll-dither.svg is drawn around exactly this track
    }
    // The built-in dither: the track plus a second tone 14 levels away, lighter on
    // a dark track and darker on a light one (scroll-dither.svg's two variants).
    const int delta = track.lightness() < 128 ? 14 : -14;
    const QColor alternate(channel_step(track.red(), delta), channel_step(track.green(), delta),
                           channel_step(track.blue(), delta));
    texture = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
    texture.fill(track);
    texture.setPixelColor(0, 0, alternate);
    texture.setPixelColor(1, 1, alternate);
  }

  cache.gutter_url = theme_cache_png_url(texture, QStringLiteral("gutter"));
  return cache.gutter_url;
}

}  // namespace patchy::ui
