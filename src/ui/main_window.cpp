#include "ui/main_window.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "filters/builtin_filters.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_presets.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/qt_geometry.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDialog>
#include <QItemSelection>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

#ifndef PHOTOSLOP_VERSION
#define PHOTOSLOP_VERSION "0.0.0"
#endif

namespace photoslop::ui {

namespace {

PixelBuffer make_solid_pixels(std::int32_t width, std::int32_t height, QColor color, PixelFormat format) {
  PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

QString tool_name(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QObject::tr("Move");
    case CanvasTool::Marquee:
      return QObject::tr("Marquee");
    case CanvasTool::EllipticalMarquee:
      return QObject::tr("Elliptical Marquee");
    case CanvasTool::Lasso:
      return QObject::tr("Lasso");
    case CanvasTool::MagicWand:
      return QObject::tr("Magic Wand");
    case CanvasTool::Brush:
      return QObject::tr("Brush");
    case CanvasTool::Clone:
      return QObject::tr("Clone Stamp");
    case CanvasTool::Smudge:
      return QObject::tr("Smudge");
    case CanvasTool::Eraser:
      return QObject::tr("Eraser");
    case CanvasTool::Gradient:
      return QObject::tr("Gradient");
    case CanvasTool::Line:
      return QObject::tr("Line");
    case CanvasTool::Rectangle:
      return QObject::tr("Rectangle");
    case CanvasTool::Ellipse:
      return QObject::tr("Ellipse");
    case CanvasTool::Fill:
      return QObject::tr("Fill");
    case CanvasTool::Eyedropper:
      return QObject::tr("Eyedropper");
    case CanvasTool::Text:
      return QObject::tr("Type");
    case CanvasTool::Pan:
      return QObject::tr("Pan");
    case CanvasTool::Zoom:
      return QObject::tr("Zoom");
  }
  return QObject::tr("Tool");
}

QIcon simple_icon(QString text, QColor accent = QColor(220, 226, 235)) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
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
  } else if (text == QStringLiteral("swap")) {
    painter.drawLine(8, 11, 23, 11);
    painter.drawLine(23, 11, 19, 7);
    painter.drawLine(23, 11, 19, 15);
    painter.drawLine(24, 21, 9, 21);
    painter.drawLine(9, 21, 13, 17);
    painter.drawLine(9, 21, 13, 25);
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
  } else {
    painter.setPen(accent);
    auto font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text.left(2).toUpper());
  }

  return QIcon(pixmap);
}

QIcon window_chrome_icon(QString role) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));

  if (role == QStringLiteral("minimize")) {
    painter.drawLine(QPointF(9.0, 21.0), QPointF(23.0, 21.0));
  } else if (role == QStringLiteral("maximize")) {
    painter.drawRect(QRectF(9.5, 9.5, 13.0, 13.0));
  } else if (role == QStringLiteral("close")) {
    painter.drawLine(QPointF(10.0, 10.0), QPointF(22.0, 22.0));
    painter.drawLine(QPointF(22.0, 10.0), QPointF(10.0, 22.0));
  }

  return QIcon(pixmap);
}

QString clean_action_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  auto label = action->text();
  label.remove(QLatin1Char('&'));
  return label.trimmed();
}

QString action_shortcut_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  QStringList shortcut_labels;
  for (const auto& shortcut : action->shortcuts()) {
    if (!shortcut.isEmpty()) {
      shortcut_labels << shortcut.toString(QKeySequence::NativeText);
    }
  }
  return shortcut_labels.join(QStringLiteral(", "));
}

void refresh_action_tooltip(QAction* action) {
  if (action == nullptr || action->isSeparator()) {
    return;
  }
  const auto label = clean_action_text(action);
  const auto shortcut = action_shortcut_text(action);
  action->setToolTip(shortcut.isEmpty() ? label : QObject::tr("%1 (%2)").arg(label, shortcut));
}

void apply_action_shortcut(QAction* action, QKeySequence shortcut) {
  action->setShortcut(shortcut);
  action->setShortcutContext(Qt::ApplicationShortcut);
  refresh_action_tooltip(action);
}


QString layer_visibility_indicator_text(bool visible) {
  return visible ? QStringLiteral("✓") : QString();
}

QString layer_visibility_tooltip(bool visible) {
  return visible ? QObject::tr("Layer visible. Click to hide.") : QObject::tr("Layer hidden. Click to show.");
}

class MouseDoubleClickFilter final : public QObject {
public:
  MouseDoubleClickFilter(std::function<void()> callback, QObject* parent)
      : QObject(parent), callback_(std::move(callback)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton) {
        if (callback_) {
          callback_();
        }
        mouse_event->accept();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> callback_;
};

class CheckGlyphBox final : public QCheckBox {
public:
  explicit CheckGlyphBox(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) {
    setMinimumHeight(24);
  }

  QSize sizeHint() const override {
    const auto text_width = fontMetrics().horizontalAdvance(text());
    const auto minimum = objectName() == QStringLiteral("shapeFillCheck") ? 58 : 92;
    return QSize(std::max(minimum, text_width + 34), 24);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool framed = objectName() == QStringLiteral("moveAutoSelectCheck") ||
                        objectName() == QStringLiteral("selectionAntiAliasCheck") ||
                        objectName() == QStringLiteral("cloneAlignedCheck") ||
                        objectName() == QStringLiteral("shapeFillCheck");
    if (framed) {
      painter.fillRect(rect(), QColor(41, 41, 41));
      painter.setPen(QPen(QColor(23, 23, 23), 1));
      painter.drawRect(rect().adjusted(0, 0, -1, -1));
      painter.setPen(QPen(QColor(93, 93, 93), 1));
      painter.drawLine(rect().topLeft(), rect().topRight());
    }

    const QRect box(7, (height() - 14) / 2, 14, 14);
    painter.setBrush(isChecked() ? QColor(20, 115, 230) : QColor(31, 31, 31));
    painter.setPen(QPen(isChecked() ? QColor(156, 207, 255) : QColor(120, 120, 120), 1));
    painter.drawRect(box);
    if (isChecked()) {
      painter.setPen(QPen(QColor(255, 255, 255), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QPointF(box.left() + 3.0, box.center().y() + 0.5), QPointF(box.left() + 6.0, box.bottom() - 3.0));
      painter.drawLine(QPointF(box.left() + 6.0, box.bottom() - 3.0), QPointF(box.right() - 2.0, box.top() + 3.0));
    }

    painter.setPen(isEnabled() ? QColor(240, 240, 240) : QColor(145, 145, 145));
    painter.drawText(QRect(box.right() + 7, 0, width() - box.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     text());
  }
};

void install_collapsible_dock_title(QDockWidget* dock,
                                    QWidget* content,
                                    const QString& object_prefix,
                                    int expanded_minimum_height = 0) {
  constexpr int kRightDockMinimumWidth = 280;
  dock->setMinimumWidth(kRightDockMinimumWidth);
  content->setMinimumWidth(kRightDockMinimumWidth - 18);
  if (expanded_minimum_height > 0) {
    dock->setMinimumHeight(expanded_minimum_height);
  }

  auto* title = new QWidget(dock);
  title->setObjectName(object_prefix + QStringLiteral("DockTitle"));
  title->setMinimumWidth(kRightDockMinimumWidth - 18);
  auto* layout = new QHBoxLayout(title);
  layout->setContentsMargins(7, 3, 7, 3);
  layout->setSpacing(6);

  auto* toggle = new QToolButton(title);
  toggle->setObjectName(object_prefix + QStringLiteral("DockCollapseButton"));
  toggle->setProperty("dockCollapseButton", true);
  toggle->setAutoRaise(false);
  toggle->setCheckable(true);
  toggle->setChecked(true);
  toggle->setText(QStringLiteral("v"));
  toggle->setFixedSize(18, 18);
  toggle->setToolTip(QObject::tr("Collapse panel"));
  layout->addWidget(toggle);

  auto* label = new QLabel(dock->windowTitle(), title);
  label->setObjectName(object_prefix + QStringLiteral("DockTitleLabel"));
  layout->addWidget(label, 1);

  QObject::connect(toggle, &QToolButton::toggled, dock,
                   [dock, content, toggle, expanded_minimum_height](bool expanded) {
    content->setVisible(expanded);
    toggle->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
    toggle->setToolTip(expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
    const auto collapsed_height = dock->titleBarWidget()->sizeHint().height() + 8;
    if (expanded_minimum_height > 0) {
      dock->setMinimumHeight(expanded ? expanded_minimum_height : collapsed_height);
    }
    dock->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : collapsed_height);
    dock->updateGeometry();
  });

  dock->setTitleBarWidget(title);
}

void restyle_layer_rows(QListWidget* list) {
  if (list == nullptr) {
    return;
  }
  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    auto* row_widget = list->itemWidget(item);
    if (row_widget == nullptr) {
      continue;
    }
    row_widget->setStyleSheet(item->isSelected()
                                  ? QStringLiteral("QWidget#layerRowWidget { background: #3a414a; }")
                                  : QStringLiteral("QWidget#layerRowWidget { background: #242628; }"));
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      auto font = name->font();
      font.setBold(item == list->currentItem());
      name->setFont(font);
    }
  }
}

QPixmap layer_mask_thumbnail(const LayerMask& mask) {
  constexpr int kSize = 28;
  QImage image(kSize, kSize, QImage::Format_RGB888);
  image.fill(QColor(mask.default_color, mask.default_color, mask.default_color));
  if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
    for (int y = 0; y < kSize; ++y) {
      const auto source_y = std::clamp(static_cast<int>((static_cast<double>(y) / kSize) * mask.pixels.height()), 0,
                                       std::max(0, mask.pixels.height() - 1));
      for (int x = 0; x < kSize; ++x) {
        const auto source_x = std::clamp(static_cast<int>((static_cast<double>(x) / kSize) * mask.pixels.width()), 0,
                                         std::max(0, mask.pixels.width() - 1));
        const auto value = *mask.pixels.pixel(source_x, source_y);
        image.setPixelColor(x, y, QColor(value, value, value));
      }
    }
  }

  QPixmap pixmap = QPixmap::fromImage(image);
  QPainter painter(&pixmap);
  painter.setPen(QPen(QColor(150, 158, 168), 1));
  painter.drawRect(QRect(0, 0, kSize - 1, kSize - 1));
  return pixmap;
}

QString adjustment_thumbnail_label(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return QStringLiteral("ADJ");
  }
  switch (settings->kind) {
    case AdjustmentKind::Levels:
      return QStringLiteral("LVL");
    case AdjustmentKind::Curves:
      return QStringLiteral("CRV");
    case AdjustmentKind::HueSaturation:
      return QStringLiteral("HSL");
    case AdjustmentKind::ColorBalance:
      return QStringLiteral("CB");
  }
  return QStringLiteral("ADJ");
}

QColor adjustment_thumbnail_accent(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return QColor(145, 175, 215);
  }
  switch (settings->kind) {
    case AdjustmentKind::Levels:
      return QColor(115, 180, 255);
    case AdjustmentKind::Curves:
      return QColor(130, 210, 155);
    case AdjustmentKind::HueSaturation:
      return QColor(215, 135, 255);
    case AdjustmentKind::ColorBalance:
      return QColor(245, 190, 100);
  }
  return QColor(145, 175, 215);
}

QString adjustment_layer_detail(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return QObject::tr("adjustment");
  }
  return QObject::tr("%1 adjustment").arg(QString::fromStdString(adjustment_display_name(settings->kind)));
}

QPixmap layer_content_thumbnail(const Layer& layer) {
  constexpr int kSize = 28;
  if (layer.kind() == LayerKind::Adjustment) {
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(QColor(30, 34, 40));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto accent = adjustment_thumbnail_accent(layer);
    painter.fillRect(QRect(0, 0, kSize, 5), accent);
    painter.setBrush(QColor(44, 49, 57));
    painter.setPen(QPen(QColor(12, 16, 22), 1));
    painter.drawRect(QRect(3, 7, 21, 15));
    painter.setPen(QPen(QColor(220, 228, 238), 1));
    const auto label = adjustment_thumbnail_label(layer);
    auto font = painter.font();
    font.setBold(true);
    font.setPixelSize(label.size() > 2 ? 7 : 8);
    painter.setFont(font);
    painter.drawText(QRect(3, 7, 21, 15), Qt::AlignCenter, label);
    painter.setPen(QPen(accent.lighter(130), 2));
    if (label == QStringLiteral("CRV")) {
      painter.drawLine(QPointF(5.0, 23.0), QPointF(10.0, 20.0));
      painter.drawLine(QPointF(10.0, 20.0), QPointF(16.0, 21.0));
      painter.drawLine(QPointF(16.0, 21.0), QPointF(23.0, 15.0));
    } else if (label == QStringLiteral("LVL")) {
      painter.drawLine(QPoint(5, 23), QPoint(9, 18));
      painter.drawLine(QPoint(9, 18), QPoint(13, 22));
      painter.drawLine(QPoint(13, 22), QPoint(18, 14));
      painter.drawLine(QPoint(18, 14), QPoint(23, 21));
    } else {
      painter.drawLine(QPoint(5, 23), QPoint(23, 23));
    }
    painter.setPen(QPen(QColor(150, 158, 168), 1));
    painter.drawRect(QRect(0, 0, kSize - 1, kSize - 1));
    return pixmap;
  }

  QImage image(kSize, kSize, QImage::Format_RGB888);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const bool dark = ((x / 7) + (y / 7)) % 2 == 0;
      image.setPixelColor(x, y, dark ? QColor(70, 74, 80) : QColor(112, 118, 126));
    }
  }

  const auto& pixels = layer.pixels();
  if (!pixels.empty() && pixels.format().bit_depth == BitDepth::UInt8 && pixels.format().channels >= 3) {
    for (int y = 0; y < kSize; ++y) {
      const auto source_y = std::clamp(static_cast<int>((static_cast<double>(y) / kSize) * pixels.height()), 0,
                                       std::max(0, pixels.height() - 1));
      for (int x = 0; x < kSize; ++x) {
        const auto source_x = std::clamp(static_cast<int>((static_cast<double>(x) / kSize) * pixels.width()), 0,
                                         std::max(0, pixels.width() - 1));
        const auto* px = pixels.pixel(source_x, source_y);
        const auto alpha = pixels.format().channels >= 4 ? static_cast<int>(px[3]) : 255;
        const auto base = image.pixelColor(x, y);
        image.setPixelColor(x, y,
                            QColor((static_cast<int>(px[0]) * alpha + base.red() * (255 - alpha)) / 255,
                                   (static_cast<int>(px[1]) * alpha + base.green() * (255 - alpha)) / 255,
                                   (static_cast<int>(px[2]) * alpha + base.blue() * (255 - alpha)) / 255));
      }
    }
  }

  QPixmap pixmap = QPixmap::fromImage(image);
  QPainter painter(&pixmap);
  painter.setPen(QPen(QColor(150, 158, 168), 1));
  painter.drawRect(QRect(0, 0, kSize - 1, kSize - 1));
  return pixmap;
}

QWidget* make_layer_row_widget(const Layer& layer, QListWidgetItem* item, QWidget* parent, int depth = 0,
                               bool ancestors_visible = true, bool group_expanded = true,
                               std::function<void(LayerId)> toggle_group_expanded = {},
                               std::function<void(LayerId, bool)> set_mask_linked = {}) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("layerRowWidget"));
  row->setAttribute(Qt::WA_StyledBackground, true);
  auto* list_parent = dynamic_cast<LayerListWidget*>(parent);
  if (list_parent != nullptr) {
    row->installEventFilter(list_parent);
  }
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(8 + std::max(0, depth) * 18, 5, 8, 5);
  layout->setSpacing(10);

  if (layer.kind() == LayerKind::Group) {
    auto* disclosure = new QToolButton(row);
    disclosure->setObjectName(QStringLiteral("layerFolderDisclosureButton"));
    disclosure->setCheckable(true);
    disclosure->setChecked(group_expanded);
    disclosure->setText(group_expanded ? QStringLiteral("v") : QStringLiteral(">"));
    disclosure->setToolButtonStyle(Qt::ToolButtonTextOnly);
    disclosure->setFixedSize(18, 20);
    disclosure->setEnabled(!layer.children().empty());
    disclosure->setToolTip(layer.children().empty()
                               ? QObject::tr("Folder is empty")
                               : group_expanded ? QObject::tr("Collapse folder") : QObject::tr("Expand folder"));
    QObject::connect(disclosure, &QToolButton::clicked, row,
                     [parent, id = layer.id(), toggle_group_expanded = std::move(toggle_group_expanded)] {
      if (toggle_group_expanded) {
        QTimer::singleShot(0, parent, [id, toggle_group_expanded] { toggle_group_expanded(id); });
      }
    });
    layout->addWidget(disclosure, 0, Qt::AlignVCenter);
  } else {
    auto* thumbnail = new QLabel(row);
    thumbnail->setObjectName(QStringLiteral("layerContentThumbnail"));
    thumbnail->setFixedSize(30, 30);
    thumbnail->setPixmap(layer_content_thumbnail(layer));
    thumbnail->setToolTip(QObject::tr("Layer thumbnail"));
    thumbnail->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      thumbnail->installEventFilter(list_parent);
    }
    layout->addWidget(thumbnail, 0, Qt::AlignVCenter);
  }

  auto* visibility = new QToolButton(row);
  visibility->setObjectName(QStringLiteral("layerVisibilityCheck"));
  visibility->setCheckable(true);
  visibility->setChecked(layer.visible());
  visibility->setText(layer_visibility_indicator_text(layer.visible()));
  visibility->setToolTip(layer_visibility_tooltip(layer.visible()));
  visibility->setToolButtonStyle(Qt::ToolButtonTextOnly);
  visibility->setFixedSize(20, 20);
  visibility->setEnabled(ancestors_visible);
  if (list_parent != nullptr) {
    visibility->installEventFilter(list_parent);
  }
  layout->addWidget(visibility, 0, Qt::AlignVCenter);

  if (layer.mask().has_value()) {
    auto* link = new QToolButton(row);
    link->setObjectName(QStringLiteral("layerMaskLinkButton"));
    link->setCheckable(true);
    link->setChecked(layer_mask_linked(layer));
    link->setText(link->isChecked() ? QStringLiteral("L") : QStringLiteral("U"));
    link->setToolTip(link->isChecked() ? QObject::tr("Layer and mask are linked")
                                       : QObject::tr("Layer and mask are unlinked"));
    link->setFixedSize(20, 20);
    link->setEnabled(ancestors_visible && layer.visible());
    QObject::connect(link, &QToolButton::toggled, row,
                     [parent, id = layer.id(), link, set_mask_linked = std::move(set_mask_linked)](bool checked) {
      link->setText(checked ? QStringLiteral("L") : QStringLiteral("U"));
      link->setToolTip(checked ? QObject::tr("Layer and mask are linked")
                               : QObject::tr("Layer and mask are unlinked"));
      if (set_mask_linked) {
        QTimer::singleShot(0, parent, [id, checked, set_mask_linked] { set_mask_linked(id, checked); });
      }
    });
    layout->addWidget(link, 0, Qt::AlignVCenter);

    auto* mask_preview = new QLabel(row);
    mask_preview->setObjectName(QStringLiteral("layerMaskThumbnail"));
    mask_preview->setFixedSize(30, 30);
    mask_preview->setPixmap(layer_mask_thumbnail(*layer.mask()));
    mask_preview->setToolTip(QObject::tr("Layer mask"));
    mask_preview->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      mask_preview->installEventFilter(list_parent);
    }
    layout->addWidget(mask_preview, 0, Qt::AlignVCenter);
  }

  auto* text_column = new QVBoxLayout();
  text_column->setContentsMargins(0, 0, 0, 0);
  text_column->setSpacing(0);
  layout->addLayout(text_column, 1);

  const auto display_name = layer.kind() == LayerKind::Group
                                ? QObject::tr("[Folder] %1").arg(QString::fromStdString(layer.name()))
                                : QString::fromStdString(layer.name());
  auto* name = new QLabel(display_name, row);
  name->setObjectName(QStringLiteral("layerRowName"));
  name->setTextFormat(Qt::PlainText);
  name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  name->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    name->installEventFilter(list_parent);
  }
  text_column->addWidget(name);

  const auto mode = blend_mode_name(layer.blend_mode());
  const auto lock = layer_locks_transparent_pixels(layer) ? QObject::tr(" locked") : QString();
  const auto effects = !layer.layer_style().empty() ? QObject::tr(" fx") : QString();
  const auto mask = layer.mask().has_value() ? QObject::tr(" mask") : QString();
  const auto dimensions = layer.kind() == LayerKind::Pixel
                              ? QObject::tr("%1 x %2").arg(layer.bounds().width).arg(layer.bounds().height)
                              : layer.kind() == LayerKind::Adjustment
                                    ? adjustment_layer_detail(layer)
                                    : QObject::tr("folder, %1 layers").arg(layer_descendant_count(layer));
  auto* details = new QLabel(QObject::tr("%1  %2%  %3%4%5%6")
                                 .arg(mode)
                                 .arg(static_cast<int>(std::round(layer.opacity() * 100.0F)))
                                 .arg(dimensions)
                                 .arg(lock)
                                 .arg(effects)
                                 .arg(mask),
                             row);
  details->setObjectName(QStringLiteral("layerRowDetails"));
  details->setTextFormat(Qt::PlainText);
  details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  details->setMinimumWidth(0);
  details->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    details->installEventFilter(list_parent);
  }
  text_column->addWidget(details);

  QObject::connect(visibility, &QToolButton::toggled, row, [item, visibility](bool checked) {
    visibility->setText(layer_visibility_indicator_text(checked));
    visibility->setToolTip(layer_visibility_tooltip(checked));
    if (item != nullptr && item->checkState() != (checked ? Qt::Checked : Qt::Unchecked)) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
  });
  return row;
}

QString open_file_filter() {
  return QObject::tr("Supported Files (*.psd *.psb *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
                     "Photoshop Documents (*.psd *.psb);;"
                     "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
                     "All Files (*.*)");
}

QString save_file_filter() {
  return QObject::tr("Photoshop Document (*.psd);;"
                     "PNG Image (*.png);;"
                     "JPEG Image (*.jpg *.jpeg);;"
                     "Bitmap Image (*.bmp);;"
                     "TIFF Image (*.tif *.tiff);;"
                     "WebP Image (*.webp)");
}

QString export_image_filter() {
  return QObject::tr("PNG Image (*.png);;"
                     "JPEG Image (*.jpg *.jpeg);;"
                     "Bitmap Image (*.bmp);;"
                     "TIFF Image (*.tif *.tiff);;"
                     "WebP Image (*.webp)");
}

QString extension_for_path(const QString& path) {
  return QFileInfo(path).suffix().toLower();
}

bool is_photoshop_document_extension(const QString& extension) {
  return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

bool is_supported_image_extension(const QString& extension) {
  static const QStringList supported = {
      QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("bmp"),
      QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp"),
  };
  return supported.contains(extension);
}

bool is_jpeg_extension(const QString& extension) {
  return extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg");
}

void write_flat_image_file(const Document& document, const QString& path, const QString& extension) {
  QImageWriter writer(path);
  if (is_jpeg_extension(extension)) {
    writer.setQuality(95);
  }
  const auto image = qimage_from_document(document, image_format_preserves_alpha(extension.toStdString()));
  if (!writer.write(image)) {
    throw std::runtime_error(writer.errorString().toStdString());
  }
}

bool is_supported_open_path(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    return false;
  }

  const auto extension = info.suffix().toLower();
  if (is_photoshop_document_extension(extension) || is_supported_image_extension(extension)) {
    return true;
  }
  return !QImageReader::imageFormat(path).isEmpty();
}

