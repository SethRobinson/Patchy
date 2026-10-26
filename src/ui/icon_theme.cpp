#include "ui/icon_theme.hpp"

#include "ui/theme_backdrop.hpp"
#include "ui/theme_package.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QList>
#include <QPainter>
#include <QPaintDevice>
#include <QPixmap>
#include <QStyle>
#include <QStyleOption>
#include <QSvgRenderer>

#include <array>
#include <memory>
#include <utility>

// Icon resources live in the static patchy_ui library; force registration before
// first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

void ensure_icon_resources() {
  static const int registered = ::qInitResources_icons();
  (void)registered;
}

QString icon_resource_path(const QString& name) {
  return QStringLiteral(":/patchy/icons/%1.svg").arg(name);
}

QString light_icon_resource_path(const QString& name) {
  return QStringLiteral(":/patchy/icons/light/%1.svg").arg(name);
}

// One drawing of an icon: the built-in SVG, or a theme package's SVG or bitmap.
struct IconFace {
  std::unique_ptr<QSvgRenderer> svg;
  QImage image;
  QImage image_2x;
  Qt::TransformationMode scaling = Qt::SmoothTransformation;

  [[nodiscard]] bool valid() const {
    return (svg != nullptr && svg->isValid()) || !image.isNull() || !image_2x.isNull();
  }
};

IconFace load_icon_face(const QString& name) {
  IconFace face;
  if (name.isEmpty()) {
    return face;
  }
  if (const auto* assets = active_theme_assets(); assets != nullptr) {
    const auto found = assets->icons.constFind(name);
    if (found != assets->icons.constEnd()) {
      if (!found->svg.isEmpty()) {
        const auto svg = assets->options.recolor_icons ? recolor_icon_svg(found->svg) : found->svg;
        face.svg = std::make_unique<QSvgRenderer>(svg);
        if (face.svg->isValid()) {
          return face;
        }
        face.svg.reset();  // a broken override falls back to the shipped icon
      } else if (!found->image.isNull() || !found->image_2x.isNull()) {
        face.image = found->image;
        face.image_2x = found->image_2x;
        face.scaling = assets->options.icon_scaling == ThemeIconScaling::Nearest ? Qt::FastTransformation
                                                                                 : Qt::SmoothTransformation;
        return face;
      }
    }
  }
  const auto svg = themed_icon_svg(name);
  if (!svg.isEmpty()) {
    face.svg = std::make_unique<QSvgRenderer>(svg);
  }
  return face;
}

// Draws a bitmap face into a `target`-sized device-pixel canvas, aspect kept and
// centered: the @2x image when the target has room for more than the 1x one.
void draw_bitmap_face(QPainter& painter, const IconFace& face, const QSize& target) {
  const bool use_1x = face.image_2x.isNull() || (!face.image.isNull() && target.width() <= face.image.width() &&
                                                 target.height() <= face.image.height());
  const auto& source = use_1x ? face.image : face.image_2x;
  if (source.isNull()) {
    return;
  }
  const auto fitted = source.size().scaled(target, Qt::KeepAspectRatio);
  const QRect rect(QPoint((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2), fitted);
  if (fitted == source.size()) {
    painter.drawImage(rect.topLeft(), source);
    return;
  }
  painter.drawImage(rect.topLeft(), source.scaled(fitted, Qt::IgnoreAspectRatio, face.scaling));
}

// Renders on demand and reloads only when the theme moves. Only the parsed
// renderer (or the decoded bitmap) is cached: rasterizing a 32x32 path set is
// microseconds, while parsing the XML is not, and a pixmap cache is one more
// thing that can go stale.
class ThemedIconEngine final : public QIconEngine {
 public:
  // `on_name`, when set, is drawn for QIcon::On (a checked button or menu item).
  explicit ThemedIconEngine(QString name, QString on_name = {})
      : name_(std::move(name)), on_name_(std::move(on_name)) {}

  void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
    if (painter == nullptr) {
      return;
    }
    const qreal ratio =
        painter->device() != nullptr ? painter->device()->devicePixelRatioF() : qreal(1.0);
    painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, ratio));
  }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    return scaledPixmap(size, mode, state, 1.0);
  }

  // Overriding this matters: the base implementation drops `scale`, and Patchy
  // supports a 67-200% interface scale, so every icon would go soft at 150%.
  QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                       qreal scale) override {
    ensure_faces();
    const auto& face = state == QIcon::On && on_face_.valid() ? on_face_ : face_;
    if (size.isEmpty() || !face.valid()) {
      return {};
    }
    const qreal ratio = scale > 0.0 ? scale : qreal(1.0);
    const QSize device_size(qRound(size.width() * ratio), qRound(size.height() * ratio));
    QPixmap rendered(device_size);
    rendered.fill(Qt::transparent);
    if (face.svg != nullptr) {
      rendered.setDevicePixelRatio(ratio);
      QPainter painter(&rendered);
      painter.setRenderHint(QPainter::Antialiasing);
      face.svg->render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
    } else {
      // Bitmaps are placed in device pixels, so a 1:1 icon stays exactly 1:1.
      QPainter painter(&rendered);
      draw_bitmap_face(painter, face, device_size);
      painter.end();
      rendered.setDevicePixelRatio(ratio);
    }
    if (mode == QIcon::Normal) {
      return rendered;
    }
    // Without this, disabled toolbar buttons stop dimming: the stock SVG engine
    // routes non-Normal modes through the style the same way.
    QStyleOption option;
    option.palette = QGuiApplication::palette();
    if (auto* style = QApplication::style(); style != nullptr) {
      auto generated = style->generatedIconPixmap(mode, rendered, &option);
      if (!generated.isNull()) {
        return generated;
      }
    }
    return rendered;
  }

  QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    Q_UNUSED(mode);
    Q_UNUSED(state);
    return size;
  }

  QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override {
    Q_UNUSED(mode);
    Q_UNUSED(state);
    // Scalable source, so this is only a hint for callers that ask.
    return {QSize(16, 16), QSize(20, 20), QSize(24, 24), QSize(32, 32), QSize(64, 64)};
  }

  QString iconName() override { return name_; }

  bool isNull() override {
    ensure_faces();
    return !face_.valid();
  }

  QIconEngine* clone() const override { return new ThemedIconEngine(name_, on_name_); }

  QString key() const override { return QStringLiteral("PatchyThemedIcon"); }

 private:
  void ensure_faces() {
    const int generation = theme_generation();
    if (generation_ == generation) {
      return;
    }
    generation_ = generation;
    face_ = load_icon_face(name_);
    on_face_ = load_icon_face(on_name_);
  }

  QString name_;
  QString on_name_;
  IconFace face_;
  IconFace on_face_;
  int generation_ = -1;
};

}  // namespace

