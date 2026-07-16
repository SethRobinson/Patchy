// MainWindow's layer-panel implementation, split out of main_window.cpp:
// layer-list row construction and thumbnails, panel/document-info refresh,
// and the layer-list drag/visibility plumbing. Pure function moves from
// main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
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
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

constexpr const char* kLayerContentThumbnailRevisionProperty = "patchyContentRevision";
constexpr const char* kLayerMaskThumbnailRevisionProperty = "patchyMaskRevision";

constexpr int kLayerRowBaseIndent = 6;
constexpr int kLayerFolderDisclosureWidth = 18;
constexpr int kLayerFolderDisclosureHeight = 20;
constexpr int kLayerRowHorizontalSpacing = 6;
constexpr int kLayerChildIndent = 22;

int layer_tree_indent_width(int depth) {
  return std::max(0, depth) * kLayerChildIndent;
}

class LayerTreeIndentWidget final : public QWidget {
public:
  explicit LayerTreeIndentWidget(int depth, QWidget* parent = nullptr)
      : QWidget(parent), depth_(std::max(0, depth)) {
    setFixedWidth(layer_tree_indent_width(depth_));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);
    if (depth_ <= 0) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(68, 74, 82, 125), 1));
    for (int level = 0; level < depth_; ++level) {
      const auto x = level * kLayerChildIndent + kLayerChildIndent / 2;
      painter.drawLine(QPoint(x, 4), QPoint(x, height() - 4));
    }
  }

private:
  int depth_{0};
};

class LayerRowElidingLabel final : public QLabel {
public:
  explicit LayerRowElidingLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("…")));
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    drawFrame(&painter);

    auto text_rect = contentsRect();
    const auto label_margin = margin();
    text_rect.adjust(label_margin, label_margin, -label_margin, -label_margin);
    if (text_rect.isEmpty()) {
      return;
    }

    QStyleOption option;
    option.initFrom(this);
    painter.setFont(font());
    const auto flags = QStyle::visualAlignment(layoutDirection(), alignment()) | Qt::TextSingleLine;
    const auto visible_text =
        painter.fontMetrics().elidedText(text(), Qt::ElideRight, text_rect.width(), Qt::TextSingleLine);
    style()->drawItemText(&painter, text_rect, flags, option.palette,
                          option.state.testFlag(QStyle::State_Enabled), visible_text, foregroundRole());
  }
};

QString layer_visibility_tooltip(bool visible) {
  return visible ? QObject::tr("Layer visible. Click to hide.") : QObject::tr("Layer hidden. Click to show.");
}

void update_layer_visibility_button(QToolButton* button, bool visible) {
  if (button == nullptr) {
    return;
  }
  button->setText(QString());
  button->setIcon(simple_icon(visible ? QStringLiteral("eye") : QStringLiteral("eyeOff"),
                              visible ? QColor(228, 236, 246) : QColor(118, 126, 136)));
  button->setToolTip(layer_visibility_tooltip(visible));
}

QString layer_lock_flag_tooltip(LayerLockFlags flag, bool inherited) {
  QString text;
  if (flag == kLayerLockTransparentPixels) {
    text = QObject::tr("Transparent pixels locked");
  } else if (flag == kLayerLockImagePixels) {
    text = QObject::tr("Image pixels locked");
  } else if (flag == kLayerLockPosition) {
    text = QObject::tr("Position locked");
  } else {
    text = QObject::tr("Layer locked");
  }
  return inherited ? QObject::tr("%1 by folder").arg(text) : text;
}

QString layer_lock_flag_icon_key(LayerLockFlags flag) {
  if (flag == kLayerLockTransparentPixels) {
    return QStringLiteral("AL");
  }
  if (flag == kLayerLockImagePixels) {
    return QStringLiteral("fill");
  }
  if (flag == kLayerLockPosition) {
    return QStringLiteral("TR");
  }
  return QStringLiteral("lock");
}

QLabel* make_layer_lock_badge(LayerLockFlags flag, bool inherited, QWidget* parent, LayerListWidget* list_parent) {
  auto* badge = new QLabel(parent);
  badge->setObjectName(QStringLiteral("layerLockBadge"));
  badge->setFixedSize(18, 18);
  badge->setAlignment(Qt::AlignCenter);
  badge->setToolTip(layer_lock_flag_tooltip(flag, inherited));
  badge->setProperty("inherited", inherited);
  const auto color = inherited ? QColor(126, 132, 140) : QColor(242, 215, 125);
  badge->setPixmap(simple_icon(layer_lock_flag_icon_key(flag), color).pixmap(QSize(14, 14)));
  if (list_parent != nullptr) {
    badge->installEventFilter(list_parent);
  }
  return badge;
}

void set_layer_lock_control_state(QToolButton* button, bool checked, bool mixed, const QString& tooltip) {
  if (button == nullptr) {
    return;
  }
  QSignalBlocker blocker(button);
  button->setChecked(checked);
  button->setProperty("mixed", mixed);
  button->setToolTip(mixed ? QObject::tr("%1\nMixed selection").arg(tooltip) : tooltip);
  button->style()->unpolish(button);
  button->style()->polish(button);
}

void set_property_label_text(QLabel* label, const QString& text) {
  if (label == nullptr) {
    return;
  }
  label->setText(text);
  label->setVisible(!text.trimmed().isEmpty());
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
  if (mask.disabled) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(232, 70, 70), 2));
    painter.drawLine(QPoint(3, 3), QPoint(kSize - 4, kSize - 4));
    painter.drawLine(QPoint(kSize - 4, 3), QPoint(3, kSize - 4));
  }
  return pixmap;
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
    case AdjustmentKind::Invert:
      return QColor(200, 205, 212);
    case AdjustmentKind::Posterize:
      return QColor(95, 205, 215);
    case AdjustmentKind::Threshold:
      return QColor(235, 128, 118);
    case AdjustmentKind::BrightnessContrast:
      return QColor(250, 225, 120);
  }
  return QColor(145, 175, 215);
}

QString layer_kind_name(LayerKind kind) {
  switch (kind) {
    case LayerKind::Pixel:
      return QObject::tr("Pixel Layer");
    case LayerKind::Group:
      return QObject::tr("Folder");
    case LayerKind::Adjustment:
      return QObject::tr("Adjustment Layer");
    case LayerKind::Text:
      return QObject::tr("Text Layer");
    case LayerKind::Vector:
      return QObject::tr("Vector Layer");
    case LayerKind::SmartObject:
      return QObject::tr("Smart Object");
  }
  return QObject::tr("Layer");
}

QString pixel_format_name(PixelFormat format) {
  QString depth;
  switch (format.bit_depth) {
    case BitDepth::UInt8:
      depth = QObject::tr("8-bit");
      break;
    case BitDepth::UInt16:
      depth = QObject::tr("16-bit");
      break;
    case BitDepth::Float32:
      depth = QObject::tr("32-bit float");
      break;
  }

  QString mode;
  switch (format.color_mode) {
    case ColorMode::RGB:
      mode = QObject::tr("RGB");
      break;
    case ColorMode::Grayscale:
      mode = QObject::tr("Grayscale");
      break;
    case ColorMode::CMYK:
      mode = QObject::tr("CMYK");
      break;
    case ColorMode::Lab:
      mode = QObject::tr("Lab");
      break;
  }

  return QObject::tr("%1 %2, %3 channels").arg(depth, mode).arg(format.channels);
}

QString rect_summary(Rect rect) {
  if (rect.empty()) {
    return QObject::tr("empty");
  }
  return QObject::tr("%1 x %2 at %3, %4").arg(rect.width).arg(rect.height).arg(rect.x).arg(rect.y);
}

QString layer_style_summary(const LayerStyle& style) {
  QStringList effects;
  if (!style.drop_shadows.empty()) {
    effects << QObject::tr("Drop Shadow");
  }
  if (!style.inner_shadows.empty()) {
    effects << QObject::tr("Inner Shadow");
  }
  if (!style.outer_glows.empty()) {
    effects << QObject::tr("Outer Glow");
  }
  if (!style.inner_glows.empty()) {
    effects << QObject::tr("Inner Glow");
  }
  if (!style.satins.empty()) {
    effects << QObject::tr("Satin");
  }
  if (!style.color_overlays.empty()) {
    effects << QObject::tr("Color Overlay");
  }
  if (!style.gradient_fills.empty()) {
    effects << QObject::tr("Gradient Fill");
  }
  if (!style.pattern_overlays.empty()) {
    effects << QObject::tr("Pattern Overlay");
  }
  if (!style.strokes.empty()) {
    effects << QObject::tr("Stroke");
  }
  if (!style.bevels.empty()) {
    effects << QObject::tr("Bevel");
  }
  return effects.isEmpty() ? QObject::tr("none") : effects.join(QObject::tr(", "));
}

QString adjustment_settings_summary(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return QObject::tr("No editable adjustment settings");
  }
  switch (settings->kind) {
    case AdjustmentKind::Levels:
      return QObject::tr("Levels: black %1, white %2, gamma %3%, output %4-%5")
          .arg(settings->levels.black_input)
          .arg(settings->levels.white_input)
          .arg(settings->levels.gamma_percent)
          .arg(settings->levels.black_output)
          .arg(settings->levels.white_output);
    case AdjustmentKind::Curves:
      return QObject::tr("Curves: RGB %1, Red %2, Green %3, Blue %4 points")
          .arg(curve_points_for_channel(settings->curves, CurvesChannel::Rgb).size())
          .arg(curve_points_for_channel(settings->curves, CurvesChannel::Red).size())
          .arg(curve_points_for_channel(settings->curves, CurvesChannel::Green).size())
          .arg(curve_points_for_channel(settings->curves, CurvesChannel::Blue).size());
    case AdjustmentKind::HueSaturation:
      if (settings->hue_saturation.colorize) {
        return QObject::tr("Colorize: hue %1, saturation %2, lightness %3")
            .arg(settings->hue_saturation.colorize_hue)
            .arg(settings->hue_saturation.colorize_saturation)
            .arg(settings->hue_saturation.colorize_lightness);
      }
      return QObject::tr("Hue/Saturation: hue %1, saturation %2, lightness %3")
          .arg(settings->hue_saturation.hue_shift)
          .arg(settings->hue_saturation.saturation_delta)
          .arg(settings->hue_saturation.lightness_delta);
    case AdjustmentKind::ColorBalance:
      return QObject::tr("Color Balance: C/R %1, M/G %2, Y/B %3")
          .arg(settings->color_balance.cyan_red)
          .arg(settings->color_balance.magenta_green)
          .arg(settings->color_balance.yellow_blue);
    case AdjustmentKind::Invert:
      return QObject::tr("Invert");
    case AdjustmentKind::Posterize:
      return QObject::tr("Posterize: %1 levels").arg(settings->posterize.levels);
    case AdjustmentKind::Threshold:
      return QObject::tr("Threshold: level %1").arg(settings->threshold.level);
    case AdjustmentKind::BrightnessContrast:
      return QObject::tr("Brightness/Contrast: brightness %1, contrast %2")
          .arg(settings->brightness_contrast.brightness)
          .arg(settings->brightness_contrast.contrast);
  }
  return QObject::tr("Adjustment");
}