QStringList supported_local_open_paths(const QMimeData* mime_data) {
  QStringList paths;
  if (mime_data == nullptr || !mime_data->hasUrls()) {
    return paths;
  }

  for (const auto& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const auto path = QDir::toNativeSeparators(url.toLocalFile());
    if (is_supported_open_path(path) && !paths.contains(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

QString path_with_default_extension(QString path, const QString& selected_filter) {
  if (!QFileInfo(path).suffix().isEmpty()) {
    return path;
  }

  if (selected_filter.contains(QStringLiteral("PNG"))) {
    return path + QStringLiteral(".png");
  }
  if (selected_filter.contains(QStringLiteral("JPEG"))) {
    return path + QStringLiteral(".jpg");
  }
  if (selected_filter.contains(QStringLiteral("Bitmap"))) {
    return path + QStringLiteral(".bmp");
  }
  if (selected_filter.contains(QStringLiteral("TIFF"))) {
    return path + QStringLiteral(".tif");
  }
  if (selected_filter.contains(QStringLiteral("WebP"))) {
    return path + QStringLiteral(".webp");
  }
  return path + QStringLiteral(".psd");
}

std::string legacy_plugin_kind_name(LegacyPhotoshopPluginKind kind) {
  switch (kind) {
    case LegacyPhotoshopPluginKind::Filter8bf:
      return "filter";
    case LegacyPhotoshopPluginKind::Format8bi:
      return "file-format";
    case LegacyPhotoshopPluginKind::Automation8li:
      return "automation";
    case LegacyPhotoshopPluginKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

struct NewDocumentSettings {
  std::int32_t width{1024};
  std::int32_t height{768};
  QColor background{Qt::white};
};

struct NewLayerSettings {
  QString name;
  int opacity{100};
  BlendMode blend_mode{BlendMode::Normal};
};

struct CanvasSizeSettings {
  std::int32_t width{0};
  std::int32_t height{0};
  CanvasAnchor anchor{CanvasAnchor::Center};
  QColor extension_color{Qt::white};
};

struct ImageSizeSettings {
  std::int32_t width{0};
  std::int32_t height{0};
  int resolution{96};
  bool resample{true};
};

struct TextToolSettings {
  QString text;
  QString family;
  int size{36};
  bool bold{false};
  bool italic{false};
};

constexpr auto kTextEditorFinishedProperty = "photoslop.textEditorFinished";

bool mark_text_editor_finished(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return false;
  }
  editor->setProperty(kTextEditorFinishedProperty, true);
  return true;
}

class TextCommitFilter final : public QObject {
public:
  TextCommitFilter(QPointer<QTextEdit> editor, std::function<void()> commit, QObject* parent)
      : QObject(parent), editor_(std::move(editor)), commit_(std::move(commit)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if ((watched == editor_ || (editor_ != nullptr && watched == editor_->viewport())) &&
        event->type() == QEvent::FocusOut) {
      QTimer::singleShot(0, this, [this] {
        if (editor_ != nullptr && !editor_->hasFocus() && !editor_->viewport()->hasFocus()) {
          commit_();
        }
      });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QTextEdit> editor_;
  std::function<void()> commit_;
};

struct LayerTransformSettings {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{1};
  std::int32_t height{1};
};

std::optional<QString> request_text_input(QWidget* parent, const QString& object_name, const QString& title,
                                          const QString& label, const QString& initial) {
  QInputDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  dialog.setLabelText(label);
  dialog.setInputMode(QInputDialog::TextInput);
  dialog.setTextEchoMode(QLineEdit::Normal);
  dialog.setTextValue(initial);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog.textValue();
}

std::optional<int> request_integer_input(QWidget* parent, const QString& object_name, const QString& title,
                                         const QString& label, int value, int minimum, int maximum, int step) {
  QInputDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  dialog.setLabelText(label);
  dialog.setInputMode(QInputDialog::IntInput);
  dialog.setIntRange(minimum, maximum);
  dialog.setIntStep(step);
  dialog.setIntValue(value);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog.intValue();
}

std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopNewDocumentDialog"));
  dialog.setWindowTitle(QObject::tr("New Document"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("newDocumentPresetCombo"));
  preset->addItem(QObject::tr("Photoslop Default"), QSize(1024, 768));
  preset->addItem(QObject::tr("HD 1920 x 1080"), QSize(1920, 1080));
  preset->addItem(QObject::tr("Square 2048"), QSize(2048, 2048));
  preset->addItem(QObject::tr("Print Letter 300ppi"), QSize(2550, 3300));
  form->addRow(QObject::tr("Preset"), preset);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("newDocumentWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(1024);
  configure_dialog_spinbox(width);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("newDocumentHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(768);
  configure_dialog_spinbox(height);
  form->addRow(QObject::tr("Width"), width);
  form->addRow(QObject::tr("Height"), height);

  auto* background = new QComboBox(&dialog);
  background->setObjectName(QStringLiteral("newDocumentBackgroundCombo"));
  background->addItem(QObject::tr("White"), QColor(Qt::white));
  background->addItem(QObject::tr("Black"), QColor(Qt::black));
  background->addItem(QObject::tr("Transparent"), QColor(0, 0, 0, 0));
  form->addRow(QObject::tr("Background"), background);

  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [preset, width, height](int index) {
    const auto size = preset->itemData(index).toSize();
    if (size.isValid()) {
      width->setValue(size.width());
      height->setValue(size.height());
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return NewDocumentSettings{width->value(), height->value(), background->currentData().value<QColor>()};
}

std::optional<NewLayerSettings> request_new_layer_settings(QWidget* parent, int layer_number) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopNewLayerDialog"));
  dialog.setWindowTitle(QObject::tr("New Layer"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* name = new QLineEdit(QObject::tr("Layer %1").arg(layer_number), &dialog);
  name->setObjectName(QStringLiteral("newLayerNameEdit"));
  form->addRow(QObject::tr("Name"), name);

  auto* blend = new QComboBox(&dialog);
  blend->setObjectName(QStringLiteral("newLayerBlendCombo"));
  add_blend_mode_items(blend);
  form->addRow(QObject::tr("Mode"), blend);

  auto* opacity = new QSpinBox(&dialog);
  opacity->setObjectName(QStringLiteral("newLayerOpacitySpin"));
  opacity->setRange(0, 100);
  opacity->setValue(100);
  configure_dialog_spinbox(opacity, 72);
  form->addRow(QObject::tr("Opacity"), opacity);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (exec_dialog(dialog) != QDialog::Accepted || name->text().trimmed().isEmpty()) {
    return std::nullopt;
  }
  return NewLayerSettings{name->text().trimmed(), opacity->value(), static_cast<BlendMode>(blend->currentData().toInt())};
}

QString format_image_size_bytes(std::int32_t width, std::int32_t height, PixelFormat format) {
  const auto bytes = static_cast<double>(std::max<std::int32_t>(0, width)) *
                     static_cast<double>(std::max<std::int32_t>(0, height)) *
                     static_cast<double>(bytes_per_pixel(format));
  if (bytes >= 1024.0 * 1024.0) {
    return QObject::tr("%1M").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
  }
  return QObject::tr("%1K").arg(bytes / 1024.0, 0, 'f', 1);
}

QPixmap image_size_preview_pixmap(const Document& document, QSize preview_size) {
  QPixmap pixmap(preview_size);
  pixmap.fill(QColor(22, 22, 22));

  QPainter painter(&pixmap);
  constexpr int kTileSize = 12;
  for (int y = 0; y < preview_size.height(); y += kTileSize) {
    for (int x = 0; x < preview_size.width(); x += kTileSize) {
      const bool light_tile = ((x / kTileSize) + (y / kTileSize)) % 2 == 0;
      painter.fillRect(QRect(x, y, kTileSize, kTileSize), light_tile ? QColor(228, 228, 228) : QColor(176, 176, 176));
    }
  }

  const auto image = qimage_from_document(document, true);
  if (!image.isNull()) {
    const auto scaled = image.scaled(preview_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint position((preview_size.width() - scaled.width()) / 2, (preview_size.height() - scaled.height()) / 2);
    painter.drawImage(position, scaled);
  }
  painter.setPen(QPen(QColor(30, 30, 30), 1));
  painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
  painter.end();
  return pixmap;
}

QIcon canvas_anchor_icon(CanvasAnchor anchor) {
  QPixmap pixmap(22, 22);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(244, 244, 244), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(QColor(244, 244, 244));

  const QPointF center(11.0, 11.0);
  if (anchor == CanvasAnchor::Center) {
    painter.drawEllipse(center, 2.3, 2.3);
    return QIcon(pixmap);
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
  return QIcon(pixmap);
}

QString canvas_color_swatch_style(QColor color) {
  return QStringLiteral("QPushButton#canvasSizeExtensionColorSwatch { background: rgb(%1, %2, %3); "
                        "border: 1px solid #9a9a9a; border-radius: 3px; padding: 0; min-width: 49px; "
                        "max-width: 49px; min-height: 24px; max-height: 24px; } "
                        "QPushButton#canvasSizeExtensionColorSwatch:hover { border-color: #c8c8c8; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}

std::optional<ImageSizeSettings> request_image_size_settings(QWidget* parent, const Document& document) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopImageSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Image Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#photoslopImageSizeDialog {
      background: #555555;
      color: #f2f2f2;
    }
    QLabel {
      background: transparent;
      color: #f2f2f2;
    }
    QLabel#imageSizePreview {
      background: #1e1e1e;
      border: 1px solid #242424;
    }
    QLabel#imageSizeUpscaleLabel {
      color: #d8d8d8;
    }
    QSpinBox, QComboBox {
      background: #4a4a4a;
      border: 1px solid #686868;
      color: #ffffff;
      min-height: 24px;
      padding: 1px 6px;
    }
    QSpinBox:focus {
      border: 1px solid #1473e6;
      background: #3f3f3f;
    }
    QToolButton#imageSizeLinkButton {
      background: #4a4a4a;
      border: 1px solid #686868;
      min-width: 24px;
      max-width: 24px;
      min-height: 46px;
      max-height: 46px;
      padding: 0;
    }
    QToolButton#imageSizeLinkButton:checked {
      border-color: #9abbe7;
      background: #424f5f;
    }
    QDialogButtonBox QPushButton {
      background: #555555;
      border: 1px solid #8b8b8b;
      border-radius: 13px;
      color: #ffffff;
      min-width: 130px;
      min-height: 24px;
      padding: 0 18px;
    }
    QDialogButtonBox QPushButton:hover {
      border-color: #b5b5b5;
      background: #606060;
    }
  )"));
  dialog.resize(632, 386);

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(7, 10, 7, 10);
  root->setSpacing(10);

  auto* body = new QHBoxLayout();
  body->setSpacing(36);
  root->addLayout(body, 1);

  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("imageSizePreview"));
  preview->setFixedSize(276, 304);
  preview->setAlignment(Qt::AlignCenter);
  preview->setPixmap(image_size_preview_pixmap(document, preview->size()));
  body->addWidget(preview, 0, Qt::AlignTop);

  auto* controls = new QWidget(&dialog);
  auto* controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(0, 0, 0, 0);
  controls_layout->setSpacing(8);
  body->addWidget(controls, 1, Qt::AlignTop);

  auto* grid = new QGridLayout();
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(7);
  grid->setColumnMinimumWidth(0, 72);
  grid->setColumnMinimumWidth(1, 28);
  grid->setColumnMinimumWidth(2, 72);
  grid->setColumnMinimumWidth(3, 128);
  controls_layout->addLayout(grid);

  auto* image_size_value = new QLabel(&dialog);
  image_size_value->setObjectName(QStringLiteral("imageSizeSizeLabel"));
  auto* dimensions_value = new QLabel(&dialog);
  dimensions_value->setObjectName(QStringLiteral("imageSizeDimensionsLabel"));

  grid->addWidget(new QLabel(QObject::tr("Image Size:"), &dialog), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(image_size_value, 0, 1, 1, 3);
  grid->addWidget(new QLabel(QObject::tr("Dimensions:"), &dialog), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(dimensions_value, 1, 1, 1, 3);

  auto* fit = new QComboBox(&dialog);
  fit->setObjectName(QStringLiteral("imageSizeFitCombo"));
  fit->addItem(QObject::tr("Original Size"), QSize(document.width(), document.height()));
  fit->addItem(QObject::tr("Fit 640 x 480"), QSize(640, 480));
  fit->addItem(QObject::tr("Fit 1024 x 768"), QSize(1024, 768));
  fit->addItem(QObject::tr("Fit 1920 x 1080"), QSize(1920, 1080));
  grid->addWidget(new QLabel(QObject::tr("Fit To:"), &dialog), 2, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(fit, 2, 1, 1, 3);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("imageSizeWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(document.width());
  configure_dialog_spinbox(width, 72);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("imageSizeHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(document.height());
  configure_dialog_spinbox(height, 72);

  auto* width_unit = new QComboBox(&dialog);
  width_unit->setObjectName(QStringLiteral("imageSizeWidthUnitCombo"));
  width_unit->addItem(QObject::tr("Pixels"));
  auto* height_unit = new QComboBox(&dialog);
  height_unit->setObjectName(QStringLiteral("imageSizeHeightUnitCombo"));
  height_unit->addItem(QObject::tr("Pixels"));

  auto* link = new QToolButton(&dialog);
  link->setObjectName(QStringLiteral("imageSizeLinkButton"));
  link->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  link->setIconSize(QSize(18, 18));
  link->setCheckable(true);
  link->setChecked(true);
  link->setToolTip(QObject::tr("Constrain proportions"));

  grid->addWidget(new QLabel(QObject::tr("Width:"), &dialog), 3, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(link, 3, 1, 2, 1, Qt::AlignCenter);
  grid->addWidget(width, 3, 2);
  grid->addWidget(width_unit, 3, 3);
  grid->addWidget(new QLabel(QObject::tr("Height:"), &dialog), 4, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(height, 4, 2);
  grid->addWidget(height_unit, 4, 3);

  auto* resolution = new QSpinBox(&dialog);
  resolution->setObjectName(QStringLiteral("imageSizeResolutionSpin"));
  resolution->setRange(1, 9999);
  resolution->setValue(96);
  configure_dialog_spinbox(resolution, 72);
  auto* resolution_unit = new QComboBox(&dialog);
  resolution_unit->setObjectName(QStringLiteral("imageSizeResolutionUnitCombo"));
  resolution_unit->addItem(QObject::tr("Pixels/Inch"));
  grid->addWidget(new QLabel(QObject::tr("Resolution:"), &dialog), 5, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(resolution, 5, 2);
  grid->addWidget(resolution_unit, 5, 3);

  auto* resample = new QCheckBox(QObject::tr("Resample:"), &dialog);
  resample->setObjectName(QStringLiteral("imageSizeResampleCheck"));
  resample->setChecked(true);
  auto* resample_method = new QComboBox(&dialog);
  resample_method->setObjectName(QStringLiteral("imageSizeResampleCombo"));
  resample_method->addItem(QObject::tr("Bicubic Sharper (reduction)"));
  resample_method->addItem(QObject::tr("Bicubic Smoother (enlargement)"));
  resample_method->addItem(QObject::tr("Nearest Neighbor"));
  grid->addWidget(resample, 6, 0, Qt::AlignRight | Qt::AlignVCenter);
  grid->addWidget(resample_method, 6, 1, 1, 3);

  auto* upscale_label = new QLabel(QObject::tr("Create a new, larger document with more detail\n"
                                               "Open in Generative Upscale..."),
                                   &dialog);
  upscale_label->setObjectName(QStringLiteral("imageSizeUpscaleLabel"));
  upscale_label->setWordWrap(true);
  upscale_label->setMinimumHeight(54);
  controls_layout->addSpacing(26);
  controls_layout->addWidget(upscale_label);
  controls_layout->addStretch(1);

  const auto update_summary = [image_size_value, dimensions_value, width, height, &document] {
    image_size_value->setText(format_image_size_bytes(width->value(), height->value(), document.format()));
    dimensions_value->setText(QObject::tr("%1 px x %2 px").arg(width->value()).arg(height->value()));
  };
  update_summary();

  const double aspect_ratio =
      document.height() > 0 ? static_cast<double>(document.width()) / static_cast<double>(document.height()) : 1.0;
  bool updating_dimensions = false;
  QObject::connect(width, &QSpinBox::valueChanged, &dialog, [height, link, aspect_ratio, update_summary,
                                                             &updating_dimensions](int value) {
    if (!updating_dimensions && link->isChecked()) {
      updating_dimensions = true;
      height->setValue(std::clamp(static_cast<int>(std::lround(static_cast<double>(value) / aspect_ratio)), 1, 30000));
      updating_dimensions = false;
    }
    update_summary();
  });
  QObject::connect(height, &QSpinBox::valueChanged, &dialog, [width, link, aspect_ratio, update_summary,
                                                              &updating_dimensions](int value) {
    if (!updating_dimensions && link->isChecked()) {
      updating_dimensions = true;
      width->setValue(std::clamp(static_cast<int>(std::lround(static_cast<double>(value) * aspect_ratio)), 1, 30000));
      updating_dimensions = false;
    }
    update_summary();
  });
  QObject::connect(fit, &QComboBox::currentIndexChanged, &dialog, [fit, width, height, update_summary](int index) {
    const auto size = fit->itemData(index).toSize();
    if (!size.isValid()) {
      return;
    }
    const QSignalBlocker block_width(width);
    const QSignalBlocker block_height(height);
    width->setValue(size.width());
    height->setValue(size.height());
    update_summary();
  });
  QObject::connect(resample, &QCheckBox::toggled, &dialog, [fit, width, height, link, resample_method](bool checked) {
    fit->setEnabled(checked);
    width->setEnabled(checked);
    height->setEnabled(checked);
    link->setEnabled(checked);
    resample_method->setEnabled(checked);
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return ImageSizeSettings{width->value(), height->value(), resolution->value(), resample->isChecked()};
}

std::optional<CanvasSizeSettings> request_canvas_size_settings(QWidget* parent, const Document& document) {
  const auto current_width = document.width();
  const auto current_height = document.height();
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopCanvasSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Canvas Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#photoslopCanvasSizeDialog {
      background: #555555;
      color: #ffffff;
    }
    QDialog#photoslopCanvasSizeDialog QWidget {
      background: #555555;
      color: #ffffff;
    }
    QDialog#photoslopCanvasSizeDialog QLabel {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
    }
    QDialog#photoslopCanvasSizeDialog QLabel[sectionLabel="true"] {
      font-weight: 700;
    }
    QDialog#photoslopCanvasSizeDialog QFrame#canvasSizeSeparator {
      background: #727272;
      color: #727272;
      min-height: 1px;
      max-height: 1px;
      border: 0;
    }
    QDialog#photoslopCanvasSizeDialog QSpinBox,
    QDialog#photoslopCanvasSizeDialog QComboBox {
      background: #4f4f4f;
      border: 1px solid #767676;
      border-radius: 3px;
      color: #ffffff;
      min-height: 22px;
      padding: 0 8px;
    }
    QDialog#photoslopCanvasSizeDialog QSpinBox:focus,
    QDialog#photoslopCanvasSizeDialog QComboBox:focus {
      border-color: #9abbe7;
      background: #4a4a4a;
    }
    QDialog#photoslopCanvasSizeDialog QComboBox::drop-down {
      border: 0;
      width: 22px;
    }
    QDialog#photoslopCanvasSizeDialog QCheckBox {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
      spacing: 7px;
    }
    QDialog#photoslopCanvasSizeDialog QCheckBox::indicator {
      width: 11px;
      height: 11px;
      background: #5a5a5a;
      border: 1px solid #8a8a8a;
    }
    QDialog#photoslopCanvasSizeDialog QCheckBox::indicator:checked {
      background: #2f75bd;
      border-color: #9abbe7;
    }
    QDialog#photoslopCanvasSizeDialog QToolButton#canvasSizeAnchorButton {
      background: #5c5c5c;
      border: 1px solid #777777;
      border-radius: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
      padding: 0;
    }
    QDialog#photoslopCanvasSizeDialog QToolButton#canvasSizeAnchorButton:hover {
      background: #656565;
      border-color: #9a9a9a;
    }
    QDialog#photoslopCanvasSizeDialog QToolButton#canvasSizeAnchorButton:checked {
      background: #4a4a4a;
      border-color: #c8c8c8;
    }
    QDialog#photoslopCanvasSizeDialog QPushButton {
      background: #555555;
      border: 1px solid #8c8c8c;
      border-radius: 13px;
      color: #ffffff;
      min-width: 70px;
      min-height: 24px;
      padding: 0 14px;
      font-size: 11px;
      font-weight: 700;
    }
    QDialog#photoslopCanvasSizeDialog QPushButton:hover {
      background: #606060;
      border-color: #b7b7b7;
    }
    QDialog#photoslopCanvasSizeDialog QPushButton#canvasSizeOkButton {
      border-color: #2d8cff;
      border-width: 2px;
    }
  )"));
  dialog.resize(453, 377);

  auto* root = new QHBoxLayout(&dialog);
  root->setContentsMargins(15, 17, 14, 14);
  root->setSpacing(12);

  auto* content = new QWidget(&dialog);
  content->setObjectName(QStringLiteral("canvasSizeContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(7);
  root->addWidget(content, 1, Qt::AlignTop);

  auto* current_size_label =
      new QLabel(QObject::tr("Current Size: %1").arg(format_image_size_bytes(current_width, current_height, document.format())),
                 &dialog);
  current_size_label->setObjectName(QStringLiteral("canvasSizeCurrentSizeLabel"));
  current_size_label->setProperty("sectionLabel", true);
  content_layout->addWidget(current_size_label);

  auto* current_grid = new QGridLayout();
  current_grid->setContentsMargins(0, 0, 0, 0);
  current_grid->setHorizontalSpacing(8);
  current_grid->setVerticalSpacing(5);
  current_grid->setColumnMinimumWidth(0, 52);
  content_layout->addLayout(current_grid);
  current_grid->addWidget(new QLabel(QObject::tr("Width"), &dialog), 0, 0);
  current_grid->addWidget(new QLabel(QObject::tr("%1 px").arg(current_width), &dialog), 0, 1);
  current_grid->addWidget(new QLabel(QObject::tr("Height"), &dialog), 1, 0);
  current_grid->addWidget(new QLabel(QObject::tr("%1 px").arg(current_height), &dialog), 1, 1);

  auto* separator = new QFrame(&dialog);
  separator->setObjectName(QStringLiteral("canvasSizeSeparator"));
  separator->setFrameShape(QFrame::HLine);
  content_layout->addWidget(separator);

  auto* new_size_label = new QLabel(&dialog);
  new_size_label->setObjectName(QStringLiteral("canvasSizeNewSizeLabel"));
  new_size_label->setProperty("sectionLabel", true);
  content_layout->addWidget(new_size_label);

  auto* size_grid = new QGridLayout();
  size_grid->setContentsMargins(0, 0, 42, 0);
  size_grid->setHorizontalSpacing(8);
  size_grid->setVerticalSpacing(7);
  size_grid->setColumnMinimumWidth(0, 38);
  content_layout->addLayout(size_grid);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("canvasSizeWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(current_width);
  configure_dialog_spinbox(width, 84);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("canvasSizeHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(current_height);
  configure_dialog_spinbox(height, 84);

  auto* width_unit = new QComboBox(&dialog);
  width_unit->setObjectName(QStringLiteral("canvasSizeWidthUnitCombo"));
  width_unit->addItem(QObject::tr("Pixels"));
  width_unit->setMinimumWidth(160);
  auto* height_unit = new QComboBox(&dialog);
  height_unit->setObjectName(QStringLiteral("canvasSizeHeightUnitCombo"));
  height_unit->addItem(QObject::tr("Pixels"));
  height_unit->setMinimumWidth(160);

  size_grid->addWidget(new QLabel(QObject::tr("Width"), &dialog), 0, 0, Qt::AlignVCenter);
  size_grid->addWidget(width, 0, 1);
  size_grid->addWidget(width_unit, 0, 2);
  size_grid->addWidget(new QLabel(QObject::tr("Height"), &dialog), 1, 0, Qt::AlignVCenter);
  size_grid->addWidget(height, 1, 1);
  size_grid->addWidget(height_unit, 1, 2);

  auto* relative = new QCheckBox(QObject::tr("Relative to current dimension"), &dialog);
  relative->setObjectName(QStringLiteral("canvasSizeRelativeCheck"));
  auto* relative_row = new QHBoxLayout();
  relative_row->setContentsMargins(45, 7, 0, 0);
  relative_row->setSpacing(0);
  relative_row->addWidget(relative);
  relative_row->addStretch(1);
  content_layout->addLayout(relative_row);

  auto* anchor_row = new QHBoxLayout();
  anchor_row->setContentsMargins(0, 4, 0, 0);
  anchor_row->setSpacing(10);
  content_layout->addLayout(anchor_row);
  anchor_row->addWidget(new QLabel(QObject::tr("Anchor"), &dialog), 0, Qt::AlignTop);

  auto* anchor_grid_widget = new QWidget(&dialog);
  anchor_grid_widget->setObjectName(QStringLiteral("canvasSizeAnchorGrid"));
  auto* anchor_grid = new QGridLayout(anchor_grid_widget);
  anchor_grid->setContentsMargins(0, 0, 0, 0);
  anchor_grid->setSpacing(0);
  anchor_row->addWidget(anchor_grid_widget, 0, Qt::AlignLeft | Qt::AlignTop);
  anchor_row->addStretch(1);

  auto* anchor_group = new QButtonGroup(&dialog);
  anchor_group->setExclusive(true);
  const std::array<std::pair<CanvasAnchor, QString>, 9> anchors{{
      {CanvasAnchor::TopLeft, QObject::tr("Anchor top left")},
      {CanvasAnchor::Top, QObject::tr("Anchor top")},
      {CanvasAnchor::TopRight, QObject::tr("Anchor top right")},
      {CanvasAnchor::Left, QObject::tr("Anchor left")},
      {CanvasAnchor::Center, QObject::tr("Anchor center")},
      {CanvasAnchor::Right, QObject::tr("Anchor right")},
      {CanvasAnchor::BottomLeft, QObject::tr("Anchor bottom left")},
      {CanvasAnchor::Bottom, QObject::tr("Anchor bottom")},
      {CanvasAnchor::BottomRight, QObject::tr("Anchor bottom right")},
  }};
  for (int index = 0; index < static_cast<int>(anchors.size()); ++index) {
    auto* button = new QToolButton(anchor_grid_widget);
    button->setObjectName(QStringLiteral("canvasSizeAnchorButton"));
    button->setIcon(canvas_anchor_icon(anchors[static_cast<std::size_t>(index)].first));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(anchors[static_cast<std::size_t>(index)].second);
    button->setCheckable(true);
    anchor_group->addButton(button, static_cast<int>(anchors[static_cast<std::size_t>(index)].first));
    anchor_grid->addWidget(button, index / 3, index % 3);
    if (anchors[static_cast<std::size_t>(index)].first == CanvasAnchor::Center) {
      button->setChecked(true);
    }
  }

  auto* extension_row = new QHBoxLayout();
  extension_row->setContentsMargins(0, 6, 0, 0);
  extension_row->setSpacing(8);
  content_layout->addLayout(extension_row);
  auto* extension_label = new QLabel(QObject::tr("Canvas extension color"), &dialog);
  extension_label->setFixedWidth(116);
  extension_row->addWidget(extension_label, 0, Qt::AlignVCenter);

  auto* extension_color = new QComboBox(&dialog);
  extension_color->setObjectName(QStringLiteral("canvasSizeExtensionColorCombo"));
  extension_color->addItem(QObject::tr("Other..."), QColor(Qt::white));
  extension_color->addItem(QObject::tr("White"), QColor(Qt::white));
  extension_color->addItem(QObject::tr("Black"), QColor(Qt::black));
  extension_color->addItem(QObject::tr("Gray"), QColor(128, 128, 128));
  extension_color->setFixedWidth(154);
  extension_row->addWidget(extension_color, 0);

  auto* color_swatch = new QPushButton(&dialog);
  color_swatch->setObjectName(QStringLiteral("canvasSizeExtensionColorSwatch"));
  color_swatch->setAccessibleName(QObject::tr("Canvas extension color"));
  color_swatch->setToolTip(QObject::tr("Choose canvas extension color"));
  color_swatch->setCursor(Qt::PointingHandCursor);
  color_swatch->setFocusPolicy(Qt::StrongFocus);
  color_swatch->setFixedSize(49, 24);
  extension_row->addWidget(color_swatch);
  content_layout->addStretch(1);

  QColor extension_color_value(Qt::white);
  const auto update_swatch = [&extension_color_value, color_swatch] {
    color_swatch->setStyleSheet(canvas_color_swatch_style(extension_color_value));
  };
  update_swatch();

  const auto choose_extension_color = [extension_color, &dialog, &extension_color_value, update_swatch] {
    const auto selected = QColorDialog::getColor(extension_color_value, &dialog, QObject::tr("Canvas Extension Color"));
    if (!selected.isValid()) {
      update_swatch();
      return;
    }
    extension_color_value = selected;
    extension_color->setItemData(0, selected);
    extension_color->setCurrentIndex(0);
    update_swatch();
  };

  QObject::connect(extension_color, &QComboBox::activated, &dialog, [extension_color, &dialog, &extension_color_value,
                                                                      update_swatch, choose_extension_color](int index) {
    if (index == 0) {
      choose_extension_color();
      return;
    }
    extension_color_value = extension_color->itemData(index).value<QColor>();
    update_swatch();
  });
  QObject::connect(color_swatch, &QPushButton::clicked, &dialog, choose_extension_color);

  const auto target_width = [current_width, width, relative] {
    return relative->isChecked() ? current_width + width->value() : width->value();
  };
  const auto target_height = [current_height, height, relative] {
    return relative->isChecked() ? current_height + height->value() : height->value();
  };
  const auto update_summary = [new_size_label, target_width, target_height, &document] {
    new_size_label->setText(
        QObject::tr("New Size: %1").arg(format_image_size_bytes(target_width(), target_height(), document.format())));
  };
  update_summary();

  QObject::connect(width, &QSpinBox::valueChanged, &dialog, [update_summary](int) { update_summary(); });
  QObject::connect(height, &QSpinBox::valueChanged, &dialog, [update_summary](int) { update_summary(); });
  QObject::connect(relative, &QCheckBox::toggled, &dialog, [current_width, current_height, width, height,
                                                            update_summary](bool checked) {
    const auto absolute_width = checked ? width->value() : current_width + width->value();
    const auto absolute_height = checked ? height->value() : current_height + height->value();
    const QSignalBlocker block_width(width);
    const QSignalBlocker block_height(height);
    if (checked) {
      width->setRange(1 - current_width, 30000 - current_width);
      height->setRange(1 - current_height, 30000 - current_height);
      width->setValue(absolute_width - current_width);
      height->setValue(absolute_height - current_height);
    } else {
      width->setRange(1, 30000);
      height->setRange(1, 30000);
      width->setValue(std::clamp(absolute_width, 1, 30000));
      height->setValue(std::clamp(absolute_height, 1, 30000));
    }
    update_summary();
  });

  auto* button_column = new QWidget(&dialog);
  button_column->setObjectName(QStringLiteral("canvasSizeButtonColumn"));
  auto* button_layout = new QVBoxLayout(button_column);
  button_layout->setContentsMargins(0, 1, 0, 0);
  button_layout->setSpacing(9);
  root->addWidget(button_column, 0, Qt::AlignTop);

  auto* ok = new QPushButton(QObject::tr("OK"), &dialog);
  ok->setObjectName(QStringLiteral("canvasSizeOkButton"));
  ok->setDefault(true);
  auto* cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel->setObjectName(QStringLiteral("canvasSizeCancelButton"));
  ok->setFixedSize(73, 28);
  cancel->setFixedSize(73, 28);
  button_layout->addWidget(ok);
  button_layout->addWidget(cancel);
  button_layout->addStretch(1);
  QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const auto checked_anchor =
      anchor_group->checkedId() < 0 ? CanvasAnchor::Center : static_cast<CanvasAnchor>(anchor_group->checkedId());
  return CanvasSizeSettings{target_width(), target_height(), checked_anchor, extension_color_value};
}

std::optional<LayerTransformSettings> request_layer_transform_settings(QWidget* parent, Rect current) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopTransformDialog"));
  dialog.setWindowTitle(QObject::tr("Free Transform"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto make_spin = [&dialog](const QString& object_name, int value, int minimum, int maximum) {
    auto* spin = new QSpinBox(&dialog);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    configure_dialog_spinbox(spin);
    return spin;
  };

  auto* x = make_spin(QStringLiteral("transformXSpin"), current.x, -30000, 30000);
  auto* y = make_spin(QStringLiteral("transformYSpin"), current.y, -30000, 30000);
  auto* width = make_spin(QStringLiteral("transformWidthSpin"), current.width, 1, 30000);
  auto* height = make_spin(QStringLiteral("transformHeightSpin"), current.height, 1, 30000);
  form->addRow(QObject::tr("X"), x);
  form->addRow(QObject::tr("Y"), y);
  form->addRow(QObject::tr("W"), width);
  form->addRow(QObject::tr("H"), height);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return LayerTransformSettings{x->value(), y->value(), width->value(), height->value()};
}

QString photoshop_style() {
  return QStringLiteral(R"(
    QMainWindow, QMenuBar, QMenu, QDockWidget, QWidget {
      background: #262626;
      color: #e6e6e6;
      font-size: 12px;
    }
    QMainWindow {
      border: 1px solid #1f1f1f;
    }
    QMenuBar {
      background: #4f4f4f;
      color: #f0f0f0;
      border-bottom: 1px solid #343434;
      min-height: 34px;
      max-height: 34px;
      padding-left: 35px;
    }
    QMenuBar::item {
      background: transparent;
      min-height: 34px;
      padding: 0 10px;
      margin: 0 1px;
    }
    QMenuBar::item:selected, QMenu::item:selected {
      background: #3a3a3a;
    }
    QLabel#photoshopBadge {
      background: #001e36;
      border: 1px solid #1473e6;
      color: #31a8ff;
      font-size: 10px;
      font-weight: 700;
    }
    QMenu {
      background: #3a3a3a;
      border: 1px solid #1f1f1f;
    }
    QMenu::item {
      padding: 7px 34px 7px 24px;
    }
    QMenu::separator {
      height: 1px;
      background: #555555;
      margin: 4px 6px;
    }
    QToolBar {
      background: #3b3b3b;
      border: 0;
      border-bottom: 1px solid #292929;
      spacing: 2px;
      padding: 3px;
    }
    QToolButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 3px;
      min-width: 26px;
      min-height: 26px;
    }
    QToolButton:hover {
      background: #4a4a4a;
      border-color: #696969;
    }
    QToolButton:checked {
      background: #2f75bd;
      border-color: #6bb3ff;
    }
    QWidget#windowChromeControls {
      background: #4f4f4f;
    }
    QToolButton[windowChromeButton="true"] {
      background: transparent;
      border: 0;
      border-radius: 0;
      padding: 0;
      min-width: 46px;
      max-width: 46px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton[windowChromeButton="true"]:hover {
      background: #626262;
      border: 0;
    }
    QToolButton[windowChromeButton="true"]:pressed {
      background: #3c3c3c;
    }
    QToolButton#windowCloseButton:hover {
      background: #c42b1c;
    }
    QToolButton#windowCloseButton:pressed {
      background: #9f2117;
    }
    QToolBar#toolPalette {
      background: #535353;
      border-right: 1px solid #202020;
      border-bottom: 0;
      padding: 3px 4px;
      spacing: 1px;
    }
    QToolBar#toolPalette QToolButton {
      min-width: 28px;
      max-width: 28px;
      min-height: 24px;
      max-height: 24px;
      padding: 1px;
    }
    QWidget#toolPaletteSpacer {
      background: #535353;
    }
    QToolBar#Options {
      background: #3d3d3d;
      min-height: 38px;
      max-height: 38px;
      border-top: 1px solid #5a5a5a;
      border-bottom: 1px solid #292929;
      spacing: 5px;
      padding: 4px 7px;
    }
    QToolBar#Options QLabel {
      color: #e1e1e1;
      padding-left: 5px;
      padding-right: 2px;
    }
    QToolBar#Options QLabel[optionLabel="true"] {
      background: #262626;
      border: 1px solid #171717;
      border-right: 0;
      border-top-color: #5d5d5d;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 7px;
    }
    QToolBar#Options QSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
      min-height: 24px;
      max-height: 24px;
      padding-left: 4px;
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
    }
    QWidget#selectionFeatherGroup {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      min-height: 24px;
      max-height: 24px;
    }
    QWidget#selectionFeatherGroup QLabel {
      background: #262626;
      border: 0;
      border-right: 1px solid #171717;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 8px;
    }
    QWidget#selectionFeatherGroup QSpinBox {
      background: #292929;
      border: 0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
    }
    QToolBar#Options QCheckBox {
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
      padding-right: 8px;
      spacing: 6px;
    }
    QToolBar#Options QCheckBox#selectionAntiAliasCheck {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding-left: 7px;
      padding-right: 10px;
    }
    QToolBar#Options QCheckBox::indicator {
      width: 14px;
      height: 14px;
      background: #1f1f1f;
      border: 1px solid #777777;
    }
    QToolBar#Options QCheckBox::indicator:hover {
      border-color: #9ccfff;
    }
    QToolBar#Options QCheckBox::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
    }
    QToolBar#Options QSlider::groove:horizontal {
      height: 4px;
      background: #1c1c1c;
      border: 1px solid #555555;
    }
    QToolBar#Options QSlider::sub-page:horizontal {
      background: #1473e6;
      border: 1px solid #5aa9ff;
    }
    QToolBar#Options QSlider::handle:horizontal {
      background: #c9d0d8;
      border: 1px solid #101010;
      width: 10px;
      margin: -5px 0;
    }
    QToolBar#Options QPushButton {
      min-height: 24px;
      max-height: 24px;
      background: #303030;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding: 1px 7px;
    }
    QToolBar#Options QPushButton:checked {
      background: #1667b7;
      border-color: #63adff;
      color: #ffffff;
    }
    QDockWidget::title {
      background: #323232;
      padding: 5px;
      border-bottom: 1px solid #202020;
    }
    QWidget#historyDockTitle, QWidget#swatchesDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
    QWidget#layersDockTitle {
      background: #2f3032;
      border-top: 1px solid #45474b;
      border-bottom: 1px solid #1b1c1e;
    }
    QWidget#historyDockTitle QLabel, QWidget#swatchesDockTitle QLabel, QWidget#propertiesDockTitle QLabel,
    QWidget#infoDockTitle QLabel, QWidget#layersDockTitle QLabel {
      color: #f0f0f0;
      font-weight: 600;
    }
    QToolButton[dockCollapseButton="true"] {
      background: transparent;
      color: #cfd3d8;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 18px;
      max-height: 18px;
      font-weight: 700;
    }
    QToolButton[dockCollapseButton="true"]:hover {
      background: #3b3d40;
      border-color: #5b5e63;
    }
    QToolButton[dockCollapseButton="true"]:checked {
      background: transparent;
      color: #cfd3d8;
      border-color: transparent;
    }
    QListWidget, QComboBox, QSpinBox, QSlider, QLineEdit, QTextEdit {
      background: #2b2b2b;
      color: #e6e6e6;
      border: 1px solid #5a5a5a;
      selection-background-color: #3a414a;
      min-height: 20px;
    }
    QListWidget::item {
      min-height: 48px;
      padding: 0;
      border-bottom: 1px solid #202225;
    }
    QListWidget::item:selected {
      background: #3a414a;
      color: #f4f6f8;
      border: 1px solid #67717d;
    }
    QListWidget#layerList::item {
      color: transparent;
    }
    QListWidget#layerList::item:selected {
      color: transparent;
    }
    QListWidget::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      background: transparent;
      border: 0;
      margin: 0;
    }
    QListWidget::indicator:checked {
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::item {
      min-height: 24px;
      padding: 4px 6px;
      border-bottom: 1px solid #3b3b3b;
    }
    QListWidget#layerStyleCategoryList::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      margin: 0;
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::indicator:checked {
      background: transparent;
      border: 0;
    }
    QLabel#layerRowName {
      color: #f0f3f8;
      font-size: 12px;
    }
    QLabel#layerRowDetails {
      color: #aeb6c2;
      font-size: 10px;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: #d7dde6;
      line-height: 130%;
    }
    QWidget#layersPanel {
      background: #28292b;
    }
    QListWidget#layerList {
      min-height: 300px;
    }
    QToolButton#layerFolderDisclosureButton {
      background: transparent;
      color: #d9e0ea;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      font-size: 10px;
      font-weight: 700;
      min-width: 18px;
      max-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover {
      border-color: #6f7b88;
      background: #30343a;
    }
    QToolButton#layerFolderDisclosureButton[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerFolderDisclosureButton:disabled {
      color: transparent;
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerVisibilityCheck {
      background: #24272b;
      color: #f2f6fb;
      border: 1px solid #6d747d;
      border-radius: 3px;
      padding: 0;
      font-size: 12px;
      font-weight: 700;
      min-width: 20px;
      max-width: 20px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerVisibilityCheck:hover {
      border-color: #d5e8ff;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:hover {
      border-color: #6d747d;
      background: #24272b;
    }
    QToolButton#layerVisibilityCheck:checked {
      background: #2e3f50;
      border-color: #9ccfff;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:checked:hover {
      background: #2e3f50;
      border-color: #9ccfff;
    }
    QToolButton#layerVisibilityCheck:!checked {
      background: #24272b;
      border-color: #7b858f;
      color: transparent;
    }
    QPushButton {
      background: #3a3a3a;
      color: #e6e6e6;
      border: 1px solid #666666;
      border-radius: 0;
      padding: 4px 8px;
    }
    QPushButton:hover {
      background: #4a4a4a;
      border-color: #8a8a8a;
    }
    QPushButton[layerActionButton="true"], QToolButton[layerActionButton="true"] {
      padding: 0;
      min-width: 40px;
      max-width: 40px;
      min-height: 34px;
      max-height: 34px;
    }
    QStatusBar {
      background: #252525;
      color: #cfcfcf;
    }
    QCheckBox, QLabel {
      color: #e1e1e1;
    }
    QCheckBox::indicator {
      width: 12px;
      height: 12px;
    }
    QTabWidget::pane {
      border-top: 1px solid #5c5c5c;
    }
    QTabBar::tab {
      background: #3f3f3f;
      color: #e1e1e1;
      border: 1px solid #2b2b2b;
      padding: 5px 12px;
      min-height: 20px;
    }
    QTabBar::tab:selected {
      background: #2b2b2b;
      border-bottom-color: #2b2b2b;
    }
  )");
}

