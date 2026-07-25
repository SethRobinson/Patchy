#include "ui/icon_theme.hpp"

#include <QApplication>
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

// Renders on demand and reparses only when the scheme moves. Only the parsed
// renderer is cached: rasterizing a 32x32 path set is microseconds, while parsing
// the XML is not, and a pixmap cache is one more thing that can go stale.
class ThemedIconEngine final : public QIconEngine {
 public:
  explicit ThemedIconEngine(QString name) : name_(std::move(name)) {}

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
  // supports a 100-200% interface scale, so every icon would go soft at 150%.
  QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                       qreal scale) override {
    Q_UNUSED(state);
    ensure_renderer();
    if (size.isEmpty() || renderer_ == nullptr || !renderer_->isValid()) {
      return {};
    }
    const qreal ratio = scale > 0.0 ? scale : qreal(1.0);
    QPixmap rendered(QSize(qRound(size.width() * ratio), qRound(size.height() * ratio)));
    rendered.fill(Qt::transparent);
    rendered.setDevicePixelRatio(ratio);
    QPainter painter(&rendered);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer_->render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
    painter.end();
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
    ensure_renderer();
    return renderer_ == nullptr || !renderer_->isValid();
  }

  QIconEngine* clone() const override { return new ThemedIconEngine(name_); }

  QString key() const override { return QStringLiteral("PatchyThemedIcon"); }

 private:
  void ensure_renderer() {
    const int generation = theme_generation();
    if (renderer_ != nullptr && generation_ == generation) {
      return;
    }
    generation_ = generation;
    const auto svg = themed_icon_svg(name_);
    renderer_ = svg.isEmpty() ? nullptr : std::make_unique<QSvgRenderer>(svg);
  }

  QString name_;
  std::unique_ptr<QSvgRenderer> renderer_;
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
  return {QStringLiteral("default-colors"), QStringLiteral("swap-colors")};
}

QStringList stylesheet_referenced_icon_names() {
  // Loaded by QStyleSheetStyle through url(), which cannot reach a QIconEngine.
  return {QStringLiteral("checkmark"),   QStringLiteral("scroll-dither"),
          QStringLiteral("tool-flyout-corner"), QStringLiteral("spin-plus"),
          QStringLiteral("spin-plus-disabled"), QStringLiteral("spin-minus"),
          QStringLiteral("spin-minus-disabled")};
}

QByteArray themed_icon_svg(const QString& name) {
  ensure_icon_resources();
  QFile file(icon_resource_path(name));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  auto svg = file.readAll();
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

QString themed_icon_url(const QString& name) {
  ensure_icon_resources();
  if (active_color_scheme() == ColorScheme::Light) {
    const auto light_path = light_icon_resource_path(name);
    if (QFile::exists(light_path)) {
      return light_path;
    }
  }
  return icon_resource_path(name);
}

}  // namespace patchy::ui
