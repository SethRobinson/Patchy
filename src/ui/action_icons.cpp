#include "ui/action_icons.hpp"

#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPolygonF>
#include <QRectF>
#include <QStyle>
#include <QStyleOption>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>

int qInitResources_icons();

namespace patchy::ui {

namespace {

void ensure_icon_resources() {
  static const int initialized = qInitResources_icons();
  (void)initialized;
}

QString resource_icon_key(const QString& value) {
  if (value == QStringLiteral("CT")) {
    return QStringLiteral("cut");
  }
  if (value == QStringLiteral("CP")) {
    return QStringLiteral("copy");
  }
  if (value == QStringLiteral("CM")) {
    return QStringLiteral("copy-merged");
  }
  if (value == QStringLiteral("TR")) {
    return QStringLiteral("transform");
  }
  if (value == QStringLiteral("SA")) {
    return QStringLiteral("select-all");
  }
  if (value == QStringLiteral("DS")) {
    return QStringLiteral("deselect");
  }
  if (value == QStringLiteral("RS")) {
    return QStringLiteral("reselect");
  }
  if (value == QStringLiteral("INV") || value == QStringLiteral("inv")) {
    return QStringLiteral("invert");
  }
  if (value == QStringLiteral("GR")) {
    return QStringLiteral("grow");
  }
  if (value == QStringLiteral("SIM")) {
    return QStringLiteral("similar");
  }
  if (value == QStringLiteral("EXP")) {
    return QStringLiteral("expand");
  }
  if (value == QStringLiteral("CTR")) {
    return QStringLiteral("contract");
  }
  if (value == QStringLiteral("BD")) {
    return QStringLiteral("border");
  }
  if (value == QStringLiteral("AL")) {
    return QStringLiteral("alpha");
  }
  if (value == QStringLiteral("ADJ")) {
    return QStringLiteral("adjustment");
  }
  if (value == QStringLiteral("fx")) {
    return QStringLiteral("effects");
  }
  if (value == QStringLiteral("RN")) {
    return QStringLiteral("rename");
  }
  if (value == QStringLiteral("LVL")) {
    return QStringLiteral("levels");
  }
  if (value == QStringLiteral("CRV")) {
    return QStringLiteral("curves");
  }
  if (value == QStringLiteral("HSL")) {
    return QStringLiteral("hsl");
  }
  if (value == QStringLiteral("CB")) {
    return QStringLiteral("color-balance");
  }
  if (value == QStringLiteral("IS")) {
    return QStringLiteral("image-size");
  }
  if (value == QStringLiteral("CS")) {
    return QStringLiteral("canvas-size");
  }
  if (value == QStringLiteral("SE")) {
    return QStringLiteral("selection-edges");
  }
  if (value == QStringLiteral("D")) {
    return QStringLiteral("default-colors");
  }
  if (value == QStringLiteral("X")) {
    return QStringLiteral("swap-colors");
  }
  if (value == QStringLiteral("N")) {
    return QStringLiteral("selection-new");
  }
  if (value == QStringLiteral("+")) {
    return QStringLiteral("selection-add");
  }
  if (value == QStringLiteral("-")) {
    return QStringLiteral("selection-subtract");
  }
  if (value == QStringLiteral("Ix")) {
    return QStringLiteral("selection-intersect");
  }
  if (value == QStringLiteral("8BF")) {
    return QStringLiteral("plugin");
  }
  if (value == QStringLiteral("FH")) {
    return QStringLiteral("flip-h");
  }
  if (value == QStringLiteral("FV")) {
    return QStringLiteral("flip-v");
  }
  if (value == QStringLiteral("zoomIn")) {
    return QStringLiteral("zoom-in");
  }
  if (value == QStringLiteral("zoomOut")) {
    return QStringLiteral("zoom-out");
  }
  if (value == QStringLiteral("1x")) {
    return QStringLiteral("zoom-reset");
  }
  if (value == QStringLiteral("ok")) {
    return QStringLiteral("apply");
  }
  return value;
}

}  // namespace

QIcon patchy_app_icon() {
  ensure_icon_resources();
  // Brand artwork is shared with the packaged OS icons, not recolored as a
  // toolbar glyph. The SVG icon engine keeps large and high-DPI icons sharp.
  return QIcon(QStringLiteral(":/patchy/branding/logo.svg"));
}

namespace {

// Draws one of the ~22 glyphs that have no authored SVG, into a 32x32 logical
// area. Kept separate from simple_icon() so the engine below can call it at paint
// time with a freshly resolved accent, rather than baking a color into a pixmap
// when the QAction is built.
void paint_simple_icon_glyph(QPainter& painter, const QString& text, const QColor& accent) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(accent, 2.2));
  painter.setBrush(Qt::NoBrush);