EditOptions edit_options(CanvasWidget& canvas) {
  EditOptions options;
  options.primary = edit_color(canvas.primary_color());
  options.secondary = edit_color(canvas.secondary_color());
  options.brush_size = canvas.brush_size();
  options.brush_softness = canvas.brush_softness();
  if (canvas.selected_document_rect().has_value()) {
    options.selection = to_core_rect(*canvas.selected_document_rect());
    const auto region = canvas.selected_document_region();
    options.selection_mask = [region](std::int32_t x, std::int32_t y) { return region.contains(QPoint(x, y)); };
    options.selection_coverage = [&canvas](std::int32_t x, std::int32_t y) {
      return static_cast<float>(canvas.selection_alpha_at(QPoint(x, y))) / 255.0F;
    };
  }
  return options;
}

Rect intersect_copy_rect(Rect a, Rect b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

QImage image_from_pixels(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

std::uint8_t layer_mask_value_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 255;
  }
  if (mask->pixels.empty() || mask->pixels.format() != PixelFormat::gray8()) {
    return mask->default_color;
  }
  if (!mask->bounds.contains(x, y)) {
    return mask->default_color;
  }
  return *mask->pixels.pixel(x - mask->bounds.x, y - mask->bounds.y);
}

PixelBuffer copy_pixels_from_layer(const Layer& layer, Rect document_rect, const CanvasWidget* canvas = nullptr) {
  const auto& source = layer.pixels();
  PixelBuffer copied(document_rect.width, document_rect.height, PixelFormat::rgba8());
  copied.clear(0);
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return copied;
  }

  const auto bounds = layer.bounds();
  for (std::int32_t y = 0; y < document_rect.height; ++y) {
    for (std::int32_t x = 0; x < document_rect.width; ++x) {
      const auto sx = document_rect.x + x - bounds.x;
      const auto sy = document_rect.y + y - bounds.y;
      if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) {
        continue;
      }
      const auto* src = source.pixel(sx, sy);
      auto* dst = copied.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      const auto source_alpha = source.format().channels >= 4 ? src[3] : 255;
      const QPoint document_point(document_rect.x + x, document_rect.y + y);
      const auto layer_alpha = layer_mask_value_at(layer, document_point.x(), document_point.y());
      const auto selection_alpha = canvas != nullptr && canvas->has_selection() ? canvas->selection_alpha_at(document_point)
                                                                                : static_cast<std::uint8_t>(255);
      dst[3] = static_cast<std::uint8_t>((static_cast<int>(source_alpha) * static_cast<int>(layer_alpha) *
                                          static_cast<int>(selection_alpha)) /
                                         (255 * 255));
    }
  }
  return copied;
}

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const CanvasWidget& canvas) {
  if (!canvas.has_selection() || pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      pixels.format().channels < 4) {
    return;
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      const auto selection_alpha = canvas.selection_alpha_at(QPoint(document_rect.x + x, document_rect.y + y));
      pixel[3] = static_cast<std::uint8_t>((static_cast<int>(pixel[3]) * static_cast<int>(selection_alpha)) / 255);
    }
  }
}

struct LayerCopyPixels {
  PixelBuffer pixels;
  QPoint origin;
  Rect document_rect;
  std::vector<LayerId> source_layer_ids;
};

void collect_layer_names(const std::vector<Layer>& layers, std::set<std::string>& names) {
  for (const auto& layer : layers) {
    names.insert(layer.name());
    collect_layer_names(layer.children(), names);
  }
}

std::string duplicate_name_stem(std::string_view name) {
  constexpr std::string_view kCopySuffix = " copy";
  constexpr std::string_view kNumberedCopySuffix = " copy ";
  const auto lower = ascii_lower_copy(name);
  if (lower.size() > kCopySuffix.size() && lower.ends_with(kCopySuffix)) {
    return std::string(name.substr(0, name.size() - kCopySuffix.size()));
  }

  const auto suffix_position = lower.rfind(kNumberedCopySuffix);
  if (suffix_position == std::string::npos || suffix_position == 0) {
    return std::string(name);
  }
  const auto number_position = suffix_position + kNumberedCopySuffix.size();
  if (number_position >= lower.size()) {
    return std::string(name);
  }

  bool suffix_is_number = true;
  for (auto index = number_position; index < lower.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(lower[index])) == 0) {
      suffix_is_number = false;
      break;
    }
  }
  return suffix_is_number ? std::string(name.substr(0, suffix_position)) : std::string(name);
}

std::string next_duplicate_layer_name(std::string_view source_name, const std::set<std::string>& existing_names) {
  const auto stem = duplicate_name_stem(source_name);
  for (int copy_index = 1;; ++copy_index) {
    auto candidate = stem + " copy";
    if (copy_index > 1) {
      candidate += " " + std::to_string(copy_index);
    }
    if (!existing_names.contains(candidate)) {
      return candidate;
    }
  }
}

Layer clone_layer_tree_with_document_ids(Document& document, const Layer& source) {
  auto cloned = source.clone_with_id(document.allocate_layer_id());
  cloned.children().clear();
  for (const auto& child : source.children()) {
    cloned.add_child(clone_layer_tree_with_document_ids(document, child));
  }
  return cloned;
}

std::vector<const Layer*> find_layers_top_to_bottom(const std::vector<Layer>& layers,
                                                    const std::vector<LayerId>& ids_top_to_bottom) {
  std::vector<const Layer*> found_layers;
  found_layers.reserve(ids_top_to_bottom.size());
  for (const auto id : ids_top_to_bottom) {
    if (const auto* layer = find_layer_in_tree(layers, id); layer != nullptr) {
      found_layers.push_back(layer);
    }
  }
  return found_layers;
}

bool has_visible_pixels(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    return pixels.format().channels >= 3;
  }
  const auto channels = pixels.format().channels;
  for (std::size_t index = 3; index < pixels.data().size(); index += channels) {
    if (pixels.data()[index] != 0) {
      return true;
    }
  }
  return false;
}

std::optional<LayerCopyPixels> collect_layer_copy_pixels(const Document& document, const std::vector<LayerId>& ids,
                                                         const CanvasWidget& canvas) {
  if (ids.empty()) {
    return std::nullopt;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto& layer : document.layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_copy.push_back(&layer);
  }
  if (layers_to_copy.empty()) {
    return std::nullopt;
  }

  Rect copy_rect;
  if (canvas.selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas.selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document.width(), document.height()));
  if (copy_rect.empty()) {
    return std::nullopt;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, &canvas);
  } else {
    Document selected_document(document.width(), document.height(), document.format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, canvas);
  }

  if (!has_visible_pixels(copied)) {
    return std::nullopt;
  }

  LayerCopyPixels payload{std::move(copied), QPoint(copy_rect.x, copy_rect.y), copy_rect, {}};
  payload.source_layer_ids.reserve(layers_to_copy.size());
  for (const auto* layer : layers_to_copy) {
    payload.source_layer_ids.push_back(layer->id());
  }
  return payload;
}

PixelBuffer scale_pixels_nearest(const PixelBuffer& source, std::int32_t width, std::int32_t height) {
  PixelBuffer scaled(width, height, source.format());
  if (source.empty() || width <= 0 || height <= 0) {
    return scaled;
  }

  const auto channels = source.format().channels;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto sy = std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(y) * source.height()) / height), 0,
                               source.height() - 1);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto sx = std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(x) * source.width()) / width), 0,
                                 source.width() - 1);
      const auto* src = source.pixel(sx, sy);
      auto* dst = scaled.pixel(x, y);
      std::copy(src, src + channels, dst);
    }
  }
  return scaled;
}

PixelBuffer render_text_pixels(const TextToolSettings& settings, QColor color, std::int32_t max_width) {
  QFont font(settings.family);
  font.setPixelSize(std::max(1, settings.size));
  font.setBold(settings.bold);
  font.setItalic(settings.italic);

  const auto text_width = std::max(64, max_width);
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  document.setDefaultTextOption(option);
  document.setPlainText(settings.text);
  document.setTextWidth(text_width);
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);

  const auto size = document.size();
  QImage image(std::max(1, static_cast<int>(std::ceil(size.width())) + 2),
               std::max(1, static_cast<int>(std::ceil(size.height())) + 2), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  document.drawContents(&painter, QRectF(0, 0, image.width(), image.height()));
  painter.end();
  return pixels_from_image_rgba(image);
}