QString format_text_points(double points) {
  auto text = QString::number(points, 'f', 2);
  while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

QString text_layer_summary(const Layer& layer, const Document& document) {
  if (!layer_is_text(layer)) {
    return QObject::tr("No editable text metadata");
  }
  const auto& metadata = layer.metadata();
  const auto value = [&metadata](const char* key, QString fallback = {}) {
    const auto found = metadata.find(key);
    return found == metadata.end() ? fallback : QString::fromStdString(found->second);
  };
  const auto text = value(kLayerMetadataText, QObject::tr("Text"));
  const auto family = value(kLayerMetadataTextFont, QObject::tr("Default"));
  const auto size_value = value(kLayerMetadataTextSize);
  bool size_ok = false;
  const auto size_pixels = size_value.toInt(&size_ok);
  const auto size = size_ok ? format_text_points(text_pixels_to_points(size_pixels, document)) : QObject::tr("?");
  const auto color = value(kLayerMetadataTextColor, QObject::tr("#000000"));
  const auto flow = value(kLayerMetadataTextFlow, QStringLiteral("point")).compare(QStringLiteral("box"),
                                                                                   Qt::CaseInsensitive) == 0
                        ? QObject::tr("Box")
                        : QObject::tr("Point");
  const auto source_block = value(kLayerMetadataTextSourceBlock);
  const auto raster_status = value(kLayerMetadataTextRasterStatus, QObject::tr("patchy_raster"));
  QStringList style;
  if (value(kLayerMetadataTextBold) == QStringLiteral("true")) {
    style << QObject::tr("bold");
  }
  if (value(kLayerMetadataTextItalic) == QStringLiteral("true")) {
    style << QObject::tr("italic");
  }
  QString source = QObject::tr("Source: Patchy text");
  if (!source_block.isEmpty()) {
    source = QObject::tr("Source: PSD %1").arg(source_block);
  }
  if (raster_status == QStringLiteral("placeholder")) {
    source += QObject::tr(" placeholder preview");
  } else if (raster_status == QStringLiteral("psd_raster_preview")) {
    source += QObject::tr(" raster preview");
  }
  return QObject::tr("%1\nFont: %2, %3 pt%4\nColor: %5\nFlow: %6\n%7")
      .arg(text.left(80), family, size, style.isEmpty() ? QString() : QObject::tr(", %1").arg(style.join(QObject::tr(", "))),
           color, flow, source);
}

int color_luminance(const QColor& color) {
  return static_cast<int>(std::round((0.2126 * color.red()) + (0.7152 * color.green()) + (0.0722 * color.blue())));
}

QColor readable_text_thumbnail_color(const QColor& preferred, const QColor& background) {
  const auto fallback = color_luminance(background) < 128 ? QColor(238, 244, 252) : QColor(32, 38, 48);
  if (!preferred.isValid()) {
    return fallback;
  }
  if (std::abs(color_luminance(preferred) - color_luminance(background)) >= 96) {
    return preferred;
  }
  return fallback;
}

void draw_adjustment_thumbnail_frame(QPainter& painter, int size, const QColor& accent) {
  QLinearGradient background(QPointF(0.0, 0.0), QPointF(0.0, static_cast<double>(size)));
  background.setColorAt(0.0, QColor(68, 76, 87));
  background.setColorAt(1.0, QColor(35, 41, 50));
  painter.fillRect(QRect(0, 0, size, size), background);
  painter.fillRect(QRect(0, 0, size, 4), accent);
  painter.fillRect(QRect(0, 4, 4, size - 4), accent.darker(118));
}

void draw_generic_adjustment_thumbnail_symbol(QPainter& painter, const QColor& accent) {
  const QRectF circle(6.0, 6.0, 16.0, 16.0);
  QPainterPath left_half;
  left_half.addEllipse(circle);
  painter.save();
  painter.setClipPath(left_half);
  painter.fillRect(QRectF(6.0, 6.0, 8.0, 16.0), QColor(238, 243, 248));
  painter.fillRect(QRectF(14.0, 6.0, 8.0, 16.0), QColor(31, 37, 46));
  painter.restore();
  painter.setPen(QPen(accent.lighter(135), 2));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(circle.adjusted(1.0, 1.0, -1.0, -1.0));
}

void draw_levels_adjustment_thumbnail_symbol(QPainter& painter, const LevelsAdjustment& settings,
                                             const QColor& accent) {
  constexpr std::array<int, 9> kBars{4, 8, 13, 16, 14, 11, 8, 6, 4};
  const QRectF graph(5.0, 6.0, 18.0, 16.0);
  painter.fillRect(graph, QColor(28, 34, 42));
  painter.setPen(QPen(QColor(82, 92, 106), 1));
  painter.drawLine(QPointF(5.0, 14.0), QPointF(23.0, 14.0));
  painter.drawLine(QPointF(14.0, 6.0), QPointF(14.0, 22.0));
  for (std::size_t index = 0; index < kBars.size(); ++index) {
    const auto x = 6.0 + static_cast<double>(index) * 1.9;
    const auto height = static_cast<double>(kBars[index]);
    const auto color = QColor::fromHsv(static_cast<int>(205.0 - static_cast<double>(index) * 10.0), 80, 240);
    painter.fillRect(QRectF(x, 22.0 - height, 1.25, height), color);
  }

  const auto x_for_input = [](int value) {
    return 5.0 + (static_cast<double>(std::clamp(value, 0, 255)) / 255.0) * 18.0;
  };
  const auto black_x = x_for_input(settings.black_input);
  const auto white_x = x_for_input(settings.white_input);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(8, 10, 13));
  painter.drawPolygon(QPolygonF{QPointF(black_x, 23.0), QPointF(black_x - 2.2, 26.0), QPointF(black_x + 2.2, 26.0)});
  painter.setBrush(accent.lighter(130));
  painter.drawPolygon(QPolygonF{QPointF(14.0, 23.0), QPointF(11.8, 26.0), QPointF(16.2, 26.0)});
  painter.setBrush(QColor(245, 248, 252));
  painter.drawPolygon(QPolygonF{QPointF(white_x, 23.0), QPointF(white_x - 2.2, 26.0), QPointF(white_x + 2.2, 26.0)});
}