  if (text == QStringLiteral("new")) {
    painter.drawRect(QRect(9, 6, 14, 20));
    painter.drawLine(16, 11, 16, 21);
    painter.drawLine(11, 16, 21, 16);
  } else if (text == QStringLiteral("dir")) {
    QPainterPath folder_path(QPointF(6.0, 11.0));
    folder_path.lineTo(13.0, 11.0);
    folder_path.lineTo(15.5, 8.0);
    folder_path.lineTo(25.0, 8.0);
    folder_path.lineTo(25.0, 24.0);
    folder_path.lineTo(6.0, 24.0);
    folder_path.closeSubpath();
    painter.drawPath(folder_path);
    painter.drawLine(6, 13, 25, 13);
  } else if (text == QStringLiteral("dup")) {
    painter.drawRect(QRect(7, 10, 13, 15));
    painter.drawRect(QRect(12, 6, 13, 15));
  } else if (text == QStringLiteral("RN")) {
    painter.drawLine(QPointF(9.0, 23.0), QPointF(22.0, 10.0));
    painter.drawLine(QPointF(18.0, 8.0), QPointF(24.0, 14.0));
    painter.drawLine(QPointF(8.0, 24.0), QPointF(13.0, 22.5));
    painter.drawLine(QPointF(7.0, 25.0), QPointF(9.0, 20.0));
  } else if (text == QStringLiteral("trash")) {
    painter.drawLine(9, 10, 23, 10);
    painter.drawRect(QRect(11, 11, 10, 15));
    painter.drawLine(13, 15, 13, 23);
    painter.drawLine(19, 15, 19, 23);
  } else if (text == QStringLiteral("fill")) {
    painter.drawPolygon(QPolygon({QPoint(9, 10), QPoint(21, 15), QPoint(15, 25), QPoint(5, 18)}));
    painter.setBrush(accent);
    painter.drawEllipse(QPoint(25, 24), 3, 3);
  } else if (text == QStringLiteral("clear")) {
    painter.drawRect(QRect(8, 8, 16, 16));
    painter.drawLine(8, 24, 24, 8);
  } else if (text == QStringLiteral("link")) {
    painter.drawRoundedRect(QRectF(6.5, 11.0, 10.0, 10.0), 4.0, 4.0);
    painter.drawRoundedRect(QRectF(15.5, 11.0, 10.0, 10.0), 4.0, 4.0);
    painter.drawLine(QPointF(13.0, 16.0), QPointF(19.0, 16.0));
  } else if (text == QStringLiteral("clip")) {
    // Clipping mask: a bent arrow coming in from the right and turning down onto
    // the base layer's top edge, pointing at the layer below that the row clips
    // to (#397). The ink is centered so the row badge centers on the base row's
    // thumbnail.
    QPainterPath arrow(QPointF(23.0, 8.5));
    arrow.lineTo(16.5, 8.5);
    arrow.quadTo(QPointF(12.0, 8.5), QPointF(12.0, 13.0));
    arrow.lineTo(12.0, 20.5);
    painter.drawPath(arrow);
    painter.drawLine(QPointF(12.0, 20.5), QPointF(15.5, 17.0));
    painter.drawLine(QPointF(12.0, 20.5), QPointF(8.5, 17.0));
    painter.drawLine(QPointF(7.0, 24.5), QPointF(25.0, 24.5));
  } else if (text == QStringLiteral("default")) {
    painter.setBrush(Qt::black);
    painter.drawRect(QRect(7, 7, 13, 13));
    painter.setBrush(Qt::white);
    painter.drawRect(QRect(13, 13, 13, 13));
  } else if (text == QStringLiteral("zoomIn") || text == QStringLiteral("zoomOut")) {
    painter.drawEllipse(QRect(7, 7, 14, 14));
    painter.drawLine(18, 18, 26, 26);
    painter.drawLine(11, 14, 17, 14);
    if (text == QStringLiteral("zoomIn")) {
      painter.drawLine(14, 11, 14, 17);
    }
  } else if (text == QStringLiteral("fill")) {
    // Fill Screen: the frame with the image pushed out past its edges.
    painter.drawRect(QRect(7, 9, 18, 14));
    painter.drawLine(11, 5, 11, 27);
    painter.drawLine(21, 5, 21, 27);
  } else if (text == QStringLiteral("fit")) {
    painter.drawRect(QRect(7, 9, 18, 14));
    painter.drawLine(7, 9, 12, 9);
    painter.drawLine(7, 9, 7, 14);
    painter.drawLine(25, 23, 20, 23);
    painter.drawLine(25, 23, 25, 18);
  } else if (text == QStringLiteral("crop")) {
    painter.drawLine(10, 5, 10, 23);
    painter.drawLine(5, 20, 23, 20);
    painter.drawLine(15, 9, 27, 9);
    painter.drawLine(22, 9, 22, 27);
  } else if (text == QStringLiteral("rotate")) {
    painter.drawArc(QRect(7, 7, 18, 18), 30 * 16, 280 * 16);
    painter.drawLine(22, 6, 26, 7);
    painter.drawLine(22, 6, 23, 11);
  } else if (text == QStringLiteral("merge")) {
    painter.drawRect(QRect(8, 8, 15, 10));
    painter.drawRect(QRect(11, 14, 15, 10));
    painter.drawLine(10, 26, 24, 26);
  } else if (text == QStringLiteral("stroke")) {
    QPen dashed(accent, 2.0);
    dashed.setStyle(Qt::DashLine);
    painter.setPen(dashed);
    painter.drawRect(QRect(8, 8, 16, 16));
  } else if (text == QStringLiteral("stroke-path")) {
    // Stroke Path: a thin path circle with one arc painted as a thick band.
    painter.setPen(QPen(accent, 1.6));
    painter.drawEllipse(QRectF(8.0, 8.0, 16.0, 16.0));
    painter.setPen(QPen(accent, 4.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(8.0, 8.0, 16.0, 16.0), 200 * 16, 140 * 16);
  } else if (text == QStringLiteral("eye") || text == QStringLiteral("eyeOff")) {
    QPainterPath eye;
    eye.moveTo(5.5, 16.0);
    eye.cubicTo(9.0, 9.5, 23.0, 9.5, 26.5, 16.0);
    eye.cubicTo(23.0, 22.5, 9.0, 22.5, 5.5, 16.0);
    painter.drawPath(eye);
    painter.drawEllipse(QPointF(16.0, 16.0), 3.8, 3.8);
    if (text == QStringLiteral("eyeOff")) {
      painter.drawLine(QPointF(7.0, 25.0), QPointF(25.0, 7.0));
    }
  } else if (text == QStringLiteral("film")) {
    // Film strip: the frame area with a sprocket-hole column along each edge.
    painter.drawRect(QRect(6, 8, 20, 16));
    painter.drawLine(11, 8, 11, 24);
    painter.drawLine(21, 8, 21, 24);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    for (const int y : {11, 16, 21}) {
      painter.drawRect(QRect(7, y - 1, 2, 2));
      painter.drawRect(QRect(23, y - 1, 2, 2));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent, 2.2));
    painter.drawPolygon(QPolygon({QPoint(14, 12), QPoint(19, 16), QPoint(14, 20)}));
  } else if (text == QStringLiteral("lock") || text == QStringLiteral("unlock")) {
    painter.drawRoundedRect(QRectF(8.0, 14.0, 16.0, 11.0), 2.0, 2.0);
    QPainterPath shackle;
    shackle.moveTo(11.0, 14.0);
    shackle.lineTo(11.0, 11.5);
    shackle.cubicTo(11.0, 6.5, 21.0, 6.5, 21.0, 11.5);
    if (text == QStringLiteral("lock")) {
      shackle.lineTo(21.0, 14.0);
    } else {
      shackle.lineTo(24.0, 14.0);
    }
    painter.drawPath(shackle);
    painter.drawLine(QPointF(16.0, 18.0), QPointF(16.0, 22.0));
  } else {
    painter.setPen(accent);
    auto font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, text.left(2).toUpper());
  }
}