QIcon tool_icon(CanvasTool tool) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(QColor(235, 238, 242), 2.4);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  switch (tool) {
    case CanvasTool::Move:
      painter.drawLine(16, 5, 16, 27);
      painter.drawLine(5, 16, 27, 16);
      painter.drawLine(16, 5, 12, 9);
      painter.drawLine(16, 5, 20, 9);
      painter.drawLine(27, 16, 23, 12);
      painter.drawLine(27, 16, 23, 20);
      break;
    case CanvasTool::Marquee:
      pen.setStyle(Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(QRect(7, 7, 18, 18));
      break;
    case CanvasTool::EllipticalMarquee:
      pen.setStyle(Qt::DashLine);
      painter.setPen(pen);
      painter.drawEllipse(QRect(7, 7, 18, 18));
      break;
    case CanvasTool::Lasso: {
      QPainterPath loop;
      loop.moveTo(8, 17);
      loop.cubicTo(8, 8, 22, 5, 26, 13);
      loop.cubicTo(30, 22, 17, 28, 10, 23);
      loop.cubicTo(5, 20, 5, 16, 8, 17);
      painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(loop);
      QPainterPath tail;
      tail.moveTo(13, 23);
      tail.cubicTo(15, 28, 21, 30, 27, 27);
      tail.moveTo(20, 25);
      tail.cubicTo(23, 26, 26, 28, 28, 30);
      painter.drawPath(tail);
      painter.setBrush(QColor(235, 238, 242));
      painter.drawEllipse(QPointF(13.5, 23.0), 1.7, 1.7);
      break;
    }
    case CanvasTool::MagicWand:
      painter.setPen(QPen(QColor(245, 248, 252), 3.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(8, 25, 21, 12);
      painter.setPen(QPen(QColor(80, 170, 255), 2.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(20, 5, 20, 10);
      painter.drawLine(20, 14, 20, 19);
      painter.drawLine(13, 12, 18, 12);
      painter.drawLine(22, 12, 27, 12);
      painter.drawLine(15, 7, 18, 10);
      painter.drawLine(22, 14, 25, 17);
      painter.setBrush(QColor(80, 170, 255));
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPoint(8, 25), 3, 3);
      break;
    case CanvasTool::Brush:
      painter.save();
      painter.translate(16, 16);
      painter.rotate(-42);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(210, 150, 75));
      painter.drawRoundedRect(QRectF(-3.0, -13.0, 6.0, 17.0), 2.0, 2.0);
      painter.setBrush(QColor(235, 238, 242));
      painter.drawRoundedRect(QRectF(-4.0, 1.0, 8.0, 9.0), 2.0, 2.0);
      painter.setBrush(QColor(45, 150, 255));
      painter.drawPolygon(QPolygon({QPoint(-4, 9), QPoint(4, 9), QPoint(0, 15)}));
      painter.restore();
      break;
    case CanvasTool::Clone:
      painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.setBrush(QColor(45, 150, 255));
      painter.drawRoundedRect(QRectF(10.0, 6.0, 12.0, 8.0), 3.0, 3.0);
      painter.setBrush(QColor(235, 238, 242));
      painter.drawRoundedRect(QRectF(8.0, 13.0, 16.0, 10.0), 3.0, 3.0);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(8, 25, 24, 25);
      painter.drawLine(11, 28, 21, 28);
      break;
    case CanvasTool::Smudge: {
      painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      QPainterPath path;
      path.moveTo(10, 8);
      path.cubicTo(17, 6, 22, 11, 18, 17);
      path.cubicTo(16, 20, 22, 20, 24, 16);
      path.cubicTo(25, 22, 20, 27, 13, 25);
      path.cubicTo(8, 23, 7, 18, 11, 15);
      painter.drawPath(path);
      painter.setPen(QPen(QColor(45, 150, 255), 2.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(8, 27, 17, 18);
      break;
    }
    case CanvasTool::Eraser:
      painter.setBrush(QColor(235, 238, 242));
      painter.drawPolygon(QPolygon({QPoint(8, 21), QPoint(18, 11), QPoint(25, 18), QPoint(15, 28)}));
      painter.setPen(QPen(QColor(36, 38, 41), 2));
      painter.drawLine(13, 16, 20, 23);
      break;
    case CanvasTool::Gradient: {
      QLinearGradient gradient(6, 24, 26, 8);
      gradient.setColorAt(0.0, QColor(245, 248, 252));
      gradient.setColorAt(1.0, QColor(45, 150, 255));
      painter.setPen(QPen(QBrush(gradient), 4));
      painter.drawLine(7, 23, 25, 9);
      painter.setPen(QPen(QColor(245, 248, 252), 1.5));
      painter.setBrush(QColor(45, 150, 255));
      painter.drawRect(QRect(19, 5, 8, 8));
      painter.setBrush(QColor(245, 248, 252));
      painter.drawRect(QRect(5, 20, 8, 8));
      break;
    }
    case CanvasTool::Fill:
      painter.drawPolygon(QPolygon({QPoint(10, 10), QPoint(21, 16), QPoint(14, 25), QPoint(6, 17)}));
      painter.setBrush(QColor(235, 238, 242));
      painter.drawEllipse(QPoint(24, 25), 3, 3);
      break;
    case CanvasTool::Line:
      painter.drawLine(8, 24, 24, 8);
      break;
    case CanvasTool::Rectangle:
      painter.drawRect(QRect(7, 9, 18, 14));
      break;
    case CanvasTool::Ellipse:
      painter.drawEllipse(QRect(7, 8, 18, 16));
      break;
    case CanvasTool::Eyedropper:
      painter.drawLine(11, 22, 23, 10);
      painter.drawRect(QRect(20, 7, 5, 5));
      painter.drawLine(8, 25, 13, 20);
      break;
    case CanvasTool::Text:
      painter.setFont(QFont(QStringLiteral("Arial"), 20, QFont::Bold));
      painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("T"));
      break;
    case CanvasTool::Pan: {
      QPainterPath path;
      path.moveTo(10, 24);
      path.lineTo(10, 12);
      path.quadTo(12, 9, 14, 12);
      path.lineTo(14, 18);
      path.lineTo(16, 10);
      path.quadTo(18, 8, 20, 11);
      path.lineTo(19, 18);
      path.lineTo(22, 13);
      path.quadTo(25, 12, 25, 16);
      path.lineTo(22, 26);
      path.closeSubpath();
      painter.drawPath(path);
      break;
    }
    case CanvasTool::Zoom:
      painter.setPen(QPen(QColor(235, 238, 242), 2.4, Qt::SolidLine, Qt::RoundCap));
      painter.drawEllipse(QRect(7, 7, 14, 14));
      painter.drawLine(18, 18, 26, 26);
      painter.drawLine(11, 14, 17, 14);
      painter.drawLine(14, 11, 14, 17);
      break;
  }
  return QIcon(pixmap);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  register_builtin_filters(filters_);
  register_builtin_formats(formats_);
  setWindowFlag(Qt::FramelessWindowHint, true);

  document_tabs_ = new QTabWidget(this);
  document_tabs_->setObjectName(QStringLiteral("documentTabs"));
  document_tabs_->setDocumentMode(true);
  document_tabs_->setTabsClosable(true);
  document_tabs_->setMovable(true);
  setAcceptDrops(true);
  document_tabs_->setAcceptDrops(true);
  document_tabs_->installEventFilter(this);
  setCentralWidget(document_tabs_);
  connect(document_tabs_, &QTabWidget::currentChanged, this, [this](int index) { activate_document_tab(index); });
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_document_tab(index); });
  reset_document(1024, 768, Qt::white, tr("New document"));
  load_tool_settings();

  create_actions();
  configure_window_chrome();
  load_recent_files();
  rebuild_recent_files_menu();
  load_bundled_legacy_plugins();
  create_docks();
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();

  setWindowTitle(QStringLiteral("Photoslop"));
  resize(1280, 860);
  setStyleSheet(photoshop_style());
  statusBar()->showMessage(tr("Ready"));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == menuBar()) {
    auto* bar = menuBar();
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      position_window_chrome_controls();
    }

    const auto is_chrome_drag_area = [this, bar](const QPoint& position) {
      if (bar->actionAt(position) != nullptr) {
        return false;
      }
      if (window_chrome_controls_ != nullptr) {
        const QRect controls_rect(window_chrome_controls_->pos(), window_chrome_controls_->size());
        if (controls_rect.contains(position)) {
          return false;
        }
      }
      return true;
    };

    switch (event->type()) {
      case QEvent::MouseButtonDblClick: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && is_chrome_drag_area(mouse_event->pos())) {
          isMaximized() ? showNormal() : showMaximized();
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && is_chrome_drag_area(mouse_event->pos())) {
          chrome_drag_position_ = mouse_event->globalPosition().toPoint() - frameGeometry().topLeft();
          chrome_dragging_ = true;
          if (auto* handle = windowHandle(); handle != nullptr && handle->startSystemMove()) {
            chrome_dragging_ = false;
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (chrome_dragging_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
          if (!isMaximized() && !isFullScreen()) {
            move(mouse_event->globalPosition().toPoint() - chrome_drag_position_);
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease:
        chrome_dragging_ = false;
        break;
      default:
        break;
    }
  }

  if (watched != document_tabs_) {
    return QMainWindow::eventFilter(watched, event);
  }

  switch (event->type()) {
    case QEvent::DragEnter:
      return accept_open_file_drag(static_cast<QDragEnterEvent*>(event));
    case QEvent::DragMove:
      return accept_open_file_drag(static_cast<QDragMoveEvent*>(event));
    case QEvent::Drop:
      return open_dropped_files(static_cast<QDropEvent*>(event));
    default:
      break;
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
  if (message != nullptr && result != nullptr && !isMaximized() && !isFullScreen()) {
    auto* native_message = static_cast<MSG*>(message);
    if (native_message->message == WM_NCHITTEST) {
      RECT window_rect;
      if (GetWindowRect(reinterpret_cast<HWND>(winId()), &window_rect) != 0) {
        constexpr int kResizeBorder = 7;
        const auto x = GET_X_LPARAM(native_message->lParam);
        const auto y = GET_Y_LPARAM(native_message->lParam);
        const bool left = x >= window_rect.left && x < window_rect.left + kResizeBorder;
        const bool right = x < window_rect.right && x >= window_rect.right - kResizeBorder;
        const bool top = y >= window_rect.top && y < window_rect.top + kResizeBorder;
        const bool bottom = y < window_rect.bottom && y >= window_rect.bottom - kResizeBorder;

        if (top && left) {
          *result = HTTOPLEFT;
          return true;
        }
        if (top && right) {
          *result = HTTOPRIGHT;
          return true;
        }
        if (bottom && left) {
          *result = HTBOTTOMLEFT;
          return true;
        }
        if (bottom && right) {
          *result = HTBOTTOMRIGHT;
          return true;
        }
        if (left) {
          *result = HTLEFT;
          return true;
        }
        if (right) {
          *result = HTRIGHT;
          return true;
        }
        if (top) {
          *result = HTTOP;
          return true;
        }
        if (bottom) {
          *result = HTBOTTOM;
          return true;
        }
      }
    }
  }
#endif
  return QMainWindow::nativeEvent(event_type, message, result);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  for (auto& target_session : sessions_) {
    if (target_session != nullptr && !confirm_close_session(*target_session)) {
      event->ignore();
      return;
    }
  }
  event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (!accept_open_file_drag(event)) {
    QMainWindow::dragEnterEvent(event);
  }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (!accept_open_file_drag(event)) {
    QMainWindow::dragMoveEvent(event);
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (!open_dropped_files(event)) {
    QMainWindow::dropEvent(event);
  }
}

void MainWindow::position_window_chrome_controls() {
  if (window_chrome_controls_ == nullptr || menuBar() == nullptr) {
    return;
  }
  window_chrome_controls_->move(std::max(0, menuBar()->width() - window_chrome_controls_->width()), 0);
  window_chrome_controls_->raise();
}

void MainWindow::configure_window_chrome() {
  auto* bar = menuBar();
  bar->setNativeMenuBar(false);
  bar->setFixedHeight(34);
  bar->installEventFilter(this);

  auto* badge = new QLabel(QStringLiteral("Ps"), bar);
  badge->setObjectName(QStringLiteral("photoshopBadge"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setAttribute(Qt::WA_TransparentForMouseEvents);
  badge->setFixedSize(18, 18);
  badge->move(9, 8);
  badge->show();

  auto* controls = new QWidget(bar);
  controls->setObjectName(QStringLiteral("windowChromeControls"));
  controls->setFixedSize(46 * 3, 34);
  window_chrome_controls_ = controls;
  auto* layout = new QHBoxLayout(controls);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  const auto add_chrome_button = [controls, layout](const QString& object_name, const QIcon& icon,
                                                    const QString& tooltip) {
    auto* button = new QToolButton(controls);
    button->setObjectName(object_name);
    button->setProperty("windowChromeButton", true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(icon);
    button->setIconSize(QSize(16, 16));
    button->setToolTip(tooltip);
    button->setFixedSize(46, 34);
    layout->addWidget(button);
    return button;
  };

  auto* minimize_button =
      add_chrome_button(QStringLiteral("windowMinimizeButton"), window_chrome_icon(QStringLiteral("minimize")),
                        tr("Minimize"));
  maximize_button_ =
      add_chrome_button(QStringLiteral("windowMaximizeButton"), window_chrome_icon(QStringLiteral("maximize")),
                        tr("Maximize / Restore"));
  auto* close_button =
      add_chrome_button(QStringLiteral("windowCloseButton"), window_chrome_icon(QStringLiteral("close")), tr("Close"));
  position_window_chrome_controls();
  controls->show();

  connect(minimize_button, &QToolButton::clicked, this, [this] { showMinimized(); });
  connect(maximize_button_, &QToolButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
  connect(close_button, &QToolButton::clicked, this, &QWidget::close);
}

void MainWindow::create_actions() {
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  auto* edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto* image_menu = menuBar()->addMenu(tr("&Image"));
  auto* layer_menu = menuBar()->addMenu(tr("&Layer"));
  auto* type_menu = menuBar()->addMenu(tr("&Type"));
  auto* select_menu = menuBar()->addMenu(tr("&Select"));
  auto* filter_menu = menuBar()->addMenu(tr("&Filter"));
  auto* plugins_menu = menuBar()->addMenu(tr("&Plugins"));
  auto* view_menu = menuBar()->addMenu(tr("&View"));
  auto* window_menu = menuBar()->addMenu(tr("&Window"));
  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  filter_menu->setObjectName(QStringLiteral("filterMenu"));

  auto* new_action = file_menu->addAction(tr("&New"));
  auto* open_action = file_menu->addAction(tr("&Open..."));
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  auto* export_flat_action = file_menu->addAction(tr("Export &Flat Image..."));
  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("&Quit"));
  new_action->setObjectName(QStringLiteral("fileNewAction"));
  open_action->setObjectName(QStringLiteral("fileOpenAction"));
  save_action->setObjectName(QStringLiteral("fileSaveAction"));
  save_as_action->setObjectName(QStringLiteral("fileSaveAsAction"));
  export_flat_action->setObjectName(QStringLiteral("fileExportFlatAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  apply_action_shortcut(new_action, QKeySequence(Qt::CTRL | Qt::Key_N));
  apply_action_shortcut(open_action, QKeySequence(Qt::CTRL | Qt::Key_O));
  apply_action_shortcut(save_action, QKeySequence(Qt::CTRL | Qt::Key_S));
  apply_action_shortcut(save_as_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  apply_action_shortcut(quit_action, QKeySequence(Qt::CTRL | Qt::Key_Q));

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  undo_action_ = edit_menu->addAction(tr("&Undo"));
  redo_action_ = edit_menu->addAction(tr("&Redo"));
  undo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  redo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  apply_action_shortcut(undo_action_, QKeySequence(Qt::CTRL | Qt::Key_Z));
  apply_action_shortcut(redo_action_, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
  connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
  edit_menu->addSeparator();
  auto* cut_action = edit_menu->addAction(tr("Cu&t"));
  auto* copy_action = edit_menu->addAction(tr("&Copy"));
  auto* copy_merged_action = edit_menu->addAction(tr("Copy Merged"));
  auto* paste_action = edit_menu->addAction(tr("&Paste"));
  auto* transform_action = edit_menu->addAction(tr("Free &Transform..."));
  cut_action->setObjectName(QStringLiteral("editCutAction"));
  copy_action->setObjectName(QStringLiteral("editCopyAction"));
  copy_merged_action->setObjectName(QStringLiteral("editCopyMergedAction"));
  paste_action->setObjectName(QStringLiteral("editPasteAction"));
  transform_action->setObjectName(QStringLiteral("editFreeTransformAction"));
  cut_action->setIcon(simple_icon(QStringLiteral("CT")));
  copy_action->setIcon(simple_icon(QStringLiteral("CP")));
  copy_merged_action->setIcon(simple_icon(QStringLiteral("CM")));
  paste_action->setIcon(simple_icon(QStringLiteral("PS")));
  transform_action->setIcon(simple_icon(QStringLiteral("TR")));
  apply_action_shortcut(cut_action, QKeySequence(Qt::CTRL | Qt::Key_X));
  apply_action_shortcut(copy_action, QKeySequence(Qt::CTRL | Qt::Key_C));
  apply_action_shortcut(copy_merged_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  apply_action_shortcut(paste_action, QKeySequence(Qt::CTRL | Qt::Key_V));
  apply_action_shortcut(transform_action, QKeySequence(Qt::CTRL | Qt::Key_T));
  connect(cut_action, &QAction::triggered, this, [this] { cut_selection(); });
  connect(copy_action, &QAction::triggered, this, [this] { copy_selection(); });
  connect(copy_merged_action, &QAction::triggered, this, [this] { copy_merged(); });
  connect(paste_action, &QAction::triggered, this, [this] { paste_clipboard(); });
  connect(transform_action, &QAction::triggered, this, [this] { transform_active_layer_dialog(); });
  edit_menu->addSeparator();
  auto* select_all_action = edit_menu->addAction(tr("Select &All"));
  auto* clear_selection_action = edit_menu->addAction(tr("&Clear Selection"));
  auto* reselect_action = edit_menu->addAction(tr("&Reselect"));
  auto* inverse_selection_action = edit_menu->addAction(tr("&Inverse"));
  auto* grow_selection_action = new QAction(tr("&Grow"), this);
  auto* similar_selection_action = new QAction(tr("Simi&lar"), this);
  auto* expand_selection_action = new QAction(tr("&Expand..."), this);
  auto* contract_selection_action = new QAction(tr("Con&tract..."), this);
  auto* border_selection_action = new QAction(tr("&Border..."), this);
  auto* layer_transparency_action = new QAction(tr("Load Layer &Transparency"), this);
  auto* stroke_selection_action = edit_menu->addAction(tr("&Stroke Selection"));
  select_all_action->setObjectName(QStringLiteral("editSelectAllAction"));
  clear_selection_action->setObjectName(QStringLiteral("editDeselectAction"));
  reselect_action->setObjectName(QStringLiteral("selectReselectAction"));
  inverse_selection_action->setObjectName(QStringLiteral("selectInverseAction"));
  grow_selection_action->setObjectName(QStringLiteral("selectGrowAction"));
  similar_selection_action->setObjectName(QStringLiteral("selectSimilarAction"));
  expand_selection_action->setObjectName(QStringLiteral("selectExpandAction"));
  contract_selection_action->setObjectName(QStringLiteral("selectContractAction"));
  border_selection_action->setObjectName(QStringLiteral("selectBorderAction"));
  layer_transparency_action->setObjectName(QStringLiteral("selectLayerTransparencyAction"));
  stroke_selection_action->setObjectName(QStringLiteral("editStrokeSelectionAction"));
  select_all_action->setIcon(simple_icon(QStringLiteral("SA")));
  clear_selection_action->setIcon(simple_icon(QStringLiteral("DS")));
  reselect_action->setIcon(simple_icon(QStringLiteral("RS")));
  inverse_selection_action->setIcon(simple_icon(QStringLiteral("INV")));
  grow_selection_action->setIcon(simple_icon(QStringLiteral("GR")));
  similar_selection_action->setIcon(simple_icon(QStringLiteral("SIM")));
  expand_selection_action->setIcon(simple_icon(QStringLiteral("EXP")));
  contract_selection_action->setIcon(simple_icon(QStringLiteral("CTR")));
  border_selection_action->setIcon(simple_icon(QStringLiteral("BD")));
  layer_transparency_action->setIcon(simple_icon(QStringLiteral("AL")));
  stroke_selection_action->setIcon(simple_icon(QStringLiteral("stroke")));
  apply_action_shortcut(select_all_action, QKeySequence(Qt::CTRL | Qt::Key_A));
  apply_action_shortcut(clear_selection_action, QKeySequence(Qt::CTRL | Qt::Key_D));
  apply_action_shortcut(reselect_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  apply_action_shortcut(inverse_selection_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  connect(select_all_action, &QAction::triggered, this, [this] { canvas_->select_all(); });
  connect(clear_selection_action, &QAction::triggered, this, [this] { canvas_->clear_selection(); });
  connect(reselect_action, &QAction::triggered, this, [this] { canvas_->reselect(); });
  connect(inverse_selection_action, &QAction::triggered, this, [this] { canvas_->invert_selection(); });
  connect(grow_selection_action, &QAction::triggered, this, [this] { canvas_->grow_selection(); });
  connect(similar_selection_action, &QAction::triggered, this, [this] { canvas_->select_similar_to_selection(); });
  connect(expand_selection_action, &QAction::triggered, this, [this] { expand_selection_dialog(); });
  connect(contract_selection_action, &QAction::triggered, this, [this] { contract_selection_dialog(); });
  connect(border_selection_action, &QAction::triggered, this, [this] { border_selection_dialog(); });
  connect(layer_transparency_action, &QAction::triggered, this, [this] { canvas_->select_active_layer_opaque_pixels(); });
  connect(stroke_selection_action, &QAction::triggered, this, [this] { stroke_selection(); });
  select_menu->addAction(select_all_action);
  select_menu->addAction(clear_selection_action);
  select_menu->addAction(reselect_action);
  select_menu->addAction(inverse_selection_action);
  select_menu->addAction(grow_selection_action);
  select_menu->addAction(similar_selection_action);
  select_menu->addAction(expand_selection_action);
  select_menu->addAction(contract_selection_action);
  select_menu->addAction(border_selection_action);
  select_menu->addAction(layer_transparency_action);
  select_menu->addSeparator();
  select_menu->addAction(stroke_selection_action);

  auto* add_layer_action = layer_menu->addAction(tr("&New Layer"));
  auto* add_folder_action = layer_menu->addAction(tr("New &Folder"));
  auto* new_adjustment_layer_menu = layer_menu->addMenu(tr("New &Adjustment Layer"));
  new_adjustment_layer_menu->setObjectName(QStringLiteral("layerNewAdjustmentMenu"));
  populate_new_adjustment_layer_menu(new_adjustment_layer_menu, QStringLiteral("layerNew"));
  auto* layer_via_copy_action = layer_menu->addAction(tr("Layer Via &Copy"));
  auto* layer_via_cut_action = layer_menu->addAction(tr("Layer Via Cu&t"));
  auto* add_mask_action = layer_menu->addAction(tr("Add Layer &Mask from Selection"));
  delete_layer_mask_action_ = layer_menu->addAction(tr("&Delete Layer Mask"));
  link_layer_mask_action_ = layer_menu->addAction(tr("Link Layer &Mask"));
  layer_menu->addSeparator();
  auto* edit_adjustment_action = layer_menu->addAction(tr("&Edit Adjustment..."));
  layer_blending_options_action_ = layer_menu->addAction(tr("&Blending Options..."));
  layer_menu->addSeparator();
  auto* duplicate_layer_action = layer_menu->addAction(tr("&Duplicate Layer"));
  auto* merge_visible_action = layer_menu->addAction(tr("Merge &Visible to New Layer"));
  merge_visible_action->setObjectName(QStringLiteral("layerMergeVisibleAction"));
  auto* merge_selected_action = layer_menu->addAction(tr("&Merge Selected to New Layer"));
  merge_selected_action->setObjectName(QStringLiteral("layerMergeSelectedAction"));
  auto* rename_layer_action = layer_menu->addAction(tr("&Rename Layer..."));
  auto* delete_layer_action = layer_menu->addAction(tr("&Delete Layer"));
  layer_menu->addSeparator();
  auto* fill_layer_action = layer_menu->addAction(tr("&Fill Layer / Selection"));
  auto* fill_background_action = layer_menu->addAction(tr("Fill With &Background Color"));
  auto* clear_layer_action = layer_menu->addAction(tr("&Clear Layer / Selection"));
  layer_menu->addSeparator();
  auto* flip_h_action = layer_menu->addAction(tr("Flip Layer &Horizontal"));
  auto* flip_v_action = layer_menu->addAction(tr("Flip Layer &Vertical"));
  layer_menu->addSeparator();
  auto* layer_up_action = layer_menu->addAction(tr("Move Layer &Up"));
  auto* layer_down_action = layer_menu->addAction(tr("Move Layer &Down"));
  add_layer_action->setObjectName(QStringLiteral("layerNewAction"));
  add_folder_action->setObjectName(QStringLiteral("layerNewFolderAction"));
  layer_via_copy_action->setObjectName(QStringLiteral("layerViaCopyAction"));
  layer_via_cut_action->setObjectName(QStringLiteral("layerViaCutAction"));
  add_mask_action->setObjectName(QStringLiteral("layerAddMaskFromSelectionAction"));
  delete_layer_mask_action_->setObjectName(QStringLiteral("layerDeleteMaskAction"));
  link_layer_mask_action_->setObjectName(QStringLiteral("layerLinkMaskAction"));
  edit_adjustment_action->setObjectName(QStringLiteral("layerEditAdjustmentAction"));
  layer_blending_options_action_->setObjectName(QStringLiteral("layerBlendingOptionsAction"));
  duplicate_layer_action->setObjectName(QStringLiteral("layerDuplicateAction"));
  delete_layer_action->setObjectName(QStringLiteral("layerDeleteAction"));
  fill_layer_action->setObjectName(QStringLiteral("layerFillForegroundAction"));
  fill_background_action->setObjectName(QStringLiteral("layerFillBackgroundAction"));
  clear_layer_action->setObjectName(QStringLiteral("layerClearAction"));
  add_layer_action->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_action->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  layer_via_copy_action->setIcon(simple_icon(QStringLiteral("copy")));
  layer_via_cut_action->setIcon(simple_icon(QStringLiteral("cut"), QColor(255, 185, 120)));
  add_mask_action->setIcon(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)));
  delete_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)));
  link_layer_mask_action_->setIcon(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)));
  link_layer_mask_action_->setCheckable(true);
  edit_adjustment_action->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  duplicate_layer_action->setIcon(simple_icon(QStringLiteral("dup")));
  merge_visible_action->setIcon(simple_icon(QStringLiteral("merge")));
  merge_selected_action->setIcon(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)));
  rename_layer_action->setIcon(simple_icon(QStringLiteral("RN")));
  delete_layer_action->setIcon(simple_icon(QStringLiteral("trash")));
  fill_layer_action->setIcon(simple_icon(QStringLiteral("fill")));
  fill_background_action->setIcon(simple_icon(QStringLiteral("fill"), QColor(160, 190, 255)));
  clear_layer_action->setIcon(simple_icon(QStringLiteral("clear")));
  flip_h_action->setIcon(simple_icon(QStringLiteral("FH")));
  flip_v_action->setIcon(simple_icon(QStringLiteral("FV")));
  layer_up_action->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
  layer_down_action->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  apply_action_shortcut(add_layer_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  apply_action_shortcut(layer_via_copy_action, QKeySequence(Qt::CTRL | Qt::Key_J));
  apply_action_shortcut(layer_via_cut_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  apply_action_shortcut(merge_visible_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  apply_action_shortcut(merge_selected_action, QKeySequence(Qt::CTRL | Qt::Key_E));
  apply_action_shortcut(fill_layer_action, QKeySequence(Qt::ALT | Qt::Key_Backspace));
  apply_action_shortcut(fill_background_action, QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  apply_action_shortcut(clear_layer_action, QKeySequence(Qt::Key_Delete));
  connect(add_layer_action, &QAction::triggered, this, [this] { add_layer(); });
  connect(add_folder_action, &QAction::triggered, this, [this] { create_layer_folder(); });
  connect(layer_via_copy_action, &QAction::triggered, this, [this] { layer_via_copy(); });
  connect(layer_via_cut_action, &QAction::triggered, this, [this] { layer_via_cut(); });
  connect(add_mask_action, &QAction::triggered, this, [this] { add_layer_mask_from_selection(); });
  connect(delete_layer_mask_action_, &QAction::triggered, this, [this] { delete_active_layer_mask(); });
  connect(link_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_linked(checked); });
  connect(edit_adjustment_action, &QAction::triggered, this, [this] { edit_active_adjustment_layer(); });
  connect(layer_blending_options_action_, &QAction::triggered, this, [this] { edit_active_layer_style(); });
  connect(duplicate_layer_action, &QAction::triggered, this, [this] { duplicate_active_layer(); });
  connect(merge_visible_action, &QAction::triggered, this, [this] { merge_visible_to_new_layer(); });
  connect(merge_selected_action, &QAction::triggered, this, [this] { merge_selected_to_new_layer(); });
  connect(rename_layer_action, &QAction::triggered, this, [this] { rename_active_layer(); });
  connect(delete_layer_action, &QAction::triggered, this, [this] { delete_active_layer(); });
  connect(fill_layer_action, &QAction::triggered, this, [this] { fill_active_layer(); });
  connect(fill_background_action, &QAction::triggered, this, [this] {
    fill_active_layer_with_color(canvas_->secondary_color(), tr("Fill background"));
  });
  connect(clear_layer_action, &QAction::triggered, this, [this] { clear_active_layer(); });
  connect(flip_h_action, &QAction::triggered, this, [this] { flip_active_layer_horizontal(); });
  connect(flip_v_action, &QAction::triggered, this, [this] { flip_active_layer_vertical(); });
  connect(layer_up_action, &QAction::triggered, this, [this] { move_active_layer(1); });
  connect(layer_down_action, &QAction::triggered, this, [this] { move_active_layer(-1); });

  auto* adjustments_menu = image_menu->addMenu(tr("&Adjustments"));
  adjustments_menu->setObjectName(QStringLiteral("imageAdjustmentsMenu"));
  const auto add_adjustment_action = [this, adjustments_menu](const QString& label, const QString& object_name,
                                                              const QString& identifier,
                                                              const QKeySequence& shortcut = {}) {
    auto* action = adjustments_menu->addAction(label);
    action->setObjectName(object_name);
    action->setIcon(simple_icon(label.left(3).toUpper()));
    if (!shortcut.isEmpty()) {
      apply_action_shortcut(action, shortcut);
    }
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    return action;
  };
  add_adjustment_action(tr("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("photoslop.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  apply_action_shortcut(levels_action, QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  apply_action_shortcut(curves_action, QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  apply_action_shortcut(hue_saturation_action, QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  apply_action_shortcut(color_balance_action, QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  add_adjustment_action(tr("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("photoslop.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(tr("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("photoslop.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  adjustments_menu->addSeparator();
  add_adjustment_action(tr("&Brightness..."), QStringLiteral("imageAdjustBrightnessAction"),
                        QStringLiteral("photoslop.filters.brightness_plus"));
  add_adjustment_action(tr("&Contrast..."), QStringLiteral("imageAdjustContrastAction"),
                        QStringLiteral("photoslop.filters.contrast_plus"));
  add_adjustment_action(tr("&Threshold"), QStringLiteral("imageAdjustThresholdAction"),
                        QStringLiteral("photoslop.filters.threshold"));
  add_adjustment_action(tr("&Posterize"), QStringLiteral("imageAdjustPosterizeAction"),
                        QStringLiteral("photoslop.filters.posterize"));
  image_menu->addSeparator();

  auto* image_size_action = image_menu->addAction(tr("&Image Size..."));
  image_size_action->setObjectName(QStringLiteral("imageSizeAction"));
  auto* canvas_size_action = image_menu->addAction(tr("&Canvas Size..."));
  canvas_size_action->setObjectName(QStringLiteral("imageCanvasSizeAction"));
  auto* crop_action = image_menu->addAction(tr("&Crop to Selection"));
  crop_action->setObjectName(QStringLiteral("imageCropToSelectionAction"));
  image_menu->addSeparator();
  auto* rotate_cw_action = image_menu->addAction(tr("Rotate 90 &Clockwise"));
  auto* rotate_ccw_action = image_menu->addAction(tr("Rotate 90 Counterclockwise"));
  rotate_cw_action->setObjectName(QStringLiteral("imageRotateClockwiseAction"));
  rotate_ccw_action->setObjectName(QStringLiteral("imageRotateCounterclockwiseAction"));
  image_size_action->setIcon(simple_icon(QStringLiteral("IS")));
  canvas_size_action->setIcon(simple_icon(QStringLiteral("CS")));
  crop_action->setIcon(simple_icon(QStringLiteral("crop")));
  rotate_cw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  rotate_ccw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  apply_action_shortcut(image_size_action, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  apply_action_shortcut(canvas_size_action, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  apply_action_shortcut(crop_action, QKeySequence(Qt::Key_C));
  apply_action_shortcut(rotate_cw_action, QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  apply_action_shortcut(rotate_ccw_action, QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  connect(image_size_action, &QAction::triggered, this, [this] { resize_image_dialog(); });
  connect(canvas_size_action, &QAction::triggered, this, [this] { resize_canvas_dialog(); });
  connect(crop_action, &QAction::triggered, this, [this] { crop_to_selection(); });
  connect(rotate_cw_action, &QAction::triggered, this, [this] { rotate_canvas_clockwise(); });
  connect(rotate_ccw_action, &QAction::triggered, this, [this] { rotate_canvas_counterclockwise(); });

  for (const auto& filter : filters_.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    if (is_adjustment_only_filter(identifier)) {
      continue;
    }
    const auto display_name = QString::fromStdString(filter.display_name);
    auto* action = filter_menu->addAction(display_name);
    action->setObjectName(filter_action_object_name(identifier));
    action->setIcon(simple_icon(display_name.left(3).toUpper()));
    action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
    refresh_action_tooltip(action);
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
  }

  auto* scan_legacy_plugins_action = plugins_menu->addAction(tr("&Scan Legacy Photoshop Plug-ins..."));
  scan_legacy_plugins_action->setObjectName(QStringLiteral("pluginsScanLegacyAction"));
  scan_legacy_plugins_action->setIcon(simple_icon(QStringLiteral("8BF")));
  connect(scan_legacy_plugins_action, &QAction::triggered, this, [this] { scan_legacy_plugins(); });
  legacy_plugins_menu_ = plugins_menu->addMenu(tr("Legacy Photoshop Plug-ins"));
  legacy_plugins_menu_->setObjectName(QStringLiteral("legacyPluginsMenu"));

  auto* zoom_in = view_menu->addAction(tr("Zoom &In"));
  auto* zoom_out = view_menu->addAction(tr("Zoom &Out"));
  auto* fit_on_screen = view_menu->addAction(tr("&Fit on Screen"));
  auto* zoom_reset = view_menu->addAction(tr("&Actual Pixels"));
  auto* selection_edges_action = view_menu->addAction(tr("Show Selection &Edges"));
  zoom_in->setObjectName(QStringLiteral("viewZoomInAction"));
  zoom_out->setObjectName(QStringLiteral("viewZoomOutAction"));
  fit_on_screen->setObjectName(QStringLiteral("viewFitOnScreenAction"));
  zoom_reset->setObjectName(QStringLiteral("viewActualPixelsAction"));
  selection_edges_action->setObjectName(QStringLiteral("viewToggleSelectionEdgesAction"));
  zoom_in->setIcon(simple_icon(QStringLiteral("zoomIn")));
  zoom_out->setIcon(simple_icon(QStringLiteral("zoomOut")));
  fit_on_screen->setIcon(simple_icon(QStringLiteral("fit")));
  zoom_reset->setIcon(simple_icon(QStringLiteral("1x")));
  selection_edges_action->setIcon(simple_icon(QStringLiteral("SE")));
  zoom_in->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  zoom_out->setShortcut(QKeySequence::ZoomOut);
  zoom_in->setShortcutContext(Qt::ApplicationShortcut);
  zoom_out->setShortcutContext(Qt::ApplicationShortcut);
  refresh_action_tooltip(zoom_in);
  refresh_action_tooltip(zoom_out);
  apply_action_shortcut(fit_on_screen, QKeySequence(Qt::CTRL | Qt::Key_0));
  apply_action_shortcut(zoom_reset, QKeySequence(Qt::CTRL | Qt::Key_1));
  apply_action_shortcut(selection_edges_action, QKeySequence(Qt::CTRL | Qt::Key_H));
  connect(zoom_in, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 1.25); });
  connect(zoom_out, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 0.8); });
  connect(fit_on_screen, &QAction::triggered, this, [this] { canvas_->fit_to_view(); });
  connect(zoom_reset, &QAction::triggered, this, [this] { canvas_->set_zoom(1.0); });
  connect(selection_edges_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->toggle_selection_edges_visible();
    }
  });

  auto* about_action = help_menu->addAction(tr("&About Photoslop"));
  connect(about_action, &QAction::triggered, this, [this] { show_about(); });

  auto* tool_palette = new QToolBar(tr("Tool Palette"), this);
  tool_palette->setObjectName(QStringLiteral("toolPalette"));
  tool_palette->setOrientation(Qt::Vertical);
  tool_palette->setMovable(false);
  tool_palette->setFloatable(false);
  tool_palette->setAllowedAreas(Qt::LeftToolBarArea);
  tool_palette->setToolButtonStyle(Qt::ToolButtonIconOnly);
  tool_palette->setIconSize(QSize(20, 20));
  tool_palette->setFixedWidth(43);
  addToolBar(Qt::LeftToolBarArea, tool_palette);

  auto* tool_group = new QActionGroup(this);
  tool_group->setExclusive(true);
  move_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Move"), CanvasTool::Move, QKeySequence(Qt::Key_V));
  auto* marquee_menu = new QMenu(tr("Marquee Tools"), tool_palette);
  marquee_menu->setObjectName(QStringLiteral("marqueeToolMenu"));
  const auto create_marquee_action = [this, tool_group, marquee_menu](const QString& label, CanvasTool tool,
                                                                      QKeySequence shortcut) {
    auto* action = new QAction(label, this);
    action->setIcon(tool_icon(tool));
    action->setCheckable(true);
    action->setData(static_cast<int>(tool));
    apply_action_shortcut(action, shortcut);
    tool_group->addAction(action);
    marquee_menu->addAction(action);
    addAction(action);
    return action;
  };
  auto* rect_marquee_action = create_marquee_action(tr("Marquee"), CanvasTool::Marquee, QKeySequence(Qt::Key_M));
  auto* elliptical_marquee_action = create_marquee_action(tr("Elliptical Marquee"), CanvasTool::EllipticalMarquee,
                                                          QKeySequence(Qt::SHIFT | Qt::Key_M));
  auto* marquee_tool_button = new QToolButton(tool_palette);
  marquee_tool_button->setObjectName(QStringLiteral("marqueeToolButton"));
  marquee_tool_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  marquee_tool_button->setPopupMode(QToolButton::DelayedPopup);
  marquee_tool_button->setMenu(marquee_menu);
  marquee_tool_button->setDefaultAction(rect_marquee_action);
  marquee_tool_button->setToolTip(rect_marquee_action->toolTip());
  tool_palette->addWidget(marquee_tool_button);
  for (auto* action : {rect_marquee_action, elliptical_marquee_action}) {
    connect(action, &QAction::triggered, marquee_tool_button, [marquee_tool_button, marquee_menu, action] {
      marquee_tool_button->setDefaultAction(action);
      marquee_tool_button->setMenu(marquee_menu);
      marquee_tool_button->setToolTip(action->toolTip());
    });
  }
  add_tool_action(tool_palette, tool_group, tr("Lasso"), CanvasTool::Lasso, QKeySequence(Qt::Key_L));
  add_tool_action(tool_palette, tool_group, tr("Magic Wand"), CanvasTool::MagicWand, QKeySequence(Qt::Key_W));
  add_tool_action(tool_palette, tool_group, tr("Brush"), CanvasTool::Brush, QKeySequence(Qt::Key_B))->setChecked(true);
  add_tool_action(tool_palette, tool_group, tr("Clone"), CanvasTool::Clone, QKeySequence(Qt::Key_S));
  add_tool_action(tool_palette, tool_group, tr("Smudge"), CanvasTool::Smudge, QKeySequence(Qt::Key_R));
  add_tool_action(tool_palette, tool_group, tr("Eraser"), CanvasTool::Eraser, QKeySequence(Qt::Key_E));
  add_tool_action(tool_palette, tool_group, tr("Gradient"), CanvasTool::Gradient, QKeySequence(Qt::Key_G));
  add_tool_action(tool_palette, tool_group, tr("Fill"), CanvasTool::Fill, QKeySequence(Qt::SHIFT | Qt::Key_G));
  auto* shape_menu = new QMenu(tr("Shape Tools"), tool_palette);
  shape_menu->setObjectName(QStringLiteral("shapeToolMenu"));
  const auto create_shape_action = [this, tool_group, shape_menu](const QString& label, CanvasTool tool,
                                                                  QKeySequence shortcut) {
    auto* action = new QAction(label, this);
    action->setIcon(tool_icon(tool));
    action->setCheckable(true);
    action->setData(static_cast<int>(tool));
    apply_action_shortcut(action, shortcut);
    tool_group->addAction(action);
    shape_menu->addAction(action);
    addAction(action);
    return action;
  };
  auto* line_tool_action =
      create_shape_action(tr("Line"), CanvasTool::Line, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  auto* rect_tool_action = create_shape_action(tr("Rect"), CanvasTool::Rectangle, QKeySequence(Qt::Key_U));
  auto* ellipse_tool_action =
      create_shape_action(tr("Ellipse"), CanvasTool::Ellipse, QKeySequence(Qt::SHIFT | Qt::Key_U));
  auto* shape_tool_button = new QToolButton(tool_palette);
  shape_tool_button->setObjectName(QStringLiteral("shapeToolButton"));
  shape_tool_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  shape_tool_button->setPopupMode(QToolButton::DelayedPopup);
  shape_tool_button->setMenu(shape_menu);
  shape_tool_button->setDefaultAction(rect_tool_action);
  shape_tool_button->setToolTip(rect_tool_action->toolTip());
  tool_palette->addWidget(shape_tool_button);
  for (auto* action : {line_tool_action, rect_tool_action, ellipse_tool_action}) {
    connect(action, &QAction::triggered, shape_tool_button, [shape_tool_button, shape_menu, action] {
      shape_tool_button->setDefaultAction(action);
      shape_tool_button->setMenu(shape_menu);
      shape_tool_button->setToolTip(action->toolTip());
    });
  }
  add_tool_action(tool_palette, tool_group, tr("Pick"), CanvasTool::Eyedropper, QKeySequence(Qt::Key_I));
  auto* type_tool_action =
      add_tool_action(tool_palette, tool_group, tr("Type"), CanvasTool::Text, QKeySequence(Qt::Key_T));
  add_tool_action(tool_palette, tool_group, tr("Hand"), CanvasTool::Pan, QKeySequence(Qt::Key_H));
  auto* zoom_tool_action = add_tool_action(tool_palette, tool_group, tr("Zoom"), CanvasTool::Zoom, QKeySequence(Qt::Key_Z));
  if (auto* zoom_button = qobject_cast<QToolButton*>(tool_palette->widgetForAction(zoom_tool_action));
      zoom_button != nullptr) {
    zoom_button->setObjectName(QStringLiteral("zoomToolButton"));
    zoom_button->installEventFilter(new MouseDoubleClickFilter(
        [this] {
          if (canvas_ != nullptr) {
            canvas_->set_zoom(1.0);
            refresh_document_info();
            statusBar()->showMessage(tr("Actual Pixels"));
          }
        },
        zoom_button));
  }
  connect(tool_group, &QActionGroup::triggered, this, [this](QAction* action) {
    const auto selected = static_cast<CanvasTool>(action->data().toInt());
    if (selected != CanvasTool::Text) {
      finish_active_text_editor();
    }
    current_tool_ = selected;
    canvas_->set_tool(selected);
    if (selected != CanvasTool::Text ||
        canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr) {
      canvas_->setFocus(Qt::OtherFocusReason);
    }
    refresh_options_bar();
    statusBar()->showMessage(tool_name(selected));
  });
  type_menu->addAction(type_tool_action);

  auto* palette_spacer = new QWidget(tool_palette);
  palette_spacer->setObjectName(QStringLiteral("toolPaletteSpacer"));
  palette_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  tool_palette->addWidget(palette_spacer);
  tool_palette->addSeparator();
  auto* default_colors_action = tool_palette->addAction(tr("Default Colors"));
  auto* swap_colors_action = tool_palette->addAction(tr("Swap Colors"));
  default_colors_action->setObjectName(QStringLiteral("colorDefaultAction"));
  swap_colors_action->setObjectName(QStringLiteral("colorSwapAction"));
  default_colors_action->setIcon(simple_icon(QStringLiteral("D")));
  swap_colors_action->setIcon(simple_icon(QStringLiteral("X")));
  apply_action_shortcut(default_colors_action, QKeySequence(Qt::Key_D));
  apply_action_shortcut(swap_colors_action, QKeySequence(Qt::Key_X));
  primary_color_button_ = new QPushButton(tr("FG"), tool_palette);
  secondary_color_button_ = new QPushButton(tr("BG"), tool_palette);
  primary_color_button_->setObjectName(QStringLiteral("foregroundColorButton"));
  secondary_color_button_->setObjectName(QStringLiteral("backgroundColorButton"));
  primary_color_button_->setToolTip(tr("Foreground color"));
  secondary_color_button_->setToolTip(tr("Background color"));
  tool_palette->addWidget(primary_color_button_);
  tool_palette->addWidget(secondary_color_button_);
  connect(primary_color_button_, &QPushButton::clicked, this, [this] { choose_primary_color(); });
  connect(secondary_color_button_, &QPushButton::clicked, this, [this] { choose_secondary_color(); });
  connect(swap_colors_action, &QAction::triggered, this, [this] { swap_colors(); });
  connect(default_colors_action, &QAction::triggered, this, [this] { default_colors(); });

  auto* toolbar = new QToolBar(tr("Options"), this);
  toolbar->setObjectName(QStringLiteral("Options"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setAllowedAreas(Qt::TopToolBarArea);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(18, 18));
  addToolBar(Qt::TopToolBarArea, toolbar);
  option_actions_.clear();
  const auto add_option_separator = [this, toolbar](std::initializer_list<CanvasTool> tools) {
    register_option_action(toolbar->addSeparator(), tools);
  };
  const auto add_option_action = [this, toolbar](const QIcon& icon, const QString& text,
                                                 std::initializer_list<CanvasTool> tools) {
    auto* action = toolbar->addAction(icon, text);
    register_option_action(action, tools);
    return action;
  };
  const auto add_option_widget = [this, toolbar](QWidget* widget, std::initializer_list<CanvasTool> tools) {
    auto* action = toolbar->addWidget(widget);
    register_option_action(action, tools);
    return action;
  };
  const auto add_option_label = [toolbar, add_option_widget](const QString& text,
                                                             std::initializer_list<CanvasTool> tools) {
    auto* label = new QLabel(text, toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    return add_option_widget(label, tools);
  };

  move_auto_select_check_ = new CheckGlyphBox(tr("Auto-Select"), toolbar);
  move_auto_select_check_->setObjectName(QStringLiteral("moveAutoSelectCheck"));
  move_auto_select_check_->setToolTip(tr("Automatically select the clicked layer while using Move"));
  move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
  });

  auto* selection_new = add_option_action(
      simple_icon(QStringLiteral("N")), tr("New Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_new->setObjectName(QStringLiteral("selectionNewModeAction"));
  auto* selection_add = add_option_action(
      simple_icon(QStringLiteral("+")), tr("Add to Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_add->setObjectName(QStringLiteral("selectionAddModeAction"));
  auto* selection_subtract = add_option_action(
      simple_icon(QStringLiteral("-")), tr("Subtract from Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_subtract->setObjectName(QStringLiteral("selectionSubtractModeAction"));
  auto* selection_intersect = add_option_action(simple_icon(QStringLiteral("Ix")), tr("Intersect Selection"),
                                                {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                                 CanvasTool::MagicWand});
  selection_intersect->setObjectName(QStringLiteral("selectionIntersectModeAction"));
  selection_new_mode_action_ = selection_new;
  selection_add_mode_action_ = selection_add;
  selection_subtract_mode_action_ = selection_subtract;
  selection_intersect_mode_action_ = selection_intersect;
  auto* selection_mode_group = new QActionGroup(this);
  selection_mode_group->setExclusive(true);
  const auto configure_selection_mode_action = [selection_mode_group](QAction* action) {
    action->setCheckable(true);
    selection_mode_group->addAction(action);
  };
  configure_selection_mode_action(selection_new);
  configure_selection_mode_action(selection_add);
  configure_selection_mode_action(selection_subtract);
  configure_selection_mode_action(selection_intersect);
  selection_new->setChecked(true);
  const auto set_selection_mode = [this](CanvasWidget::SelectionMode mode) {
    current_selection_mode_ = mode;
    if (canvas_ != nullptr) {
      canvas_->set_selection_mode(mode);
    }
    refresh_options_bar();
  };
  connect(selection_new, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Replace); });
  connect(selection_add, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Add); });
  connect(selection_subtract, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Subtract); });
  connect(selection_intersect, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Intersect); });
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});

  auto* feather_group = new QWidget(toolbar);
  feather_group->setObjectName(QStringLiteral("selectionFeatherGroup"));
  auto* feather_layout = new QHBoxLayout(feather_group);
  feather_layout->setContentsMargins(0, 0, 0, 0);
  feather_layout->setSpacing(0);
  auto* feather_label = new QLabel(tr("Feather:"), feather_group);
  feather_label->setAlignment(Qt::AlignCenter);
  feather_layout->addWidget(feather_label);
  auto* feather = new QSpinBox(feather_group);
  feather->setObjectName(QStringLiteral("selectionFeatherSpin"));
  feather->setRange(0, 250);
  feather->setSuffix(QStringLiteral(" px"));
  feather->setValue(current_selection_feather_radius_);
  configure_toolbar_spinbox(feather, 64);
  feather_layout->addWidget(feather);
  add_option_widget(feather_group,
                    {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  auto* anti_alias = new CheckGlyphBox(tr("Anti-alias"), toolbar);
  anti_alias->setObjectName(QStringLiteral("selectionAntiAliasCheck"));
  anti_alias->setChecked(current_selection_antialias_);
  add_option_widget(anti_alias,
                    {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  const auto apply_selection_edge_settings = [this, feather, anti_alias] {
    current_selection_feather_radius_ = feather->value();
    current_selection_antialias_ = anti_alias->isChecked();
    if (canvas_ != nullptr) {
      canvas_->set_selection_feather_radius(current_selection_feather_radius_);
      canvas_->set_selection_antialias(current_selection_antialias_);
    }
  };
  connect(feather, &QSpinBox::valueChanged, this, [apply_selection_edge_settings](int) {
    apply_selection_edge_settings();
  });
  connect(anti_alias, &QCheckBox::toggled, this, [apply_selection_edge_settings](bool) {
    apply_selection_edge_settings();
  });
  add_option_label(tr("Style:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* style_combo = new QComboBox(toolbar);
  style_combo->setObjectName(QStringLiteral("selectionStyleCombo"));
  style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  style_combo->setCurrentText(tr("Normal"));
  style_combo->setFixedWidth(92);
  add_option_widget(style_combo, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Width:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_width = new QSpinBox(toolbar);
  fixed_width->setObjectName(QStringLiteral("selectionFixedWidthSpin"));
  fixed_width->setRange(1, 30000);
  fixed_width->setValue(document().width());
  fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_width, 78);
  add_option_widget(fixed_width, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Height:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_height = new QSpinBox(toolbar);
  fixed_height->setObjectName(QStringLiteral("selectionFixedHeightSpin"));
  fixed_height->setRange(1, 30000);
  fixed_height->setValue(document().height());
  fixed_height->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_height, 78);
  add_option_widget(fixed_height, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  const auto apply_marquee_settings = [this, style_combo, fixed_width, fixed_height] {
    switch (style_combo->currentIndex()) {
      case 1:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_marquee_width_ = fixed_width->value();
    current_marquee_height_ = fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_marquee_style(current_marquee_style_);
      canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
    }
  };
  connect(style_combo, &QComboBox::currentIndexChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_width, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_height, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand});

  add_option_label(tr("Preset:"), {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser});
  brush_preset_combo_ = new QComboBox(toolbar);
  brush_preset_combo_->setObjectName(QStringLiteral("brushPresetCombo"));
  brush_preset_combo_->setMinimumWidth(132);
  for (const auto& preset : builtin_brush_presets()) {
    brush_preset_combo_->addItem(preset.name, preset.id);
  }
  {
    QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
    const auto saved_preset = settings.value(QStringLiteral("tools/brushPreset"), QStringLiteral("soft_round")).toString();
    const auto preset_index = brush_preset_combo_->findData(saved_preset);
    if (preset_index >= 0) {
      brush_preset_combo_->setCurrentIndex(preset_index);
    }
  }
  add_option_widget(brush_preset_combo_, {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser});

  add_option_label(tr("Size:"), {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser,
                                  CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_size = new QSpinBox(toolbar);
  brush_size->setObjectName(QStringLiteral("brushSizeSpin"));
  brush_size->setRange(1, 256);
  brush_size->setValue(canvas_->brush_size());
  configure_toolbar_spinbox(brush_size, 46);
  add_option_widget(brush_size,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_size_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_size_slider->setObjectName(QStringLiteral("brushSizeSlider"));
  brush_size_slider->setRange(1, 256);
  brush_size_slider->setValue(canvas_->brush_size());
  brush_size_slider->setFixedWidth(150);
  brush_size_slider->setToolTip(tr("Brush size"));
  add_option_widget(brush_size_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Opacity:"), {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser,
                                     CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_opacity = new QSpinBox(toolbar);
  brush_opacity->setObjectName(QStringLiteral("brushOpacitySpin"));
  brush_opacity->setRange(1, 100);
  brush_opacity->setValue(canvas_->brush_opacity());
  brush_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_opacity, 52);
  add_option_widget(brush_opacity,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_opacity_slider->setObjectName(QStringLiteral("brushOpacitySlider"));
  brush_opacity_slider->setRange(1, 100);
  brush_opacity_slider->setValue(canvas_->brush_opacity());
  brush_opacity_slider->setFixedWidth(120);
  brush_opacity_slider->setToolTip(tr("Brush opacity"));
  add_option_widget(brush_opacity_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Soft:"), {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser,
                                  CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_softness = new QSpinBox(toolbar);
  brush_softness->setObjectName(QStringLiteral("brushSoftnessSpin"));
  brush_softness->setRange(0, 100);
  brush_softness->setValue(canvas_->brush_softness());
  brush_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_softness, 52);
  add_option_widget(brush_softness,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_softness_slider->setObjectName(QStringLiteral("brushSoftnessSlider"));
  brush_softness_slider->setRange(0, 100);
  brush_softness_slider->setValue(canvas_->brush_softness());
  brush_softness_slider->setFixedWidth(110);
  brush_softness_slider->setToolTip(tr("Brush edge softness"));
  add_option_widget(brush_softness_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(brush_size, &QSpinBox::valueChanged, brush_size_slider, &QSlider::setValue);
  connect(brush_size_slider, &QSlider::valueChanged, brush_size, &QSpinBox::setValue);
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) {
    canvas_->set_brush_size(value);
    save_tool_settings();
  });
  connect(brush_opacity, &QSpinBox::valueChanged, brush_opacity_slider, &QSlider::setValue);
  connect(brush_opacity_slider, &QSlider::valueChanged, brush_opacity, &QSpinBox::setValue);
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    canvas_->set_brush_opacity(value);
    save_tool_settings();
  });
  connect(brush_softness, &QSpinBox::valueChanged, brush_softness_slider, &QSlider::setValue);
  connect(brush_softness_slider, &QSlider::valueChanged, brush_softness, &QSpinBox::setValue);
  connect(brush_softness, &QSpinBox::valueChanged, this, [this](int value) {
    canvas_->set_brush_softness(value);
    save_tool_settings();
  });
  connect(brush_preset_combo_, &QComboBox::currentIndexChanged, this,
          [this, brush_size, brush_opacity, brush_softness](int index) {
    if (brush_preset_combo_ == nullptr || index < 0) {
      return;
    }
    const auto preset_id = brush_preset_combo_->itemData(index).toString();
    const auto* preset = find_brush_preset(preset_id);
    if (preset == nullptr) {
      return;
    }
    canvas_->set_brush_build_up(preset->build_up);
    brush_size->setValue(preset->size);
    brush_opacity->setValue(preset->opacity);
    brush_softness->setValue(preset->softness);
    save_tool_settings();
    statusBar()->showMessage(tr("Brush preset: %1").arg(preset->name));
  });
  clone_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  clone_aligned_check_->setObjectName(QStringLiteral("cloneAlignedCheck"));
  clone_aligned_check_->setChecked(canvas_->clone_aligned());
  clone_aligned_check_->setToolTip(tr("Keep clone source offset aligned across strokes"));
  add_option_widget(clone_aligned_check_, {CanvasTool::Clone});
  connect(clone_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    canvas_->set_clone_aligned(checked);
    save_tool_settings();
  });
  auto* brush_smaller_action = new QAction(tr("Brush Smaller"), this);
  auto* brush_larger_action = new QAction(tr("Brush Larger"), this);
  auto* brush_much_smaller_action = new QAction(tr("Brush Much Smaller"), this);
  auto* brush_much_larger_action = new QAction(tr("Brush Much Larger"), this);
  brush_smaller_action->setObjectName(QStringLiteral("brushSmallerAction"));
  brush_larger_action->setObjectName(QStringLiteral("brushLargerAction"));
  brush_much_smaller_action->setObjectName(QStringLiteral("brushMuchSmallerAction"));
  brush_much_larger_action->setObjectName(QStringLiteral("brushMuchLargerAction"));
  apply_action_shortcut(brush_smaller_action, QKeySequence(Qt::Key_BracketLeft));
  apply_action_shortcut(brush_larger_action, QKeySequence(Qt::Key_BracketRight));
  apply_action_shortcut(brush_much_smaller_action, QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  apply_action_shortcut(brush_much_larger_action, QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  addAction(brush_smaller_action);
  addAction(brush_larger_action);
  addAction(brush_much_smaller_action);
  addAction(brush_much_larger_action);
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 1)); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 1)); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 10)); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 10)); });

  add_option_label(tr("Tol:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    canvas_->set_wand_tolerance(value);
    save_tool_settings();
  });

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) { canvas_->set_fill_shapes(checked); });

  add_option_label(tr("Font:"), {CanvasTool::Text});
  text_font_combo_ = new QFontComboBox(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  text_font_combo_->setFixedWidth(210);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  add_option_label(tr("Size:"), {CanvasTool::Text});
  text_size_spin_ = new QSpinBox(toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->setRange(6, 300);
  text_size_spin_->setValue(48);
  text_size_spin_->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(text_size_spin_, 62);
  add_option_widget(text_size_spin_, {CanvasTool::Text});
  text_bold_button_ = new QPushButton(tr("B"), toolbar);
  text_bold_button_->setObjectName(QStringLiteral("textBoldButton"));
  text_bold_button_->setCheckable(true);
  text_bold_button_->setToolTip(tr("Bold"));
  text_bold_button_->setFixedSize(30, 26);
  QFont bold_button_font = text_bold_button_->font();
  bold_button_font.setBold(true);
  text_bold_button_->setFont(bold_button_font);
  add_option_widget(text_bold_button_, {CanvasTool::Text});
  text_italic_button_ = new QPushButton(tr("I"), toolbar);
  text_italic_button_->setObjectName(QStringLiteral("textItalicButton"));
  text_italic_button_->setCheckable(true);
  text_italic_button_->setToolTip(tr("Italic"));
  text_italic_button_->setFixedSize(30, 26);
  QFont italic_button_font = text_italic_button_->font();
  italic_button_font.setItalic(true);
  text_italic_button_->setFont(italic_button_font);
  add_option_widget(text_italic_button_, {CanvasTool::Text});
  connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) { apply_text_options_to_active_editor(); });
  connect(text_size_spin_, &QSpinBox::valueChanged, this,
          [this](int) { apply_text_options_to_active_editor(); });
  connect(text_bold_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });
  connect(text_italic_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });

  window_menu->addAction(tool_palette->toggleViewAction());
  window_menu->addAction(toolbar->toggleViewAction());
  for (auto* action : menuBar()->actions()) {
    hide_menu_action_icons(action->menu());
  }
  refresh_options_bar();
  refresh_color_buttons();

  update_undo_redo_actions();
}

void MainWindow::create_docks() {
  auto* layers_dock = new QDockWidget(tr("Layers"), this);
  layers_dock->setObjectName(QStringLiteral("layersDock"));
  layers_dock->setMinimumHeight(500);
  auto* layers_panel = new QWidget(layers_dock);
  layers_panel->setObjectName(QStringLiteral("layersPanel"));
  layers_panel->setMinimumHeight(440);
  auto* layers_layout = new QVBoxLayout(layers_panel);
  layers_layout->setContentsMargins(6, 6, 6, 6);
  layers_layout->setSpacing(6);

  auto* layer_list = new LayerListWidget(layers_panel);
  layer_list->set_drop_finished_callback([this] { handle_layer_drop(); });
  layer_list->set_ctrl_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    if (target == LayerCtrlClickTarget::MaskThumbnail) {
      canvas_->select_layer_mask_pixels(id);
    } else {
      canvas_->select_layer_opaque_pixels(id);
    }
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(250);
  layer_list_->setMinimumHeight(300);
  layer_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layer_list_->setDragEnabled(true);
  layer_list_->setAcceptDrops(true);
  layer_list_->setDropIndicatorShown(true);
  layer_list_->setDragDropOverwriteMode(false);
  layer_list_->setDefaultDropAction(Qt::MoveAction);
  layer_list_->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list_, &QListWidget::itemSelectionChanged, this, [this] { set_active_layer_from_selection(); });
  connect(layer_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
    const auto active = document().active_layer_id();
    auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
    if (layer != nullptr && layer->kind() == LayerKind::Adjustment) {
      edit_active_adjustment_layer();
    }
  });
  connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    set_layer_visibility_from_item(item);
  });
  connect(layer_list_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_layer_context_menu(position); });
  connect(layer_list_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
    if (updating_layer_list_) {
      return;
    }
    if (const auto* list = dynamic_cast<const LayerListWidget*>(layer_list_); list != nullptr && list->drop_in_progress()) {
      return;
    }
    QTimer::singleShot(0, this, [this] { reorder_layers_from_list(); });
  });

  auto* layer_control_grid = new QGridLayout();
  layer_control_grid->setContentsMargins(0, 0, 0, 0);
  layer_control_grid->setHorizontalSpacing(6);
  layer_control_grid->setVerticalSpacing(4);
  auto* mode_label = new QLabel(tr("Mode"), layers_panel);
  layer_control_grid->addWidget(mode_label, 0, 0);
  blend_combo_ = new QComboBox(layers_panel);
  add_blend_mode_items(blend_combo_);
  blend_combo_->setObjectName(QStringLiteral("layerBlendModeCombo"));
  layer_control_grid->addWidget(blend_combo_, 0, 1, 1, 2);
  connect(blend_combo_, &QComboBox::currentIndexChanged, this, [this](int index) { set_active_layer_blend(index); });

  auto* opacity_label = new QLabel(tr("Opacity"), layers_panel);
  layer_control_grid->addWidget(opacity_label, 1, 0);
  opacity_slider_ = new QSlider(Qt::Horizontal, layers_panel);
  opacity_slider_->setRange(0, 100);
  opacity_slider_->setValue(100);
  layer_control_grid->addWidget(opacity_slider_, 1, 1);
  opacity_spin_ = new QSpinBox(layers_panel);
  opacity_spin_->setObjectName(QStringLiteral("layerOpacitySpin"));
  opacity_spin_->setRange(0, 100);
  opacity_spin_->setSuffix(QStringLiteral("%"));
  opacity_spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  opacity_spin_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  opacity_spin_->setFixedWidth(54);
  layer_control_grid->addWidget(opacity_spin_, 1, 2);
  connect(opacity_slider_, &QSlider::valueChanged, opacity_spin_, &QSpinBox::setValue);
  connect(opacity_spin_, &QSpinBox::valueChanged, opacity_slider_, &QSlider::setValue);
  connect(opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) { set_active_layer_opacity(value); });
  layers_layout->addLayout(layer_control_grid);
  layers_layout->addWidget(layer_list_, 1);

  auto* layer_buttons = new QHBoxLayout();
  layer_buttons->setContentsMargins(0, 0, 0, 0);
  layer_buttons->setSpacing(10);
  auto* add_button = new QPushButton(layers_panel);
  auto* add_folder_button = new QPushButton(layers_panel);
  auto* adjustment_button = new QToolButton(layers_panel);
  auto* duplicate_button = new QPushButton(layers_panel);
  auto* rename_button = new QPushButton(layers_panel);
  auto* delete_button = new QPushButton(layers_panel);
  adjustment_button->setObjectName(QStringLiteral("layerNewAdjustmentButton"));
  add_folder_button->setObjectName(QStringLiteral("layerNewFolderButton"));
  add_button->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_button->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  adjustment_button->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  duplicate_button->setIcon(simple_icon(QStringLiteral("dup")));
  rename_button->setIcon(simple_icon(QStringLiteral("RN")));
  delete_button->setIcon(simple_icon(QStringLiteral("trash")));
  add_button->setToolTip(tr("New Layer"));
  add_folder_button->setToolTip(tr("New Folder"));
  adjustment_button->setToolTip(tr("New Adjustment Layer"));
  duplicate_button->setToolTip(tr("Duplicate Layer"));
  rename_button->setToolTip(tr("Rename Layer"));
  delete_button->setToolTip(tr("Delete Layer"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, rename_button, delete_button}) {
    button->setProperty("layerActionButton", true);
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(40, 34);
  }
  adjustment_button->setProperty("layerActionButton", true);
  adjustment_button->setIconSize(QSize(24, 24));
  adjustment_button->setFixedSize(40, 34);
  auto* adjustment_button_menu = new QMenu(adjustment_button);
  adjustment_button_menu->setObjectName(QStringLiteral("layerNewAdjustmentButtonMenu"));
  populate_new_adjustment_layer_menu(adjustment_button_menu);
  hide_menu_action_icons(adjustment_button_menu);
  adjustment_button->setMenu(adjustment_button_menu);
  adjustment_button->setPopupMode(QToolButton::InstantPopup);
  layer_buttons->addWidget(add_button);
  layer_buttons->addWidget(add_folder_button);
  layer_buttons->addWidget(adjustment_button);
  layer_buttons->addWidget(duplicate_button);
  layer_buttons->addWidget(rename_button);
  layer_buttons->addWidget(delete_button);
  layer_buttons->addStretch(1);
  layers_layout->addLayout(layer_buttons);
  connect(add_button, &QPushButton::clicked, this, [this] { add_layer(); });
  connect(add_folder_button, &QPushButton::clicked, this, [this] { create_layer_folder(); });
  connect(duplicate_button, &QPushButton::clicked, this, [this] { duplicate_active_layer(); });
  connect(rename_button, &QPushButton::clicked, this, [this] { rename_active_layer(); });
  connect(delete_button, &QPushButton::clicked, this, [this] { delete_active_layer(); });

  lock_transparency_check_ = new QCheckBox(tr("Lock transparent pixels"), layers_panel);
  lock_transparency_check_->setObjectName(QStringLiteral("layerLockTransparencyCheck"));
  lock_transparency_check_->setToolTip(tr("Preserve existing transparency while painting, filling, or erasing"));
  layers_layout->addWidget(lock_transparency_check_);
  connect(lock_transparency_check_, &QCheckBox::toggled, this,
          [this](bool checked) { set_active_layer_lock_transparency(checked); });

  layers_dock->setWidget(layers_panel);
  install_collapsible_dock_title(layers_dock, layers_panel, QStringLiteral("layers"), 500);
  addDockWidget(Qt::RightDockWidgetArea, layers_dock);

  auto* history_dock = new QDockWidget(tr("History"), this);
  history_dock->setObjectName(QStringLiteral("historyDock"));
  history_list_ = new QListWidget(history_dock);
  history_list_->setObjectName(QStringLiteral("historyList"));
  history_dock->setWidget(history_list_);
  install_collapsible_dock_title(history_dock, history_list_, QStringLiteral("history"));
  addDockWidget(Qt::RightDockWidgetArea, history_dock);

  auto* properties_dock = new QDockWidget(tr("Properties"), this);
  properties_dock->setObjectName(QStringLiteral("propertiesDock"));
  auto* properties_panel = new QWidget(properties_dock);
  auto* properties_layout = new QVBoxLayout(properties_panel);
  document_info_label_ = new QLabel(properties_panel);
  document_info_label_->setObjectName(QStringLiteral("documentInfoLabel"));
  document_info_label_->setWordWrap(true);
  properties_layout->addWidget(document_info_label_);
  properties_layout->addStretch(1);
  properties_dock->setWidget(properties_panel);
  install_collapsible_dock_title(properties_dock, properties_panel, QStringLiteral("properties"));
  addDockWidget(Qt::RightDockWidgetArea, properties_dock);

  auto* info_dock = new QDockWidget(tr("Info"), this);
  info_dock->setObjectName(QStringLiteral("infoDock"));
  auto* info_panel = new QWidget(info_dock);
  auto* info_layout = new QVBoxLayout(info_panel);
  info_layout->setContentsMargins(8, 8, 8, 8);
  canvas_info_label_ = new QLabel(info_panel);
  canvas_info_label_->setObjectName(QStringLiteral("canvasInfoLabel"));
  canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
  canvas_info_label_->setWordWrap(true);
  info_layout->addWidget(canvas_info_label_);
  info_layout->addStretch(1);
  info_dock->setWidget(info_panel);
  install_collapsible_dock_title(info_dock, info_panel, QStringLiteral("info"));
  addDockWidget(Qt::RightDockWidgetArea, info_dock);

  create_swatches_dock();
}

void MainWindow::create_swatches_dock() {
  auto* swatches_dock = new QDockWidget(tr("Swatches"), this);
  swatches_dock->setObjectName(QStringLiteral("swatchesDock"));
  auto* swatches_panel = new QWidget(swatches_dock);
  auto* swatches_layout = new QGridLayout(swatches_panel);
  swatches_layout->setContentsMargins(8, 8, 8, 8);
  swatches_layout->setSpacing(5);

  const std::vector<QColor> swatches = {
      QColor(0, 0, 0),       QColor(255, 255, 255), QColor(220, 20, 40),  QColor(255, 140, 0),
      QColor(255, 220, 0),   QColor(30, 160, 80),   QColor(0, 150, 220),  QColor(50, 90, 220),
      QColor(140, 70, 220),  QColor(230, 60, 170),  QColor(110, 70, 35),  QColor(128, 128, 128),
      QColor(35, 40, 48),    QColor(245, 248, 252), QColor(80, 200, 180), QColor(255, 105, 105),
  };

  int index = 0;
  for (const auto& color : swatches) {
    auto* button = new QPushButton(swatches_panel);
    button->setObjectName(QStringLiteral("swatchButton"));
    button->setToolTip(tr("Set foreground color"));
    button->setStyleSheet(swatch_button_style(color));
    swatches_layout->addWidget(button, index / 4, index % 4);
    connect(button, &QPushButton::clicked, this, [this, color] {
      canvas_->set_primary_color(color);
      refresh_color_buttons();
      statusBar()->showMessage(tr("Foreground color changed"));
    });
    ++index;
  }

  swatches_dock->setWidget(swatches_panel);
  install_collapsible_dock_title(swatches_dock, swatches_panel, QStringLiteral("swatches"));
  addDockWidget(Qt::RightDockWidgetArea, swatches_dock);
}

void MainWindow::configure_canvas(CanvasWidget* canvas) {
  canvas->setObjectName(QStringLiteral("canvas"));
  canvas->set_before_edit_callback([this](QString label) { push_undo_snapshot(std::move(label)); });
  canvas->set_color_picked_callback([this, canvas](QColor color) {
    canvas->set_primary_color(color);
    refresh_color_buttons();
    statusBar()->showMessage(tr("Picked color"));
  });
  canvas->set_text_requested_callback([this](QPoint point) { add_text_at(point); });
  canvas->set_active_layer_changed_callback([this](LayerId layer_id) {
    reveal_layer_in_layer_list(layer_id);
    refresh_layer_controls();
  });
  canvas->set_status_callback([this](QString message) { statusBar()->showMessage(message); });
  canvas->set_info_callback([this](CanvasInfoState info) { update_canvas_info(std::move(info)); });
  canvas->set_document_changed_callback([this, canvas] {
    if (canvas == canvas_) {
      refresh_layer_thumbnails();
    }
  });
}

void MainWindow::add_document_session(Document document, QString title, QString path) {
  auto session = std::make_unique<DocumentSession>();
  session->document = std::move(document);
  session->title = std::move(title);
  session->path = std::move(path);
  collect_initially_collapsed_layer_groups(session->document.layers(), session->collapsed_layer_groups);
  session->canvas = new CanvasWidget(document_tabs_);
  session->canvas->setAcceptDrops(true);
  session->canvas->installEventFilter(this);
  configure_canvas(session->canvas);
  session->canvas->set_document(&session->document);
  if (canvas_ != nullptr) {
    session->canvas->set_brush_size(canvas_->brush_size());
    session->canvas->set_brush_opacity(canvas_->brush_opacity());
    session->canvas->set_brush_softness(canvas_->brush_softness());
    session->canvas->set_wand_tolerance(canvas_->wand_tolerance());
    session->canvas->set_clone_aligned(canvas_->clone_aligned());
  }
  if (move_auto_select_check_ != nullptr) {
    session->canvas->set_auto_select_layer(move_auto_select_check_->isChecked());
  }
  if (clone_aligned_check_ != nullptr) {
    session->canvas->set_clone_aligned(clone_aligned_check_->isChecked());
  }
  session->canvas->set_tool(current_tool_);
  session->canvas->set_selection_mode(current_selection_mode_);
  session->canvas->set_marquee_style(current_marquee_style_);
  session->canvas->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  session->canvas->set_selection_feather_radius(current_selection_feather_radius_);
  session->canvas->set_selection_antialias(current_selection_antialias_);

  auto* canvas = session->canvas;
  const auto tab_title = session->title;
  sessions_.push_back(std::move(session));
  const auto tab_index = document_tabs_->addTab(canvas, tab_title);
  document_tabs_->setCurrentIndex(tab_index);
  canvas_ = canvas;
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::activate_document_tab(int index) {
  auto* canvas = index >= 0 ? dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)) : nullptr;
  if (canvas == nullptr || session_for_canvas(canvas) == nullptr) {
    canvas_ = nullptr;
    return;
  }
  canvas_ = canvas;
  canvas_->set_tool(current_tool_);
  canvas_->set_selection_mode(current_selection_mode_);
  canvas_->set_marquee_style(current_marquee_style_);
  canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  canvas_->set_selection_feather_radius(current_selection_feather_radius_);
  canvas_->set_selection_antialias(current_selection_antialias_);
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_undo_redo_actions();
}