void draw_curves_adjustment_thumbnail_symbol(QPainter& painter, const CurvesAdjustment& settings,
                                             const QColor& accent) {
  const QRectF graph(5.0, 6.0, 18.0, 18.0);
  painter.fillRect(graph, QColor(26, 32, 40));
  painter.setPen(QPen(QColor(83, 95, 110), 1));
  painter.drawLine(QPointF(11.0, 6.0), QPointF(11.0, 24.0));
  painter.drawLine(QPointF(17.0, 6.0), QPointF(17.0, 24.0));
  painter.drawLine(QPointF(5.0, 12.0), QPointF(23.0, 12.0));
  painter.drawLine(QPointF(5.0, 18.0), QPointF(23.0, 18.0));
  painter.setPen(QPen(QColor(210, 216, 225, 100), 1));
  painter.drawLine(QPointF(5.0, 24.0), QPointF(23.0, 6.0));

  const auto x_for_input = [](int input) {
    return 5.0 + (static_cast<double>(std::clamp(input, 0, 255)) / 255.0) * 18.0;
  };
  const auto y_for_output = [](int output) {
    return 24.0 - (static_cast<double>(std::clamp(output, 0, 255)) / 255.0) * 18.0;
  };
  const auto path_for_lut = [&](const std::array<std::uint8_t, 256>& lut) {
    QPainterPath path;
    path.moveTo(QPointF(x_for_input(0), y_for_output(lut[0])));
    for (int input = 1; input < 256; ++input) {
      path.lineTo(QPointF(x_for_input(input), y_for_output(lut[static_cast<std::size_t>(input)])));
    }
    return path;
  };
  const auto composite_lut = build_curve_lut(curve_points_for_channel(settings, CurvesChannel::Rgb));
  const auto channel_lut = build_curves_lut(settings);
  painter.setPen(QPen(QColor(235, 82, 82, 130), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path_for_lut(channel_lut.red));
  painter.setPen(QPen(QColor(76, 218, 126, 130), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path_for_lut(channel_lut.green));
  painter.setPen(QPen(QColor(82, 139, 244, 130), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path_for_lut(channel_lut.blue));
  const auto curve = path_for_lut(composite_lut);
  painter.setPen(QPen(QColor(8, 13, 11, 160), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(curve.translated(0.0, 1.0));
  painter.setPen(QPen(accent.lighter(135), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(curve);
  painter.setBrush(QColor(238, 246, 241));
  painter.setPen(QPen(QColor(18, 34, 24), 1));
  for (const auto& point : curve_points_for_channel(settings, CurvesChannel::Rgb)) {
    painter.drawEllipse(QPointF(x_for_input(point.input), y_for_output(point.output)), 1.7, 1.7);
  }
}

void draw_hue_saturation_adjustment_thumbnail_symbol(QPainter& painter, const HueSaturationAdjustment& settings,
                                                     const QColor& accent) {
  const QRectF wheel(5.0, 5.0, 18.0, 18.0);
  QPainterPath wheel_clip;
  wheel_clip.addEllipse(wheel);
  painter.save();
  painter.setClipPath(wheel_clip);
  const auto saturation = std::clamp(210 + settings.saturation_delta, 80, 255);
  const auto value = std::clamp(235 + settings.lightness_delta, 120, 255);
  for (int slice = 0; slice < 6; ++slice) {
    const auto hue = (slice * 60 + settings.hue_shift + 360) % 360;
    painter.fillRect(QRectF(5.0 + static_cast<double>(slice) * 3.0, 5.0, 3.2, 18.0),
                     QColor::fromHsv(hue, saturation, value));
  }
  painter.restore();
  painter.setPen(QPen(QColor(246, 249, 252), 2));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(wheel.adjusted(1.0, 1.0, -1.0, -1.0));
  painter.setBrush(QColor(35, 41, 50));
  painter.setPen(QPen(accent.lighter(140), 1));
  painter.drawEllipse(QRectF(10.0, 10.0, 8.0, 8.0));
}

void draw_color_balance_adjustment_thumbnail_symbol(QPainter& painter, const ColorBalanceAdjustment& settings) {
  const auto draw_bar = [&painter](double y, const QColor& left, const QColor& right, int value) {
    QLinearGradient gradient(QPointF(6.0, y), QPointF(22.0, y));
    gradient.setColorAt(0.0, left);
    gradient.setColorAt(0.5, QColor(220, 224, 230));
    gradient.setColorAt(1.0, right);
    painter.setPen(QPen(QColor(20, 25, 32), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(6.0, y - 1.5, 16.0, 3.0), 1.5, 1.5);
    const auto x = 6.0 + (static_cast<double>(std::clamp(value, -100, 100) + 100) / 200.0) * 16.0;
    painter.setBrush(QColor(250, 252, 255));
    painter.setPen(QPen(QColor(31, 37, 46), 1));
    painter.drawEllipse(QPointF(x, y), 2.1, 2.1);
  };
  draw_bar(9.0, QColor(0, 210, 230), QColor(255, 82, 82), settings.cyan_red);
  draw_bar(15.0, QColor(235, 72, 220), QColor(70, 220, 105), settings.magenta_green);
  draw_bar(21.0, QColor(248, 222, 72), QColor(85, 130, 255), settings.yellow_blue);
}

void draw_brightness_contrast_adjustment_thumbnail_symbol(QPainter& painter, const QColor& accent) {
  // A small sun: brightness rays around a half-filled (contrast) disc.
  const QPointF center(14.0, 14.0);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(accent.lighter(115), 1.6));
  for (int ray = 0; ray < 8; ++ray) {
    const auto angle = ray * std::numbers::pi / 4.0;
    const QPointF direction(std::cos(angle), std::sin(angle));
    painter.drawLine(center + direction * 7.0, center + direction * 9.5);
  }
  const QRectF disc(center.x() - 5.0, center.y() - 5.0, 10.0, 10.0);
  QPainterPath left_half;
  left_half.moveTo(center);
  left_half.arcTo(disc, 90.0, 180.0);
  left_half.closeSubpath();
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(238, 243, 248));
  painter.drawEllipse(disc);
  painter.setBrush(QColor(31, 37, 46));
  painter.drawPath(left_half);
  painter.setPen(QPen(accent.lighter(115), 1.2));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(disc);
}

void draw_posterize_adjustment_thumbnail_symbol(QPainter& painter, const QColor& accent) {
  // A descending stair-step: the banded tone ramp posterize produces.
  constexpr int kSteps = 4;
  const QRectF graph(6.0, 7.0, 16.0, 14.0);
  painter.setPen(Qt::NoPen);
  for (int step = 0; step < kSteps; ++step) {
    const auto step_width = graph.width() / kSteps;
    const auto step_height = graph.height() * (kSteps - step) / kSteps;
    const auto gray = 70 + step * 55;
    painter.setBrush(QColor(gray, gray, gray));
    painter.drawRect(QRectF(graph.left() + step * step_width, graph.bottom() - step_height,
                            step_width, step_height));
  }
  painter.setPen(QPen(accent.lighter(120), 1.5));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(graph);
}

void draw_threshold_adjustment_thumbnail_symbol(QPainter& painter, const QColor& accent) {
  // A hard vertical black/white split: everything below the level goes black,
  // everything above goes white.
  const QRectF square(7.0, 7.0, 14.0, 14.0);
  painter.setPen(Qt::NoPen);
  painter.fillRect(QRectF(square.left(), square.top(), square.width() / 2.0, square.height()),
                   QColor(238, 243, 248));
  painter.fillRect(QRectF(square.center().x(), square.top(), square.width() / 2.0, square.height()),
                   QColor(31, 37, 46));
  painter.setPen(QPen(accent.lighter(120), 1.5));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(square);
  painter.drawLine(QPointF(square.center().x(), square.top()), QPointF(square.center().x(), square.bottom()));
}

void draw_invert_adjustment_thumbnail_symbol(QPainter& painter, const QColor& accent) {
  // A square split along the diagonal into a light and a dark half: a negative.
  const QRectF square(7.0, 7.0, 14.0, 14.0);
  QPainterPath upper_left;
  upper_left.moveTo(square.topLeft());
  upper_left.lineTo(square.topRight());
  upper_left.lineTo(square.bottomLeft());
  upper_left.closeSubpath();
  QPainterPath lower_right;
  lower_right.moveTo(square.topRight());
  lower_right.lineTo(square.bottomRight());
  lower_right.lineTo(square.bottomLeft());
  lower_right.closeSubpath();
  painter.setPen(Qt::NoPen);
  painter.fillPath(upper_left, QColor(238, 243, 248));
  painter.fillPath(lower_right, QColor(31, 37, 46));
  painter.setPen(QPen(accent.lighter(120), 1.5));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(square);
  painter.drawLine(square.topRight(), square.bottomLeft());
}

QPixmap layer_content_thumbnail(const Layer& layer) {
  constexpr int kSize = 28;
  if (layer.kind() == LayerKind::Group) {
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath shadow;
    shadow.moveTo(4.6, 23.2);
    shadow.lineTo(4.6, 9.2);
    shadow.quadTo(4.6, 7.7, 6.1, 7.7);
    shadow.lineTo(11.8, 7.7);
    shadow.lineTo(14.0, 5.0);
    shadow.lineTo(23.0, 5.0);
    shadow.quadTo(24.6, 5.0, 24.6, 6.6);
    shadow.lineTo(24.6, 23.2);
    shadow.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawPath(shadow.translated(1.2, 1.4));

    QPainterPath back;
    back.moveTo(4.6, 22.6);
    back.lineTo(4.6, 9.0);
    back.quadTo(4.6, 7.6, 6.0, 7.6);
    back.lineTo(11.9, 7.6);
    back.lineTo(14.2, 4.8);
    back.lineTo(22.7, 4.8);
    back.quadTo(24.2, 4.8, 24.2, 6.3);
    back.lineTo(24.2, 22.6);
    back.closeSubpath();
    QLinearGradient back_gradient(QPointF(5.0, 5.0), QPointF(24.0, 22.0));
    back_gradient.setColorAt(0.0, QColor(255, 218, 105));
    back_gradient.setColorAt(1.0, QColor(190, 128, 32));
    painter.setBrush(back_gradient);
    painter.drawPath(back);

    QPainterPath front_outline;
    front_outline.moveTo(3.7, 13.4);
    front_outline.lineTo(25.2, 13.4);
    front_outline.lineTo(22.9, 23.3);
    front_outline.quadTo(22.6, 24.6, 21.1, 24.6);
    front_outline.lineTo(5.4, 24.6);
    front_outline.quadTo(4.2, 24.6, 3.9, 23.3);
    front_outline.closeSubpath();
    painter.setBrush(QColor(246, 200, 84));
    painter.drawPath(front_outline);

    QPainterPath front;
    front.moveTo(5.7, 15.4);
    front.lineTo(23.0, 15.4);
    front.lineTo(21.3, 22.3);
    front.lineTo(6.3, 22.3);
    front.closeSubpath();
    QLinearGradient front_gradient(QPointF(5.0, 14.0), QPointF(23.0, 23.0));
    front_gradient.setColorAt(0.0, QColor(104, 76, 31));
    front_gradient.setColorAt(1.0, QColor(56, 46, 29));
    painter.setBrush(front_gradient);
    painter.drawPath(front);

    painter.fillRect(QRectF(6.0, 14.6, 17.1, 1.0), QColor(255, 233, 132));
    painter.setPen(QPen(QColor(255, 223, 108), 1.0));
    painter.drawPath(back);
    painter.setPen(QPen(QColor(255, 231, 132), 1.0));
    painter.drawPath(front_outline);
    return pixmap;
  }
  if (layer_is_text(layer)) {
    const QColor background(35, 38, 44);
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(background);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(150, 158, 168), 1));
    painter.drawRect(QRect(0, 0, kSize - 1, kSize - 1));
    QColor text_color;
    if (const auto found = layer.metadata().find(kLayerMetadataTextColor); found != layer.metadata().end()) {
      const QColor stored(QString::fromStdString(found->second));
      if (stored.isValid()) {
        text_color = stored;
      }
    }
    text_color = readable_text_thumbnail_color(text_color, background);
    auto font = painter.font();
    font.setBold(true);
    font.setPixelSize(20);
    painter.setFont(font);
    // Center the glyph by its actual rendered ink: font metrics can disagree with the
    // rasterizer's hinting by a pixel or two, which reads as off-center in a 28px tile.
    const auto glyph = QStringLiteral("T");
    const QPoint probe_baseline(kSize / 2, kSize * 3 / 2);
    QImage ink(kSize * 2, kSize * 2, QImage::Format_ARGB32_Premultiplied);
    ink.fill(Qt::transparent);
    {
      QPainter probe(&ink);
      probe.setRenderHint(QPainter::Antialiasing);
      probe.setFont(font);
      probe.setPen(Qt::white);
      probe.drawText(probe_baseline, glyph);
    }
    QRect ink_rect;
    for (int y = 0; y < ink.height(); ++y) {
      for (int x = 0; x < ink.width(); ++x) {
        if (qAlpha(ink.pixel(x, y)) > 32) {
          ink_rect = ink_rect.isNull() ? QRect(x, y, 1, 1) : ink_rect.united(QRect(x, y, 1, 1));
        }
      }
    }
    // Integer division floors, biasing a half-pixel remainder up/left, which offsets the
    // down/right weight the drop shadow adds.
    const QPoint baseline = probe_baseline + QPoint((kSize - ink_rect.width()) / 2 - ink_rect.x(),
                                                    (kSize - ink_rect.height()) / 2 - ink_rect.y());
    painter.setPen(QColor(12, 14, 18, 180));
    painter.drawText(baseline + QPoint(1, 1), glyph);
    painter.setPen(text_color);
    painter.drawText(baseline, glyph);
    return pixmap;
  }
  if (layer.kind() == LayerKind::Adjustment) {
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(QColor(35, 41, 50));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto settings = adjustment_settings_from_layer(layer);
    const auto accent = adjustment_thumbnail_accent(layer);
    draw_adjustment_thumbnail_frame(painter, kSize, accent);
    if (!settings.has_value()) {
      draw_generic_adjustment_thumbnail_symbol(painter, accent);
    } else {
      switch (settings->kind) {
        case AdjustmentKind::Levels:
          draw_levels_adjustment_thumbnail_symbol(painter, settings->levels, accent);
          break;
        case AdjustmentKind::Curves:
          draw_curves_adjustment_thumbnail_symbol(painter, settings->curves, accent);
          break;
        case AdjustmentKind::HueSaturation:
          draw_hue_saturation_adjustment_thumbnail_symbol(painter, settings->hue_saturation, accent);
          break;
        case AdjustmentKind::ColorBalance:
          draw_color_balance_adjustment_thumbnail_symbol(painter, settings->color_balance);
          break;
        case AdjustmentKind::Invert:
          draw_invert_adjustment_thumbnail_symbol(painter, accent);
          break;
        case AdjustmentKind::Posterize:
          draw_posterize_adjustment_thumbnail_symbol(painter, accent);
          break;
        case AdjustmentKind::Threshold:
          draw_threshold_adjustment_thumbnail_symbol(painter, accent);
          break;
        case AdjustmentKind::BrightnessContrast:
          draw_brightness_contrast_adjustment_thumbnail_symbol(painter, accent);
          break;
      }
    }
    painter.setBrush(Qt::NoBrush);
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

QWidget* make_layer_row_widget(const Layer& layer, QListWidgetItem* item, QWidget* parent,
                               const std::function<QPixmap(const Layer&)>& content_thumbnail,
                               const std::function<QPixmap(const Layer&)>& mask_thumbnail, int depth = 0,
                               bool ancestors_visible = true, bool group_expanded = true,
                               LayerLockFlags ancestor_lock_flags = kLayerLockNone,
                               std::function<void(LayerId)> toggle_group_expanded = {},
                               std::function<void(LayerId, bool)> set_mask_linked = {},
                               bool content_target_active = false, bool mask_target_active = false,
                               bool smart_filter_mask_target_active = false,
                               bool smart_filter_mask_size_supported = true,
                               std::function<void(LayerId)> open_layer_styles = {},
                               std::function<void(LayerId)> open_smart_object = {}, bool clipped = false,
                               std::function<void(LayerId)> toggle_clipping = {},
                               std::function<void(LayerId, bool)> set_smart_filter_stack_enabled = {},
                               std::function<void(LayerId, std::size_t, bool)> set_smart_filter_enabled = {},
                               std::function<void(LayerId, std::size_t)> edit_smart_filter = {},
                               std::function<void(LayerId, std::size_t)> edit_smart_filter_blending = {},
                               std::function<void(LayerId, std::size_t)> duplicate_smart_filter = {},
                               std::function<void(LayerId, std::size_t, int)> move_smart_filter = {},
                               std::function<void(LayerId, std::size_t)> delete_smart_filter = {}) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("layerRowWidget"));
  row->setAttribute(Qt::WA_StyledBackground, true);
  row->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  auto* list_parent = dynamic_cast<LayerListWidget*>(parent);
  if (list_parent != nullptr) {
    row->installEventFilter(list_parent);
  }
  auto* row_layout = new QVBoxLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 0);
  row_layout->setSpacing(0);

  auto* main_row = new QWidget(row);
  main_row->setObjectName(QStringLiteral("layerMainRow"));
  main_row->setFixedHeight(44);
  main_row->setAutoFillBackground(false);
  if (list_parent != nullptr) {
    main_row->installEventFilter(list_parent);
  }
  row_layout->addWidget(main_row);

  auto* layout = new QHBoxLayout(main_row);
  layout->setContentsMargins(kLayerRowBaseIndent, 4, 8, 4);
  layout->setSpacing(kLayerRowHorizontalSpacing);

  auto* visibility = new QToolButton(row);
  visibility->setObjectName(QStringLiteral("layerVisibilityCheck"));
  visibility->setCheckable(true);
  visibility->setChecked(layer.visible());
  update_layer_visibility_button(visibility, layer.visible());
  visibility->setToolButtonStyle(Qt::ToolButtonIconOnly);
  visibility->setIconSize(QSize(16, 16));
  visibility->setFixedSize(22, 22);
  visibility->setEnabled(ancestors_visible);
  if (list_parent != nullptr) {
    visibility->installEventFilter(list_parent);
  }
  layout->addWidget(visibility, 0, Qt::AlignVCenter);

  layout->addWidget(new LayerTreeIndentWidget(depth, row), 0, Qt::AlignVCenter);

  if (clipped) {
    auto* clip_badge = new QToolButton(row);
    clip_badge->setObjectName(QStringLiteral("layerClippingBadgeButton"));
    clip_badge->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clip_badge->setIcon(simple_icon(QStringLiteral("clip"), QColor(150, 205, 255)));
    clip_badge->setIconSize(QSize(20, 20));
    clip_badge->setFixedSize(24, 24);
    clip_badge->setFocusPolicy(Qt::NoFocus);
    clip_badge->setToolTip(QObject::tr("Clipped to the layer below. Click to release."));
    clip_badge->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      clip_badge->installEventFilter(list_parent);
    }
    QObject::connect(clip_badge, &QToolButton::clicked, row, [parent, id = layer.id(), toggle_clipping] {
      if (toggle_clipping) {
        // Deferred: the handler rebuilds the layer rows, deleting this button.
        QTimer::singleShot(0, parent, [id, toggle_clipping] { toggle_clipping(id); });
      }
    });
    layout->addWidget(clip_badge, 0, Qt::AlignVCenter);
  }

  const auto is_group = layer.kind() == LayerKind::Group;
  if (is_group) {
    auto* disclosure = new QToolButton(row);
    disclosure->setObjectName(QStringLiteral("layerFolderDisclosureButton"));
    disclosure->setCheckable(true);
    disclosure->setChecked(group_expanded);
    disclosure->setArrowType(group_expanded ? Qt::DownArrow : Qt::RightArrow);
    disclosure->setToolButtonStyle(Qt::ToolButtonIconOnly);
    disclosure->setFixedSize(kLayerFolderDisclosureWidth, kLayerFolderDisclosureHeight);
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
    layout->addSpacing(kLayerFolderDisclosureWidth);
  }

  auto* thumbnail = new QLabel(row);
  thumbnail->setObjectName(QStringLiteral("layerContentThumbnail"));
  thumbnail->setFixedSize(30, 30);
  thumbnail->setPixmap(content_thumbnail ? content_thumbnail(layer) : layer_content_thumbnail(layer));
  thumbnail->setProperty(kLayerContentThumbnailRevisionProperty,
                         QVariant::fromValue<qulonglong>(static_cast<qulonglong>(layer.content_revision())));
  thumbnail->setToolTip(layer.kind() == LayerKind::Group
                            ? QObject::tr("Folder layer")
                            : layer.kind() == LayerKind::Adjustment
                                ? QObject::tr("Adjustment Layer")
                                : layer_is_text(layer) ? QObject::tr("Text layer") : QObject::tr("Layer thumbnail"));
  thumbnail->setProperty("layerTargetActive", content_target_active);
  thumbnail->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    thumbnail->installEventFilter(list_parent);
  }
  layout->addWidget(thumbnail, 0, Qt::AlignVCenter);

  if (layer.mask().has_value()) {
    if (layer.kind() == LayerKind::Pixel && !layer_is_text(layer)) {
      thumbnail->setToolTip(QObject::tr("Layer pixels. Click to edit them instead of the mask."));
    }
    auto* link = new QToolButton(row);
    link->setObjectName(QStringLiteral("layerMaskLinkButton"));
    link->setCheckable(true);
    link->setChecked(layer_mask_linked(layer));
    link->setText(QString());
    link->setIcon(simple_icon(link->isChecked() ? QStringLiteral("link") : QStringLiteral("off"),
                              link->isChecked() ? QColor(220, 226, 235) : QColor(112, 120, 130)));
    link->setToolButtonStyle(Qt::ToolButtonIconOnly);
    link->setIconSize(QSize(19, 19));
    link->setToolTip(link->isChecked() ? QObject::tr("Layer and mask are linked. Click to unlink.")
                                       : QObject::tr("Layer and mask are unlinked. Click to link."));
    link->setFixedSize(24, 24);
    link->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      link->installEventFilter(list_parent);
    }
    QObject::connect(link, &QToolButton::toggled, row,
                     [parent, id = layer.id(), link, set_mask_linked = std::move(set_mask_linked)](bool checked) {
      link->setIcon(simple_icon(checked ? QStringLiteral("link") : QStringLiteral("off"),
                                checked ? QColor(220, 226, 235) : QColor(112, 120, 130)));
      link->setToolTip(checked ? QObject::tr("Layer and mask are linked. Click to unlink.")
                               : QObject::tr("Layer and mask are unlinked. Click to link."));
      if (set_mask_linked) {
        QTimer::singleShot(0, parent, [id, checked, set_mask_linked] { set_mask_linked(id, checked); });
      }
    });
    layout->addWidget(link, 0, Qt::AlignVCenter);

    auto* mask_preview = new QLabel(row);
    mask_preview->setObjectName(QStringLiteral("layerMaskThumbnail"));
    mask_preview->setFixedSize(30, 30);
    mask_preview->setPixmap(mask_thumbnail ? mask_thumbnail(layer) : layer_mask_thumbnail(*layer.mask()));
    mask_preview->setProperty(kLayerMaskThumbnailRevisionProperty,
                              QVariant::fromValue<qulonglong>(static_cast<qulonglong>(layer.content_revision())));
    mask_preview->setToolTip(
        QObject::tr("Layer mask. Click to edit it with the paint tools, Alt-click to view it, Shift-click to "
                    "disable it."));
    mask_preview->setProperty("layerTargetActive", mask_target_active);
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

  const auto display_name = QString::fromStdString(layer.name());
  auto* name = new LayerRowElidingLabel(display_name, row);
  name->setObjectName(QStringLiteral("layerRowName"));
  name->setTextFormat(Qt::PlainText);
  name->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    name->installEventFilter(list_parent);
  }
  text_column->addWidget(name);

  const auto mode = blend_mode_name(layer.blend_mode());
  auto detail_text = QStringLiteral("%1  %2%")
                         .arg(mode)
                         .arg(static_cast<int>(std::round(layer.opacity() * 100.0F)));
  if (layer.mask().has_value()) {
    detail_text += QStringLiteral("  %1").arg(QObject::tr("mask"));
  }
  if (clipped) {
    detail_text += QStringLiteral("  %1").arg(QObject::tr("clipped"));
  }
  auto* details = new LayerRowElidingLabel(detail_text, row);
  details->setObjectName(QStringLiteral("layerRowDetails"));
  details->setTextFormat(Qt::PlainText);
  details->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    details->installEventFilter(list_parent);
  }
  auto* details_row = new QHBoxLayout();
  details_row->setContentsMargins(0, 0, 0, 0);
  details_row->setSpacing(3);
  details_row->addWidget(details, 1, Qt::AlignVCenter);

  // The old "fx"/"smart" text badges are icon buttons now: clicking one opens the
  // layer styles dialog / the smart object contents for this row's layer.
  const auto add_badge_button = [&](const QString& object_name, const QString& icon_key,
                                    const QString& tooltip, std::function<void(LayerId)> open) {
    auto* badge = new QToolButton(row);
    badge->setObjectName(object_name);
    badge->setToolButtonStyle(Qt::ToolButtonIconOnly);
    badge->setIcon(simple_icon(icon_key));
    badge->setIconSize(QSize(16, 16));
    badge->setFixedSize(20, 20);
    badge->setFocusPolicy(Qt::NoFocus);
    badge->setToolTip(tooltip);
    badge->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      badge->installEventFilter(list_parent);
    }
    QObject::connect(badge, &QToolButton::clicked, row,
                     [parent, id = layer.id(), open = std::move(open)] {
      if (open) {
        // Deferred: the handler can rebuild the layer rows, deleting this button.
        QTimer::singleShot(0, parent, [id, open] { open(id); });
      }
    });
    details_row->addWidget(badge, 0, Qt::AlignVCenter);
    return badge;
  };
  if (!layer.layer_style().empty()) {
    // badge-fx.svg, not the shared effects.svg: badge art needs heavier strokes
    // to stay readable standing alone at row scale.
    add_badge_button(QStringLiteral("layerFxBadgeButton"), QStringLiteral("badge-fx"),
                     QObject::tr("Layer styles. Click to edit them."), std::move(open_layer_styles));
  }
  if (layer_is_smart_object(layer)) {
    const bool linked = smart_object_lock_reason(layer) == "external";
    auto* smart_badge = add_badge_button(
        QStringLiteral("layerSmartObjectBadgeButton"),
        linked ? QStringLiteral("smart-object-linked") : QStringLiteral("smart-object"),
        linked ? QObject::tr("Linked smart object. Click to open the linked file.")
               : QObject::tr("Smart object. Click to edit its contents."),
        std::move(open_smart_object));
    smart_badge->setProperty("smartObjectLinked", linked);
  }
  text_column->addLayout(details_row);

  const auto direct_lock_flags = layer_lock_flags(layer);
  const auto effective_lock_flags = ancestor_lock_flags | direct_lock_flags;
  auto* lock_badges = new QHBoxLayout();
  lock_badges->setContentsMargins(0, 0, 0, 0);
  lock_badges->setSpacing(2);
  for (const auto flag : {kLayerLockTransparentPixels, kLayerLockImagePixels, kLayerLockPosition}) {
    if ((effective_lock_flags & flag) == kLayerLockNone) {
      continue;
    }
    const bool inherited = (ancestor_lock_flags & flag) != kLayerLockNone &&
                           (direct_lock_flags & flag) == kLayerLockNone;
    lock_badges->addWidget(make_layer_lock_badge(flag, inherited, row, list_parent), 0, Qt::AlignVCenter);
  }
  layout->addLayout(lock_badges, 0);

  QObject::connect(visibility, &QToolButton::toggled, row, [item, visibility](bool checked) {
    update_layer_visibility_button(visibility, checked);
    if (item != nullptr && item->checkState() != (checked ? Qt::Checked : Qt::Unchecked)) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
  });

  if (const auto* smart_filters = layer.smart_filter_stack(); smart_filters != nullptr) {
    constexpr int kSmartFilterRowHeight = 26;
    const auto stack_supported =
        smart_filters->support == SmartFilterStackSupport::Supported;
    const auto smart_object_lock = smart_object_lock_reason(layer);
    const auto image_pixels_locked =
        (effective_lock_flags & kLayerLockImagePixels) != kLayerLockNone;
    const auto controls_supported =
        stack_supported &&
        (smart_object_lock.empty() || smart_object_lock == "external") &&
        !image_pixels_locked;
    const auto preservation_tooltip = image_pixels_locked
        ? QObject::tr("Layer pixels are locked.")
        : stack_supported
            ? QObject::tr(
              "This Smart Object is preview-locked. Its Smart Filters are preserved unchanged.")
            : QObject::tr(
              "This Smart Filter stack contains unsupported Photoshop data. Patchy preserves it unchanged, so the "
              "controls are disabled.");
    const auto button_style = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 0; "
        "min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; } "
        "QToolButton:hover { background: #30343a; border-color: #59636f; } "
        "QToolButton:disabled { background: transparent; border-color: transparent; }");
    const auto configure_visibility_button = [&](QToolButton* button, bool visible,
                                                  const QString& visible_tooltip,
                                                  const QString& hidden_tooltip) {
      button->setCheckable(true);
      button->setChecked(visible);
      button->setText(QString());
      button->setIcon(simple_icon(visible ? QStringLiteral("eye") : QStringLiteral("eyeOff"),
                                  visible ? QColor(228, 236, 246) : QColor(118, 126, 136)));
      button->setIconSize(QSize(15, 15));
      button->setFixedSize(20, 20);
      button->setFocusPolicy(Qt::NoFocus);
      button->setStyleSheet(button_style);
      button->setToolTip(controls_supported
                             ? (visible ? visible_tooltip : hidden_tooltip)
                             : preservation_tooltip);
      button->setEnabled(controls_supported && ancestors_visible);
      if (list_parent != nullptr) {
        button->installEventFilter(list_parent);
      }
    };
    const auto mask_pixmap = [](const SmartFilterMask& mask) {
      constexpr int kSize = 22;
      QImage image(kSize, kSize, QImage::Format_RGB888);
      image.fill(QColor(mask.default_color, mask.default_color, mask.default_color));
      if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
        for (int y = 0; y < kSize; ++y) {
          const auto source_y = std::clamp(
              static_cast<int>((static_cast<double>(y) / kSize) * mask.pixels.height()), 0,
              std::max(0, mask.pixels.height() - 1));
          for (int x = 0; x < kSize; ++x) {
            const auto source_x = std::clamp(
                static_cast<int>((static_cast<double>(x) / kSize) * mask.pixels.width()), 0,
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
      if (!mask.enabled) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(232, 70, 70), 2));
        painter.drawLine(QPoint(3, 3), QPoint(kSize - 4, kSize - 4));
        painter.drawLine(QPoint(kSize - 4, 3), QPoint(3, kSize - 4));
      }
      return pixmap;
    };
    const auto make_nested_row = [&](const QString& object_name, int extra_indent) {
      auto* nested_row = new QWidget(row);
      nested_row->setObjectName(object_name);
      nested_row->setFixedHeight(kSmartFilterRowHeight);
      nested_row->setAutoFillBackground(false);
      if (!controls_supported) {
        nested_row->setToolTip(preservation_tooltip);
      }
      if (list_parent != nullptr) {
        nested_row->installEventFilter(list_parent);
      }
      auto* nested_layout = new QHBoxLayout(nested_row);
      const auto indent = kLayerRowBaseIndent + 22 + kLayerRowHorizontalSpacing +
                          layer_tree_indent_width(depth) + kLayerFolderDisclosureWidth +
                          kLayerRowHorizontalSpacing + extra_indent;
      nested_layout->setContentsMargins(indent, 1, 8, 1);
      nested_layout->setSpacing(5);
      row_layout->addWidget(nested_row);
      return std::pair{nested_row, nested_layout};
    };

    auto [header, header_layout] =
        make_nested_row(QStringLiteral("layerSmartFiltersRow"), 0);
    auto* stack_visibility = new QToolButton(header);
    stack_visibility->setObjectName(QStringLiteral("layerSmartFiltersVisibilityButton"));
    configure_visibility_button(
        stack_visibility, smart_filters->enabled,
        QObject::tr("Smart Filters visible. Click to hide."),
        QObject::tr("Smart Filters hidden. Click to show."));
    QObject::connect(
        stack_visibility, &QToolButton::toggled, row,
        [parent, id = layer.id(), stack_visibility, controls_supported,
         preservation_tooltip, set_smart_filter_stack_enabled](bool checked) {
          stack_visibility->setIcon(simple_icon(
              checked ? QStringLiteral("eye") : QStringLiteral("eyeOff"),
              checked ? QColor(228, 236, 246) : QColor(118, 126, 136)));
          stack_visibility->setToolTip(
              controls_supported
                  ? (checked ? QObject::tr("Smart Filters visible. Click to hide.")
                             : QObject::tr("Smart Filters hidden. Click to show."))
                  : preservation_tooltip);
          if (set_smart_filter_stack_enabled) {
            QTimer::singleShot(0, parent,
                               [id, checked, set_smart_filter_stack_enabled] {
              set_smart_filter_stack_enabled(id, checked);
            });
          }
        });
    header_layout->addWidget(stack_visibility, 0, Qt::AlignVCenter);

    auto* header_label = new LayerRowElidingLabel(QObject::tr("Smart Filters"), header);
    header_label->setObjectName(QStringLiteral("layerSmartFiltersLabel"));
    header_label->setStyleSheet(QStringLiteral("background: transparent;"));
    auto header_font = header_label->font();
    header_font.setBold(true);
    header_label->setFont(header_font);
    header_label->setEnabled(ancestors_visible && layer.visible());
    if (list_parent != nullptr) {
      header_label->installEventFilter(list_parent);
    }
    header_layout->addWidget(header_label, 1, Qt::AlignVCenter);

    if (!smart_filters->mask.pixels.empty()) {
      auto* smart_filter_mask_thumbnail = new QLabel(header);
      smart_filter_mask_thumbnail->setObjectName(QStringLiteral("layerSmartFilterMaskThumbnail"));
      smart_filter_mask_thumbnail->setFixedSize(22, 22);
      smart_filter_mask_thumbnail->setPixmap(mask_pixmap(smart_filters->mask));
      smart_filter_mask_thumbnail->setStyleSheet(QStringLiteral("background: transparent;"));
      smart_filter_mask_thumbnail->setToolTip(
          controls_supported && smart_filter_mask_size_supported
              ? QObject::tr(
                    "Shared Smart Filter mask. Click to edit it, Ctrl-click to load it as a selection, Alt-click to view it, or Shift-click to disable it.")
              : smart_filter_mask_size_supported
                    ? preservation_tooltip
                    : QObject::tr(
                          "This Smart Filter mask can only be preserved, not edited"));
      smart_filter_mask_thumbnail->setProperty(
          "layerTargetActive", smart_filter_mask_target_active);
      smart_filter_mask_thumbnail->setEnabled(controls_supported &&
                                              smart_filter_mask_size_supported &&
                                              ancestors_visible &&
                                              layer.visible());
      if (list_parent != nullptr) {
        smart_filter_mask_thumbnail->installEventFilter(list_parent);
      }
      header_layout->addWidget(smart_filter_mask_thumbnail, 0, Qt::AlignVCenter);
    }

    for (std::size_t execution_index = smart_filters->entries.size(); execution_index-- > 0;) {
      const auto& entry = smart_filters->entries[execution_index];
      auto [entry_row, entry_layout] =
          make_nested_row(QStringLiteral("layerSmartFilterEntryRow"), 14);
      entry_row->setProperty("smartFilterExecutionIndex",
                             QVariant::fromValue<qulonglong>(
                                 static_cast<qulonglong>(execution_index)));

      auto* entry_visibility = new QToolButton(entry_row);
      entry_visibility->setObjectName(QStringLiteral("layerSmartFilterVisibilityButton"));
      entry_visibility->setProperty("smartFilterExecutionIndex",
                                    QVariant::fromValue<qulonglong>(
                                        static_cast<qulonglong>(execution_index)));
      configure_visibility_button(
          entry_visibility, entry.enabled,
          QObject::tr("Smart Filter visible. Click to hide."),
          QObject::tr("Smart Filter hidden. Click to show."));
      QObject::connect(
          entry_visibility, &QToolButton::toggled, row,
          [parent, id = layer.id(), execution_index, entry_visibility,
           controls_supported, preservation_tooltip,
           set_smart_filter_enabled](bool checked) {
            entry_visibility->setIcon(simple_icon(
                checked ? QStringLiteral("eye") : QStringLiteral("eyeOff"),
                checked ? QColor(228, 236, 246) : QColor(118, 126, 136)));
            entry_visibility->setToolTip(
                controls_supported
                    ? (checked ? QObject::tr("Smart Filter visible. Click to hide.")
                               : QObject::tr("Smart Filter hidden. Click to show."))
                    : preservation_tooltip);
            if (set_smart_filter_enabled) {
              QTimer::singleShot(0, parent,
                                 [id, execution_index, checked,
                                  set_smart_filter_enabled] {
                set_smart_filter_enabled(id, execution_index, checked);
              });
            }
          });
      entry_layout->addWidget(entry_visibility, 0, Qt::AlignVCenter);

      QString entry_name;
      QString entry_tooltip;
      if (entry.kind == SmartFilterKind::GaussianBlur) {
        entry_name = QObject::tr("Gaussian Blur");
        entry_tooltip = entry_name;
        if (const auto* gaussian =
                std::get_if<GaussianBlurSmartFilter>(&entry.parameters);
            gaussian != nullptr) {
          auto radius = QString::number(gaussian->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip += QObject::tr(" (%1 px)").arg(radius);
        }
      } else if (entry.kind == SmartFilterKind::HighPass) {
        entry_name = QObject::tr("High Pass");
        entry_tooltip = entry_name;
        if (const auto* high_pass =
                std::get_if<HighPassSmartFilter>(&entry.parameters);
            high_pass != nullptr) {
          auto radius = QString::number(high_pass->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip += QObject::tr(" (%1 px)").arg(radius);
        }
      } else if (entry.kind == SmartFilterKind::Median) {
        entry_name = QObject::tr("Median");
        entry_tooltip = entry_name;
        if (const auto* median =
                std::get_if<MedianSmartFilter>(&entry.parameters);
            median != nullptr) {
          auto radius = QString::number(median->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip += QObject::tr(" (%1 px)").arg(radius);
        }
      } else if (entry.kind == SmartFilterKind::DustAndScratches) {
        entry_name = QObject::tr("Dust & Scratches");
        entry_tooltip = entry_name;
        if (const auto* dust =
                std::get_if<DustAndScratchesSmartFilter>(&entry.parameters);
            dust != nullptr) {
          entry_tooltip += QObject::tr(" (Radius %1 px, Threshold %2)")
                               .arg(dust->radius_pixels)
                               .arg(dust->threshold);
        }
      } else if (entry.kind == SmartFilterKind::SurfaceBlur) {
        entry_name = QObject::tr("Surface Blur");
        entry_tooltip = entry_name;
        if (const auto* surface =
                std::get_if<SurfaceBlurSmartFilter>(&entry.parameters);
            surface != nullptr) {
          auto radius = QString::number(surface->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip += QObject::tr(" (Radius %1 px, Threshold %2)")
                               .arg(radius)
                               .arg(surface->threshold);
        }
      } else if (entry.kind == SmartFilterKind::UnsharpMask) {
        entry_name = QObject::tr("Unsharp Mask");
        entry_tooltip = entry_name;
        if (const auto *unsharp =
                std::get_if<UnsharpMaskSmartFilter>(&entry.parameters);
            unsharp != nullptr) {
          auto radius = QString::number(unsharp->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip +=
              QObject::tr(" (Amount %1%, Radius %2 px, Threshold %3)")
                  .arg(QString::number(unsharp->amount_percent, 'g', 6))
                  .arg(radius)
                  .arg(unsharp->threshold);
        }
      } else if (entry.kind == SmartFilterKind::MotionBlur) {
        entry_name = QObject::tr("Motion Blur");
        entry_tooltip = entry_name;
        if (const auto *motion =
                std::get_if<MotionBlurSmartFilter>(&entry.parameters);
            motion != nullptr) {
          entry_tooltip += QObject::tr(" (Angle %1 degrees, Distance %2 px)")
                               .arg(motion->angle_degrees)
                               .arg(motion->distance_pixels);
        }
      } else if (entry.kind == SmartFilterKind::PlasticWrap) {
        entry_name = QObject::tr("Plastic Wrap");
        entry_tooltip = entry_name;
        if (const auto *plastic =
                std::get_if<PlasticWrapSmartFilter>(&entry.parameters);
            plastic != nullptr) {
          entry_tooltip +=
              QObject::tr(" (Highlight %1, Detail %2, Smoothness %3)")
                  .arg(plastic->highlight_strength)
                  .arg(plastic->detail)
                  .arg(plastic->smoothness);
        }
      } else if (entry.kind == SmartFilterKind::Mosaic) {
        entry_name = QObject::tr("Mosaic");
        entry_tooltip = entry_name;
        if (const auto *mosaic =
                std::get_if<MosaicSmartFilter>(&entry.parameters);
            mosaic != nullptr) {
          entry_tooltip += QObject::tr(" (Cell Size %1 px)")
                               .arg(mosaic->cell_size_pixels);
        }
      } else if (entry.kind == SmartFilterKind::Emboss) {
        entry_name = QObject::tr("Emboss");
        entry_tooltip = entry_name;
        if (const auto *emboss =
                std::get_if<EmbossSmartFilter>(&entry.parameters);
            emboss != nullptr) {
          entry_tooltip +=
              QObject::tr(" (Angle %1 degrees, Height %2 px, Amount %3%)")
                  .arg(emboss->angle_degrees)
                  .arg(emboss->height_pixels)
                  .arg(emboss->amount_percent);
        }
      } else if (entry.kind == SmartFilterKind::BoxBlur) {
        entry_name = QObject::tr("Box Blur");
        entry_tooltip = entry_name;
        if (const auto *box =
                std::get_if<BoxBlurSmartFilter>(&entry.parameters);
            box != nullptr) {
          auto radius = QString::number(box->radius_pixels, 'f', 2);
          while (radius.endsWith(QLatin1Char('0'))) {
            radius.chop(1);
          }
          if (radius.endsWith(QLatin1Char('.'))) {
            radius.chop(1);
          }
          entry_tooltip += QObject::tr(" (%1 px)").arg(radius);
        }
      } else if (!entry.native_name.empty()) {
        entry_name = QString::fromStdString(entry.native_name);
        entry_tooltip = entry_name;
      } else {
        entry_name = QObject::tr("Unsupported Smart Filter");
        entry_tooltip = entry_name;
      }
      auto* entry_label = new LayerRowElidingLabel(entry_name, entry_row);
      entry_label->setObjectName(QStringLiteral("layerSmartFilterEntryLabel"));
      entry_label->setProperty("smartFilterExecutionIndex",
                               QVariant::fromValue<qulonglong>(
                                   static_cast<qulonglong>(execution_index)));
      entry_label->setStyleSheet(QStringLiteral("background: transparent;"));
      entry_label->setEnabled(ancestors_visible && layer.visible() && entry.enabled &&
                              smart_filters->enabled);
      entry_label->setToolTip(controls_supported ? entry_tooltip
                                                 : preservation_tooltip);
      if (list_parent != nullptr) {
        entry_label->installEventFilter(list_parent);
      }
      entry_layout->addWidget(entry_label, 1, Qt::AlignVCenter);

      auto* edit_button = new QToolButton(entry_row);
      edit_button->setObjectName(QStringLiteral("layerSmartFilterEditButton"));
      edit_button->setProperty("smartFilterExecutionIndex",
                               QVariant::fromValue<qulonglong>(
                                   static_cast<qulonglong>(execution_index)));
      edit_button->setIcon(simple_icon(QStringLiteral("RN")));
      edit_button->setIconSize(QSize(15, 15));
      edit_button->setFixedSize(20, 20);
      edit_button->setFocusPolicy(Qt::NoFocus);
      edit_button->setStyleSheet(button_style);
      edit_button->setToolTip(controls_supported ? QObject::tr("Edit Smart Filter")
                                                  : preservation_tooltip);
      edit_button->setEnabled(controls_supported && ancestors_visible);
      if (list_parent != nullptr) {
        edit_button->installEventFilter(list_parent);
      }
      QObject::connect(edit_button, &QToolButton::clicked, row,
                       [parent, id = layer.id(), execution_index,
                        edit_smart_filter] {
        if (edit_smart_filter) {
          QTimer::singleShot(0, parent, [id, execution_index, edit_smart_filter] {
            edit_smart_filter(id, execution_index);
          });
        }
      });
      entry_layout->addWidget(edit_button, 0, Qt::AlignVCenter);

      auto* more_button = new QToolButton(entry_row);
      more_button->setObjectName(QStringLiteral("layerSmartFilterMoreButton"));
      more_button->setProperty("smartFilterExecutionIndex",
                               QVariant::fromValue<qulonglong>(
                                   static_cast<qulonglong>(execution_index)));
      more_button->setText(QString::fromUtf8("\xE2\x8B\xAF"));
      auto more_font = more_button->font();
      more_font.setBold(true);
      more_font.setPointSize(std::max(11, more_font.pointSize()));
      more_button->setFont(more_font);
      more_button->setFixedSize(20, 20);
      more_button->setFocusPolicy(Qt::NoFocus);
      more_button->setStyleSheet(
          button_style +
          QStringLiteral(" QToolButton::menu-indicator { image: none; width: 0px; }"));
      more_button->setToolTip(controls_supported
                                  ? QObject::tr("Smart Filter actions")
                                  : preservation_tooltip);
      more_button->setEnabled(controls_supported && ancestors_visible);
      more_button->setPopupMode(QToolButton::InstantPopup);
      if (list_parent != nullptr) {
        more_button->installEventFilter(list_parent);
      }

      auto* action_menu = new QMenu(more_button);
      more_button->setMenu(action_menu);
      const auto configure_action = [&](QAction* action,
                                        const QString& object_name) {
        action->setObjectName(object_name);
        action->setProperty(
            "smartFilterExecutionIndex",
            QVariant::fromValue<qulonglong>(
                static_cast<qulonglong>(execution_index)));
      };

      auto* blending_action =
          action_menu->addAction(QObject::tr("Edit Blending Options..."));
      configure_action(blending_action,
                       QStringLiteral("layerSmartFilterBlendingAction"));
      blending_action->setEnabled(controls_supported && ancestors_visible);
      QObject::connect(
          blending_action, &QAction::triggered, row,
          [parent, id = layer.id(), execution_index,
           edit_smart_filter_blending] {
            if (edit_smart_filter_blending) {
              QTimer::singleShot(
                  0, parent,
                  [id, execution_index, edit_smart_filter_blending] {
                    edit_smart_filter_blending(id, execution_index);
                  });
            }
          });

      action_menu->addSeparator();

      auto* duplicate_action =
          action_menu->addAction(QObject::tr("Duplicate Smart Filter"));
      configure_action(duplicate_action,
                       QStringLiteral("layerSmartFilterDuplicateAction"));
      duplicate_action->setEnabled(controls_supported && ancestors_visible);
      QObject::connect(
          duplicate_action, &QAction::triggered, row,
          [parent, id = layer.id(), execution_index,
           duplicate_smart_filter] {
            if (duplicate_smart_filter) {
              QTimer::singleShot(
                  0, parent,
                  [id, execution_index, duplicate_smart_filter] {
                    duplicate_smart_filter(id, execution_index);
                  });
            }
          });

      auto* move_up_action =
          action_menu->addAction(QObject::tr("Move Smart Filter up"));
      configure_action(move_up_action,
                       QStringLiteral("layerSmartFilterMoveUpAction"));
      move_up_action->setEnabled(
          controls_supported && ancestors_visible &&
          execution_index + 1U < smart_filters->entries.size());
      QObject::connect(
          move_up_action, &QAction::triggered, row,
          [parent, id = layer.id(), execution_index, move_smart_filter] {
            if (move_smart_filter) {
              QTimer::singleShot(0, parent,
                                 [id, execution_index, move_smart_filter] {
                move_smart_filter(id, execution_index, -1);
              });
            }
          });

      auto* move_down_action =
          action_menu->addAction(QObject::tr("Move Smart Filter down"));
      configure_action(move_down_action,
                       QStringLiteral("layerSmartFilterMoveDownAction"));
      move_down_action->setEnabled(controls_supported && ancestors_visible &&
                                   execution_index > 0U);
      QObject::connect(
          move_down_action, &QAction::triggered, row,
          [parent, id = layer.id(), execution_index, move_smart_filter] {
            if (move_smart_filter) {
              QTimer::singleShot(0, parent,
                                 [id, execution_index, move_smart_filter] {
                move_smart_filter(id, execution_index, 1);
              });
            }
          });

      action_menu->addSeparator();
      auto* delete_action =
          action_menu->addAction(QObject::tr("Delete Smart Filter"));
      configure_action(delete_action,
                       QStringLiteral("layerSmartFilterDeleteAction"));
      delete_action->setEnabled(controls_supported && ancestors_visible);
      QObject::connect(delete_action, &QAction::triggered, row,
                       [parent, id = layer.id(), execution_index,
                        delete_smart_filter] {
        if (delete_smart_filter) {
          QTimer::singleShot(0, parent,
                             [id, execution_index, delete_smart_filter] {
            delete_smart_filter(id, execution_index);
          });
        }
      });
      entry_layout->addWidget(more_button, 0, Qt::AlignVCenter);
    }
  }
  // Hover moves must reach the list's event filter for the Alt-hover clip
  // boundary cursor; untracked children would swallow them.
  row->setMouseTracking(true);
  for (auto* child : row->findChildren<QWidget*>()) {
    child->setMouseTracking(true);
  }
  return row;
}

}  // namespace

bool MainWindow::handle_layer_action_button_drag_event(QObject* watched, QEvent* event) {
  auto* button = qobject_cast<QWidget*>(watched);
  if (button == nullptr || !button->property("layerDropAction").isValid()) {
    return false;
  }

  if (preview_dialog_edit_locked()) {
    if (auto* drop_event = dynamic_cast<QDropEvent*>(event); drop_event != nullptr) {
      // Color drags (the color picker's swatch drag-and-drop) never touch the
      // document; let them through the lock so e.g. a layer-style picker's
      // custom slots stay droppable. Layer and file drags stay blocked.
      if (drop_event->mimeData() != nullptr && drop_event->mimeData()->hasColor()) {
        return false;
      }
      drop_event->ignore();
    }
    return event != nullptr && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
                                event->type() == QEvent::Drop || event->type() == QEvent::DragLeave);
  }

  const auto set_drop_active = [button](bool active) {
    button->setProperty("layerDropActive", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
  };
  const auto hide_tooltip = [] {
    QToolTip::hideText();
  };
  const auto show_tooltip = [button] {
    if (!button->toolTip().isEmpty()) {
      QToolTip::showText(button->mapToGlobal(button->rect().center()), button->toolTip(), button);
    }
  };

  if (event->type() == QEvent::DragLeave) {
    set_drop_active(false);
    hide_tooltip();
    event->accept();
    return true;
  }

  if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop) {
    return false;
  }

  auto* drop_event = static_cast<QDropEvent*>(event);
  auto ids = layer_ids_from_mime_data(drop_event->mimeData());
  if (ids.empty()) {
    set_drop_active(false);
    hide_tooltip();
    return false;
  }

  drop_event->setDropAction(Qt::MoveAction);
  drop_event->accept();
  if (event->type() == QEvent::Drop) {
    set_drop_active(false);
    hide_tooltip();
    if (button->property("layerDropAction").toString() == QStringLiteral("delete")) {
      delete_layers(std::move(ids));
    } else if (button->property("layerDropAction").toString() == QStringLiteral("folder")) {
      create_layer_folder_from_layers(std::move(ids));
    } else {
      duplicate_layers(std::move(ids));
    }
  } else {
    set_drop_active(true);
    show_tooltip();
  }
  return true;
}

void MainWindow::handle_layer_drop() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
    return;
  }
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
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
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
      update_layer_visibility_button(visibility, visible);
    }
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      name->setEnabled(visible);
    }
    if (auto* details = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")); details != nullptr) {
      details->setEnabled(visible);
    }
    if (auto* fx_badge = row_widget->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
        fx_badge != nullptr) {
      fx_badge->setEnabled(visible);
    }
    if (auto* smart_badge = row_widget->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
        smart_badge != nullptr) {
      smart_badge->setEnabled(visible);
    }
  }
  canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
  refresh_layer_controls();
  if (is_group) {
    refresh_layer_list();
  } else {
    restyle_layer_rows(layer_list_);
  }
  statusBar()->showMessage(visible ? tr("Layer shown") : tr("Layer hidden"));
}