// Same contract as ThemedIconEngine in icon_theme.cpp: resolve the color when the
// icon is painted, so a live scheme change needs no QAction::setIcon call. An
// invalid `accent` means "use the theme's icon ink"; an explicit one is a
// semantic color (a red delete cross, say) and is left alone.
//
// The fourth constructor takes the glyph itself, for the procedural marks that
// are not in the shared vocabulary (window buttons, canvas anchors). They still
// resolve their ink from a role at paint time, which is the whole point: baking
// the color into a QPixmap is what left the window buttons near-white and
// invisible on Light's title bar.
class ProceduralIconEngine final : public QIconEngine {
 public:
  using GlyphPainter = std::function<void(QPainter&, const QColor&)>;
  // Paints in device pixels on an unscaled painter, given the pixmap's device size and pixel
  // ratio, so a glyph can put its edges on whole pixels at any screen scale.
  using PixelGlyphPainter = std::function<void(QPainter&, const QColor&, const QSize&, qreal)>;

  ProceduralIconEngine(QString name, QColor ThemePalette::* ink_role, PixelGlyphPainter glyph)
      : text_(std::move(name)), accent_role_(ink_role), pixel_glyph_(std::move(glyph)) {}
  ProceduralIconEngine(QString text, QColor accent)
      : text_(std::move(text)), accent_(std::move(accent)) {}
  ProceduralIconEngine(QString text, QColor ThemePalette::* accent_role)
      : text_(std::move(text)), accent_role_(accent_role) {}
  ProceduralIconEngine(QString name, qreal authored_size, QColor ThemePalette::* ink_role,
                       GlyphPainter glyph)
      : text_(std::move(name)),
        accent_role_(ink_role),
        authored_size_(authored_size > 0.0 ? authored_size : 32.0),
        glyph_(std::move(glyph)) {}

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

  QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                       qreal scale) override {
    Q_UNUSED(state);
    if (size.isEmpty()) {
      return {};
    }
    const qreal ratio = scale > 0.0 ? scale : qreal(1.0);
    QPixmap rendered(QSize(qRound(size.width() * ratio), qRound(size.height() * ratio)));
    rendered.fill(Qt::transparent);
    if (pixel_glyph_) {
      {
        QPainter painter(&rendered);
        painter.setRenderHint(QPainter::Antialiasing);
        pixel_glyph_(painter, resolved_accent(), rendered.size(), ratio);
      }
      rendered.setDevicePixelRatio(ratio);
      return styled_for_mode(std::move(rendered), mode);
    }
    rendered.setDevicePixelRatio(ratio);
    QPainter painter(&rendered);
    // The glyph code is authored against a square area, 32x32 unless the caller
    // supplied its own.
    painter.scale(static_cast<qreal>(size.width()) / authored_size_,
                  static_cast<qreal>(size.height()) / authored_size_);
    if (glyph_) {
      painter.setRenderHint(QPainter::Antialiasing);
      glyph_(painter, resolved_accent());
    } else {
      paint_simple_icon_glyph(painter, text_, resolved_accent());
    }
    painter.end();
    return styled_for_mode(std::move(rendered), mode);
  }

  QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    Q_UNUSED(mode);
    Q_UNUSED(state);
    return size;
  }

  QString iconName() override { return text_; }
  bool isNull() override { return false; }
  QIconEngine* clone() const override {
    if (pixel_glyph_) {
      return new ProceduralIconEngine(text_, accent_role_, pixel_glyph_);
    }
    if (glyph_) {
      return new ProceduralIconEngine(text_, authored_size_, accent_role_, glyph_);
    }
    return accent_role_ != nullptr ? new ProceduralIconEngine(text_, accent_role_)
                                   : new ProceduralIconEngine(text_, accent_);
  }
  QString key() const override { return QStringLiteral("PatchyProceduralIcon"); }

 private:
  [[nodiscard]] static QPixmap styled_for_mode(QPixmap rendered, QIcon::Mode mode) {
    if (mode == QIcon::Normal) {
      return rendered;
    }
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

  // A role wins over an explicit color, and an invalid explicit color falls back
  // to the theme's icon ink. Resolved here rather than stored so the glyph
  // follows a live scheme change.
  [[nodiscard]] QColor resolved_accent() const {
    if (accent_role_ != nullptr) {
      return theme().*accent_role_;
    }
    return accent_.isValid() ? accent_ : theme().icon_ink;
  }

  QString text_;
  QColor accent_;
  QColor ThemePalette::* accent_role_ = nullptr;
  qreal authored_size_ = 32.0;
  GlyphPainter glyph_;
  PixelGlyphPainter pixel_glyph_;
};

}  // namespace