void MainWindow::close_document_tab(int index) {
  if (sessions_.size() <= 1 || index < 0 || index >= document_tabs_->count()) {
    return;
  }
  auto* widget = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [widget](const auto& candidate) {
    return candidate->canvas == widget;
  });
  if (found == sessions_.end()) {
    return;
  }
  if (!confirm_close_session(**found)) {
    return;
  }
  document_tabs_->removeTab(index);
  sessions_.erase(found);
  delete widget;
  activate_document_tab(document_tabs_->currentIndex());
  refresh_document_tab_titles();
}

bool MainWindow::confirm_close_session(DocumentSession& target_session) {
  if (!session_is_modified(target_session)) {
    return true;
  }

  const auto title = target_session.title.isEmpty() ? tr("Untitled") : target_session.title;
  const auto answer = QMessageBox::warning(this, tr("Save changes?"),
                                           tr("Save changes to %1 before closing?").arg(title),
                                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                           QMessageBox::Save);
  if (answer == QMessageBox::Cancel) {
    return false;
  }
  if (answer == QMessageBox::Discard) {
    return true;
  }
  return maybe_save_session(target_session);
}

bool MainWindow::maybe_save_session(DocumentSession& target_session) {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [&target_session](const auto& candidate) {
    return candidate.get() == &target_session;
  });
  if (found == sessions_.end()) {
    return false;
  }

  if (document_tabs_ != nullptr) {
    for (int index = 0; index < document_tabs_->count(); ++index) {
      if (document_tabs_->widget(index) == target_session.canvas) {
        document_tabs_->setCurrentIndex(index);
        break;
      }
    }
  }
  return save_document() && !session_is_modified(target_session);
}