void MainWindow::refresh_layer_list() {
  if (layer_list_ == nullptr) {
    return;
  }
  const auto scroll_value = layer_list_->verticalScrollBar() != nullptr ? layer_list_->verticalScrollBar()->value() : 0;
  const auto horizontal_scroll_value =
      layer_list_->horizontalScrollBar() != nullptr ? layer_list_->horizontalScrollBar()->value() : 0;
  updating_layer_list_ = true;
  QSignalBlocker blocker(layer_list_);
  layer_list_->clear();
  if (!has_active_document()) {
    updating_layer_list_ = false;
    return;
  }

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
  const auto edit_target =
      canvas_ != nullptr ? canvas_->layer_edit_target() : CanvasWidget::LayerEditTarget::Content;
  int row_to_select = -1;
  std::function<void(const std::vector<Layer>&, int, bool, LayerLockFlags)> append_layers =
      [&](const std::vector<Layer>& layers, int depth, bool ancestors_visible, LayerLockFlags ancestor_lock_flags) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto effective_visible = ancestors_visible && it->visible();
      const auto direct_lock_flags = layer_lock_flags(*it);
      const auto effective_lock_flags = ancestor_lock_flags | direct_lock_flags;
      const auto is_group = it->kind() == LayerKind::Group;
      const auto group_expanded = !is_group || !collapsed_groups.contains(it->id());
      const auto sibling_index = static_cast<std::size_t>(std::distance(layers.begin(), it.base())) - 1U;
      const auto row_clipped =
          !is_group && it->clipped() && effective_clip_base(layers, sibling_index) != nullptr;
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
      item->setToolTip(tr("%1\n%2% opacity%3%4%5%6%7%8")
                           .arg(QString::fromStdString(it->name()))
                           .arg(std::round(it->opacity() * 100.0F))
                           .arg(!ancestors_visible
                                    ? tr("\nHidden by parent folder")
                                    : QString())
                           .arg(effective_lock_flags != kLayerLockNone ? tr("\nLocked") : QString())
                           .arg(layer_locks_transparent_pixels(*it) ? tr("\nTransparent pixels locked") : QString())
                           .arg(it->mask().has_value() ? tr("\nLayer mask") : QString())
                           .arg(folder_detail)
                           .arg(row_clipped ? tr("\nClipped to the layer below") : QString()));
      const auto* smart_filters = it->smart_filter_stack();
      const auto smart_filter_row_count =
          smart_filters != nullptr
              ? 1 + static_cast<int>(smart_filters->entries.size())
              : 0;
      item->setSizeHint(QSize(0, 44 + 26 * smart_filter_row_count));
      item->setForeground(effective_visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
      if (active.has_value() && *active == it->id()) {
        auto font = item->font();
        font.setBold(true);
        item->setFont(font);
        row_to_select = layer_list_->row(item);
      }
      layer_list_->setItemWidget(
          item, make_layer_row_widget(*it, item, layer_list_,
                                      [this](const Layer& row_layer) { return cached_layer_content_thumbnail(row_layer); },
                                      [this](const Layer& row_layer) { return cached_layer_mask_thumbnail(row_layer); },
                                      depth, ancestors_visible, group_expanded, ancestor_lock_flags,
                                      [this](LayerId layer_id) { toggle_layer_folder_expanded(layer_id); },
                                      [this](LayerId layer_id, bool linked) {
        if (auto* layer = document().find_layer(layer_id); layer != nullptr && layer->mask().has_value()) {
          document().set_active_layer(layer_id);
          set_active_layer_mask_linked(linked);
        }
      },
                                      active.has_value() && *active == it->id() &&
                                          edit_target == CanvasWidget::LayerEditTarget::Content,
                                      active.has_value() && *active == it->id() &&
                                          edit_target == CanvasWidget::LayerEditTarget::Mask,
                                      active.has_value() && *active == it->id() &&
                                          edit_target == CanvasWidget::LayerEditTarget::SmartFilterMask,
                                      smart_filter_mask_document_editing_supported(
                                          document().width(), document().height()),
                                      [this](LayerId layer_id) {
        if (!has_active_document()) {
          return;
        }
        const auto* badge_layer = document().find_layer(layer_id);
        if (badge_layer == nullptr || badge_layer->kind() == LayerKind::Group) {
          // Matches the row double-click rule: no blending dialog for folders.
          return;
        }
        reveal_layer_in_layer_list(layer_id);
        if (document().active_layer_id() != layer_id) {
          document().set_active_layer(layer_id);
        }
        edit_active_layer_style();
      },
                                      [this](LayerId layer_id) {
        if (!has_active_document() || document().find_layer(layer_id) == nullptr) {
          return;
        }
        reveal_layer_in_layer_list(layer_id);
        if (document().active_layer_id() != layer_id) {
          document().set_active_layer(layer_id);
        }
        open_smart_object_contents();
      },
                                      row_clipped,
                                      [this](LayerId layer_id) {
        if (!has_active_document() || document().find_layer(layer_id) == nullptr) {
          return;
        }
        reveal_layer_in_layer_list(layer_id);
        if (document().active_layer_id() != layer_id) {
          document().set_active_layer(layer_id);
        }
        toggle_active_layer_clipping();
      },
                                      [this](LayerId layer_id, bool enabled) {
        set_smart_filter_stack_enabled(layer_id, enabled);
      },
                                      [this](LayerId layer_id, std::size_t execution_index,
                                             bool enabled) {
        set_smart_filter_enabled(layer_id, execution_index, enabled);
      },
                                      [this](LayerId layer_id, std::size_t execution_index) {
        edit_smart_filter(layer_id, execution_index);
      },
                                      [this](LayerId layer_id, std::size_t execution_index) {
        edit_smart_filter_blending(layer_id, execution_index);
      },
                                      [this](LayerId layer_id, std::size_t execution_index) {
        duplicate_smart_filter(layer_id, execution_index);
      },
                                      [this](LayerId layer_id, std::size_t execution_index,
                                             int visual_direction) {
        move_smart_filter(layer_id, execution_index, visual_direction);
      },
                                      [this](LayerId layer_id, std::size_t execution_index) {
        delete_smart_filter(layer_id, execution_index);
      }));
      if (is_group && group_expanded) {
        append_layers(it->children(), depth + 1, effective_visible, effective_lock_flags);
      }
    }
  };
  append_layers(doc.layers(), 0, true, kLayerLockNone);

  if (row_to_select >= 0) {
    layer_list_->setCurrentRow(row_to_select);
  } else if (auto* selection_model = layer_list_->selectionModel(); selection_model != nullptr) {
    selection_model->clear();
  }
  updating_layer_list_ = false;
  if (canvas_ != nullptr) {
    canvas_->set_selected_layer_ids(selected_layer_ids());
  }
  restyle_layer_rows(layer_list_);
  if (auto* list = dynamic_cast<LayerListWidget*>(layer_list_); list != nullptr) {
    list->refresh_row_widths();
  }
  if (auto* scroll_bar = layer_list_->verticalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  if (auto* scroll_bar = layer_list_->horizontalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(horizontal_scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  layer_list_->viewport()->update();
  layer_list_->viewport()->repaint();
}

QPixmap MainWindow::cached_layer_content_thumbnail(const Layer& layer) {
  auto& entry = layer_thumbnail_cache_[layer.id()];
  const auto revision = layer.content_revision();
  if (entry.content.isNull() || entry.content_revision != revision) {
    entry.content = layer_content_thumbnail(layer);
    entry.content_revision = revision;
  }
  return entry.content;
}

QPixmap MainWindow::cached_layer_mask_thumbnail(const Layer& layer) {
  if (!layer.mask().has_value()) {
    return {};
  }
  auto& entry = layer_thumbnail_cache_[layer.id()];
  const auto revision = layer.content_revision();
  if (entry.mask.isNull() || entry.mask_revision != revision) {
    entry.mask = layer_mask_thumbnail(*layer.mask());
    entry.mask_revision = revision;
  }
  return entry.mask;
}

void MainWindow::refresh_layer_thumbnails() {
  const auto started = std::chrono::steady_clock::now();
  if (layer_list_ == nullptr || !has_active_document()) {
    return;
  }
  const auto& doc = document();
  int content_refreshed = 0;
  int content_skipped = 0;
  int mask_refreshed = 0;
  int mask_skipped = 0;
  std::set<LayerId> live_layer_ids;
  for (int row_index = 0; row_index < layer_list_->count(); ++row_index) {
    auto* item = layer_list_->item(row_index);
    if (item == nullptr) {
      continue;
    }
    const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    live_layer_ids.insert(layer_id);
    const auto* layer = doc.find_layer(layer_id);
    auto* row = layer_list_->itemWidget(item);
    if (layer == nullptr || row == nullptr) {
      continue;
    }
    const auto revision = static_cast<qulonglong>(layer->content_revision());
    if (auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")); thumbnail != nullptr &&
        (layer->kind() == LayerKind::Pixel || layer_is_text(*layer))) {
      if (thumbnail->property(kLayerContentThumbnailRevisionProperty).toULongLong() != revision) {
        thumbnail->setPixmap(cached_layer_content_thumbnail(*layer));
        thumbnail->setProperty(kLayerContentThumbnailRevisionProperty, QVariant::fromValue<qulonglong>(revision));
        ++content_refreshed;
      } else {
        ++content_skipped;
      }
    }
    if (auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
        mask_thumbnail != nullptr && layer->mask().has_value()) {
      if (mask_thumbnail->property(kLayerMaskThumbnailRevisionProperty).toULongLong() != revision) {
        mask_thumbnail->setPixmap(cached_layer_mask_thumbnail(*layer));
        mask_thumbnail->setProperty(kLayerMaskThumbnailRevisionProperty, QVariant::fromValue<qulonglong>(revision));
        ++mask_refreshed;
      } else {
        ++mask_skipped;
      }
    }
  }
  // Collapsed folders keep their children out of the list; only prune cache
  // entries whose layer no longer exists in the document at all.
  std::erase_if(layer_thumbnail_cache_, [&](const auto& cache_entry) {
    return !live_layer_ids.contains(cache_entry.first) && doc.find_layer(cache_entry.first) == nullptr;
  });
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  std::ostringstream detail;
  detail << "content_refreshed=" << content_refreshed << " content_skipped=" << content_skipped
         << " mask_refreshed=" << mask_refreshed << " mask_skipped=" << mask_skipped;
  log_ui_profile("refresh_layer_thumbnails", elapsed, detail.str());
}

void MainWindow::refresh_layer_controls() {
  refresh_convert_for_smart_filters_action_state();
  if (!updating_layer_controls_) {
    finish_pending_layer_opacity_edit();
    finish_pending_layer_fill_opacity_edit();
  }
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
    if (fill_opacity_slider_ != nullptr) {
      fill_opacity_slider_->setValue(100);
      fill_opacity_slider_->setEnabled(false);
    }
    if (fill_opacity_spin_ != nullptr) {
      fill_opacity_spin_->setValue(100);
      fill_opacity_spin_->setEnabled(false);
    }
    if (blend_combo_ != nullptr) {
      blend_combo_->setCurrentIndex(0);
    }
    set_layer_lock_control_state(lock_transparent_pixels_button_, false, false, tr("Lock transparent pixels"));
    set_layer_lock_control_state(lock_image_pixels_button_, false, false, tr("Lock image pixels"));
    set_layer_lock_control_state(lock_position_button_, false, false, tr("Lock position"));
    set_layer_lock_control_state(lock_all_button_, false, false, tr("Lock all"));
    if (layer_blending_options_action_ != nullptr) {
      layer_blending_options_action_->setEnabled(false);
    }
    refresh_layer_style_action_states();
    if (layer_rasterize_action_ != nullptr) {
      layer_rasterize_action_->setEnabled(false);
    }
    if (layer_rasterize_layer_style_action_ != nullptr) {
      layer_rasterize_layer_style_action_->setEnabled(false);
    }
    if (delete_layer_mask_action_ != nullptr) {
      delete_layer_mask_action_->setEnabled(false);
    }
    if (link_layer_mask_action_ != nullptr) {
      link_layer_mask_action_->setEnabled(false);
      link_layer_mask_action_->setChecked(true);
    }
    if (disable_layer_mask_action_ != nullptr) {
      disable_layer_mask_action_->setEnabled(false);
      disable_layer_mask_action_->setChecked(false);
    }
    if (invert_layer_mask_action_ != nullptr) {
      invert_layer_mask_action_->setEnabled(false);
    }
    if (apply_layer_mask_action_ != nullptr) {
      apply_layer_mask_action_->setEnabled(false);
    }
    refresh_layer_clipping_action_state();
    if (edit_layer_mask_action_ != nullptr) {
      edit_layer_mask_action_->setEnabled(false);
      edit_layer_mask_action_->setChecked(false);
    }
    if (mask_overlay_action_ != nullptr) {
      mask_overlay_action_->setEnabled(false);
      mask_overlay_action_->setChecked(false);
    }
    if (view_layer_mask_action_ != nullptr) {
      view_layer_mask_action_->setEnabled(false);
      view_layer_mask_action_->setChecked(false);
    }
    if (mask_edit_mode_chip_ != nullptr) {
      mask_edit_mode_chip_->setVisible(false);
    }
  };

  if (!has_active_document()) {
    reset();
    updating_layer_controls_ = false;
    refresh_document_info();
    return;
  }

  const auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    reset();
    updating_layer_controls_ = false;
    refresh_document_info();
    return;
  }

  const auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    reset();
    updating_layer_controls_ = false;
    refresh_document_info();
    return;
  }

  if (opacity_slider_ != nullptr) {
    opacity_slider_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  if (opacity_spin_ != nullptr) {
    opacity_spin_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  const bool fill_enabled = layer->kind() != LayerKind::Group;
  if (fill_opacity_slider_ != nullptr) {
    fill_opacity_slider_->setValue(static_cast<int>(std::round(layer->fill_opacity() * 100.0F)));
    fill_opacity_slider_->setEnabled(fill_enabled);
  }
  if (fill_opacity_spin_ != nullptr) {
    fill_opacity_spin_->setValue(static_cast<int>(std::round(layer->fill_opacity() * 100.0F)));
    fill_opacity_spin_->setEnabled(fill_enabled);
  }
  if (visible_check_ != nullptr) {
    visible_check_->setChecked(layer->visible());
  }
  if (blend_combo_ != nullptr) {
    const auto blend_value = static_cast<int>(layer->blend_mode());
    const auto index = blend_combo_->findData(blend_value);
    blend_combo_->setCurrentIndex(index >= 0 ? index : 0);
  }
  const auto ids_for_lock_controls = selected_or_active_layer_ids();
  const auto lock_state_for_flag = [this, &ids_for_lock_controls](LayerLockFlags flag) {
    bool any = false;
    bool all = !ids_for_lock_controls.empty();
    for (const auto id : ids_for_lock_controls) {
      const auto* selected_layer = document().find_layer(id);
      const bool locked = selected_layer != nullptr && (layer_lock_flags(*selected_layer) & flag) == flag;
      any = any || locked;
      all = all && locked;
    }
    return std::pair<bool, bool>{all, any && !all};
  };
  const auto [transparent_all, transparent_mixed] = lock_state_for_flag(kLayerLockTransparentPixels);
  const auto [image_all, image_mixed] = lock_state_for_flag(kLayerLockImagePixels);
  const auto [position_all, position_mixed] = lock_state_for_flag(kLayerLockPosition);
  bool any_lock = false;
  bool all_lock = !ids_for_lock_controls.empty();
  for (const auto id : ids_for_lock_controls) {
    const auto* selected_layer = document().find_layer(id);
    const auto flags = selected_layer != nullptr ? layer_lock_flags(*selected_layer) : kLayerLockNone;
    any_lock = any_lock || flags != kLayerLockNone;
    all_lock = all_lock && (flags & kLayerLockAll) == kLayerLockAll;
  }
  set_layer_lock_control_state(lock_transparent_pixels_button_, transparent_all, transparent_mixed,
                               tr("Lock transparent pixels"));
  set_layer_lock_control_state(lock_image_pixels_button_, image_all, image_mixed, tr("Lock image pixels"));
  set_layer_lock_control_state(lock_position_button_, position_all, position_mixed, tr("Lock position"));
  set_layer_lock_control_state(lock_all_button_, all_lock, any_lock && !all_lock, tr("Lock all"));
  const auto edit_allowed = !preview_dialog_edit_locked();
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(edit_allowed);
  }
  refresh_layer_style_action_states();
  const auto active_pixels_locked = layer_id_locks_image_pixels(layer->id());
  if (layer_rasterize_action_ != nullptr) {
    layer_rasterize_action_->setEnabled(edit_allowed && !active_pixels_locked && layer_can_rasterize(*layer));
  }
  if (layer_rasterize_layer_style_action_ != nullptr) {
    layer_rasterize_layer_style_action_->setEnabled(edit_allowed && !active_pixels_locked && layer_can_rasterize_layer_style(*layer));
  }
  if (delete_layer_mask_action_ != nullptr) {
    delete_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value());
  }
  if (link_layer_mask_action_ != nullptr) {
    link_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value());
    link_layer_mask_action_->setChecked(layer_mask_linked(*layer));
  }
  if (disable_layer_mask_action_ != nullptr) {
    const auto* smart_filters = std::as_const(*layer).smart_filter_stack();
    const auto has_editable_smart_mask =
        smart_filters != nullptr &&
        smart_filters->support == SmartFilterStackSupport::Supported &&
        !smart_filters->mask.pixels.empty() &&
        smart_filter_mask_document_editing_supported(
            document().width(), document().height());
    const auto editing_smart_mask =
        canvas_ != nullptr && canvas_->editing_smart_filter_mask() &&
        canvas_->smart_filter_mask_owner_id() == layer->id();
    disable_layer_mask_action_->setEnabled(
        edit_allowed && !active_pixels_locked &&
        (layer->mask().has_value() || has_editable_smart_mask));
    disable_layer_mask_action_->setChecked(
        editing_smart_mask ? !smart_filters->mask.enabled
                           : layer->mask().has_value() &&
                                 layer->mask()->disabled);
  }
  if (invert_layer_mask_action_ != nullptr) {
    const auto* smart_filters = std::as_const(*layer).smart_filter_stack();
    const auto has_editable_smart_mask =
        smart_filters != nullptr &&
        smart_filters->support == SmartFilterStackSupport::Supported &&
        !smart_filters->mask.pixels.empty() &&
        smart_filter_mask_document_editing_supported(
            document().width(), document().height());
    invert_layer_mask_action_->setEnabled(
        edit_allowed && !active_pixels_locked &&
        (layer->mask().has_value() || has_editable_smart_mask));
  }
  if (apply_layer_mask_action_ != nullptr) {
    apply_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value() && layer->kind() == LayerKind::Pixel);
  }
  refresh_layer_clipping_action_state();
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask &&
      !layer->mask().has_value()) {
    // The mask is gone (deleted, applied, or undone away) - fall back to editing pixels.
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
    update_layer_target_styles(layer_list_, active, CanvasWidget::LayerEditTarget::Content);
  }
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask &&
      !layer->mask().has_value() &&
      canvas_->mask_display_mode() != CanvasWidget::MaskDisplayMode::None) {
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
  }
  if (canvas_ != nullptr && canvas_->editing_smart_filter_mask()) {
    const auto owner = canvas_->smart_filter_mask_owner_id();
    const auto* owner_layer = owner.has_value()
                                  ? std::as_const(document()).find_layer(*owner)
                                  : nullptr;
    const auto* owner_stack = owner_layer != nullptr
                                  ? owner_layer->smart_filter_stack()
                                  : nullptr;
    const auto pixels = owner_stack != nullptr &&
                                owner_stack->support ==
                                    SmartFilterStackSupport::Supported
                            ? materialize_smart_filter_mask(
                                  owner_stack->mask, document().width(),
                                  document().height())
                            : std::nullopt;
    if (!owner.has_value() || !pixels.has_value() ||
        active != owner) {
      canvas_->clear_smart_filter_mask_edit_target();
      update_layer_target_styles(layer_list_, active,
                                 CanvasWidget::LayerEditTarget::Content);
    } else {
      static_cast<void>(canvas_->resync_smart_filter_mask_edit_target(
          *owner, std::move(*pixels)));
    }
  }
  const auto editing_layer_mask =
      canvas_ != nullptr && layer->mask().has_value() &&
      canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask;
  const auto* smart_filters = std::as_const(*layer).smart_filter_stack();
  const auto has_editable_smart_mask =
      smart_filters != nullptr &&
      smart_filters->support == SmartFilterStackSupport::Supported &&
      !smart_filters->mask.pixels.empty() &&
      smart_filter_mask_document_editing_supported(
          document().width(), document().height());
  const auto editing_smart_mask =
      canvas_ != nullptr && canvas_->editing_smart_filter_mask() &&
      canvas_->smart_filter_mask_owner_id() == layer->id();
  const auto editing_mask = editing_layer_mask || editing_smart_mask;
  if (edit_layer_mask_action_ != nullptr) {
    edit_layer_mask_action_->setEnabled(
        edit_allowed && (layer->mask().has_value() || has_editable_smart_mask));
    edit_layer_mask_action_->setChecked(editing_mask);
  }
  if (mask_overlay_action_ != nullptr) {
    mask_overlay_action_->setEnabled(layer->mask().has_value() ||
                                     has_editable_smart_mask);
    mask_overlay_action_->setChecked(canvas_ != nullptr && editing_mask &&
                                     canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Overlay);
  }
  if (view_layer_mask_action_ != nullptr) {
    view_layer_mask_action_->setEnabled(layer->mask().has_value() ||
                                        has_editable_smart_mask);
    view_layer_mask_action_->setChecked(canvas_ != nullptr && editing_mask &&
                                        canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale);
  }
  if (canvas_ != nullptr) {
    update_layer_target_styles(layer_list_, active, canvas_->layer_edit_target());
  }
  refresh_edit_target_chip();
  updating_layer_controls_ = false;
  refresh_document_info();
}