QIcon simple_icon(QString text, QColor accent) {
  ensure_icon_resources();
  const auto key = resource_icon_key(text);
  if (QFile::exists(QStringLiteral(":/patchy/icons/%1.svg").arg(key))) {
    return themed_svg_icon(key);
  }
  return QIcon(new ProceduralIconEngine(std::move(text), std::move(accent)));
}

QIcon simple_icon(QString text, QColor ThemePalette::* accent_role) {
  ensure_icon_resources();
  const auto key = resource_icon_key(text);
  // An authored SVG carries its own colors through the icon color map, so the
  // role only applies to the procedural glyphs.
  if (QFile::exists(QStringLiteral(":/patchy/icons/%1.svg").arg(key))) {
    return themed_svg_icon(key);
  }
  return QIcon(new ProceduralIconEngine(std::move(text), accent_role));
}

QIcon themed_glyph_icon(QString name, qreal authored_size, QColor ThemePalette::* ink_role,
                        std::function<void(QPainter&, const QColor&)> glyph) {
  return QIcon(
      new ProceduralIconEngine(std::move(name), authored_size, ink_role, std::move(glyph)));
}

QIcon layer_lock_icon(const QString& kind, QColor ThemePalette::* ink_role) {
  // Two designs on a 16 px grid, chosen by the logical size (GitHub issue 484). The 16 px lock
  // button icons span 1 to 15, a 14 px glyph with 5 px to the button's border; smaller renders
  // (the 14 px row badges) span 2 to 15 of the grid scaled to their size. Painted as pixel
  // art at the device size: straight edges round to whole pixels, and the pencil's diagonals, the
  // shackle's rounded top and the keyhole are decided pixel by pixel, so every pixel is fully
  // inked or empty at 100%, 150% and 200%.
  auto glyph = [kind](QPainter& painter, const QColor& theme_ink, const QSize& device, qreal ratio) {
    // Overlapping parts are painted opaque and the ink's alpha applied once at the end, so a
    // translucent theme color stays even where they meet.
    QColor ink = theme_ink;
    ink.setAlpha(255);
    const int side = std::min(device.width(), device.height());
    const int left = (device.width() - side) / 2;
    const int top = (device.height() - side) / 2;
    const bool large = side >= qRound(16.0 * ratio);
    constexpr qreal grid = 16.0;
    const qreal unit = side / grid;
    // The button design rounds from the nearer edge, so its glyphs stay mirror-symmetric at 150%.
    const auto px = [unit, large, grid, side](qreal at) {
      if (large && at > grid / 2.0) {
        return side - qRound((grid - at) * unit);
      }
      return qRound(at * unit);
    };
    // The device rect between grid edges, at least one pixel each way.
    const auto cell = [&](qreal x0, qreal y0, qreal x1, qreal y1) {
      const int x = px(x0);
      const int y = px(y0);
      return QRect(left + x, top + y, std::max(1, px(x1) - x), std::max(1, px(y1) - y));
    };
    const auto fill = [&painter, &ink](std::initializer_list<QRect> rects) {
      for (const auto& rect : rects) {
        painter.fillRect(rect, ink);
      }
    };
    // Inks every pixel of box for which inside(x, y) holds, one run of pixels at a time.
    const auto plot = [&painter, &ink](const QRect& box, const auto& inside) {
      for (int y = box.top(); y <= box.bottom(); ++y) {
        int run = -1;
        for (int x = box.left(); x <= box.right() + 1; ++x) {
          const bool on = x <= box.right() && inside(x, y);
          if (on && run < 0) {
            run = x;
          } else if (!on && run >= 0) {
            painter.fillRect(QRect(run, y, x - run, 1), ink);
            run = -1;
          }
        }
      }
    };
    // Whether a pixel's center lies in a rounded rect, so rounded corners come out stepped.
    const auto in_rounded = [](const QRect& rect, qreal radius, int x, int y) {
      const qreal cx = x + 0.5;
      const qreal cy = y + 0.5;
      const qreal x0 = rect.left();
      const qreal y0 = rect.top();
      const qreal x1 = rect.left() + rect.width();
      const qreal y1 = rect.top() + rect.height();
      if (cx < x0 || cx > x1 || cy < y0 || cy > y1) {
        return false;
      }
      const qreal r = std::min({radius, (x1 - x0) / 2.0, (y1 - y0) / 2.0});
      const qreal nx = std::clamp(cx, x0 + r, x1 - r);
      const qreal ny = std::clamp(cy, y0 + r, y1 - r);
      return (cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) <= r * r;
    };
    const auto clearing = [&painter](auto&& paint) {
      painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
      paint();
      painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    };
    // A four-way move arrow: full-length stems and stepped heads depth rows deep, each row one
    // pixel wider on both sides than the row nearer the point.
    const auto arrow = [&](const QRect& stem_v, const QRect& stem_h, int depth) {
      fill({stem_v, stem_h});
      const int v_bottom = stem_v.top() + stem_v.height();
      const int h_right = stem_h.left() + stem_h.width();
      for (int row = 0; row < depth; ++row) {
        fill({QRect(stem_v.left() - row, stem_v.top() + row, stem_v.width() + 2 * row, 1),
              QRect(stem_v.left() - row, v_bottom - 1 - row, stem_v.width() + 2 * row, 1),
              QRect(stem_h.left() + row, stem_h.top() - row, 1, stem_h.height() + 2 * row),
              QRect(h_right - 1 - row, stem_h.top() - row, 1, stem_h.height() + 2 * row)});
      }
    };
    // A pencil filling box from its point at the lower left to a solid end cap at the upper right,
    // as pixel art in diagonal coordinates: p = x - y runs along it and q = x + y across it, so
    // its sides are exact 45 degree stairs and the point is a square corner. Sized for a 20 px
    // box and scaled with it; line is the outline's thickness across, in steps at that size.
    const auto pencil = [&](const QRect& box, qreal line_steps) {
      const int span = box.width();
      const qreal scale = span / 20.0;
      const int axis = span - 1;
      const int half = std::max(2, qRound(5.0 * scale));
      const int line = std::max(1, qRound(line_steps * scale));
      const int cap = std::max(1, qRound(4.0 * scale));
      const int lead = std::max(1, qRound(2.5 * scale));
      const int point = -axis;
      const int end = axis - half;
      plot(box, [&](int x, int y) {
        const int p = (x - box.left()) - (y - box.top());
        const int across = std::abs((x - box.left()) + (y - box.top()) - axis);
        if (p < point || p > end) {
          return false;
        }
        if (p < point + half) {
          // The cone widens one step per step along, so its two edges run straight down and
          // straight across; solid lead at the point, an outline behind it.
          const int width = p - point;
          return across <= width && (p < point + lead || across > width - line);
        }
        // The body: outline sides, a band where it meets the cone, and the solid end cap.
        return across <= half && (across > half - line || p < point + half + line || p > end - cap);
      });
    };
    // A padlock: a shackle with a stepped round top over a solid body, then the keyhole cleared.
    const auto padlock = [&](const QRect& outer, qreal outer_radius, const QRect& opening, qreal opening_radius,
                             const QRect& body) {
      plot(outer, [&](int x, int y) {
        return in_rounded(outer, outer_radius, x, y) && !in_rounded(opening, opening_radius, x, y);
      });
      fill({body});
    };
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    if (large) {
      if (kind == QStringLiteral("transparent")) {
        // A framed 3x3 checkerboard of 4 px cells: the corners and the middle solid.
        fill({cell(1, 1, 15, 2), cell(1, 14, 15, 15), cell(1, 1, 2, 15), cell(14, 1, 15, 15),
              cell(1, 1, 6, 6), cell(10, 1, 15, 6), cell(6, 6, 10, 10), cell(1, 10, 6, 15),
              cell(10, 10, 15, 15)});
      } else if (kind == QStringLiteral("pixels")) {
        // A two-step outline, as heavy as the arrow's two-pixel stems.
        pencil(cell(1, 1, 15, 15), 3.0);
      } else if (kind == QStringLiteral("position")) {
        // Two-pixel stems; heads three rows deep.
        arrow(cell(7, 1, 9, 15), cell(1, 7, 15, 9), std::max(1, px(4) - px(1)));
      } else if (kind == QStringLiteral("autonest")) {
        // An artboard frame with crop ticks at two corners, holding a small solid layer.
        fill({cell(4, 4, 12, 5), cell(4, 11, 12, 12), cell(4, 4, 5, 12), cell(11, 4, 12, 12),
              cell(4, 1, 5, 4), cell(1, 4, 4, 5), cell(11, 12, 12, 15), cell(12, 11, 15, 12),
              cell(6, 6, 10, 10)});
      } else {
        // "all": a two-pixel shackle over a 12x8 body with a keyhole slot.
        padlock(cell(4, 1, 12, 9), 3.0 * unit, cell(6, 3, 10, 10), unit, cell(2, 7, 14, 15));
        clearing([&] { fill({cell(7, 9, 9, 13)}); });
      }
    } else if (kind == QStringLiteral("transparent")) {
      // A framed 3x3 checkerboard: the corners and the middle solid.
      fill({cell(2, 2, 15, 3), cell(2, 14, 15, 15), cell(2, 2, 3, 15), cell(14, 2, 15, 15),
            cell(2, 2, 7, 7), cell(10, 2, 15, 7), cell(2, 10, 7, 15), cell(10, 10, 15, 15),
            cell(7, 7, 10, 10)});
    } else if (kind == QStringLiteral("pixels")) {
      pencil(cell(2, 2, 15, 15), 2.0);
    } else if (kind == QStringLiteral("position")) {
      // One-unit stems; heads three units deep.
      arrow(cell(8, 2, 9, 15), cell(2, 8, 15, 9), std::max(1, px(5) - px(2)));
    } else if (kind == QStringLiteral("autonest")) {
      fill({cell(4, 4, 13, 5), cell(4, 12, 13, 13), cell(4, 4, 5, 13), cell(12, 4, 13, 13),
            cell(4, 2, 5, 4), cell(2, 4, 4, 5), cell(12, 13, 13, 15), cell(13, 12, 15, 13),
            cell(6, 6, 11, 11)});
    } else {
      // "all": a two-unit shackle over a solid body with a keyhole slot.
      padlock(cell(4, 2, 13, 10), 3.0 * unit, cell(6, 4, 11, 11), unit, cell(3, 8, 14, 15));
      clearing([&] { fill({cell(8, 10, 9, 13)}); });
    }
    if (theme_ink.alpha() < 255) {
      painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      painter.fillRect(QRect(QPoint(0, 0), device), QColor(0, 0, 0, theme_ink.alpha()));
    }
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  };
  return QIcon(new ProceduralIconEngine(QStringLiteral("layer-lock-%1").arg(kind), ink_role, std::move(glyph)));
}