bool MainWindow::session_is_modified(const DocumentSession& target_session) const noexcept {
  return target_session.revision != target_session.saved_revision;
}

void MainWindow::refresh_document_tab_titles() {
  if (document_tabs_ == nullptr) {
    return;
  }
  for (int index = 0; index < document_tabs_->count(); ++index) {
    auto* canvas = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
    const auto* target_session = session_for_canvas(canvas);
    if (target_session == nullptr) {
      continue;
    }
    auto title = target_session->title.isEmpty() ? tr("Untitled") : target_session->title;
    if (session_is_modified(*target_session)) {
      title.append(QStringLiteral("*"));
    }
    document_tabs_->setTabText(index, title);
  }
}

void MainWindow::set_session_saved(DocumentSession& target_session) {
  target_session.saved_revision = target_session.revision;
  refresh_document_tab_titles();
  update_undo_redo_actions();
  refresh_document_info();
}

void MainWindow::mark_session_modified(DocumentSession& target_session) {
  ++target_session.revision;
  refresh_document_tab_titles();
  update_undo_redo_actions();
  refresh_document_info();
}

MainWindow::DocumentSession* MainWindow::session_for_canvas(CanvasWidget* canvas) noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [canvas](const auto& candidate) {
    return candidate->canvas == canvas;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

const MainWindow::DocumentSession* MainWindow::session_for_canvas(CanvasWidget* canvas) const noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [canvas](const auto& candidate) {
    return candidate->canvas == canvas;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

Document& MainWindow::document() {
  auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return active_session->document;
}

const Document& MainWindow::document() const {
  const auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return active_session->document;
}

MainWindow::DocumentSession& MainWindow::session() {
  auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *active_session;
}

const MainWindow::DocumentSession& MainWindow::session() const {
  const auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *active_session;
}

void MainWindow::reset_document(std::int32_t width, std::int32_t height, QColor background, QString history_label) {
  Document new_document(width, height, PixelFormat::rgb8());
  const auto background_format = background.alpha() == 0 ? PixelFormat::rgba8() : PixelFormat::rgb8();
  new_document.add_pixel_layer("Background", make_solid_pixels(new_document.width(), new_document.height(), background,
                                                            background_format));
  new_document.add_pixel_layer("Paint Layer", make_solid_pixels(new_document.width(), new_document.height(), QColor(0, 0, 0, 0),
                                                             PixelFormat::rgba8()));
  add_document_session(std::move(new_document), tr("Untitled-%1").arg(sessions_.size() + 1));
  auto& active_session = session();
  active_session.undo_stack.clear();
  active_session.redo_stack.clear();
  if (history_list_ != nullptr) {
    history_list_->clear();
  }
  update_history(std::move(history_label));
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Created %1 x %2 document").arg(width).arg(height));
}

void MainWindow::create_new_document() {
  const auto settings = request_new_document_settings(this);
  if (!settings.has_value()) {
    return;
  }
  reset_document(settings->width, settings->height, settings->background, tr("New document"));
}

void MainWindow::resize_image_dialog() {
  auto& doc = document();
  const auto settings = request_image_size_settings(this, doc);
  if (!settings.has_value()) {
    return;
  }
  if (!settings->resample) {
    statusBar()->showMessage(tr("Image size unchanged; print resolution metadata is not supported yet"));
    return;
  }
  if (settings->width == doc.width() && settings->height == doc.height()) {
    return;
  }

  push_undo_snapshot(tr("Image size"));
  resize_image_and_layers(doc, settings->width, settings->height);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Image %1 x %2 at %3 ppi")
                               .arg(settings->width)
                               .arg(settings->height)
                               .arg(settings->resolution));
}

void MainWindow::resize_canvas_dialog() {
  auto& doc = document();
  const auto settings = request_canvas_size_settings(this, doc);
  if (!settings.has_value()) {
    return;
  }
  if (settings->width == doc.width() && settings->height == doc.height()) {
    return;
  }

  push_undo_snapshot(tr("Canvas size"));
  resize_canvas_and_layers(doc, settings->width, settings->height, settings->anchor, edit_color(settings->extension_color));
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Canvas %1 x %2").arg(settings->width).arg(settings->height));
}

void MainWindow::open_document() {
  const auto path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), open_file_filter());
  if (path.isEmpty()) {
    return;
  }
  open_document_path(path);
}

bool MainWindow::accept_open_file_drag(QDropEvent* event) {
  if (event == nullptr || supported_local_open_paths(event->mimeData()).isEmpty()) {
    if (event != nullptr) {
      event->ignore();
    }
    return false;
  }

  if ((event->possibleActions() & Qt::CopyAction) != 0) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->acceptProposedAction();
  }
  return true;
}

bool MainWindow::open_dropped_files(QDropEvent* event) {
  if (event == nullptr) {
    return false;
  }

  const auto paths = supported_local_open_paths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    statusBar()->showMessage(tr("Drop a supported image or Photoshop document"));
    return false;
  }

  if ((event->possibleActions() & Qt::CopyAction) != 0) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->acceptProposedAction();
  }

  for (const auto& path : paths) {
    open_document_path(path);
  }
  return true;
}

void MainWindow::open_document_path(QString path) {
  try {
    const auto info = QFileInfo(path);
    const auto extension = info.suffix().toLower();
    Document opened;
    if (is_photoshop_document_extension(extension)) {
      opened = psd::DocumentIo::read_file(path.toStdString());
    } else {
      QImageReader reader(path);
      reader.setAutoTransform(true);
      const auto image = reader.read();
      if (image.isNull()) {
        throw std::runtime_error(reader.errorString().toStdString());
      }
      opened = document_from_qimage(image, info.completeBaseName().toStdString());
    }
    add_document_session(std::move(opened), info.fileName(), path);
    if (is_photoshop_document_extension(extension)) {
      show_compatibility_report(this, document(), info.fileName());
    }
    canvas_->fit_to_view();
    session().undo_stack.clear();
    session().redo_stack.clear();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Open"));
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    add_recent_file(path);
    statusBar()->showMessage(tr("Opened %1").arg(path));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Open failed"), QString::fromUtf8(error.what()));
  }
}

bool MainWindow::save_document() {
  if (session().path.isEmpty()) {
    return save_document_as();
  }
  return save_document_to_path(session().path);
}

bool MainWindow::save_document_as() {
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(this, tr("Save As"), session().path, save_file_filter(), &selected_filter);
  if (path.isEmpty()) {
    return false;
  }
  path = path_with_default_extension(path, selected_filter);
  return save_document_to_path(path);
}

bool MainWindow::save_document_to_path(QString path) {
  try {
    const auto extension = extension_for_path(path);
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_layered_rgb8_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension);
    }
    auto& active_session = session();
    active_session.path = path;
    active_session.title = QFileInfo(path).fileName();
    set_session_saved(active_session);
    update_history(tr("Save"));
    add_recent_file(path);
    statusBar()->showMessage(tr("Saved %1").arg(path));
    return true;
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(error.what()));
  }
  return false;
}

void MainWindow::export_flat_image() {
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(this, tr("Export Flat Image"), QString(), export_image_filter(), &selected_filter);
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);

  try {
    const auto extension = extension_for_path(path);
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_flat_rgb8_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension);
    }
    update_history(tr("Export flat image"));
    statusBar()->showMessage(tr("Exported %1").arg(path));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Export failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::scan_legacy_plugins() {
  const auto paths = QFileDialog::getOpenFileNames(this, tr("Scan Legacy Photoshop Plug-ins"), QString(),
                                                  tr("Photoshop Plug-ins (*.8bf *.8bi *.8li);;All Files (*.*)"));
  if (paths.isEmpty()) {
    return;
  }

  QStringList report;
  int available = 0;
  for (const auto& path : paths) {
    if (register_legacy_plugin_path(path, &report)) {
      ++available;
    }
  }

  QMessageBox::information(this, tr("Legacy Photoshop Plug-ins"),
                           tr("%1 plug-in action(s) available under Plug-ins > Legacy Photoshop Plug-ins.\n\n%2")
                               .arg(available)
                               .arg(report.join('\n')));
}

void MainWindow::load_bundled_legacy_plugins() {
  const QDir fixture_dir(QCoreApplication::applicationDirPath() + QStringLiteral("/test-fixtures/photoshop-plugins"));
  if (!fixture_dir.exists()) {
    return;
  }

  const auto files =
      fixture_dir.entryInfoList({QStringLiteral("*.8bf"), QStringLiteral("*.8bi"), QStringLiteral("*.8li")},
                                QDir::Files | QDir::Readable, QDir::Name);
  for (const auto& file : files) {
    register_legacy_plugin_path(file.absoluteFilePath());
  }
}

bool MainWindow::register_legacy_plugin_path(const QString& path, QStringList* report) {
  LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(path.toStdString());
  const auto file_name = QFileInfo(path).fileName();
  if (report != nullptr) {
    *report << tr("%1: %2 (%3, %4)")
                   .arg(file_name, QString::fromStdString(probe.reason),
                        QString::fromStdString(legacy_plugin_kind_name(probe.kind)),
                        QString::fromStdString(probe.architecture));
  }
  if (!probe.supported) {
    return false;
  }

  const auto identifier = "legacy.photoshop." + QFileInfo(path).completeBaseName().toStdString();
  PluginDescriptor descriptor;
  descriptor.kind = probe.kind == LegacyPhotoshopPluginKind::Format8bi ? PHOTOSLOP_PLUGIN_FILE_FORMAT
                                                                        : PHOTOSLOP_PLUGIN_FILTER;
  descriptor.identifier = identifier;
  descriptor.display_name = QFileInfo(path).completeBaseName().toStdString();
  descriptor.path = path.toStdString();
  try {
    if (plugin_host_.find(identifier) == nullptr) {
      plugin_host_.register_plugin(std::move(descriptor));
    }
    if (const auto* registered = plugin_host_.find(identifier); registered != nullptr) {
      add_legacy_plugin_action(*registered);
      return true;
    }
  } catch (const std::exception&) {
  }
  return false;
}

void MainWindow::add_legacy_plugin_action(const PluginDescriptor& descriptor) {
  if (legacy_plugins_menu_ == nullptr) {
    return;
  }
  const auto identifier = QString::fromStdString(descriptor.identifier);
  for (auto* action : legacy_plugins_menu_->actions()) {
    if (action->data().toString() == identifier) {
      return;
    }
  }

  auto* action = legacy_plugins_menu_->addAction(QString::fromStdString(descriptor.display_name));
  action->setData(identifier);
  action->setObjectName(QStringLiteral("legacyPluginAction"));
  action->setIcon(simple_icon(QStringLiteral("8BF"), QColor(105, 185, 255)));
  action->setIconVisibleInMenu(false);
  connect(action, &QAction::triggered, this, [this, identifier] { run_legacy_plugin(identifier); });
}

void MainWindow::run_legacy_plugin(QString identifier) {
  const auto* descriptor = plugin_host_.find(identifier.toStdString());
  if (descriptor == nullptr) {
    return;
  }
  const auto active = document().active_layer_id();
  if (!active.has_value()) {
    statusBar()->showMessage(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  auto* layer = document().find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }

  const auto name = QString::fromStdString(descriptor->display_name).toLower();
  if (!name.contains(QStringLiteral("greyscale")) && !name.contains(QStringLiteral("white to transparent"))) {
    QMessageBox::information(this, tr("Legacy Photoshop Plug-in"),
                             tr("%1 was scanned and is available, but this build only has compatibility shims for the "
                                "bundled Greyscale and White to Transparent test filters. A full 8BF host still needs "
                                "the out-of-process Photoshop SDK adapter.")
                                 .arg(QString::fromStdString(descriptor->display_name)));
    return;
  }

  push_undo_snapshot(tr("Legacy plug-in"));
  auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      if (name.contains(QStringLiteral("greyscale"))) {
        const auto gray = static_cast<std::uint8_t>(
            std::clamp(std::lround(0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]), 0L, 255L));
        px[0] = gray;
        px[1] = gray;
        px[2] = gray;
      } else {
        if (channels < 4) {
          continue;
        }
        const auto whiteness = std::min({px[0], px[1], px[2]});
        px[3] = static_cast<std::uint8_t>(255 - whiteness);
      }
    }
  }
  canvas_->document_changed(to_qrect(layer->bounds()));
  statusBar()->showMessage(tr("Applied %1").arg(QString::fromStdString(descriptor->display_name)));
}

void MainWindow::cut_selection() {
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to cut"));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<LayerId> layers_to_cut;
  for (const auto& layer : document().layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_cut.push_back(layer.id());
  }
  if (layers_to_cut.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing cut"));
    return;
  }

  copy_selection();
  if (!clipboard_.has_value() || clipboard_->pixels.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Cut"));
  Rect affected;
  auto options = edit_options(*canvas_);
  for (const auto id : layers_to_cut) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Cut %1 layer(s)").arg(static_cast<qulonglong>(layers_to_cut.size())));
}

void MainWindow::copy_selection() {
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to copy"));
    return;
  }

  ids = root_drop_layer_ids(document().layers(), ids);
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to copy"));
    return;
  }

  const auto selected_layers = find_layers_top_to_bottom(document().layers(), ids);
  if (selected_layers.empty()) {
    statusBar()->showMessage(tr("Select a layer to copy"));
    return;
  }

  const auto contains_non_pixel_layer =
      std::any_of(selected_layers.begin(), selected_layers.end(), [](const Layer* layer) {
        return layer != nullptr && layer->kind() != LayerKind::Pixel;
      });
  if (!canvas_->selected_document_rect().has_value() || contains_non_pixel_layer) {
    ClipboardPayload payload;
    payload.layers_top_to_bottom.reserve(selected_layers.size());
    for (const auto* layer : selected_layers) {
      payload.layers_top_to_bottom.push_back(*layer);
    }
    clipboard_ = std::move(payload);
    QApplication::clipboard()->clear();
    update_history(tr("Copy"));
    statusBar()->showMessage(tr("Copied %1 layer(s)").arg(static_cast<qulonglong>(selected_layers.size())));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto* layer : selected_layers) {
    if (layer == nullptr || !selected.contains(layer->id()) || layer->kind() != LayerKind::Pixel ||
        !layer->visible()) {
      continue;
    }
    layers_to_copy.push_back(layer);
  }
  if (layers_to_copy.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing copied"));
    return;
  }

  Rect copy_rect;
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas_->selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document().width(), document().height()));
  if (copy_rect.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, canvas_);
  } else {
    Document selected_document(document().width(), document().height(), document().format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, *canvas_);
  }

  clipboard_ = ClipboardPayload{std::move(copied), QPoint(copy_rect.x, copy_rect.y)};
  QApplication::clipboard()->setImage(image_from_pixels(clipboard_->pixels));
  update_history(tr("Copy"));
  statusBar()->showMessage(
      tr("Copied %1 layer(s), %2 x %3 px")
          .arg(static_cast<qulonglong>(layers_to_copy.size()))
          .arg(copy_rect.width)
          .arg(copy_rect.height));
}

void MainWindow::copy_merged() {
  auto copy_rect = Rect::from_size(document().width(), document().height());
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = intersect_copy_rect(copy_rect, to_core_rect(*canvas_->selected_document_rect()));
  }
  if (copy_rect.empty()) {
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  const auto image = qimage_from_document(document(), true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
  clipboard_ = ClipboardPayload{pixels_from_image_rgba(image), QPoint(copy_rect.x, copy_rect.y)};
  QApplication::clipboard()->setImage(image);
  update_history(tr("Copy merged"));
  statusBar()->showMessage(tr("Copied merged %1 x %2 px").arg(copy_rect.width).arg(copy_rect.height));
}

void MainWindow::paste_clipboard() {
  if (clipboard_.has_value() && !clipboard_->layers_top_to_bottom.empty()) {
    auto& doc = document();
    std::set<std::string> existing_names;
    collect_layer_names(doc.layers(), existing_names);

    push_undo_snapshot(tr("Paste"));
    for (auto it = clipboard_->layers_top_to_bottom.rbegin(); it != clipboard_->layers_top_to_bottom.rend(); ++it) {
      auto pasted = clone_layer_tree_with_document_ids(doc, *it);
      pasted.set_name(next_duplicate_layer_name(it->name(), existing_names));
      existing_names.insert(pasted.name());
      doc.add_layer(std::move(pasted));
    }
    refresh_layer_list();
    refresh_layer_controls();
    canvas_->document_changed();
    statusBar()->showMessage(
        tr("Pasted %1 layer(s)").arg(static_cast<qulonglong>(clipboard_->layers_top_to_bottom.size())));
    return;
  }

  PixelBuffer pixels;
  QPoint origin;
  if (clipboard_.has_value() && !clipboard_->pixels.empty()) {
    pixels = clipboard_->pixels;
    origin = clipboard_->origin;
  } else {
    const auto image = QApplication::clipboard()->image();
    if (image.isNull()) {
      statusBar()->showMessage(tr("Clipboard does not contain an image"));
      return;
    }
    pixels = pixels_from_image_rgba(image);
    origin = QPoint(std::max(0, (document().width() - pixels.width()) / 2),
                    std::max(0, (document().height() - pixels.height()) / 2));
  }

  push_undo_snapshot(tr("Paste"));
  Layer pasted(document().allocate_layer_id(), "Pasted Layer", std::move(pixels));
  pasted.set_bounds(Rect{origin.x(), origin.y(), pasted.pixels().width(), pasted.pixels().height()});
  document().add_layer(std::move(pasted));
  if (move_tool_action_ != nullptr) {
    move_tool_action_->trigger();
  } else {
    current_tool_ = CanvasTool::Move;
    canvas_->set_tool(current_tool_);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Pasted as new layer"));
}

void MainWindow::transform_active_layer_dialog() {
  if (canvas_ == nullptr || !canvas_->begin_free_transform()) {
    statusBar()->showMessage(tr("Select a pixel layer to transform"));
  }
}

void MainWindow::add_text_at(QPoint document_point) {
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }

  std::optional<LayerId> editing_layer;
  std::optional<bool> editing_layer_was_visible;
  QString initial_text;
  QString family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : font().family();
  int document_text_size = text_size_spin_ != nullptr ? text_size_spin_->value() : 48;
  bool text_bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  bool text_italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  QColor text_color = canvas_->primary_color();
  int document_editor_width = std::max(160, std::min(520, document().width() - document_point.x() - 8));
  int document_editor_height = 96;
  if (const auto active = document().active_layer_id(); active.has_value()) {
    if (auto* layer = document().find_layer(*active); layer != nullptr && layer_is_text(*layer) &&
        layer->bounds().contains(document_point.x(), document_point.y())) {
      editing_layer = *active;
      editing_layer_was_visible = layer->visible();
      initial_text = QString::fromStdString(layer->metadata().at(kLayerMetadataText));
      if (const auto found = layer->metadata().find(kLayerMetadataTextFont); found != layer->metadata().end()) {
        const auto stored_family = QString::fromStdString(found->second);
        if (stored_family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) != 0) {
          family = stored_family;
        }
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextSize); found != layer->metadata().end()) {
        document_text_size = std::max(1, std::atoi(found->second.c_str()));
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextColor); found != layer->metadata().end()) {
        const QColor stored(QString::fromStdString(found->second));
        if (stored.isValid()) {
          text_color = stored;
        }
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextBold); found != layer->metadata().end()) {
        text_bold = found->second == "true";
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextItalic); found != layer->metadata().end()) {
        text_italic = found->second == "true";
      }
      if (text_font_combo_ != nullptr) {
        text_font_combo_->setCurrentFont(QFont(family));
      }
      if (text_size_spin_ != nullptr) {
        text_size_spin_->setValue(document_text_size);
      }
      if (text_bold_button_ != nullptr) {
        text_bold_button_->setChecked(text_bold);
      }
      if (text_italic_button_ != nullptr) {
        text_italic_button_->setChecked(text_italic);
      }
      document_point = QPoint(layer->bounds().x, layer->bounds().y);
      document_editor_width = std::max(160, layer->bounds().width);
      document_editor_height = std::max(64, layer->bounds().height + document_text_size);
      layer->set_visible(false);
      canvas_->document_changed(to_qrect(layer->bounds()));
    }
  }

  auto* editor = new QTextEdit(canvas_);
  editor->setObjectName(QStringLiteral("inlineTextEditor"));
  editor->setAcceptRichText(false);
  editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
  editor->selectAll();
  QFont editor_font(family);
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))));
  editor_font.setBold(text_bold);
  editor_font.setItalic(text_italic);
  editor->setFont(editor_font);
  editor->document()->setDocumentMargin(0);
  editor->document()->setDefaultFont(editor_font);
  editor->setFrameShape(QFrame::NoFrame);
  editor->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAutoFillBackground(false);
  editor->setLineWrapMode(QTextEdit::NoWrap);
  editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setProperty("photoslop.documentTextSize", document_text_size);
  editor->setProperty("photoslop.documentTextWidth", document_editor_width);
  editor->setProperty("photoslop.documentTextColor", text_color);
  editor->setProperty("photoslop.documentTextX", document_point.x());
  editor->setProperty("photoslop.documentTextY", document_point.y());
  if (editing_layer.has_value()) {
    editor->setProperty("photoslop.editingLayerId", QVariant::fromValue<qulonglong>(*editing_layer));
  }
  if (editing_layer_was_visible.has_value()) {
    editor->setProperty("photoslop.editingLayerWasVisible", *editing_layer_was_visible);
  }
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(),
                      std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom()))),
                      std::max(32, static_cast<int>(std::round(document_editor_height * canvas_->zoom()))));
  const auto resize_editor = [editor = QPointer<QTextEdit>(editor), this] {
    if (editor == nullptr) {
      return;
    }
    const auto document_editor_width = std::max(64, editor->property("photoslop.documentTextWidth").toInt());
    const auto document_text_size = std::max(1, editor->property("photoslop.documentTextSize").toInt());
    const auto minimum_width = std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom())));
    editor->document()->setTextWidth(-1);
    const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
    const auto width = std::max(minimum_width, content_width);
    editor->setProperty("photoslop.documentTextWidth",
                        std::max(document_editor_width,
                                 static_cast<int>(std::ceil(static_cast<double>(width) / canvas_->zoom()))));
    editor->document()->setTextWidth(width);
    const auto text_height =
        std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
    const auto minimum_height =
        std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * canvas_->zoom() * 1.45)));
    editor->resize(width, std::max(text_height, minimum_height));
  };
  connect(editor, &QTextEdit::textChanged, editor, resize_editor);
  editor->show();
  resize_editor();
  editor->setFocus(Qt::OtherFocusReason);

  auto committed = std::make_shared<bool>(false);
  const auto commit = [this, editor = QPointer<QTextEdit>(editor), document_point, editing_layer, committed] {
    if (*committed || editor == nullptr) {
      return;
    }
    *committed = true;
    commit_text_editor(editor, document_point, editing_layer);
  };
  auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), editor);
  connect(shortcut, &QShortcut::activated, editor, commit);
  auto* cancel_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
  cancel_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(cancel_shortcut, &QShortcut::activated, editor,
          [this, editor = QPointer<QTextEdit>(editor), editing_layer, committed] {
            if (*committed || editor == nullptr) {
              return;
            }
            *committed = true;
            cancel_text_editor(editor, editing_layer);
          });
  connect(qApp, &QApplication::focusChanged, editor, [this, editor = QPointer<QTextEdit>(editor), commit](QWidget* old,
                                                                                                          QWidget* now) {
    if (editor == nullptr) {
      return;
    }
    const auto left_editor = old == editor || editor->isAncestorOf(old);
    const auto entered_editor = now == editor || editor->isAncestorOf(now);
    const auto entered_canvas = canvas_ != nullptr && (now == canvas_ || canvas_->isAncestorOf(now));
    if (((left_editor && !entered_editor) || entered_canvas) && !entered_editor && !is_text_option_widget(now)) {
      commit();
    }
  });
}

void MainWindow::cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id) {
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  const auto restore_existing_visibility =
      editor->property("photoslop.editingLayerWasVisible").isValid()
          ? editor->property("photoslop.editingLayerWasVisible").toBool()
          : true;
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed(to_qrect(layer->bounds()));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  statusBar()->showMessage(tr("Canceled text edit"));
}