std::span<const IconColorRole> icon_color_roles() {
#define PATCHY_ICON_COLOR(hex, member) IconColorRole{QLatin1StringView(hex), &ThemePalette::member}
  static const auto roles = std::to_array<IconColorRole>({
      PATCHY_ICON_COLOR("#dce2eb", icon_ink),
      PATCHY_ICON_COLOR("#74c0ff", icon_accent),
      PATCHY_ICON_COLOR("#b8dcff", icon_accent_soft),
      PATCHY_ICON_COLOR("#acd8ff", icon_accent_tint),
      PATCHY_ICON_COLOR("#ff9696", icon_danger),
      PATCHY_ICON_COLOR("#ffc078", icon_warning),
      PATCHY_ICON_COLOR("#f5cd69", icon_folder),
      PATCHY_ICON_COLOR("#3a3320", icon_folder_fill),
      PATCHY_ICON_COLOR("#9be9a8", icon_success),
      PATCHY_ICON_COLOR("#242628", icon_surface),
  });
#undef PATCHY_ICON_COLOR
  return roles;
}

QStringList literal_color_icon_names() {
  // These draw the foreground/background paint swatches, so their black and
  // white fills are the subject of the drawing rather than chrome and are
  // allowed to sit outside the substitution map. Their outlines are ordinary
  // icon ink and still recolor, which is what keeps the glyph visible on a light
  // toolbar.
  // The Quick Mask tint is the mask overlay's own red, which does not change
  // with the scheme either; its frame and diagonal are icon ink.
  return {QStringLiteral("default-colors"), QStringLiteral("swap-colors"), QStringLiteral("quick-mask"),
          QStringLiteral("quick-mask-on")};
}

QStringList stylesheet_referenced_icon_names() {
  // Loaded by QStyleSheetStyle through url(), which cannot reach a QIconEngine.
  return {QStringLiteral("checkmark"),   QStringLiteral("scroll-dither"),
          QStringLiteral("tool-flyout-corner"), QStringLiteral("spin-plus"),
          QStringLiteral("spin-plus-disabled"), QStringLiteral("spin-minus"),
          QStringLiteral("spin-minus-disabled")};
}

QStringList builtin_icon_stems() {
  ensure_icon_resources();
  auto stems = QDir(QStringLiteral(":/patchy/icons")).entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
  for (auto& stem : stems) {
    stem.chop(4);
  }
  return stems;
}

QByteArray builtin_icon_svg(const QString& name) {
  ensure_icon_resources();
  QFile file(icon_resource_path(name));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QByteArray themed_icon_svg(const QString& name) {
  const auto svg = builtin_icon_svg(name);
  return svg.isEmpty() ? svg : recolor_icon_svg(svg);
}

QByteArray recolor_icon_svg(QByteArray svg) {
  const auto& palette = theme();
  for (const auto& [source, member] : icon_color_roles()) {
    // Authored icons write hex in lowercase; ui_icon_color_map_covers_every_authored_color
    // enforces that so this stays a plain byte replace.
    const QByteArray needle(source.data(), source.size());
    const auto replacement = (palette.*member).name(QColor::HexRgb).toLatin1();
    if (replacement != needle) {
      svg.replace(needle, replacement);
    }
  }
  return svg;
}

QIcon themed_svg_icon(const QString& name) {
  ensure_icon_resources();
  return QIcon(new ThemedIconEngine(name));
}

QIcon themed_svg_icon(const QString& name, const QString& on_name) {
  ensure_icon_resources();
  return QIcon(new ThemedIconEngine(name, on_name));
}

QString themed_icon_url(const QString& name) {
  ensure_icon_resources();
  if (name == QStringLiteral("scroll-dither")) {
    if (auto gutter = themed_scroll_gutter_url(); !gutter.isEmpty()) {
      return gutter;
    }
  }
  if (active_color_scheme() == ColorScheme::Light) {
    const auto light_path = light_icon_resource_path(name);
    if (QFile::exists(light_path)) {
      return light_path;
    }
  }
  return icon_resource_path(name);
}

}  // namespace patchy::ui