QIcon window_chrome_icon(QString role) {
  // Ink is the menu-bar text color because these buttons share that bar; Light
  // turns both to near-black together.
  auto glyph = [role](QPainter& painter, const QColor& ink) {
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    if (role == QStringLiteral("minimize")) {
      painter.drawLine(QPointF(9.0, 21.0), QPointF(23.0, 21.0));
    } else if (role == QStringLiteral("maximize")) {
      painter.drawRect(QRectF(9.5, 9.5, 13.0, 13.0));
    } else if (role == QStringLiteral("close")) {
      painter.drawLine(QPointF(10.0, 10.0), QPointF(22.0, 22.0));
      painter.drawLine(QPointF(22.0, 10.0), QPointF(10.0, 22.0));
    }
  };
  return themed_glyph_icon(std::move(role), 32.0, &ThemePalette::text_bright, std::move(glyph));
}

QIcon canvas_anchor_icon(CanvasAnchor anchor) {
  // Same reason as window_chrome_icon: these arrows sit on the Canvas Size grid
  // cells, which Light turns pale, so the ink has to be resolved at paint time.
  // dlg_raised_text is the label color of that dialog family.
  auto glyph = [anchor](QPainter& painter, const QColor& ink) {
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(ink);

    const QPointF center(11.0, 11.0);
    if (anchor == CanvasAnchor::Center) {
      painter.drawEllipse(center, 2.3, 2.3);
      return;
    }

    QPointF target(11.0, 11.0);
    switch (anchor) {
      case CanvasAnchor::TopLeft:
        target = QPointF(5.5, 5.5);
        break;
      case CanvasAnchor::Top:
        target = QPointF(11.0, 4.8);
        break;
      case CanvasAnchor::TopRight:
        target = QPointF(16.5, 5.5);
        break;
      case CanvasAnchor::Left:
        target = QPointF(4.8, 11.0);
        break;
      case CanvasAnchor::Center:
        break;
      case CanvasAnchor::Right:
        target = QPointF(17.2, 11.0);
        break;
      case CanvasAnchor::BottomLeft:
        target = QPointF(5.5, 16.5);
        break;
      case CanvasAnchor::Bottom:
        target = QPointF(11.0, 17.2);
        break;
      case CanvasAnchor::BottomRight:
        target = QPointF(16.5, 16.5);
        break;
    }

    painter.drawLine(center, target);
    const auto angle = std::atan2(target.y() - center.y(), target.x() - center.x());
    constexpr double kArrowSize = 4.0;
    constexpr double kArrowAngle = 0.72;
    const QPointF wing_a(target.x() - std::cos(angle - kArrowAngle) * kArrowSize,
                         target.y() - std::sin(angle - kArrowAngle) * kArrowSize);
    const QPointF wing_b(target.x() - std::cos(angle + kArrowAngle) * kArrowSize,
                         target.y() - std::sin(angle + kArrowAngle) * kArrowSize);
    painter.drawLine(target, wing_a);
    painter.drawLine(target, wing_b);
  };
  return themed_glyph_icon(QStringLiteral("canvas-anchor"), 22.0, &ThemePalette::dlg_raised_text,
                           std::move(glyph));
}