void MainWindow::commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id) {
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  const auto text = editor->toPlainText().trimmed();
  const auto restore_existing_visibility =
      editor->property("photoslop.editingLayerWasVisible").isValid()
          ? editor->property("photoslop.editingLayerWasVisible").toBool()
          : true;
  const auto restore_hidden_text_layer = [this, layer_id, restore_existing_visibility] {
    if (!layer_id.has_value()) {
      return;
    }
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed(to_qrect(layer->bounds()));
      refresh_layer_list();
      refresh_layer_controls();
    }
  };
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();
  if (text.isEmpty()) {
    restore_hidden_text_layer();
    return;
  }

  const auto text_size = std::max(1, editor->property("photoslop.documentTextSize").toInt());
  const auto text_width =
      std::max(64, editor->property("photoslop.documentTextWidth").toInt());
  const auto text_color = editor->property("photoslop.documentTextColor").value<QColor>().isValid()
                              ? editor->property("photoslop.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  TextToolSettings settings{text, editor->font().family(), text_size, editor->font().bold(), editor->font().italic()};
  auto pixels = render_text_pixels(settings, text_color, text_width);
  if (pixels.empty()) {
    restore_hidden_text_layer();
    return;
  }

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
    }
  }
  push_undo_snapshot(tr("Type"));
  auto name = settings.text.simplified();
  if (name.size() > 24) {
    name = name.left(24) + QStringLiteral("...");
  }
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_name(tr("Text: %1").arg(name).toStdString());
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(Rect{document_point.x(), document_point.y(), layer->pixels().width(), layer->pixels().height()});
      layer->set_visible(restore_existing_visibility);
      layer->metadata()[kLayerMetadataText] = settings.text.toStdString();
      layer->metadata()[kLayerMetadataTextFont] = settings.family.toStdString();
      layer->metadata()[kLayerMetadataTextSize] = std::to_string(settings.size);
      layer->metadata()[kLayerMetadataTextColor] = text_color.name(QColor::HexRgb).toStdString();
      layer->metadata()[kLayerMetadataTextBold] = settings.bold ? "true" : "false";
      layer->metadata()[kLayerMetadataTextItalic] = settings.italic ? "true" : "false";
    }
  } else {
    Layer text_layer(document().allocate_layer_id(), tr("Text: %1").arg(name).toStdString(), std::move(pixels));
    text_layer.set_bounds(
        Rect{document_point.x(), document_point.y(), text_layer.pixels().width(), text_layer.pixels().height()});
    text_layer.metadata()[kLayerMetadataText] = settings.text.toStdString();
    text_layer.metadata()[kLayerMetadataTextFont] = settings.family.toStdString();
    text_layer.metadata()[kLayerMetadataTextSize] = std::to_string(settings.size);
    text_layer.metadata()[kLayerMetadataTextColor] = text_color.name(QColor::HexRgb).toStdString();
    text_layer.metadata()[kLayerMetadataTextBold] = settings.bold ? "true" : "false";
    text_layer.metadata()[kLayerMetadataTextItalic] = settings.italic ? "true" : "false";
    document().add_layer(std::move(text_layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created text layer"));
}

void MainWindow::finish_active_text_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }
  const QPoint document_point(editor->property("photoslop.documentTextX").toInt(),
                              editor->property("photoslop.documentTextY").toInt());
  std::optional<LayerId> layer_id;
  if (editor->property("photoslop.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("photoslop.editingLayerId").toULongLong());
  }
  commit_text_editor(editor, document_point, layer_id);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::apply_filter(const QString& identifier) {
  auto& doc = document();
  auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }

  try {
    const auto identifier_text = identifier.toStdString();
    const auto* filter = filters_.find(identifier_text);
    if (filter == nullptr) {
      throw std::invalid_argument("Unknown filter identifier");
    }
    const auto display_name = QString::fromStdString(filter->display_name);
    const auto dialog_spec = filter_dialog_spec_for(*filter);
    const auto selection = canvas_->selected_document_region();
    const auto bounds = layer->bounds();
    const auto original_pixels = layer->pixels();
    const auto foreground = canvas_->primary_color();
    const auto background = canvas_->secondary_color();
    const auto preview_changed = [this, active, original_pixels, selection, bounds, identifier, foreground,
                                  background](FilterPreviewSettings settings) {
      auto* preview_layer = document().find_layer(*active);
      if (preview_layer == nullptr) {
        return;
      }
      try {
        preview_layer->set_pixels(build_filter_preview_pixels(original_pixels, selection, bounds, identifier, filters_,
                                                              settings, foreground, background));
        canvas_->document_changed(to_qrect(bounds));
      } catch (const std::exception& error) {
        statusBar()->showMessage(tr("Filter preview failed: %1").arg(QString::fromUtf8(error.what())));
      }
    };

    const auto settings = request_filter_settings(this, dialog_spec, preview_changed);
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    layer->set_pixels(original_pixels);
    canvas_->document_changed(to_qrect(layer->bounds()));
    if (!settings.has_value()) {
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }

    auto final_pixels = build_filter_preview_pixels(original_pixels, selection, bounds, identifier, filters_,
                                                    FilterPreviewSettings{true, *settings}, foreground, background);
    if (pixel_buffers_equal(final_pixels, original_pixels)) {
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      return;
    }

    push_undo_snapshot(tr("Filter: %1").arg(display_name));
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    layer->set_pixels(std::move(final_pixels));
    canvas_->document_changed(to_qrect(bounds));
    statusBar()->showMessage(tr("Applied %1").arg(display_name));
  } catch (const std::exception& error) {
    if (active.has_value()) {
      if (auto* restore_layer = doc.find_layer(*active); restore_layer != nullptr) {
        canvas_->document_changed(to_qrect(restore_layer->bounds()));
      }
    }
    QMessageBox::critical(this, tr("Filter failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::populate_new_adjustment_layer_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }

  const auto add_adjustment = [this, menu, &object_name_prefix](const QString& label, const QString& object_key,
                                                               const QString& icon_label, auto callback) {
    auto* action = menu->addAction(simple_icon(icon_label), label);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_adjustment(tr("&Levels..."), QStringLiteral("LevelsAdjustment"), QStringLiteral("LVL"),
                 [this] { new_levels_adjustment_layer(); });
  add_adjustment(tr("&Curves..."), QStringLiteral("CurvesAdjustment"), QStringLiteral("CRV"),
                 [this] { new_curves_adjustment_layer(); });
  add_adjustment(tr("&Hue/Saturation..."), QStringLiteral("HueSaturationAdjustment"), QStringLiteral("HSL"),
                 [this] { new_hue_saturation_adjustment_layer(); });
  add_adjustment(tr("Color &Balance..."), QStringLiteral("ColorBalanceAdjustment"), QStringLiteral("CB"),
                 [this] { new_color_balance_adjustment_layer(); });
}

void MainWindow::new_levels_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const LevelsSettings& levels) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Levels;
    settings.levels = LevelsAdjustment{std::clamp(levels.black_input, 0, 254),
                                       std::clamp(levels.white_input,
                                                  std::clamp(levels.black_input, 0, 254) + 1, 255),
                                       std::clamp(levels.gamma_percent, 10, 999)};
    update_adjustment_layer_preview(tr("Levels"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto settings = request_levels_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }
  apply_levels_adjustment(settings->black_input, settings->white_input, settings->gamma_percent, true);
}

void MainWindow::levels_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  const auto original_pixels = layer->pixels();
  const auto selection = canvas_->selected_document_region();
  const auto preview_changed = [this, active_id, bounds, original_pixels, selection](bool enabled,
                                                                                    const LevelsSettings& settings) {
    auto* preview_layer = document().find_layer(active_id);
    if (preview_layer == nullptr) {
      return;
    }
    auto pixels = original_pixels;
    if (enabled && !(settings.black_input == 0 && settings.white_input == 255 && settings.gamma_percent == 100)) {
      apply_levels_to_pixels(pixels, bounds, selection, settings);
    }
    preview_layer->set_pixels(std::move(pixels));
    canvas_->document_changed(to_qrect(bounds));
  };

  const auto settings = request_levels_settings(this, preview_changed);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(original_pixels);
  canvas_->document_changed(to_qrect(bounds));
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }
  apply_levels_adjustment(settings->black_input, settings->white_input, settings->gamma_percent);
}

void MainWindow::apply_levels_adjustment(int black_input, int white_input, int gamma_percent, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Levels;
  settings.levels = LevelsAdjustment{std::clamp(black_input, 0, 254),
                                     std::clamp(white_input, std::clamp(black_input, 0, 254) + 1, 255),
                                     std::clamp(gamma_percent, 10, 999)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Levels"), settings);
}

void MainWindow::new_curves_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const CurvesSettings& curves) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Curves;
    settings.curves = CurvesAdjustment{std::clamp(curves.shadow_output, 0, 255),
                                       std::clamp(curves.midtone_output, 0, 255),
                                       std::clamp(curves.highlight_output, 0, 255)};
    update_adjustment_layer_preview(tr("Curves"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto settings = request_curves_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(settings->shadow_output, settings->midtone_output, settings->highlight_output, true);
}

void MainWindow::curves_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  const auto original_pixels = layer->pixels();
  const auto selection = canvas_->selected_document_region();
  const auto preview_changed = [this, active_id, bounds, original_pixels, selection](bool enabled,
                                                                                    const CurvesSettings& settings) {
    auto* preview_layer = document().find_layer(active_id);
    if (preview_layer == nullptr) {
      return;
    }
    auto pixels = original_pixels;
    if (enabled &&
        !(settings.shadow_output == 0 && settings.midtone_output == 128 && settings.highlight_output == 255)) {
      apply_curves_to_pixels(pixels, bounds, selection, settings);
    }
    preview_layer->set_pixels(std::move(pixels));
    canvas_->document_changed(to_qrect(bounds));
  };

  const auto settings = request_curves_settings(this, preview_changed);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(original_pixels);
  canvas_->document_changed(to_qrect(bounds));
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(settings->shadow_output, settings->midtone_output, settings->highlight_output);
}

void MainWindow::apply_curves_adjustment(int shadow_output, int midtone_output, int highlight_output,
                                        bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Curves;
  settings.curves = CurvesAdjustment{std::clamp(shadow_output, 0, 255), std::clamp(midtone_output, 0, 255),
                                     std::clamp(highlight_output, 0, 255)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Curves"), settings);
}

void MainWindow::new_hue_saturation_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](
                                   bool enabled, const HueSaturationSettings& hue_saturation) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation = HueSaturationAdjustment{std::clamp(hue_saturation.hue_shift, -180, 180),
                                                      std::clamp(hue_saturation.saturation_delta, -100, 100),
                                                      std::clamp(hue_saturation.lightness_delta, -100, 100)};
    update_adjustment_layer_preview(tr("Hue/Saturation"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto settings = request_hue_saturation_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(settings->hue_shift, settings->saturation_delta, settings->lightness_delta, true);
}

void MainWindow::hue_saturation_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  const auto original_pixels = layer->pixels();
  const auto selection = canvas_->selected_document_region();
  const auto preview_changed = [this, active_id, bounds, original_pixels, selection](
                                   bool enabled, const HueSaturationSettings& settings) {
    auto* preview_layer = document().find_layer(active_id);
    if (preview_layer == nullptr) {
      return;
    }
    auto pixels = original_pixels;
    if (enabled && !(settings.hue_shift == 0 && settings.saturation_delta == 0 && settings.lightness_delta == 0)) {
      apply_hue_saturation_to_pixels(pixels, bounds, selection, settings);
    }
    preview_layer->set_pixels(std::move(pixels));
    canvas_->document_changed(to_qrect(bounds));
  };

  const auto settings = request_hue_saturation_settings(this, preview_changed);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(original_pixels);
  canvas_->document_changed(to_qrect(bounds));
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(settings->hue_shift, settings->saturation_delta, settings->lightness_delta);
}

void MainWindow::apply_hue_saturation_adjustment(int hue_shift, int saturation_delta, int lightness_delta,
                                                 bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::HueSaturation;
  settings.hue_saturation = HueSaturationAdjustment{std::clamp(hue_shift, -180, 180),
                                                    std::clamp(saturation_delta, -100, 100),
                                                    std::clamp(lightness_delta, -100, 100)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Hue/Saturation"), settings);
}

void MainWindow::new_color_balance_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ColorBalanceSettings& color_balance) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::ColorBalance;
    settings.color_balance = ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                                    std::clamp(color_balance.magenta_green, -100, 100),
                                                    std::clamp(color_balance.yellow_blue, -100, 100)};
    update_adjustment_layer_preview(tr("Color Balance"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto settings = request_color_balance_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue, true);
}

void MainWindow::color_balance_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  const auto original_pixels = layer->pixels();
  const auto selection = canvas_->selected_document_region();
  const auto preview_changed = [this, active_id, bounds, original_pixels, selection](
                                   bool enabled, const ColorBalanceSettings& settings) {
    auto* preview_layer = document().find_layer(active_id);
    if (preview_layer == nullptr) {
      return;
    }
    auto pixels = original_pixels;
    if (enabled && !(settings.cyan_red == 0 && settings.magenta_green == 0 && settings.yellow_blue == 0)) {
      apply_color_balance_to_pixels(pixels, bounds, selection, settings);
    }
    preview_layer->set_pixels(std::move(pixels));
    canvas_->document_changed(to_qrect(bounds));
  };

  const auto settings = request_color_balance_settings(this, preview_changed);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(original_pixels);
  canvas_->document_changed(to_qrect(bounds));
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue);
}

void MainWindow::apply_color_balance_adjustment(int cyan_red, int magenta_green, int yellow_blue, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance = ColorBalanceAdjustment{std::clamp(cyan_red, -100, 100),
                                                  std::clamp(magenta_green, -100, 100),
                                                  std::clamp(yellow_blue, -100, 100)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Color Balance"), settings);
}

Layer MainWindow::build_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  auto& doc = document();
  Layer layer(doc.allocate_layer_id(), label.toStdString(), LayerKind::Adjustment);
  layer.set_bounds(Rect::from_size(doc.width(), doc.height()));
  configure_adjustment_layer(layer, settings);

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  if (!selection.isEmpty() && !selection_rect.isEmpty()) {
    PixelBuffer mask_pixels(selection_rect.width(), selection_rect.height(), PixelFormat::gray8());
    mask_pixels.clear(0);
    for (int y = 0; y < selection_rect.height(); ++y) {
      for (int x = 0; x < selection_rect.width(); ++x) {
        const QPoint document_point(selection_rect.x() + x, selection_rect.y() + y);
        *mask_pixels.pixel(x, y) = canvas_->selection_alpha_at(document_point);
      }
    }
    layer.set_mask(LayerMask{to_core_rect(selection_rect), std::move(mask_pixels), 0, false});
  }
  return layer;
}

void MainWindow::update_adjustment_layer_preview(QString label, const AdjustmentSettings& settings, bool enabled,
                                                 std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (canvas_ == nullptr || !enabled || !adjustment_has_effect(settings)) {
    remove_adjustment_layer_preview(preview_id, restore_active_layer);
    return;
  }

  auto& doc = document();
  if (preview_id.has_value()) {
    if (auto* layer = doc.find_layer(*preview_id); layer != nullptr) {
      layer->set_name(label.toStdString());
      layer->set_bounds(Rect::from_size(doc.width(), doc.height()));
      configure_adjustment_layer(*layer, settings);
      canvas_->document_changed();
      return;
    }
    preview_id.reset();
  }

  auto preview = build_adjustment_layer(label, settings);
  preview_id = preview.id();
  doc.add_layer(std::move(preview));
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  canvas_->document_changed();
}

void MainWindow::remove_adjustment_layer_preview(std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (!preview_id.has_value()) {
    return;
  }

  auto& doc = document();
  const auto removed = doc.remove_layer(*preview_id);
  preview_id.reset();
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  if (removed && canvas_ != nullptr) {
    canvas_->document_changed();
  }
}

void MainWindow::create_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  if (canvas_ == nullptr) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("%1 adjustment layer").arg(label));
  auto layer = build_adjustment_layer(label, settings);

  doc.add_layer(std::move(layer));
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Added %1 adjustment layer").arg(label));
}

void MainWindow::edit_active_adjustment_layer() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Adjustment) {
    statusBar()->showMessage(tr("Select an adjustment layer to edit its settings"));
    return;
  }

  const auto original_settings = adjustment_settings_from_layer(*layer);
  if (!original_settings.has_value()) {
    statusBar()->showMessage(tr("This adjustment layer has no editable settings"));
    return;
  }

  const auto layer_id = layer->id();
  auto apply_settings = [this, &doc, layer_id](const AdjustmentSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    configure_adjustment_layer(*target, settings);
    if (canvas_ != nullptr) {
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };

  std::optional<AdjustmentSettings> accepted_settings;
  switch (original_settings->kind) {
    case AdjustmentKind::Levels: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled, const LevelsSettings& levels) {
        auto settings = *original_settings;
        settings.levels = LevelsAdjustment{std::clamp(levels.black_input, 0, 254),
                                           std::clamp(levels.white_input,
                                                      std::clamp(levels.black_input, 0, 254) + 1, 255),
                                           std::clamp(levels.gamma_percent, 10, 999)};
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_levels_settings(this, preview_changed,
                                                  LevelsSettings{original_settings->levels.black_input,
                                                                 original_settings->levels.white_input,
                                                                 original_settings->levels.gamma_percent});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->levels =
            LevelsAdjustment{std::clamp(result->black_input, 0, 254),
                             std::clamp(result->white_input, std::clamp(result->black_input, 0, 254) + 1, 255),
                             std::clamp(result->gamma_percent, 10, 999)};
      }
      break;
    }
    case AdjustmentKind::Curves: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled, const CurvesSettings& curves) {
        auto settings = *original_settings;
        settings.curves = CurvesAdjustment{std::clamp(curves.shadow_output, 0, 255),
                                           std::clamp(curves.midtone_output, 0, 255),
                                           std::clamp(curves.highlight_output, 0, 255)};
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_curves_settings(this, preview_changed,
                                                  CurvesSettings{original_settings->curves.shadow_output,
                                                                 original_settings->curves.midtone_output,
                                                                 original_settings->curves.highlight_output});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->curves =
            CurvesAdjustment{std::clamp(result->shadow_output, 0, 255),
                             std::clamp(result->midtone_output, 0, 255),
                             std::clamp(result->highlight_output, 0, 255)};
      }
      break;
    }
    case AdjustmentKind::HueSaturation: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled,
                                                                       const HueSaturationSettings& hue_saturation) {
        auto settings = *original_settings;
        settings.hue_saturation =
            HueSaturationAdjustment{std::clamp(hue_saturation.hue_shift, -180, 180),
                                    std::clamp(hue_saturation.saturation_delta, -100, 100),
                                    std::clamp(hue_saturation.lightness_delta, -100, 100)};
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_hue_saturation_settings(
          this, preview_changed,
          HueSaturationSettings{original_settings->hue_saturation.hue_shift,
                                original_settings->hue_saturation.saturation_delta,
                                original_settings->hue_saturation.lightness_delta});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->hue_saturation =
            HueSaturationAdjustment{std::clamp(result->hue_shift, -180, 180),
                                    std::clamp(result->saturation_delta, -100, 100),
                                    std::clamp(result->lightness_delta, -100, 100)};
      }
      break;
    }
    case AdjustmentKind::ColorBalance: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled,
                                                                       const ColorBalanceSettings& color_balance) {
        auto settings = *original_settings;
        settings.color_balance =
            ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                   std::clamp(color_balance.magenta_green, -100, 100),
                                   std::clamp(color_balance.yellow_blue, -100, 100)};
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_color_balance_settings(
          this, preview_changed,
          ColorBalanceSettings{original_settings->color_balance.cyan_red,
                               original_settings->color_balance.magenta_green,
                               original_settings->color_balance.yellow_blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->color_balance =
            ColorBalanceAdjustment{std::clamp(result->cyan_red, -100, 100),
                                   std::clamp(result->magenta_green, -100, 100),
                                   std::clamp(result->yellow_blue, -100, 100)};
      }
      break;
    }
  }

  apply_settings(*original_settings);
  if (!accepted_settings.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cancelled adjustment edit"));
    return;
  }

  push_undo_snapshot(tr("Edit %1 adjustment")
                         .arg(QString::fromStdString(adjustment_display_name(original_settings->kind))));
  apply_settings(*accepted_settings);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Updated adjustment layer"));
}

void MainWindow::add_layer() {
  auto& doc = document();
  const auto settings = request_new_layer_settings(this, static_cast<int>(doc.layers().size()) + 1);
  if (!settings.has_value()) {
    return;
  }

  push_undo_snapshot(tr("New layer"));
  auto layer_pixels =
      make_solid_pixels(doc.width(), doc.height(), QColor(0, 0, 0, 0), PixelFormat::rgba8());
  auto& layer = doc.add_pixel_layer(settings->name.toStdString(), std::move(layer_pixels));
  layer.set_opacity(static_cast<float>(settings->opacity) / 100.0F);
  layer.set_blend_mode(settings->blend_mode);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::create_layer_folder() {
  auto& doc = document();
  std::set<std::string> existing_names;
  std::function<void(const std::vector<Layer>&)> collect_names = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      existing_names.insert(layer.name());
      collect_names(layer.children());
    }
  };
  collect_names(doc.layers());

  int suffix = 1;
  std::string name;
  do {
    name = tr("Folder %1").arg(suffix++).toStdString();
  } while (existing_names.contains(name));

  push_undo_snapshot(tr("New folder"));
  Layer folder(doc.allocate_layer_id(), name, LayerKind::Group);
  folder.set_blend_mode(BlendMode::PassThrough);
  doc.add_layer(std::move(folder));
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created folder"));
}

void MainWindow::layer_via_copy() {
  const auto ids = selected_or_active_layer_ids();
  const auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to copy to a new layer"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via copy"));
  Layer copied(doc.allocate_layer_id(), tr("Layer Via Copy").toStdString(), payload->pixels);
  copied.set_bounds(Rect{payload->origin.x(), payload->origin.y(), copied.pixels().width(), copied.pixels().height()});
  doc.add_layer(std::move(copied));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(payload->document_rect));
  statusBar()->showMessage(tr("Copied selection to a new layer"));
}

void MainWindow::layer_via_cut() {
  const auto ids = selected_or_active_layer_ids();
  auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to cut to a new layer"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via cut"));
  auto options = edit_options(*canvas_);
  Rect affected = payload->document_rect;
  const auto selected_rect = canvas_->selected_document_rect();
  for (const auto id : payload->source_layer_ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto clear_area = selected_rect.has_value() ? to_core_rect(*selected_rect) : layer->bounds();
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, clear_area, options));
  }

  Layer cut_layer(doc.allocate_layer_id(), tr("Layer Via Cut").toStdString(), std::move(payload->pixels));
  cut_layer.set_bounds(Rect{payload->origin.x(), payload->origin.y(), cut_layer.pixels().width(), cut_layer.pixels().height()});
  doc.add_layer(std::move(cut_layer));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(tr("Cut selection to a new layer"));
}

void MainWindow::add_layer_mask_from_selection() {
  if (canvas_ == nullptr) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    statusBar()->showMessage(tr("Select a pixel or adjustment layer before adding a mask"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || (layer->kind() != LayerKind::Pixel && layer->kind() != LayerKind::Adjustment)) {
    statusBar()->showMessage(tr("Select a pixel or adjustment layer before adding a mask"));
    return;
  }

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  if (selection.isEmpty() || selection_rect.isEmpty()) {
    statusBar()->showMessage(tr("Make a selection before adding a layer mask"));
    return;
  }

  PixelBuffer mask_pixels(selection_rect.width(), selection_rect.height(), PixelFormat::gray8());
  mask_pixels.clear(0);
  for (int y = 0; y < selection_rect.height(); ++y) {
    for (int x = 0; x < selection_rect.width(); ++x) {
      const QPoint document_point(selection_rect.x() + x, selection_rect.y() + y);
      *mask_pixels.pixel(x, y) = canvas_->selection_alpha_at(document_point);
    }
  }

  push_undo_snapshot(tr("Add layer mask"));
  const auto before = layer_render_bounds(*layer);
  layer->set_mask(LayerMask{to_core_rect(selection_rect), std::move(mask_pixels), 0, false});
  const auto after = layer_render_bounds(*layer);
  canvas_->document_changed(to_qrect(unite_rect(before, after)));
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Added layer mask from selection"));
}

void MainWindow::delete_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || !layer->mask().has_value()) {
    statusBar()->showMessage(tr("Active layer has no mask"));
    return;
  }

  push_undo_snapshot(tr("Delete layer mask"));
  const auto affected = layer_render_bounds(*layer);
  layer->clear_mask();
  layer->metadata().erase(kLayerMetadataMaskLinked);
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Deleted layer mask"));
}

void MainWindow::set_active_layer_mask_linked(bool linked) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || !layer->mask().has_value()) {
    return;
  }
  if (layer_mask_linked(*layer) == linked) {
    refresh_layer_controls();
    return;
  }

  push_undo_snapshot(linked ? tr("Link layer mask") : tr("Unlink layer mask"));
  set_layer_mask_linked(*layer, linked);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(linked ? tr("Layer and mask linked") : tr("Layer and mask unlinked"));
}

void MainWindow::duplicate_active_layer() {
  auto ids = selected_or_active_layer_ids();
  ids = root_drop_layer_ids(document().layers(), ids);
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  std::set<std::string> existing_names;
  collect_layer_names(doc.layers(), existing_names);

  push_undo_snapshot(tr("Duplicate layer"));
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    const auto id = *it;
    const auto* source = doc.find_layer(id);
    if (source == nullptr) {
      continue;
    }

    auto duplicate = clone_layer_tree_with_document_ids(doc, *source);
    duplicate.set_name(next_duplicate_layer_name(source->name(), existing_names));
    existing_names.insert(duplicate.name());
    doc.add_layer(std::move(duplicate));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::rename_active_layer() {
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*doc.active_layer_id());
  if (layer == nullptr) {
    return;
  }

  const auto new_name = request_text_input(this, QStringLiteral("photoslopRenameLayerDialog"), tr("Rename Layer"),
                                           tr("Name"), QString::fromStdString(layer->name()));
  if (!new_name.has_value() || new_name->trimmed().isEmpty()) {
    return;
  }

  push_undo_snapshot(tr("Rename layer"));
  layer->set_name(new_name->trimmed().toStdString());
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::edit_active_layer_style() {
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  const auto layer_id = *doc.active_layer_id();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return;
  }

  const auto original_opacity = layer->opacity();
  const auto original_blend_mode = layer->blend_mode();
  const auto original_style = layer->layer_style();
  auto apply_settings = [this, &doc, layer_id](const LayerStyleSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    target->set_opacity(static_cast<float>(settings.opacity) / 100.0F);
    target->set_blend_mode(settings.blend_mode);
    target->layer_style() = settings.style;
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };
  auto restore_original = [this, &doc, layer_id, original_opacity, original_blend_mode, original_style] {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    target->set_opacity(original_opacity);
    target->set_blend_mode(original_blend_mode);
    target->layer_style() = original_style;
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };

  const auto settings = request_layer_style_settings(this, *layer, apply_settings);
  if (!settings.has_value()) {
    restore_original();
    refresh_layer_list();
    refresh_layer_controls();
    return;
  }

  restore_original();
  push_undo_snapshot(tr("Layer style"));
  apply_settings(*settings);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated layer style"));
}

void MainWindow::delete_active_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Delete layer"));
  for (const auto id : ids) {
    doc.remove_layer(id);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::move_active_layer(int direction) {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty() || direction == 0) {
    return;
  }

  auto& layers = document().layers();
  const std::set<LayerId> selected(ids.begin(), ids.end());
  push_undo_snapshot(tr("Move layer"));
  if (direction > 0) {
    for (int index = static_cast<int>(layers.size()) - 2; index >= 0; --index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index + 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index + 1);
      }
    }
  } else {
    for (int index = 1; index < static_cast<int>(layers.size()); ++index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index - 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index - 1);
      }
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::handle_layer_drop() {
  auto* list = dynamic_cast<LayerListWidget*>(layer_list_);
  if (list == nullptr) {
    reorder_layers_from_list();
    return;
  }

  auto request = list->take_drop_request();
  if (!request.has_value()) {
    reorder_layers_from_list();
    return;
  }

  auto& doc = document();
  auto trial_layers = doc.layers();
  const auto before_signature = layer_tree_signature(doc.layers());
  if (!move_layers_for_drop(trial_layers, *request) || layer_tree_signature(trial_layers) == before_signature) {
    refresh_layer_list();
    return;
  }

  push_undo_snapshot(tr("Reorder layers"));
  doc.layers() = std::move(trial_layers);
  if (request->position == LayerDropPosition::OnItem && request->target_layer_id.has_value()) {
    if (const auto* target = doc.find_layer(*request->target_layer_id);
        target != nullptr && target->kind() == LayerKind::Group) {
      session().collapsed_layer_groups.erase(*request->target_layer_id);
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Reordered layers"));
}

void MainWindow::reorder_layers_from_list() {
  if (updating_layer_list_ || layer_list_ == nullptr) {
    return;
  }

  auto& layers = document().layers();
  if (layer_list_->count() != static_cast<int>(layers.size())) {
    refresh_layer_list();
    return;
  }

  std::vector<LayerId> top_to_bottom;
  top_to_bottom.reserve(static_cast<std::size_t>(layer_list_->count()));
  std::set<LayerId> seen_ids;
  for (int row = 0; row < layer_list_->count(); ++row) {
    const auto id = static_cast<LayerId>(layer_list_->item(row)->data(kLayerIdRole).toULongLong());
    if (id == 0 || seen_ids.contains(id)) {
      refresh_layer_list();
      return;
    }
    seen_ids.insert(id);
    top_to_bottom.push_back(id);
  }

  std::vector<LayerId> current_top_to_bottom;
  current_top_to_bottom.reserve(layers.size());
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    current_top_to_bottom.push_back(it->id());
  }
  if (top_to_bottom == current_top_to_bottom) {
    return;
  }
  for (const auto id : top_to_bottom) {
    if (std::find_if(layers.begin(), layers.end(), [id](const Layer& layer) { return layer.id() == id; }) ==
        layers.end()) {
      refresh_layer_list();
      return;
    }
  }

  push_undo_snapshot(tr("Reorder layers"));
  auto old_layers = std::move(layers);
  std::vector<Layer> reordered;
  reordered.reserve(old_layers.size());
  for (auto id_it = top_to_bottom.rbegin(); id_it != top_to_bottom.rend(); ++id_it) {
    const auto found = std::find_if(old_layers.begin(), old_layers.end(), [id = *id_it](const Layer& layer) {
      return layer.id() == id;
    });
    if (found == old_layers.end()) {
      layers = std::move(old_layers);
      refresh_layer_list();
      return;
    }
    reordered.push_back(std::move(*found));
    old_layers.erase(found);
  }
  if (!old_layers.empty()) {
    layers = std::move(old_layers);
    refresh_layer_list();
    return;
  }
  layers = std::move(reordered);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Reordered layers"));
}

void MainWindow::toggle_layer_folder_expanded(LayerId id) {
  const auto* layer = document().find_layer(id);
  if (layer == nullptr || layer->kind() != LayerKind::Group || layer->children().empty()) {
    return;
  }

  auto& collapsed_groups = session().collapsed_layer_groups;
  const auto was_collapsed = collapsed_groups.contains(id);
  if (was_collapsed) {
    collapsed_groups.erase(id);
  } else {
    collapsed_groups.insert(id);
  }

  refresh_layer_list();
  statusBar()->showMessage(was_collapsed ? tr("Folder expanded") : tr("Folder collapsed"));
}

void MainWindow::reveal_layer_in_layer_list(LayerId id) {
  if (layer_list_ == nullptr) {
    return;
  }

  std::vector<LayerId> ancestors;
  if (!collect_layer_ancestor_groups(document().layers(), id, ancestors)) {
    return;
  }
  for (const auto ancestor_id : ancestors) {
    session().collapsed_layer_groups.erase(ancestor_id);
  }

  refresh_layer_list();
  for (int row = 0; row < layer_list_->count(); ++row) {
    auto* item = layer_list_->item(row);
    if (item == nullptr || static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()) != id) {
      continue;
    }
    layer_list_->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    layer_list_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    restyle_layer_rows(layer_list_);
    break;
  }
}

void MainWindow::set_layer_visibility_from_item(QListWidgetItem* item) {
  if (updating_layer_list_ || item == nullptr) {
    return;
  }
  const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  auto* layer = document().find_layer(id);
  if (layer == nullptr) {
    return;
  }

  const bool visible = item->checkState() == Qt::Checked;
  if (layer->visible() == visible) {
    return;
  }

  layer->set_visible(visible);
  const auto is_group = layer->kind() == LayerKind::Group;
  item->setForeground(visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
  if (auto* row_widget = layer_list_ != nullptr ? layer_list_->itemWidget(item) : nullptr; row_widget != nullptr) {
    if (auto* visibility = row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
        visibility != nullptr) {
      QSignalBlocker visibility_blocker(visibility);
      visibility->setChecked(visible);
      visibility->setText(layer_visibility_indicator_text(visible));
      visibility->setToolTip(layer_visibility_tooltip(visible));
    }
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      name->setEnabled(visible);
    }
    if (auto* details = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")); details != nullptr) {
      details->setEnabled(visible);
    }
  }
  canvas_->document_changed(to_qrect(layer_render_bounds(*layer)));
  refresh_layer_controls();
  if (is_group) {
    refresh_layer_list();
  } else {
    restyle_layer_rows(layer_list_);
  }
  statusBar()->showMessage(visible ? tr("Layer shown") : tr("Layer hidden"));
}