void MainWindow::refresh_document_info() {
  refresh_palette_panel();
  schedule_palette_compliance_check();
  if (zoom_status_edit_ != nullptr) {
    if (!has_active_document() || canvas_ == nullptr) {
      zoom_status_edit_->clear_display();
      zoom_status_edit_->setEnabled(false);
    } else {
      zoom_status_edit_->setEnabled(true);
      zoom_status_edit_->set_display_zoom(canvas_->zoom());
    }
  }
  if (document_info_label_ == nullptr) {
    return;
  }

  if (!has_active_document()) {
    set_property_label_text(document_info_label_, tr("No document"));
    set_property_label_text(active_layer_info_label_, tr("Layer: No active layer"));
    for (auto* label : {active_layer_geometry_label_, active_layer_mask_label_, active_layer_adjustment_label_,
                        active_layer_text_label_, active_tool_info_label_}) {
      set_property_label_text(label, QString());
    }
    if (canvas_info_label_ != nullptr) {
      canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
    }
    return;
  }

  const auto& doc = document();
  const auto& active_session = session();
  const auto zoom_percent = canvas_ == nullptr ? 100 : static_cast<int>(std::round(canvas_->zoom() * 100.0));
  // Physical size in the ruler unit (inches while the rulers are pixel/percent),
  // per-axis PPI so anisotropic documents report their true print size.
  const auto info_unit = measurement_unit_is_physical(ruler_unit_) ? ruler_unit_ : MeasurementUnit::Inches;
  const auto physical_width =
      pixels_to_measurement_unit(doc.width(), info_unit, doc.print_settings().horizontal_ppi, doc.width());
  const auto physical_height =
      pixels_to_measurement_unit(doc.height(), info_unit, doc.print_settings().vertical_ppi, doc.height());
  set_property_label_text(document_info_label_,
                          tr("Document: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 layers | Zoom %9% | %10")
                              .arg(doc.width())
                              .arg(doc.height())
                              .arg(physical_width, 0, 'f', 2)
                              .arg(physical_height, 0, 'f', 2)
                              .arg(measurement_unit_suffix(info_unit))
                              .arg(static_cast<int>(std::round(doc.print_settings().horizontal_ppi)))
                              .arg(pixel_format_name(doc.format()))
                              .arg(layer_tree_count(doc.layers()))
                              .arg(zoom_percent)
                              .arg(session_is_modified(active_session) ? tr("Unsaved changes") : tr("Saved")));

  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr) {
    set_property_label_text(active_layer_info_label_, tr("Layer: No active layer"));
    for (auto* label : {active_layer_geometry_label_, active_layer_mask_label_, active_layer_adjustment_label_,
                        active_layer_text_label_}) {
      set_property_label_text(label, QString());
    }
  } else {
    const auto effective_lock_flags = layer_id_effective_lock_flags(layer->id());
    QStringList lock_parts;
    if ((effective_lock_flags & kLayerLockTransparentPixels) != kLayerLockNone) {
      lock_parts << tr("transparent");
    }
    if ((effective_lock_flags & kLayerLockImagePixels) != kLayerLockNone) {
      lock_parts << tr("image pixels");
    }
    if ((effective_lock_flags & kLayerLockPosition) != kLayerLockNone) {
      lock_parts << tr("position");
    }
    const auto lock_state = lock_parts.isEmpty() ? QString() : tr(" | Locks: %1").arg(lock_parts.join(QStringLiteral(", ")));
    set_property_label_text(active_layer_info_label_,
                            tr("Layer: %1 | %2 | Mode: %3 | Opacity: %4% | %5%6")
                                .arg(QString::fromStdString(layer->name()))
                                .arg(layer_is_text(*layer) ? layer_kind_name(LayerKind::Text)
                                                           : layer_kind_name(layer->kind()))
                                .arg(blend_mode_name(layer->blend_mode()))
                                .arg(static_cast<int>(std::round(layer->opacity() * 100.0F)))
                                .arg(layer->visible() ? tr("Visible") : tr("Hidden"))
                                .arg(lock_state));
    QString geometry = tr("Geometry: Bounds: %1").arg(rect_summary(layer->bounds()));
    if (layer->kind() == LayerKind::Pixel || layer->kind() == LayerKind::Text) {
      geometry += tr(" | Pixels: %1").arg(pixel_format_name(layer->pixels().format()));
    } else if (layer->kind() == LayerKind::Group) {
      geometry += tr(" | Contents: %1 layers").arg(layer_descendant_count(*layer));
    }
    geometry += tr(" | Effects: %1").arg(layer_style_summary(layer->layer_style()));
    set_property_label_text(active_layer_geometry_label_, geometry);
    if (!layer->mask().has_value()) {
      set_property_label_text(active_layer_mask_label_, QString());
    } else {
      const auto& mask = *layer->mask();
      const auto target = canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask
                              ? tr("Target: Mask")
                              : tr("Target: Pixels");
      set_property_label_text(active_layer_mask_label_,
                              tr("Mask: %1 | %2 | %3 | Bounds %4 | Default %5")
                                  .arg(mask.disabled ? tr("Disabled") : tr("Enabled"))
                                  .arg(layer_mask_linked(*layer) ? tr("Linked") : tr("Unlinked"))
                                  .arg(target)
                                  .arg(rect_summary(mask.bounds))
                                  .arg(mask.default_color));
    }
    if (layer->kind() == LayerKind::Adjustment) {
      set_property_label_text(active_layer_adjustment_label_,
                              tr("Adjustment: %1").arg(adjustment_settings_summary(*layer)));
    } else {
      set_property_label_text(active_layer_adjustment_label_, QString());
    }
    if (layer_is_text(*layer)) {
      set_property_label_text(active_layer_text_label_, tr("Text: %1").arg(text_layer_summary(*layer, doc)));
    } else {
      set_property_label_text(active_layer_text_label_, QString());
    }
  }

  if (active_tool_info_label_ != nullptr && canvas_ != nullptr) {
    QStringList lines;
    lines << tr("Tool: %1").arg(tool_name(current_tool_));
    if (current_tool_ == CanvasTool::Brush || current_tool_ == CanvasTool::MixerBrush ||
        current_tool_ == CanvasTool::PatternStamp ||
        current_tool_ == CanvasTool::Clone ||
        current_tool_ == CanvasTool::Healing ||
        current_tool_ == CanvasTool::Smudge || current_tool_ == CanvasTool::Eraser ||
        current_tool_ == CanvasTool::Line || current_tool_ == CanvasTool::Rectangle ||
        current_tool_ == CanvasTool::Ellipse) {
      lines << tr("Size: %1 px").arg(canvas_->brush_size());
      if (current_tool_ == CanvasTool::MixerBrush) {
        lines << tr("Wet: %1% | Load: %2% | Mix: %3% | Flow: %4%")
                     .arg(canvas_->mixer_wet())
                     .arg(canvas_->mixer_load())
                     .arg(canvas_->mixer_mix())
                     .arg(canvas_->mixer_flow());
      } else {
        lines << tr("Opacity: %1%").arg(canvas_->brush_opacity());
      }
      lines << tr("Softness: %1%").arg(canvas_->brush_softness());
      if (current_tool_ == CanvasTool::Brush || current_tool_ == CanvasTool::PatternStamp) {
        lines << tr("Flow: %1%").arg(canvas_->brush_flow());
        if (current_tool_ == CanvasTool::Brush) {
          lines << (canvas_->brush_build_up() ? tr("Airbrush: on") : tr("Airbrush: off"));
        }
      }
    } else if (current_tool_ == CanvasTool::Dodge || current_tool_ == CanvasTool::Burn ||
               current_tool_ == CanvasTool::Sponge || current_tool_ == CanvasTool::BlurBrush ||
               current_tool_ == CanvasTool::SharpenBrush) {
      lines << tr("Size: %1 px").arg(canvas_->brush_size())
            << tr("Strength: %1%").arg(canvas_->local_adjustment_strength())
            << tr("Softness: %1%").arg(canvas_->brush_softness());
    } else if (current_tool_ == CanvasTool::MagicWand) {
      lines << tr("Tolerance: %1 | %2 | %3")
                   .arg(canvas_->wand_tolerance())
                   .arg(canvas_->wand_contiguous() ? tr("contiguous") : tr("non-contiguous"))
                   .arg(canvas_->wand_sample_all_layers() ? tr("sample all layers") : tr("active layer"));
    } else if (current_tool_ == CanvasTool::QuickSelect) {
      lines << tr("Size: %1 px | %2 | %3")
                   .arg(canvas_->quick_select_size())
                   .arg(canvas_->quick_select_sample_all_layers() ? tr("sample all layers") : tr("active layer"))
                   .arg(canvas_->quick_select_enhance_edge() ? tr("enhance edge") : tr("raw edge"));
    } else if (current_tool_ == CanvasTool::MagneticLasso) {
      lines << tr("Width: %1 px | Contrast: %2% | Frequency: %3")
                   .arg(canvas_->magnetic_lasso_width())
                   .arg(canvas_->magnetic_lasso_edge_contrast())
                   .arg(canvas_->magnetic_lasso_frequency());
      lines << tr("Selection: feather %1 px, %2")
                   .arg(current_selection_feather_radius_)
                   .arg(current_selection_antialias_ ? tr("anti-aliased") : tr("hard edge"));
    } else if (current_tool_ == CanvasTool::Marquee || current_tool_ == CanvasTool::EllipticalMarquee ||
               current_tool_ == CanvasTool::Lasso) {
      lines << tr("Selection: feather %1 px, %2")
                   .arg(current_selection_feather_radius_)
                   .arg(current_selection_antialias_ ? tr("anti-aliased") : tr("hard edge"));
    } else if (current_tool_ == CanvasTool::Text && text_size_spin_ != nullptr) {
      lines << tr("Text size: %1 pt").arg(format_text_points(text_size_spin_->value()));
    }
    set_property_label_text(active_tool_info_label_, lines.join(QStringLiteral(" | ")));
  }
}

void MainWindow::update_canvas_info(CanvasInfoState info) {
  if (canvas_info_label_ == nullptr) {
    return;
  }

  auto x_value = QStringLiteral("-");
  auto y_value = QStringLiteral("-");
  QString color_line = tr("RGB: -");
  if (info.inside_document) {
    x_value = QString::number(info.document_point.x());
    y_value = QString::number(info.document_point.y());
    const auto color = info.color;
    color_line = tr("RGB: %1, %2, %3  #%4%5%6")
                     .arg(color.red())
                     .arg(color.green())
                     .arg(color.blue())
                     .arg(color.red(), 2, 16, QLatin1Char('0'))
                     .arg(color.green(), 2, 16, QLatin1Char('0'))
                     .arg(color.blue(), 2, 16, QLatin1Char('0'))
                     .toUpper();
  }

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

  canvas_info_label_->setText(
      tr("X: %1\nY: %2\n%3\n%4").arg(x_value).arg(y_value).arg(color_line).arg(rect_line));
}

}  // namespace patchy::ui