QIcon up_direction_arrow_icon(int direction) {
  // Same wing construction as canvas_anchor_icon, but a full-length arrow on
  // ordinary dialog chrome, so the standard icon ink applies.
  auto glyph = [direction](QPainter& painter, const QColor& ink) {
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(ink);
    const QPointF center(11.0, 11.0);
    QPointF target(11.0, 4.2);
    switch (direction) {
      case 1:
        target = QPointF(17.8, 11.0);
        break;
      case 2:
        target = QPointF(11.0, 17.8);
        break;
      case 3:
        target = QPointF(4.2, 11.0);
        break;
      default:
        break;
    }
    const QPointF tail(2.0 * center.x() - target.x(), 2.0 * center.y() - target.y());
    painter.drawLine(tail, target);
    const auto angle = std::atan2(target.y() - center.y(), target.x() - center.x());
    constexpr double kArrowSize = 4.5;
    constexpr double kArrowAngle = 0.72;
    const QPointF wing_a(target.x() - std::cos(angle - kArrowAngle) * kArrowSize,
                         target.y() - std::sin(angle - kArrowAngle) * kArrowSize);
    const QPointF wing_b(target.x() - std::cos(angle + kArrowAngle) * kArrowSize,
                         target.y() - std::sin(angle + kArrowAngle) * kArrowSize);
    painter.drawLine(target, wing_a);
    painter.drawLine(target, wing_b);
  };
  return themed_glyph_icon(QStringLiteral("up-direction-%1").arg(direction), 22.0,
                           &ThemePalette::icon_ink, std::move(glyph));
}

QIcon line_arrowhead_icon(bool at_end) {
  // Drawn in a 22-unit square: a horizontal line with a filled head at its left
  // (start) or right (end) tip, the way the Line tool draws it.
  auto glyph = [at_end](QPainter& painter, const QColor& ink) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!at_end) {
      painter.setTransform(QTransform(-1.0, 0.0, 0.0, 1.0, 22.0, 0.0), true);
    }
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::FlatCap));
    painter.drawLine(QPointF(2.5, 11.0), QPointF(13.0, 11.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    const QPointF head[] = {QPointF(19.5, 11.0), QPointF(11.5, 5.5), QPointF(11.5, 16.5)};
    painter.drawPolygon(head, 3);
  };
  return themed_glyph_icon(at_end ? QStringLiteral("line-arrowhead-end") : QStringLiteral("line-arrowhead-start"),
                           22.0, &ThemePalette::icon_ink, std::move(glyph));
}