void MainWindow::show_layer_context_menu(QPoint position) {
  if (layer_list_ == nullptr) {
    return;
  }

  auto* item = layer_list_->itemAt(position);
  if (item != nullptr && !item->isSelected()) {
    layer_list_->clearSelection();
    layer_list_->setCurrentItem(item);
    item->setSelected(true);
  }

  const auto ids = selected_or_active_layer_ids();
  const auto has_layer = !ids.empty();
  const auto active_id = document().active_layer_id();
  auto* active_layer = active_id.has_value() ? document().find_layer(*active_id) : nullptr;

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("layerContextMenu"));
  QAction* edit_adjustment_action = nullptr;
  if (active_layer != nullptr && active_layer->kind() == LayerKind::Adjustment) {
    edit_adjustment_action = menu.addAction(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)),
                                            tr("Edit Adjustment..."));
    menu.addSeparator();
  }
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(active_layer != nullptr);
    menu.addAction(layer_blending_options_action_);
    menu.addSeparator();
  }
  auto* new_action = menu.addAction(simple_icon(QStringLiteral("new")), tr("New Layer"));
  auto* new_folder_action = menu.addAction(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)), tr("New Folder"));
  auto* new_adjustment_menu = menu.addMenu(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)),
                                           tr("New Adjustment Layer"));
  populate_new_adjustment_layer_menu(new_adjustment_menu);
  auto* duplicate_action = menu.addAction(simple_icon(QStringLiteral("dup")), tr("Duplicate Layer"));
  auto* rename_action = menu.addAction(simple_icon(QStringLiteral("RN")), tr("Rename Layer..."));
  auto* delete_action = menu.addAction(simple_icon(QStringLiteral("trash")), tr("Delete Layer"));
  menu.addSeparator();
  auto* merge_selected_action =
      menu.addAction(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)), tr("Merge Selected to New Layer"));
  auto* merge_visible_action = menu.addAction(simple_icon(QStringLiteral("merge")), tr("Merge Visible to New Layer"));
  menu.addSeparator();
  auto* visibility_action = menu.addAction(tr("Visible"));
  visibility_action->setCheckable(true);
  visibility_action->setChecked(active_layer == nullptr || active_layer->visible());
  auto* lock_action = menu.addAction(tr("Lock Transparent Pixels"));
  lock_action->setCheckable(true);
  lock_action->setChecked(active_layer != nullptr && layer_locks_transparent_pixels(*active_layer));
  auto* select_opaque_action = menu.addAction(tr("Load Layer Transparency"));
  auto* add_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)),
                                         tr("Add Layer Mask from Selection"));
  auto* delete_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)),
                                            tr("Delete Layer Mask"));
  auto* link_mask_action = menu.addAction(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)),
                                          tr("Link Layer Mask"));
  link_mask_action->setCheckable(true);
  link_mask_action->setChecked(active_layer == nullptr || layer_mask_linked(*active_layer));

  duplicate_action->setEnabled(has_layer);
  rename_action->setEnabled(active_layer != nullptr);
  delete_action->setEnabled(has_layer);
  merge_selected_action->setEnabled(has_layer);
  visibility_action->setEnabled(has_layer);
  lock_action->setEnabled(has_layer);
  select_opaque_action->setEnabled(active_layer != nullptr && canvas_ != nullptr);
  add_mask_action->setEnabled(active_layer != nullptr && active_layer->kind() == LayerKind::Pixel &&
                              canvas_ != nullptr && canvas_->has_selection());
  delete_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  link_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());

  hide_menu_action_icons(&menu);
  auto* chosen = menu.exec(layer_list_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) {
    return;
  }
  if (chosen == layer_blending_options_action_) {
    return;
  }
  if (chosen == edit_adjustment_action) {
    edit_active_adjustment_layer();
  } else if (chosen == new_action) {
    add_layer();
  } else if (chosen == new_folder_action) {
    create_layer_folder();
  } else if (chosen == duplicate_action) {
    duplicate_active_layer();
  } else if (chosen == rename_action) {
    rename_active_layer();
  } else if (chosen == delete_action) {
    delete_active_layer();
  } else if (chosen == merge_selected_action) {
    merge_selected_to_new_layer();
  } else if (chosen == merge_visible_action) {
    merge_visible_to_new_layer();
  } else if (chosen == visibility_action) {
    set_active_layer_visible(visibility_action->isChecked());
  } else if (chosen == lock_action) {
    set_active_layer_lock_transparency(lock_action->isChecked());
  } else if (chosen == select_opaque_action && active_layer != nullptr && canvas_ != nullptr) {
    canvas_->select_layer_opaque_pixels(active_layer->id());
  } else if (chosen == add_mask_action) {
    add_layer_mask_from_selection();
  } else if (chosen == delete_mask_action) {
    delete_active_layer_mask();
  } else if (chosen == link_mask_action) {
    set_active_layer_mask_linked(link_mask_action->isChecked());
  }
}

void MainWindow::merge_visible_to_new_layer() {
  auto& doc = document();
  push_undo_snapshot(tr("Merge visible"));
  auto merged = Compositor{}.flatten_rgb8(doc);
  doc.add_pixel_layer(tr("Merged Visible").toStdString(), std::move(merged));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged visible layers to a new layer"));
}

void MainWindow::merge_selected_to_new_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  Document selected_document(doc.width(), doc.height(), doc.format());
  for (const auto& layer : doc.layers()) {
    if (std::find(ids.begin(), ids.end(), layer.id()) != ids.end()) {
      selected_document.add_layer(layer);
    }
  }
  if (selected_document.layers().empty()) {
    return;
  }

  push_undo_snapshot(tr("Merge selected"));
  auto merged = Compositor{}.flatten_rgb8(selected_document);
  doc.add_pixel_layer(tr("Merged Selected").toStdString(), std::move(merged));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged selected layers to a new layer"));
}

void MainWindow::fill_active_layer() {
  fill_active_layer_with_color(canvas_->primary_color(), tr("Fill"));
}

void MainWindow::fill_active_layer_with_color(QColor color, QString label) {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(label);
  auto options = edit_options(*canvas_);
  options.primary = edit_color(color);
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto target = canvas_->has_selection() && canvas_->selected_document_rect().has_value()
                            ? to_core_rect(*canvas_->selected_document_rect())
                            : layer->bounds();
    affected = unite_rect(affected, photoslop::fill_rect(doc, id, target, options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::clear_active_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Clear"));
  Rect affected;
  auto options = edit_options(*canvas_);
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::stroke_selection() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto selection = canvas_->selected_document_region();
  if (selection.isEmpty()) {
    statusBar()->showMessage(tr("Make a selection before stroking"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    statusBar()->showMessage(tr("Select an editable pixel layer first"));
    return;
  }

  push_undo_snapshot(tr("Stroke selection"));
  auto options = edit_options(*canvas_);
  options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
  const QRect canvas_rect(0, 0, doc.width(), doc.height());
  const auto stroke_region = selection_outline_region(selection, canvas_->brush_size(), canvas_rect);
  if (stroke_region.isEmpty()) {
    return;
  }
  options.selection = to_core_rect(stroke_region.boundingRect());
  options.selection_mask = [stroke_region](std::int32_t x, std::int32_t y) { return stroke_region.contains(QPoint(x, y)); };
  const auto affected = photoslop::fill_rect(doc, *active, to_core_rect(stroke_region.boundingRect()), options);
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  statusBar()->showMessage(tr("Stroked selection"));
}

void MainWindow::expand_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before expanding"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("photoslopExpandSelectionDialog"),
                                            tr("Expand Selection"), tr("Expand by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->expand_selection(*pixels);
  }
}

void MainWindow::contract_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before contracting"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("photoslopContractSelectionDialog"),
                                            tr("Contract Selection"), tr("Contract by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->contract_selection(*pixels);
  }
}

void MainWindow::border_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before selecting a border"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("photoslopBorderSelectionDialog"),
                                            tr("Border Selection"), tr("Width"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->border_selection(*pixels);
  }
}

void MainWindow::flip_active_layer_horizontal() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip horizontal"));
  Rect affected;
  for (const auto id : ids) {
    affected = unite_rect(affected, photoslop::flip_layer_horizontal(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::flip_active_layer_vertical() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip vertical"));
  Rect affected;
  for (const auto id : ids) {
    affected = unite_rect(affected, photoslop::flip_layer_vertical(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::crop_to_selection() {
  const auto selection = canvas_->selected_document_rect();
  if (!selection.has_value() || selection->isEmpty()) {
    statusBar()->showMessage(tr("Make a rectangular selection before cropping"));
    return;
  }

  push_undo_snapshot(tr("Crop"));
  auto& doc = document();
  if (!photoslop::crop_document(doc, to_core_rect(*selection))) {
    return;
  }
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Cropped to selection"));
}

void MainWindow::rotate_canvas_clockwise() {
  auto& doc = document();
  push_undo_snapshot(tr("Rotate canvas"));
  photoslop::rotate_document_clockwise(doc);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas clockwise"));
}

void MainWindow::rotate_canvas_counterclockwise() {
  auto& doc = document();
  push_undo_snapshot(tr("Rotate canvas"));
  photoslop::rotate_document_counterclockwise(doc);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas counterclockwise"));
}

std::vector<LayerId> MainWindow::selected_layer_ids() const {
  std::vector<LayerId> ids;
  if (layer_list_ == nullptr) {
    return ids;
  }
  ids.reserve(static_cast<std::size_t>(layer_list_->selectedItems().size()));
  for (int row = 0; row < layer_list_->count(); ++row) {
    const auto* item = layer_list_->item(row);
    if (item != nullptr && item->isSelected()) {
      ids.push_back(static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()));
    }
  }
  return ids;
}

std::vector<LayerId> MainWindow::selected_or_active_layer_ids() const {
  auto ids = selected_layer_ids();
  const auto active = document().active_layer_id();
  if (ids.empty() && active.has_value()) {
    ids.push_back(*active);
  }
  return ids;
}

void MainWindow::set_active_layer_from_selection() {
  if (updating_layer_controls_) {
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->set_selected_layer_ids(selected_layer_ids());
  }
  if (layer_list_->currentItem() == nullptr) {
    return;
  }

  const auto id = static_cast<LayerId>(layer_list_->currentItem()->data(kLayerIdRole).toULongLong());
  auto& doc = document();
  if (doc.find_layer(id) != nullptr) {
    doc.set_active_layer(id);
    refresh_layer_controls();
    restyle_layer_rows(layer_list_);
  }
}

void MainWindow::set_active_layer_opacity(int value) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Opacity"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_opacity(static_cast<float>(value) / 100.0F);
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::set_active_layer_blend(int index) {
  if (updating_layer_controls_ || index < 0) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Blend mode"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_blend_mode(static_cast<BlendMode>(blend_combo_->itemData(index).toInt()));
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::set_active_layer_visible(bool visible) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Visibility"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_visible(visible);
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::set_active_layer_lock_transparency(bool locked) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Lock transparency"));
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    set_layer_locks_transparent_pixels(*layer, locked);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(locked ? tr("Transparent pixels locked") : tr("Transparent pixels unlocked"));
}

void MainWindow::undo() {
  auto& active_session = session();
  if (active_session.undo_stack.empty()) {
    return;
  }
  active_session.redo_stack.push_back(DocumentSession::HistoryState{active_session.document, active_session.revision});
  active_session.document = active_session.undo_stack.back().document;
  active_session.revision = active_session.undo_stack.back().revision;
  active_session.undo_stack.pop_back();
  canvas_->set_document(&active_session.document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Undo"));
  update_history(tr("Undo"));
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::redo() {
  auto& active_session = session();
  if (active_session.redo_stack.empty()) {
    return;
  }
  active_session.undo_stack.push_back(DocumentSession::HistoryState{active_session.document, active_session.revision});
  active_session.document = active_session.redo_stack.back().document;
  active_session.revision = active_session.redo_stack.back().revision;
  active_session.redo_stack.pop_back();
  canvas_->set_document(&active_session.document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Redo"));
  update_history(tr("Redo"));
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::push_undo_snapshot(QString label) {
  constexpr std::size_t kMaxUndo = 40;
  auto& active_session = session();
  active_session.undo_stack.push_back(DocumentSession::HistoryState{active_session.document, active_session.revision});
  if (active_session.undo_stack.size() > kMaxUndo) {
    active_session.undo_stack.erase(active_session.undo_stack.begin());
  }
  active_session.redo_stack.clear();
  mark_session_modified(active_session);
  update_history(label);
  update_undo_redo_actions();
  statusBar()->showMessage(label);
}

void MainWindow::refresh_layer_list() {
  if (layer_list_ == nullptr) {
    return;
  }
  const auto scroll_value = layer_list_->verticalScrollBar() != nullptr ? layer_list_->verticalScrollBar()->value() : 0;
  updating_layer_list_ = true;
  QSignalBlocker blocker(layer_list_);
  layer_list_->clear();

  const auto& doc = document();
  auto& collapsed_groups = session().collapsed_layer_groups;
  std::set<LayerId> current_group_ids;
  collect_layer_group_ids(doc.layers(), current_group_ids);
  for (auto collapsed = collapsed_groups.begin(); collapsed != collapsed_groups.end();) {
    if (!current_group_ids.contains(*collapsed)) {
      collapsed = collapsed_groups.erase(collapsed);
    } else {
      ++collapsed;
    }
  }

  const auto active = doc.active_layer_id();
  int row_to_select = -1;
  std::function<void(const std::vector<Layer>&, int, bool)> append_layers =
      [&](const std::vector<Layer>& layers, int depth, bool ancestors_visible) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto effective_visible = ancestors_visible && it->visible();
      const auto is_group = it->kind() == LayerKind::Group;
      const auto group_expanded = !is_group || !collapsed_groups.contains(it->id());
      auto* item = new QListWidgetItem(QString::fromStdString(it->name()), layer_list_);
      item->setData(kLayerIdRole, QVariant::fromValue<qulonglong>(static_cast<qulonglong>(it->id())));
      item->setData(kLayerDepthRole, depth);
      item->setData(kLayerIsGroupRole, is_group);
      item->setData(kLayerGroupExpandedRole, group_expanded);
      item->setFlags((item->flags() & ~Qt::ItemIsUserCheckable) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      item->setCheckState(it->visible() ? Qt::Checked : Qt::Unchecked);
      const auto folder_detail =
          is_group ? tr("\nFolder with %1 layers%2")
                         .arg(layer_descendant_count(*it))
                         .arg(group_expanded ? QString() : tr("\nCollapsed"))
                   : QString();
      item->setToolTip(tr("%1\n%2% opacity%3%4%5")
                           .arg(QString::fromStdString(it->name()))
                           .arg(std::round(it->opacity() * 100.0F))
                           .arg(!ancestors_visible
                                    ? tr("\nHidden by parent folder")
                                    : layer_locks_transparent_pixels(*it) ? tr("\nTransparent pixels locked") : QString())
                           .arg(it->mask().has_value() ? tr("\nLayer mask") : QString())
                           .arg(folder_detail));
      item->setSizeHint(QSize(0, 50));
      item->setForeground(effective_visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
      if (active.has_value() && *active == it->id()) {
        auto font = item->font();
        font.setBold(true);
        item->setFont(font);
        row_to_select = layer_list_->row(item);
      }
      layer_list_->setItemWidget(
          item, make_layer_row_widget(*it, item, layer_list_, depth, ancestors_visible, group_expanded,
                                      [this](LayerId layer_id) { toggle_layer_folder_expanded(layer_id); },
                                      [this](LayerId layer_id, bool linked) {
        if (auto* layer = document().find_layer(layer_id); layer != nullptr && layer->mask().has_value()) {
          document().set_active_layer(layer_id);
          set_active_layer_mask_linked(linked);
        }
      }));
      if (is_group && group_expanded) {
        append_layers(it->children(), depth + 1, effective_visible);
      }
    }
  };
  append_layers(doc.layers(), 0, true);

  if (row_to_select >= 0) {
    layer_list_->setCurrentRow(row_to_select);
  }
  updating_layer_list_ = false;
  if (canvas_ != nullptr) {
    canvas_->set_selected_layer_ids(selected_layer_ids());
  }
  restyle_layer_rows(layer_list_);
  if (auto* scroll_bar = layer_list_->verticalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  layer_list_->viewport()->update();
  layer_list_->viewport()->repaint();
}

void MainWindow::refresh_layer_thumbnails() {
  if (layer_list_ == nullptr) {
    return;
  }
  const auto& doc = document();
  for (int row_index = 0; row_index < layer_list_->count(); ++row_index) {
    auto* item = layer_list_->item(row_index);
    if (item == nullptr) {
      continue;
    }
    const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    const auto* layer = doc.find_layer(layer_id);
    auto* row = layer_list_->itemWidget(item);
    if (layer == nullptr || row == nullptr) {
      continue;
    }
    if (auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")); thumbnail != nullptr &&
        layer->kind() == LayerKind::Pixel) {
      thumbnail->setPixmap(layer_content_thumbnail(*layer));
    }
    if (auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
        mask_thumbnail != nullptr && layer->mask().has_value()) {
      mask_thumbnail->setPixmap(layer_mask_thumbnail(*layer->mask()));
    }
  }
}

void MainWindow::refresh_layer_controls() {
  updating_layer_controls_ = true;
  const auto reset = [this] {
    if (visible_check_ != nullptr) {
      visible_check_->setChecked(true);
    }
    if (opacity_slider_ != nullptr) {
      opacity_slider_->setValue(100);
    }
    if (opacity_spin_ != nullptr) {
      opacity_spin_->setValue(100);
    }
    if (blend_combo_ != nullptr) {
      blend_combo_->setCurrentIndex(0);
    }
    if (lock_transparency_check_ != nullptr) {
      lock_transparency_check_->setChecked(false);
    }
    if (layer_blending_options_action_ != nullptr) {
      layer_blending_options_action_->setEnabled(false);
    }
    if (delete_layer_mask_action_ != nullptr) {
      delete_layer_mask_action_->setEnabled(false);
    }
    if (link_layer_mask_action_ != nullptr) {
      link_layer_mask_action_->setEnabled(false);
      link_layer_mask_action_->setChecked(true);
    }
  };

  const auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    reset();
    updating_layer_controls_ = false;
    return;
  }

  const auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    reset();
    updating_layer_controls_ = false;
    return;
  }

  if (opacity_slider_ != nullptr) {
    opacity_slider_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  if (opacity_spin_ != nullptr) {
    opacity_spin_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  if (visible_check_ != nullptr) {
    visible_check_->setChecked(layer->visible());
  }
  if (blend_combo_ != nullptr) {
    const auto blend_value = static_cast<int>(layer->blend_mode());
    const auto index = blend_combo_->findData(blend_value);
    blend_combo_->setCurrentIndex(index >= 0 ? index : 0);
  }
  if (lock_transparency_check_ != nullptr) {
    lock_transparency_check_->setChecked(layer_locks_transparent_pixels(*layer));
  }
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(true);
  }
  if (delete_layer_mask_action_ != nullptr) {
    delete_layer_mask_action_->setEnabled(layer->mask().has_value());
  }
  if (link_layer_mask_action_ != nullptr) {
    link_layer_mask_action_->setEnabled(layer->mask().has_value());
    link_layer_mask_action_->setChecked(layer_mask_linked(*layer));
  }
  updating_layer_controls_ = false;
}

void MainWindow::refresh_document_info() {
  if (document_info_label_ == nullptr) {
    return;
  }

  const auto& doc = document();
  const auto& active_session = session();
  document_info_label_->setText(tr("%1 x %2 px\n%3 layers\nZoom %4%\n%5")
                                    .arg(doc.width())
                                    .arg(doc.height())
                                    .arg(layer_tree_count(doc.layers()))
                                    .arg(static_cast<int>(std::round(canvas_->zoom() * 100.0)))
                                    .arg(session_is_modified(active_session) ? tr("Unsaved changes") : tr("Saved")));
}

void MainWindow::update_canvas_info(CanvasInfoState info) {
  if (canvas_info_label_ == nullptr) {
    return;
  }

  if (!info.inside_document) {
    canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
    return;
  }

  const auto color = info.color;
  const auto color_line = tr("RGB: %1, %2, %3  #%4%5%6")
                              .arg(color.red())
                              .arg(color.green())
                              .arg(color.blue())
                              .arg(color.red(), 2, 16, QLatin1Char('0'))
                              .arg(color.green(), 2, 16, QLatin1Char('0'))
                              .arg(color.blue(), 2, 16, QLatin1Char('0'))
                              .toUpper();
  QString rect_line = tr("Rect: -");
  if (info.active_rect.has_value() && !info.active_rect->isEmpty()) {
    const auto rect = info.active_rect->normalized();
    rect_line = tr("%1: %2 x %3  at %4, %5")
                    .arg(info.active_rect_label.isEmpty() ? tr("Rect") : info.active_rect_label)
                    .arg(rect.width())
                    .arg(rect.height())
                    .arg(rect.x())
                    .arg(rect.y());
  }

  canvas_info_label_->setText(tr("X: %1\nY: %2\n%3\n%4")
                                  .arg(info.document_point.x())
                                  .arg(info.document_point.y())
                                  .arg(color_line)
                                  .arg(rect_line));
}

void MainWindow::choose_primary_color() {
  show_color_panel(true);
}

void MainWindow::choose_secondary_color() {
  show_color_panel(false);
}

void MainWindow::show_color_panel(bool foreground) {
  if (canvas_ == nullptr) {
    return;
  }
  if (color_dialog_ != nullptr) {
    if (color_dialog_->property("photoslop.colorTarget").toBool() == foreground) {
      color_dialog_->show();
      color_dialog_->raise();
      color_dialog_->activateWindow();
      return;
    }
    color_dialog_->close();
  }

  auto* dialog = create_photoslop_color_panel(
      this, foreground ? canvas_->primary_color() : canvas_->secondary_color(),
      foreground ? tr("Foreground Color") : tr("Background Color"),
      [this, foreground](QColor color) {
        color.setAlpha(255);
        if (foreground) {
          canvas_->set_primary_color(color);
          statusBar()->showMessage(tr("Foreground color changed"));
        } else {
          canvas_->set_secondary_color(color);
          statusBar()->showMessage(tr("Background color changed"));
        }
        refresh_color_buttons();
      });
  dialog->setProperty("photoslop.colorTarget", foreground);
  color_dialog_ = dialog;
  connect(dialog, &QObject::destroyed, this, [this, dialog] {
    if (color_dialog_ == dialog) {
      color_dialog_ = nullptr;
    }
  });
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::swap_colors() {
  const auto primary = canvas_->primary_color();
  canvas_->set_primary_color(canvas_->secondary_color());
  canvas_->set_secondary_color(primary);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Swapped foreground/background"));
}

void MainWindow::default_colors() {
  canvas_->set_primary_color(Qt::black);
  canvas_->set_secondary_color(Qt::white);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Default colors"));
}

void MainWindow::refresh_color_buttons() {
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setText(tr("FG"));
    primary_color_button_->setToolTip(tr("Foreground color %1").arg(canvas_->primary_color().name(QColor::HexRgb).toUpper()));
    primary_color_button_->setStyleSheet(color_button_style(canvas_->primary_color()));
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setText(tr("BG"));
    secondary_color_button_->setToolTip(tr("Background color %1").arg(canvas_->secondary_color().name(QColor::HexRgb).toUpper()));
    secondary_color_button_->setStyleSheet(color_button_style(canvas_->secondary_color()));
  }
}

void MainWindow::load_tool_settings() {
  if (canvas_ == nullptr) {
    return;
  }
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  canvas_->set_brush_size(settings.value(QStringLiteral("tools/brushSize"), canvas_->brush_size()).toInt());
  canvas_->set_brush_opacity(settings.value(QStringLiteral("tools/brushOpacity"), canvas_->brush_opacity()).toInt());
  canvas_->set_brush_softness(settings.value(QStringLiteral("tools/brushSoftness"), canvas_->brush_softness()).toInt());
  if (settings.contains(QStringLiteral("tools/brushBuildUp"))) {
    canvas_->set_brush_build_up(settings.value(QStringLiteral("tools/brushBuildUp"), canvas_->brush_build_up()).toBool());
  } else if (const auto* preset =
                 find_brush_preset(settings.value(QStringLiteral("tools/brushPreset"), QString()).toString());
             preset != nullptr) {
    canvas_->set_brush_build_up(preset->build_up);
  }
  canvas_->set_wand_tolerance(settings.value(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance()).toInt());
  canvas_->set_clone_aligned(settings.value(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned()).toBool());
}

void MainWindow::save_tool_settings() const {
  if (canvas_ == nullptr) {
    return;
  }
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  settings.setValue(QStringLiteral("tools/brushSize"), canvas_->brush_size());
  settings.setValue(QStringLiteral("tools/brushOpacity"), canvas_->brush_opacity());
  settings.setValue(QStringLiteral("tools/brushSoftness"), canvas_->brush_softness());
  settings.setValue(QStringLiteral("tools/brushBuildUp"), canvas_->brush_build_up());
  settings.setValue(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance());
  settings.setValue(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned());
  if (brush_preset_combo_ != nullptr && brush_preset_combo_->currentIndex() >= 0) {
    settings.setValue(QStringLiteral("tools/brushPreset"), brush_preset_combo_->currentData().toString());
  }
}

void MainWindow::apply_text_options_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_color = editor->property("photoslop.documentTextColor").value<QColor>().isValid()
                              ? editor->property("photoslop.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto document_text_size =
      text_size_spin_ != nullptr ? text_size_spin_->value()
                                 : std::max(1, editor->property("photoslop.documentTextSize").toInt());
  const auto family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : editor->font().family();

  QFont editor_font(family);
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))));
  editor_font.setBold(text_bold_button_ != nullptr && text_bold_button_->isChecked());
  editor_font.setItalic(text_italic_button_ != nullptr && text_italic_button_->isChecked());

  editor->setProperty("photoslop.documentTextSize", document_text_size);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));

  const auto saved_cursor = editor->textCursor();
  QTextCursor document_cursor(editor->document());
  document_cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(editor_font);
  format.setForeground(QBrush(text_color));
  document_cursor.mergeCharFormat(format);
  editor->setTextCursor(saved_cursor);

  const auto document_editor_width = std::max(64, editor->property("photoslop.documentTextWidth").toInt());
  const auto minimum_width = std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom())));
  editor->document()->setTextWidth(-1);
  const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
  const auto width = std::max(minimum_width, content_width);
  editor->setProperty("photoslop.documentTextWidth",
                      std::max(document_editor_width,
                               static_cast<int>(std::ceil(static_cast<double>(width) / canvas_->zoom()))));
  editor->document()->setTextWidth(width);
  const auto text_height = std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
  const auto minimum_height =
      std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * canvas_->zoom() * 1.45)));
  editor->resize(width, std::max(text_height, minimum_height));
  editor->updateGeometry();
  editor->update();
}

bool MainWindow::is_text_option_widget(QWidget* widget) const {
  if (widget == nullptr) {
    return false;
  }
  const auto owns = [widget](const QWidget* candidate) {
    return candidate != nullptr && (widget == candidate || candidate->isAncestorOf(widget));
  };
  return owns(text_font_combo_) || owns(text_size_spin_) || owns(text_bold_button_) || owns(text_italic_button_);
}

void MainWindow::register_option_action(QAction* action, std::initializer_list<CanvasTool> tools) {
  if (action == nullptr) {
    return;
  }
  option_actions_.emplace_back(action, std::vector<CanvasTool>(tools.begin(), tools.end()));
}

void MainWindow::refresh_options_bar() {
  for (const auto& [action, tools] : option_actions_) {
    if (action == nullptr) {
      continue;
    }
    const auto visible = tools.empty() || std::find(tools.begin(), tools.end(), current_tool_) != tools.end();
    action->setVisible(visible);
  }

  if (move_auto_select_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(move_auto_select_check_);
    move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  }
  if (clone_aligned_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(clone_aligned_check_);
    clone_aligned_check_->setChecked(canvas_->clone_aligned());
  }
  if (canvas_ != nullptr) {
    current_selection_mode_ = canvas_->selection_mode();
  }
  const auto set_checked = [](QAction* action, bool checked) {
    if (action == nullptr) {
      return;
    }
    QSignalBlocker blocker(action);
    action->setChecked(checked);
  };
  set_checked(selection_new_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Replace);
  set_checked(selection_add_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Add);
  set_checked(selection_subtract_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Subtract);
  set_checked(selection_intersect_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Intersect);
}

void MainWindow::load_recent_files() {
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
  recent_files_.erase(std::remove_if(recent_files_.begin(), recent_files_.end(), [](const QString& path) {
                        return path.trimmed().isEmpty() || !QFileInfo::exists(path);
                      }),
                      recent_files_.end());
  while (recent_files_.size() > 10) {
    recent_files_.removeLast();
  }
}

void MainWindow::save_recent_files() const {
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  settings.setValue(QStringLiteral("recentFiles"), recent_files_);
}

void MainWindow::add_recent_file(QString path) {
  path = QFileInfo(path).absoluteFilePath();
  if (path.isEmpty()) {
    return;
  }
  recent_files_.removeAll(path);
  recent_files_.prepend(path);
  while (recent_files_.size() > 10) {
    recent_files_.removeLast();
  }
  save_recent_files();
  rebuild_recent_files_menu();
}

void MainWindow::rebuild_recent_files_menu() {
  if (recent_files_menu_ == nullptr) {
    return;
  }
  recent_files_menu_->clear();
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());
  int index = 1;
  for (const auto& path : recent_files_) {
    const auto label = tr("&%1 %2").arg(index++).arg(QFileInfo(path).fileName());
    auto* action = recent_files_menu_->addAction(label);
    action->setToolTip(path);
    action->setData(path);
    connect(action, &QAction::triggered, this, [this, path] { open_recent_document(path); });
  }
  if (!recent_files_.isEmpty()) {
    recent_files_menu_->addSeparator();
    auto* clear_action = recent_files_menu_->addAction(tr("Clear Recent Files"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      recent_files_.clear();
      save_recent_files();
      rebuild_recent_files_menu();
    });
  }
}

void MainWindow::open_recent_document(QString path) {
  if (!QFileInfo::exists(path)) {
    recent_files_.removeAll(path);
    save_recent_files();
    rebuild_recent_files_menu();
    statusBar()->showMessage(tr("Recent file is missing"));
    return;
  }
  open_document_path(path);
}

QAction* MainWindow::add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                                     QKeySequence shortcut) {
  auto* action = palette->addAction(label);
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  apply_action_shortcut(action, shortcut);
  group->addAction(action);
  return action;
}

void MainWindow::update_history(QString label) {
  if (history_list_ != nullptr) {
    history_list_->insertItem(0, label);
    history_list_->setCurrentRow(0);
  }
  refresh_document_info();
}

void MainWindow::update_undo_redo_actions() {
  const auto* active_session =
      document_tabs_ == nullptr ? nullptr : session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    if (undo_action_ != nullptr) {
      undo_action_->setEnabled(false);
    }
    if (redo_action_ != nullptr) {
      redo_action_->setEnabled(false);
    }
    return;
  }
  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(!active_session->undo_stack.empty());
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(!active_session->redo_stack.empty());
  }
}

void MainWindow::show_about() {
  QMessageBox::about(this, tr("About Photoslop"),
                     tr("Photoslop %1\nNative PSD-oriented pixel editor.").arg(PHOTOSLOP_VERSION));
}

}  // namespace photoslop::ui