namespace {

// GitHub issue 405: the Align and Distribute glyphs are axis-aligned bars and
// one-pixel anchor lines, so they are laid out on an 18 px grid (the options
// bar's icon size) and every edge is rounded to whole device pixels at the size
// actually rendered (18, 27, 36 px for 100%, 150%, 200%) and filled without
// antialiasing. Scaling one 22-unit drawing put edges between pixels and left
// soft, half-covered strokes. `rects` are QRect(x, y, w, h) on the 18 px grid in
// the glyph's base orientation (anchor lines vertical); `mirror` flips it left
// to right and `transpose` swaps the axes for the vertical modes.
void paint_pixel_grid_glyph(QPainter& painter, const QColor& ink, std::initializer_list<QRect> rects,
                            bool mirror, bool transpose) {
  constexpr double kGrid = 18.0;
  const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : qreal(1.0);
  // The engine scales the 22-unit authored square to the requested size; the
  // device size in pixels is that square under the full device transform.
  const int size = std::max(1, qRound(22.0 * painter.deviceTransform().m11()));
  const double scale = size / kGrid;
  const auto edge = [scale](int value) { return static_cast<int>(std::floor(value * scale + 0.5)); };
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setWorldTransform(QTransform::fromScale(1.0 / ratio, 1.0 / ratio));
  painter.setPen(Qt::NoPen);
  for (const QRect& rect : rects) {
    int left = edge(rect.x());
    int right = std::max(left + 1, edge(rect.x() + rect.width()));
    int top = edge(rect.y());
    int bottom = std::max(top + 1, edge(rect.y() + rect.height()));
    if (mirror) {
      const int mirrored_left = size - right;
      right = size - left;
      left = mirrored_left;
    }
    if (transpose) {
      std::swap(left, top);
      std::swap(right, bottom);
    }
    painter.fillRect(QRectF(left, top, right - left, bottom - top), ink);
  }
  painter.restore();
}

}  // namespace

QIcon align_edge_icon(AlignEdge edge) {
  // A one-pixel anchor line at the edge or center the bars line up with, and a
  // long and a short bar (4 px thick at 18 px) so the metaphor reads at the
  // options bar's size. Pixel-grid layout: paint_pixel_grid_glyph.
  auto glyph = [edge](QPainter& painter, const QColor& ink) {
    const bool transpose = !align_edge_is_horizontal(edge);
    if (edge == AlignEdge::HorizontalCenter || edge == AlignEdge::VerticalCenter) {
      // Odd bar lengths so the line runs through their middle column.
      paint_pixel_grid_glyph(painter, ink, {QRect(3, 3, 11, 4), QRect(5, 11, 7, 4), QRect(8, 1, 1, 16)}, false,
                             transpose);
      return;
    }
    const bool mirror = edge == AlignEdge::Right || edge == AlignEdge::Bottom;
    paint_pixel_grid_glyph(painter, ink, {QRect(3, 1, 1, 16), QRect(5, 3, 10, 4), QRect(5, 11, 6, 4)}, mirror,
                           transpose);
  };
  return themed_glyph_icon(QStringLiteral("align-edge-%1").arg(static_cast<int>(edge)), 22.0,
                           &ThemePalette::icon_ink, std::move(glyph));
}

QIcon distribute_mode_icon(DistributeMode mode) {
  // The vertical modes are the horizontal glyphs transposed, in align_edge_icon's
  // ink, line weight and bar thickness so the two button groups read as one set
  // (GitHub issue 405). Feature modes: a long and a short bar, each with its own
  // anchor line at the edge or center the command spaces evenly. Spacing modes:
  // three bars with the two equal gaps underlined. Pixel-grid layout:
  // paint_pixel_grid_glyph.
  const bool vertical = mode == DistributeMode::Top || mode == DistributeMode::VerticalCenter ||
                        mode == DistributeMode::Bottom || mode == DistributeMode::VerticalSpacing;
  auto glyph = [mode, vertical](QPainter& painter, const QColor& ink) {
    switch (mode) {
      case DistributeMode::HorizontalSpacing:
      case DistributeMode::VerticalSpacing:
        paint_pixel_grid_glyph(painter, ink,
                               {QRect(2, 2, 3, 11), QRect(8, 4, 3, 7), QRect(14, 3, 3, 9), QRect(5, 15, 3, 1),
                                QRect(11, 15, 3, 1)},
                               false, vertical);
        break;
      case DistributeMode::HorizontalCenter:
      case DistributeMode::VerticalCenter:
        // Odd bar widths so each line runs through its bar's middle column.
        paint_pixel_grid_glyph(painter, ink,
                               {QRect(2, 3, 5, 12), QRect(4, 1, 1, 16), QRect(11, 5, 5, 8), QRect(13, 1, 1, 16)},
                               false, vertical);
        break;
      default:
        paint_pixel_grid_glyph(painter, ink,
                               {QRect(2, 1, 1, 16), QRect(4, 3, 4, 12), QRect(10, 1, 1, 16), QRect(12, 5, 4, 8)},
                               mode == DistributeMode::Right || mode == DistributeMode::Bottom, vertical);
        break;
    }
  };
  return themed_glyph_icon(QStringLiteral("distribute-mode-%1").arg(static_cast<int>(mode)), 22.0,
                           &ThemePalette::icon_ink, std::move(glyph));
}

QIcon text_alignment_icon(Qt::Alignment alignment, bool vertical) {
  // Drawn in a 22-unit square: four ragged lines of text, the word-processor convention.
  // Long and short lines alternate so the shared edge (or center) reads at 18 px.
  const int mode = (alignment & Qt::AlignHCenter) != 0 ? 1 : (alignment & Qt::AlignRight) != 0 ? 2 : 0;
  auto glyph = [mode, vertical](QPainter& painter, const QColor& ink) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    constexpr double kLengths[] = {16.0, 10.0, 16.0, 8.0};
    constexpr double kStart = 3.0;
    constexpr double kSpan = 16.0;
    constexpr double kThickness = 2.0;
    for (int line = 0; line < 4; ++line) {
      const double length = kLengths[line];
      const double offset = mode == 0 ? kStart : mode == 1 ? kStart + (kSpan - length) / 2.0 : kStart + kSpan - length;
      // Horizontal type stacks lines top to bottom; vertical type sets columns right to left.
      const double across = 4.0 + line * 4.4;
      if (vertical) {
        painter.drawRect(QRectF(22.0 - across - kThickness, offset, kThickness, length));
      } else {
        painter.drawRect(QRectF(offset, across, length, kThickness));
      }
    }
  };
  return themed_glyph_icon(QStringLiteral("text-align-%1-%2").arg(mode).arg(vertical ? 1 : 0), 22.0,
                           &ThemePalette::icon_ink, std::move(glyph));
}

QIcon text_character_style_icon(int style) {
  // Drawn in a 22-unit square from the interface font's own letters (bold), so the eight marks
  // read as one set: two capitals, a capital and a small capital, x with a raised or lowered 2,
  // an underlined U, a struck S, 1 with a raised "st" and a built fraction; then the faux pair,
  // a B thickened by an outline and a serifed I leaning on a shear.
  auto glyph = [style](QPainter& painter, const QColor& ink) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto letters = [](const QString& text, double size, double x, double baseline) {
      auto font = QApplication::font();
      font.setBold(true);
      font.setPixelSize(100);
      QPainterPath path;
      path.addText(0.0, 0.0, font, text);
      QTransform transform;
      transform.translate(x, baseline);
      transform.scale(size / 100.0, size / 100.0);
      return transform.map(path);
    };
    QPainterPath path;
    const auto append_after = [&](const QString& text, double size, double baseline, double gap) {
      const auto x = path.isEmpty() ? 0.0 : path.boundingRect().right() + gap;
      path.addPath(letters(text, size, x, baseline));
    };
    std::optional<QRectF> bar;
    double outline = 0.0;
    switch (style) {
      case 0:
        append_after(QStringLiteral("AA"), 13.0, 16.0, 0.0);
        break;
      case 1:
        append_after(QStringLiteral("A"), 13.0, 16.0, 0.0);
        append_after(QStringLiteral("A"), 9.0, 16.0, 0.6);
        break;
      case 2:
        append_after(QStringLiteral("x"), 14.0, 18.0, 0.0);
        append_after(QStringLiteral("2"), 8.5, 10.0, 0.6);
        break;
      case 3:
        append_after(QStringLiteral("x"), 14.0, 14.0, 0.0);
        append_after(QStringLiteral("2"), 8.5, 20.0, 0.6);
        break;
      case 4:
        append_after(QStringLiteral("U"), 14.0, 15.5, 0.0);
        bar = QRectF(0.0, 17.6, 0.0, 1.8);
        break;
      case 5:
        append_after(QStringLiteral("S"), 14.0, 17.0, 0.0);
        bar = QRectF(0.0, 10.4, 0.0, 1.8);
        break;
      case 6:
        append_after(QStringLiteral("1"), 14.0, 18.0, 0.0);
        append_after(QStringLiteral("st"), 8.5, 10.0, 0.6);
        break;
      case 8:
        append_after(QStringLiteral("B"), 15.0, 17.5, 0.0);
        outline = 0.9;
        break;
      case 9: {
        // Stem and serifs upright, then sheared about the baseline like the render's faux italic.
        QPainterPath letter;
        letter.addRect(QRectF(8.4, 3.5, 3.2, 14.0));
        letter.addRect(QRectF(6.0, 3.5, 8.0, 2.2));
        letter.addRect(QRectF(6.0, 15.3, 8.0, 2.2));
        letter.setFillRule(Qt::WindingFill);
        QTransform shear;
        shear.translate(0.0, 17.5);
        shear.shear(-0.3, 0.0);
        shear.translate(0.0, -17.5);
        path.addPath(shear.map(letter));
        break;
      }
      default: {
        path.addPath(letters(QStringLiteral("1"), 9.0, 0.0, 10.0));
        const auto slash_left = path.boundingRect().right() - 0.5;
        QPainterPath slash;
        slash.moveTo(slash_left + 6.0, 2.5);
        slash.lineTo(slash_left + 7.6, 2.5);
        slash.lineTo(slash_left + 1.6, 19.5);
        slash.lineTo(slash_left, 19.5);
        slash.closeSubpath();
        path.addPath(slash);
        path.addPath(letters(QStringLiteral("2"), 9.0, slash_left + 4.5, 19.5));
        break;
      }
    }
    // Centre the letters across the square; a bar spans them with a little overhang.
    const auto bounds = path.boundingRect();
    const auto shift = 11.0 - (bounds.left() + bounds.right()) / 2.0;
    path.translate(shift, 0.0);
    painter.setPen(outline > 0.0 ? QPen(ink, outline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen));
    painter.setBrush(ink);
    painter.drawPath(path);
    if (bar.has_value()) {
      const auto letters_bounds = path.boundingRect();
      painter.drawRect(QRectF(letters_bounds.left() - 1.5, bar->top(), letters_bounds.width() + 3.0, bar->height()));
    }
  };
  return themed_glyph_icon(QStringLiteral("text-character-style-%1").arg(style), 22.0, &ThemePalette::icon_ink,
                           std::move(glyph));
}

}  // namespace patchy::ui
