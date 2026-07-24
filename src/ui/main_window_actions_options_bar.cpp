// MainWindow::build_options_bar(): the Options-bar phase of create_actions()
// (the top toolbar, its FlowLayout/OptionsFlowContainer hosts and every
// per-tool option control), split out of main_window_actions.cpp along with
// the anonymous-namespace widget helpers only this phase uses.
// Pure function move; behavior must stay identical, and the construction
// order is load-bearing (see create_actions() for the phase order).

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
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
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
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
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <initializer_list>
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

// A left-to-right layout that wraps its items onto additional rows when the
// available width is too small, skipping hidden widgets so the active tool's
// options pack tightly. Used by the Options bar so controls fold to a second
// line instead of being clipped.
class FlowLayout final : public QLayout {
public:
  explicit FlowLayout(QWidget* parent, int horizontal_spacing = 6, int vertical_spacing = 4)
      : QLayout(parent), horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {
    setContentsMargins(0, 0, 0, 0);
  }
  ~FlowLayout() override {
    while (QLayoutItem* item = takeAt(0)) {
      delete item;
    }
  }

  void addItem(QLayoutItem* item) override { items_.append(item); }
  int count() const override { return static_cast<int>(items_.size()); }
  QLayoutItem* itemAt(int index) const override { return items_.value(index); }
  QLayoutItem* takeAt(int index) override {
    return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return do_layout(QRect(0, 0, width, 0), true); }
  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    do_layout(rect, false);
  }
  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSize() const override {
    QSize size;
    for (auto* item : items_) {
      const QWidget* widget = item->widget();
      if (widget != nullptr && widget->isHidden()) {
        continue;
      }
      size = size.expandedTo(item->minimumSize());
    }
    const auto margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
  }

private:
  int do_layout(const QRect& rect, bool test_only) const {
    const auto margins = contentsMargins();
    const QRect effective = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = effective.x();
    int y = effective.y();
    int line_height = 0;
    for (auto* item : items_) {
      QWidget* widget = item->widget();
      if (widget != nullptr && widget->isHidden()) {
        continue;
      }
      const QSize hint = item->sizeHint();
      int next_x = x + hint.width() + horizontal_spacing_;
      if (next_x - horizontal_spacing_ > effective.right() + 1 && line_height > 0) {
        x = effective.x();
        y = y + line_height + vertical_spacing_;
        next_x = x + hint.width() + horizontal_spacing_;
        line_height = 0;
      }
      if (!test_only) {
        item->setGeometry(QRect(QPoint(x, y), hint));
      }
      x = next_x;
      line_height = std::max(line_height, hint.height());
    }
    return y + line_height - rect.y() + margins.bottom();
  }

  QList<QLayoutItem*> items_;
  int horizontal_spacing_;
  int vertical_spacing_;
};

// Hosts the Options bar controls in a FlowLayout and reports the wrapped height
// for its current width so the surrounding QToolBar grows to a second row.
class OptionsFlowContainer final : public QWidget {
public:
  using QWidget::QWidget;

  QSize sizeHint() const override {
    const int available = width() > 0 ? width() : 1200;
    const int height = layout() != nullptr ? layout()->heightForWidth(available) : 0;
    return QSize(available, height);
  }
  QSize minimumSizeHint() const override {
    const int available = width() > 0 ? width() : 0;
    const int height = layout() != nullptr ? layout()->heightForWidth(std::max(available, 1)) : 0;
    return QSize(layout() != nullptr ? layout()->minimumSize().width() : 0, height);
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    // Width changed: the wrapped height may differ, so ask the toolbar to relayout.
    updateGeometry();
  }
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

}  // namespace

void MainWindow::build_options_bar(ActionBuildContext& ctx) {
  // The startup-defaults donor canvas resolved by create_actions() (see the
  // comment there); a local alias keeps the moved body identical.
  auto* canvas_defaults = ctx.canvas_defaults;
  auto* toolbar = new QToolBar(tr("Options"), this);
  toolbar->setObjectName(QStringLiteral("Options"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setAllowedAreas(Qt::TopToolBarArea);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(18, 18));
  addToolBar(Qt::TopToolBarArea, toolbar);

  // Host the tool options in a wrapping flow layout so they fold onto a second
  // row when the window is too narrow, instead of being clipped off the edge.
  auto* options_content = new OptionsFlowContainer(toolbar);
  options_content->setObjectName(QStringLiteral("OptionsContent"));
  options_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto* options_flow = new FlowLayout(options_content, 5, 4);
  options_flow->setContentsMargins(0, 3, 0, 3);
  options_content->setLayout(options_flow);
  toolbar->addWidget(options_content);
  options_flow_container_ = options_content;

  option_actions_.clear();
  transform_option_actions_.clear();
  warp_option_actions_.clear();
  transform_session_actions_.clear();
  const auto make_option_separator = [options_content, options_flow]() -> QWidget* {
    auto* line = new QFrame(options_content);
    line->setObjectName(QStringLiteral("optionSeparator"));
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(24);
    options_flow->addWidget(line);
    return line;
  };
  const auto add_option_separator = [this, make_option_separator](std::initializer_list<CanvasTool> tools) {
    register_option_action(make_option_separator(), tools);
  };
  const auto add_option_action = [this, options_content, options_flow](const QIcon& icon, const QString& text,
                                                                       std::initializer_list<CanvasTool> tools) {
    auto* action = new QAction(icon, text, this);
    auto* button = new QToolButton(options_content);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    // Tagged for the compact Options-bar button style (see the app stylesheet).
    // The default QToolButton min-height + padding makes it the tallest item in
    // the row, which grows the whole Options toolbar when a selection tool is
    // active. The icon keeps its full size; only the padding around it shrinks.
    button->setProperty("optionsBarButton", true);
    options_flow->addWidget(button);
    register_option_action(button, tools);
    return action;
  };
  const auto add_option_widget = [this, options_flow](QWidget* widget, std::initializer_list<CanvasTool> tools) {
    options_flow->addWidget(widget);
    register_option_action(widget, tools);
    return widget;
  };
  const auto add_transform_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_option_actions_.push_back(widget);
    return widget;
  };
  const auto add_option_label = [options_content, add_option_widget](const QString& text,
                                                                     std::initializer_list<CanvasTool> tools) {
    auto* label = new QLabel(text, options_content);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    return add_option_widget(label, tools);
  };

  move_auto_select_check_ = new CheckGlyphBox(tr("Auto-Select"), toolbar);
  move_auto_select_check_->setObjectName(QStringLiteral("moveAutoSelectCheck"));
  move_auto_select_check_->setToolTip(tr("Automatically select the clicked layer while using Move"));
  move_auto_select_check_->setChecked(canvas_defaults->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
  });
  move_show_transform_controls_check_ = new CheckGlyphBox(tr("Show Transform Controls"), toolbar);
  move_show_transform_controls_check_->setObjectName(QStringLiteral("moveShowTransformControlsCheck"));
  move_show_transform_controls_check_->setToolTip(tr("Show transform controls when selecting a layer with Move"));
  move_show_transform_controls_check_->setChecked(canvas_defaults->show_transform_controls());
  add_option_widget(move_show_transform_controls_check_, {CanvasTool::Move});
  connect(move_show_transform_controls_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_show_transform_controls(checked);
    }
  });

  transform_reference_combo_ = new QComboBox(toolbar);
  transform_reference_combo_->setObjectName(QStringLiteral("freeTransformReferenceCombo"));
  transform_reference_combo_->setToolTip(tr("Reference point"));
  transform_reference_combo_->setMinimumWidth(96);
  add_transform_option_widget(transform_reference_combo_);
  register_retranslation([this] {
    if (transform_reference_combo_ == nullptr) {
      return;
    }
    const auto current = transform_reference_combo_->currentData();
    QSignalBlocker blocker(transform_reference_combo_);
    transform_reference_combo_->clear();
    transform_reference_combo_->addItem(tr("Top Left"), static_cast<int>(CanvasAnchor::TopLeft));
    transform_reference_combo_->addItem(tr("Top"), static_cast<int>(CanvasAnchor::Top));
    transform_reference_combo_->addItem(tr("Top Right"), static_cast<int>(CanvasAnchor::TopRight));
    transform_reference_combo_->addItem(tr("Left"), static_cast<int>(CanvasAnchor::Left));
    transform_reference_combo_->addItem(tr("Center"), static_cast<int>(CanvasAnchor::Center));
    transform_reference_combo_->addItem(tr("Right"), static_cast<int>(CanvasAnchor::Right));
    transform_reference_combo_->addItem(tr("Bottom Left"), static_cast<int>(CanvasAnchor::BottomLeft));
    transform_reference_combo_->addItem(tr("Bottom"), static_cast<int>(CanvasAnchor::Bottom));
    transform_reference_combo_->addItem(tr("Bottom Right"), static_cast<int>(CanvasAnchor::BottomRight));
    const auto index = transform_reference_combo_->findData(current.isValid() ? current : QVariant(static_cast<int>(CanvasAnchor::Center)));
    transform_reference_combo_->setCurrentIndex(index >= 0 ? index : transform_reference_combo_->findData(static_cast<int>(CanvasAnchor::Center)));
  });
  connect(transform_reference_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_reference_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_reference_point(
        static_cast<CanvasAnchor>(transform_reference_combo_->itemData(index).toInt()));
    sync_transform_controls_from_canvas();
  });

  const auto make_transform_label = [toolbar, add_transform_option_widget](const char* source) {
    auto* label = new QLabel(QObject::tr(source), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, source);
    add_transform_option_widget(label);
    return label;
  };
  const auto make_transform_spin = [toolbar, add_transform_option_widget](const QString& object_name,
                                                                          double minimum, double maximum,
                                                                          int decimals, const QString& suffix) {
    auto* spin = new QDoubleSpinBox(toolbar);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setSuffix(suffix);
    spin->setMinimumWidth(82);
    configure_dialog_spinbox(spin, 82);
    add_transform_option_widget(spin);
    return spin;
  };

  make_transform_label("X:");
  transform_x_spin_ = make_transform_spin(QStringLiteral("freeTransformXSpin"), -30000.0, 30000.0, 2,
                                          QStringLiteral(" px"));
  transform_x_spin_->setToolTip(tr("Reference X position"));
  make_transform_label("Y:");
  transform_y_spin_ = make_transform_spin(QStringLiteral("freeTransformYSpin"), -30000.0, 30000.0, 2,
                                          QStringLiteral(" px"));
  transform_y_spin_->setToolTip(tr("Reference Y position"));
  make_transform_label("W:");
  transform_scale_x_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleXSpin"), -10000.0, 10000.0, 2,
                                                 QStringLiteral("%"));
  transform_scale_x_spin_->setToolTip(tr("Horizontal scale"));
  transform_link_scale_button_ = new QPushButton(toolbar);
  transform_link_scale_button_->setObjectName(QStringLiteral("freeTransformLinkScaleButton"));
  transform_link_scale_button_->setCheckable(true);
  transform_link_scale_button_->setChecked(true);
  transform_link_scale_button_->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  transform_link_scale_button_->setToolTip(tr("Link horizontal and vertical scale"));
  transform_link_scale_button_->setFixedWidth(28);
  add_transform_option_widget(transform_link_scale_button_);
  make_transform_label("H:");
  transform_scale_y_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleYSpin"), -10000.0, 10000.0, 2,
                                                 QStringLiteral("%"));
  transform_scale_y_spin_->setToolTip(tr("Vertical scale"));
  make_transform_label("Angle:");
  transform_rotation_spin_ = make_transform_spin(QStringLiteral("freeTransformRotationSpin"), -3600.0, 3600.0, 2,
                                                 QStringLiteral(" deg"));
  transform_rotation_spin_->setToolTip(tr("Rotation angle"));
  transform_interpolation_combo_ = new QComboBox(toolbar);
  transform_interpolation_combo_->setObjectName(QStringLiteral("freeTransformInterpolationCombo"));
  transform_interpolation_combo_->setToolTip(tr("Interpolation"));
  transform_interpolation_combo_->setMinimumWidth(132);
  add_transform_option_widget(transform_interpolation_combo_);
  register_retranslation([this] {
    if (transform_interpolation_combo_ == nullptr) {
      return;
    }
    const auto current = transform_interpolation_combo_->currentData();
    QSignalBlocker blocker(transform_interpolation_combo_);
    transform_interpolation_combo_->clear();
    transform_interpolation_combo_->addItem(tr("Nearest Neighbor"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::NearestNeighbor));
    transform_interpolation_combo_->addItem(tr("Bilinear"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bilinear));
    transform_interpolation_combo_->addItem(tr("Bicubic"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic));
    const auto fallback = static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic);
    const auto index = transform_interpolation_combo_->findData(current.isValid() ? current : QVariant(fallback));
    transform_interpolation_combo_->setCurrentIndex(index >= 0 ? index : transform_interpolation_combo_->findData(fallback));
  });
  const auto apply_transform_from_spin = [this] { apply_transform_controls_from_ui(); };
  connect(transform_x_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_y_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_scale_x_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_scale_y_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_x_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_x_spin_);
      transform_scale_x_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_link_scale_button_, &QPushButton::toggled, this, [this](bool checked) {
    if (checked && transform_scale_x_spin_ != nullptr && transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(transform_scale_x_spin_->value());
      apply_transform_controls_from_ui();
    }
  });
  connect(transform_rotation_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_interpolation_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_interpolation_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_interpolation(
        static_cast<CanvasWidget::TransformInterpolation>(transform_interpolation_combo_->itemData(index).toInt()));
  });
  // Warp Transform options: visible only while the warp cage is active.
  const auto add_warp_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    warp_option_actions_.push_back(widget);
    return widget;
  };
  {
    auto* label = new QLabel(QObject::tr("Warp:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, "Warp:");
    add_warp_option_widget(label);
  }
  warp_style_combo_ = new QComboBox(toolbar);
  warp_style_combo_->setObjectName(QStringLiteral("warpStyleCombo"));
  warp_style_combo_->setToolTip(tr("Warp style"));
  warp_style_combo_->setMinimumWidth(110);
  add_warp_option_widget(warp_style_combo_);
  register_retranslation([this] {
    if (warp_style_combo_ == nullptr) {
      return;
    }
    const auto current = warp_style_combo_->currentData();
    QSignalBlocker blocker(warp_style_combo_);
    warp_style_combo_->clear();
    warp_style_combo_->addItem(tr("Custom"), QStringLiteral("warpCustom"));
    warp_style_combo_->addItem(tr("Arc"), QStringLiteral("warpArc"));
    warp_style_combo_->addItem(tr("Arc Lower"), QStringLiteral("warpArcLower"));
    warp_style_combo_->addItem(tr("Arc Upper"), QStringLiteral("warpArcUpper"));
    warp_style_combo_->addItem(tr("Arch"), QStringLiteral("warpArch"));
    warp_style_combo_->addItem(tr("Bulge"), QStringLiteral("warpBulge"));
    warp_style_combo_->addItem(tr("Shell Lower"), QStringLiteral("warpShellLower"));
    warp_style_combo_->addItem(tr("Shell Upper"), QStringLiteral("warpShellUpper"));
    warp_style_combo_->addItem(tr("Flag"), QStringLiteral("warpFlag"));
    warp_style_combo_->addItem(tr("Wave"), QStringLiteral("warpWave"));
    warp_style_combo_->addItem(tr("Fish"), QStringLiteral("warpFish"));
    warp_style_combo_->addItem(tr("Rise"), QStringLiteral("warpRise"));
    warp_style_combo_->addItem(tr("Fisheye"), QStringLiteral("warpFisheye"));
    warp_style_combo_->addItem(tr("Inflate"), QStringLiteral("warpInflate"));
    warp_style_combo_->addItem(tr("Squeeze"), QStringLiteral("warpSqueeze"));
    warp_style_combo_->addItem(tr("Twist"), QStringLiteral("warpTwist"));
    const auto index = warp_style_combo_->findData(current.isValid() ? current : QVariant(QStringLiteral("warpCustom")));
    warp_style_combo_->setCurrentIndex(std::max(0, index));
  });
  {
    auto* label = new QLabel(QObject::tr("Bend:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, "Bend:");
    add_warp_option_widget(label);
  }
  warp_bend_spin_ = new QDoubleSpinBox(toolbar);
  warp_bend_spin_->setObjectName(QStringLiteral("warpBendSpin"));
  warp_bend_spin_->setRange(-100.0, 100.0);
  warp_bend_spin_->setDecimals(0);
  warp_bend_spin_->setKeyboardTracking(false);
  warp_bend_spin_->setSuffix(QStringLiteral("%"));
  warp_bend_spin_->setValue(50.0);
  warp_bend_spin_->setToolTip(tr("Warp bend"));
  configure_dialog_spinbox(warp_bend_spin_, 74);
  add_warp_option_widget(warp_bend_spin_);
  const auto apply_warp_style_from_ui = [this] {
    if (updating_transform_controls_ || canvas_ == nullptr || warp_style_combo_ == nullptr ||
        warp_bend_spin_ == nullptr) {
      return;
    }
    canvas_->apply_warp_style_preset(warp_style_combo_->currentData().toString(), warp_bend_spin_->value());
  };
  connect(warp_style_combo_, &QComboBox::currentIndexChanged, this,
          [apply_warp_style_from_ui](int index) {
            if (index >= 0) {
              apply_warp_style_from_ui();
            }
          });
  connect(warp_bend_spin_, &QDoubleSpinBox::valueChanged, this,
          [apply_warp_style_from_ui](double) { apply_warp_style_from_ui(); });

  // Shared session trio, laid out after both control sets so it closes the row in
  // either mode (Photoshop's options-bar order: mode toggle, then cancel/commit).
  // Apply/cancel dispatch on whichever session is active.
  const auto add_session_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_session_actions_.push_back(widget);
    return widget;
  };
  transform_warp_mode_button_ = new QPushButton(toolbar);
  transform_warp_mode_button_->setObjectName(QStringLiteral("transformWarpModeButton"));
  transform_warp_mode_button_->setCheckable(true);
  transform_warp_mode_button_->setIcon(simple_icon(QStringLiteral("warp"), QColor(220, 226, 235)));
  transform_warp_mode_button_->setToolTip(tr("Switch between free transform and warp"));
  transform_warp_mode_button_->setFixedWidth(30);
  // Session buttons render their icons at 20px (the QPushButton default of 16px
  // reads tiny on the bar); optionsSessionButton relaxes the QSS side padding so
  // the larger icon is not clipped.
  transform_warp_mode_button_->setIconSize(QSize(20, 20));
  transform_warp_mode_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_warp_mode_button_);
  transform_apply_button_ = new QPushButton(toolbar);
  transform_apply_button_->setObjectName(QStringLiteral("freeTransformApplyButton"));
  transform_apply_button_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  transform_apply_button_->setToolTip(tr("Apply transform"));
  transform_apply_button_->setFixedWidth(30);
  transform_apply_button_->setIconSize(QSize(20, 20));
  transform_apply_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_apply_button_);
  transform_cancel_button_ = new QPushButton(toolbar);
  transform_cancel_button_->setObjectName(QStringLiteral("freeTransformCancelButton"));
  transform_cancel_button_->setIcon(simple_icon(QStringLiteral("clear"), QColor(255, 150, 150)));
  transform_cancel_button_->setToolTip(tr("Cancel transform"));
  transform_cancel_button_->setFixedWidth(30);
  transform_cancel_button_->setIconSize(QSize(20, 20));
  transform_cancel_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_cancel_button_);
  connect(transform_warp_mode_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->switch_warp_to_free_transform();
    } else if (canvas_->free_transform_active()) {
      canvas_->begin_warp_transform();  // refusal reasons land in the status bar
    }
    // Re-sync the checked state (a refused switch leaves the mode unchanged).
    refresh_options_bar();
  });
  connect(transform_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->finish_warp_transform();
    } else {
      canvas_->finish_free_transform();
    }
  });
  connect(transform_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->cancel_warp_transform();
    } else {
      canvas_->cancel_free_transform();
    }
  });

  auto* selection_new = add_option_action(
      simple_icon(QStringLiteral("N")), tr("New Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_new->setObjectName(QStringLiteral("selectionNewModeAction"));
  auto* selection_add = add_option_action(
      simple_icon(QStringLiteral("+")), tr("Add to Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_add->setObjectName(QStringLiteral("selectionAddModeAction"));
  auto* selection_subtract = add_option_action(
      simple_icon(QStringLiteral("-")), tr("Subtract from Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_subtract->setObjectName(QStringLiteral("selectionSubtractModeAction"));
  auto* selection_intersect = add_option_action(simple_icon(QStringLiteral("Ix")), tr("Intersect Selection"),
                                                {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                                 CanvasTool::MagneticLasso, CanvasTool::MagicWand});
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
    // Each selection tool keeps its own combine mode; store it for the active
    // tool (so new documents inherit it) and apply it to the live canvas.
    if (const auto index = CanvasWidget::selection_tool_index(current_tool_); index >= 0) {
      selection_modes_[static_cast<std::size_t>(index)] = mode;
    }
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
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect});

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
  add_option_widget(feather_group, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                    CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect});
  auto* anti_alias = new CheckGlyphBox(tr("Anti-alias"), toolbar);
  anti_alias->setObjectName(QStringLiteral("selectionAntiAliasCheck"));
  anti_alias->setChecked(current_selection_antialias_);
  add_option_widget(anti_alias,
                    {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                     CanvasTool::MagneticLasso, CanvasTool::MagicWand});
  const auto apply_selection_edge_settings = [this, feather, anti_alias] {
    current_selection_feather_radius_ = feather->value();
    current_selection_antialias_ = anti_alias->isChecked();
    if (canvas_ != nullptr) {
      canvas_->set_selection_feather_radius(current_selection_feather_radius_);
      canvas_->set_selection_antialias(current_selection_antialias_);
    }
    refresh_document_info();
  };
  connect(feather, &QSpinBox::valueChanged, this, [apply_selection_edge_settings](int) {
    apply_selection_edge_settings();
  });
  connect(anti_alias, &QCheckBox::toggled, this, [apply_selection_edge_settings](bool) {
    apply_selection_edge_settings();
  });
  add_option_label(tr("Radius:"), {CanvasTool::Marquee});
  auto* marquee_corner_radius = new QSpinBox(toolbar);
  marquee_corner_radius->setObjectName(QStringLiteral("selectionCornerRadiusSpin"));
  marquee_corner_radius->setRange(0, 512);
  marquee_corner_radius->setValue(current_marquee_corner_radius_);
  marquee_corner_radius->setSuffix(QStringLiteral(" px"));
  marquee_corner_radius->setToolTip(tr("Rounded-corner radius for the rectangular marquee (0 = sharp corners)"));
  configure_toolbar_spinbox(marquee_corner_radius, 64);
  add_option_widget(marquee_corner_radius, {CanvasTool::Marquee});
  connect(marquee_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_marquee_corner_radius_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_marquee_corner_radius(value);
    }
  });
  add_option_label(tr("Style:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* style_combo = new QComboBox(toolbar);
  style_combo->setObjectName(QStringLiteral("selectionStyleCombo"));
  style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  style_combo->setCurrentText(tr("Normal"));
  style_combo->setFixedWidth(92);
  QPointer<QComboBox> selection_style_combo(style_combo);
  register_retranslation([selection_style_combo] {
    if (selection_style_combo == nullptr || selection_style_combo->count() < 3) {
      return;
    }
    QSignalBlocker blocker(selection_style_combo);
    selection_style_combo->setItemText(0, QObject::tr("Normal"));
    selection_style_combo->setItemText(1, QObject::tr("Fixed Ratio"));
    selection_style_combo->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(style_combo, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Width:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_width = new QSpinBox(toolbar);
  fixed_width->setObjectName(QStringLiteral("selectionFixedWidthSpin"));
  fixed_width->setRange(1, 30000);
  fixed_width->setValue(has_active_document() ? document().width() : 1024);
  fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_width, 78);
  add_option_widget(fixed_width, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Height:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_height = new QSpinBox(toolbar);
  fixed_height->setObjectName(QStringLiteral("selectionFixedHeightSpin"));
  fixed_height->setRange(1, 30000);
  fixed_height->setValue(has_active_document() ? document().height() : 768);
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
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand});

  add_option_label(tr("Preset:"),
                   {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                    CanvasTool::Eraser});
  brush_preset_combo_ = new QComboBox(toolbar);
  brush_preset_combo_->setObjectName(QStringLiteral("brushPresetCombo"));
  brush_preset_combo_->setMinimumWidth(132);
  for (const auto& preset : builtin_brush_presets()) {
    brush_preset_combo_->addItem(brush_preset_display_name(preset), preset.id);
  }
  {
    const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
    if (preset_index >= 0) {
      brush_preset_combo_->setCurrentIndex(preset_index);
    }
  }
  add_option_widget(
      brush_preset_combo_,
      {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser});

  // The raster brush controls double as the shape tools' Pixels-mode options;
  // refresh_vector_tool_options_visibility hides them in the vector modes.
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Size:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_size = new QSpinBox(toolbar);
  brush_size->setObjectName(QStringLiteral("brushSizeSpin"));
  brush_size->setRange(1, kMaxBrushSize);
  brush_size->setValue(canvas_defaults->brush_size());
  configure_toolbar_spinbox(brush_size, 58);
  add_option_widget(brush_size,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_size_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_size_slider->setObjectName(QStringLiteral("brushSizeSlider"));
  brush_size_slider->setRange(1, kMaxBrushSize);
  brush_size_slider->setValue(canvas_defaults->brush_size());
  brush_size_slider->setFixedWidth(150);
  brush_size_slider->setToolTip(tr("Brush size — press [ or ], or Alt+Right-drag on the canvas"));
  add_option_widget(brush_size_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Opacity:"),
      {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_opacity = new QSpinBox(toolbar);
  brush_opacity->setObjectName(QStringLiteral("brushOpacitySpin"));
  brush_opacity->setRange(1, 100);
  brush_opacity->setValue(canvas_defaults->brush_opacity());
  brush_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_opacity, 52);
  add_option_widget(brush_opacity,
                    {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_opacity_slider->setObjectName(QStringLiteral("brushOpacitySlider"));
  brush_opacity_slider->setRange(1, 100);
  brush_opacity_slider->setValue(canvas_defaults->brush_opacity());
  brush_opacity_slider->setFixedWidth(120);
  brush_opacity_slider->setToolTip(tr("Brush opacity — press number keys (5 = 50%, 0 = 100%)"));
  add_option_widget(brush_opacity_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Soft:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_softness = new QSpinBox(toolbar);
  brush_softness->setObjectName(QStringLiteral("brushSoftnessSpin"));
  brush_softness->setRange(0, 100);
  brush_softness->setValue(canvas_defaults->brush_softness());
  brush_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_softness, 52);
  add_option_widget(brush_softness,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_softness_slider->setObjectName(QStringLiteral("brushSoftnessSlider"));
  brush_softness_slider->setRange(0, 100);
  brush_softness_slider->setValue(canvas_defaults->brush_softness());
  brush_softness_slider->setFixedWidth(110);
  brush_softness_slider->setToolTip(tr("Brush edge softness — Alt+Right-drag up or down on the canvas"));
  add_option_widget(brush_softness_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  for (auto* raster_only :
       std::initializer_list<QWidget*>{brush_size, brush_size_slider, brush_opacity,
                                       brush_opacity_slider, brush_softness, brush_softness_slider}) {
    vector_pixel_only_option_widgets_.push_back(raster_only);
  }
  connect(brush_size, &QSpinBox::valueChanged, brush_size_slider, &QSlider::setValue);
  connect(brush_size_slider, &QSlider::valueChanged, brush_size, &QSpinBox::setValue);
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_size(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_opacity, &QSpinBox::valueChanged, brush_opacity_slider, &QSlider::setValue);
  connect(brush_opacity_slider, &QSlider::valueChanged, brush_opacity, &QSpinBox::setValue);
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_opacity(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_flow_label = add_option_label(tr("Flow:"), {CanvasTool::Brush, CanvasTool::PatternStamp});
  bind_widget_text(brush_flow_label, "Flow:");
  auto* brush_flow = new QSpinBox(toolbar);
  brush_flow->setObjectName(QStringLiteral("brushFlowSpin"));
  brush_flow->setRange(1, 100);
  brush_flow->setValue(canvas_defaults->brush_flow());
  brush_flow->setSuffix(QStringLiteral("%"));
  brush_flow->setToolTip(tr("Brush flow - Shift+number keys (number keys with Airbrush)"));
  bind_tooltip(brush_flow, "Brush flow - Shift+number keys (number keys with Airbrush)");
  configure_toolbar_spinbox(brush_flow, 60);
  add_option_widget(brush_flow, {CanvasTool::Brush, CanvasTool::PatternStamp});
  auto* brush_airbrush = new CheckGlyphBox(tr("Airbrush"), toolbar);
  brush_airbrush->setObjectName(QStringLiteral("brushAirbrushCheck"));
  bind_widget_text(brush_airbrush, "Airbrush");
  brush_airbrush->setChecked(canvas_defaults->brush_build_up());
  brush_airbrush->setToolTip(tr("Build paint while the pointer is held still"));
  bind_tooltip(brush_airbrush, "Build paint while the pointer is held still");
  add_option_widget(brush_airbrush, {CanvasTool::Brush});
  connect(brush_flow, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_flow(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_airbrush, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_build_up(checked);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  const auto add_mixer_percentage = [this, toolbar, add_option_label, add_option_widget](
                                        const char* label_source, const char* object_name,
                                        int minimum, int value, auto setter) {
    auto* label = add_option_label(tr(label_source), {CanvasTool::MixerBrush});
    bind_widget_text(label, label_source);
    auto* spin = new QSpinBox(toolbar);
    spin->setObjectName(QString::fromLatin1(object_name));
    spin->setRange(minimum, 100);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral("%"));
    configure_toolbar_spinbox(spin, 60);
    add_option_widget(spin, {CanvasTool::MixerBrush});
    connect(spin, &QSpinBox::valueChanged, this, [this, setter](int new_value) {
      setter(*this, new_value);
      schedule_save_tool_settings();
      refresh_document_info();
    });
    return spin;
  };
  add_mixer_percentage("Wet:", "mixerWetSpin", 0, current_mixer_wet_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_wet_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_wet(value);
                         }
                       });
  add_mixer_percentage("Load:", "mixerLoadSpin", 1, current_mixer_load_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_load_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_load(value);
                         }
                       });
  add_mixer_percentage("Mix:", "mixerMixSpin", 0, current_mixer_mix_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_mix_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_mix(value);
                         }
                       });
  add_mixer_percentage("Flow:", "mixerFlowSpin", 1, current_mixer_flow_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_flow_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_flow(value);
                         }
                       });

  connect(brush_softness, &QSpinBox::valueChanged, brush_softness_slider, &QSlider::setValue);
  connect(brush_softness_slider, &QSlider::valueChanged, brush_softness, &QSpinBox::setValue);
  connect(brush_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_softness(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_preset_combo_, &QComboBox::currentIndexChanged, this,
          [this, brush_size, brush_opacity, brush_flow, brush_softness,
           brush_airbrush](int index) {
    if (brush_preset_combo_ == nullptr || canvas_ == nullptr || index < 0) {
      return;
    }
    const auto preset_id = brush_preset_combo_->itemData(index).toString();
    const auto* preset = find_brush_preset(preset_id);
    if (preset == nullptr) {
      return;
    }
    if (preset_id == QStringLiteral("airbrush")) {
      // The quick Airbrush preset is a predictable soft Round brush. Existing sampled tips
      // already cover Smoke/Spray/Spatter/Stipple, so do not invent a duplicate airbrush tip or
      // carry a surprising Round dynamics session into this basic preset.
      round_brush_dynamics_ = {};
      round_brush_base_angle_degrees_ = 0.0;
      round_brush_base_roundness_ = 100.0;
      set_active_brush_tip(builtin_round_brush_tip_id(), false, false);
    }
    apply_brush_preset(*canvas_, *preset);
    brush_size->setValue(preset->size);
    brush_opacity->setValue(preset->opacity);
    brush_flow->setValue(preset->flow);
    brush_softness->setValue(preset->softness);
    brush_airbrush->setChecked(preset->build_up);
    save_tool_settings();
    refresh_document_info();
    statusBar()->showMessage(tr("Brush preset: %1").arg(brush_preset_display_name(*preset)));
  });

  add_option_label(tr("Tip:"),
                   {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                    CanvasTool::Eraser});
  brush_tip_picker_ = new BrushTipPicker(brush_tip_library(), toolbar);
  // The options bar is built after load_tool_settings() reset the active tip to Round.
  brush_tip_picker_->set_current_tip_id(active_brush_tip_id_);
  add_option_widget(brush_tip_picker_,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                     CanvasTool::Eraser});
  connect(brush_tip_picker_, &BrushTipPicker::tip_selected, this,
          [this](const QString& id) { set_active_brush_tip(id, true); });
  connect(brush_tip_picker_, &BrushTipPicker::import_requested, this,
          [this] { import_brush_tips_from_abr(); });
  connect(brush_tip_picker_, &BrushTipPicker::define_requested, this,
          [this] { define_brush_tip_from_selection(); });
  connect(brush_tip_picker_, &BrushTipPicker::manage_requested, this, [this] { open_brush_tip_manager(); });
  connect(&brush_tip_library(), &BrushTipLibrary::changed, this, [this] {
    // A removed tip must not stay active; re-resolving also refreshes renamed/respaced tips.
    // Re-applying after a library edit must not reset Flow/Airbrush to imported tool settings.
    set_active_brush_tip(active_brush_tip_id_, false, false);
  });
  QPointer<BrushTipPicker> tip_picker(brush_tip_picker_);
  register_retranslation([tip_picker] {
    if (tip_picker != nullptr) {
      tip_picker->refresh();
    }
  });

  brush_dynamics_button_ = new BrushDynamicsButton(toolbar);
  add_option_widget(brush_dynamics_button_, {CanvasTool::Brush});
  connect(brush_dynamics_button_, &BrushDynamicsButton::dynamics_edited, this,
          [this](const QString& tip_id, const patchy::BrushDynamics& dynamics, double base_angle,
                 double base_roundness) {
            if (tip_id == builtin_round_brush_tip_id()) {
              // Session-only: the Round brush's dynamics live in the window, not the library,
              // and deliberately reset on the next launch.
              round_brush_dynamics_ = dynamics;
              round_brush_base_angle_degrees_ = base_angle;
              round_brush_base_roundness_ = base_roundness;
              if (canvas_ != nullptr &&
                  (active_brush_tip_id_.isEmpty() ||
                   active_brush_tip_id_ == builtin_round_brush_tip_id())) {
                canvas_->set_brush_dynamics(dynamics);
                canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
              }
              return;
            }
            if (canvas_ != nullptr && tip_id == active_brush_tip_id_) {
              canvas_->set_brush_dynamics(dynamics);
              canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
            }
            // Persisting to the sidecar emits changed(), which re-applies the (identical) values.
            brush_tip_library().set_tip_dynamics(tip_id, dynamics, base_angle, base_roundness);
          });
  // The options bar is built after load_tool_settings() already selected the startup tip, so
  // seed the button's model now (Round session values, or the entry if a tip is active).
  if (active_brush_tip_id_.isEmpty() || active_brush_tip_id_ == builtin_round_brush_tip_id()) {
    brush_dynamics_button_->set_round_session(builtin_round_brush_tip_id(), round_brush_dynamics_,
                                              round_brush_base_angle_degrees_,
                                              round_brush_base_roundness_);
  } else if (const auto* entry = brush_tip_library().find_entry(active_brush_tip_id_);
             entry != nullptr) {
    brush_dynamics_button_->set_active_entry(entry);
  }
  QPointer<BrushDynamicsButton> dynamics_button(brush_dynamics_button_);
  register_retranslation([dynamics_button] {
    if (dynamics_button != nullptr) {
      dynamics_button->retranslate();
    }
  });

  auto* pattern_label = add_option_label(tr("Pattern:"), {CanvasTool::PatternStamp});
  bind_widget_text(pattern_label, "Pattern:");
  pattern_stamp_pattern_combo_ = new QComboBox(toolbar);
  pattern_stamp_pattern_combo_->setObjectName(QStringLiteral("patternStampPatternCombo"));
  pattern_stamp_pattern_combo_->setIconSize(QSize(18, 18));
  pattern_stamp_pattern_combo_->setMinimumWidth(150);
  refresh_pattern_stamp_pattern_combo();
  add_option_widget(pattern_stamp_pattern_combo_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_pattern_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (pattern_stamp_pattern_combo_ == nullptr || index < 0) {
      return;
    }
    current_pattern_stamp_pattern_id_ = pattern_stamp_pattern_combo_->itemData(index).toString();
    apply_pattern_stamp_settings_to_canvas(canvas_);
    schedule_save_tool_settings();
  });

  auto* manage_patterns = new QPushButton(toolbar);
  manage_patterns->setObjectName(QStringLiteral("patternStampManageButton"));
  manage_patterns->setText(tr("Manage..."));
  bind_widget_text(manage_patterns, "Manage...");
  manage_patterns->setToolTip(tr("Import or manage patterns"));
  bind_tooltip(manage_patterns, "Import or manage patterns");
  add_option_widget(manage_patterns, {CanvasTool::PatternStamp});
  connect(manage_patterns, &QPushButton::clicked, this, [this] {
    const auto selected_storage_id =
        request_pattern_manager(this, pattern_library(), current_pattern_stamp_pattern_id_);
    if (selected_storage_id.isEmpty()) {
      return;
    }
    if (const auto* entry = pattern_library().find_entry(selected_storage_id); entry != nullptr) {
      current_pattern_stamp_pattern_id_ = entry->id;
      refresh_pattern_stamp_pattern_combo();
      apply_pattern_stamp_settings_to_canvas(canvas_);
      save_tool_settings();
    }
  });

  pattern_stamp_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  pattern_stamp_aligned_check_->setObjectName(QStringLiteral("patternStampAlignedCheck"));
  pattern_stamp_aligned_check_->setChecked(current_pattern_stamp_aligned_);
  pattern_stamp_aligned_check_->setToolTip(tr("Keep pattern alignment continuous across strokes"));
  bind_tooltip(pattern_stamp_aligned_check_, "Keep pattern alignment continuous across strokes");
  add_option_widget(pattern_stamp_aligned_check_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_pattern_stamp_aligned_ = checked;
    apply_pattern_stamp_settings_to_canvas(canvas_);
    save_tool_settings();
  });

  add_option_label(tr("Method:"), {CanvasTool::Gradient});
  gradient_method_combo_ = new QComboBox(toolbar);
  gradient_method_combo_->setObjectName(QStringLiteral("gradientMethodCombo"));
  gradient_method_combo_->addItem(tr("Linear"), static_cast<int>(GradientMethod::Linear));
  gradient_method_combo_->addItem(tr("Radial"), static_cast<int>(GradientMethod::Radial));
  gradient_method_combo_->setFixedWidth(86);
  add_option_widget(gradient_method_combo_, {CanvasTool::Gradient});
  QPointer<QComboBox> gradient_method_combo(gradient_method_combo_);
  register_retranslation([gradient_method_combo] {
    if (gradient_method_combo == nullptr || gradient_method_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(gradient_method_combo);
    gradient_method_combo->setItemText(0, QCoreApplication::translate(kMainWindowTranslationContext, "Linear"));
    gradient_method_combo->setItemText(1, QCoreApplication::translate(kMainWindowTranslationContext, "Radial"));
  });

  add_option_label(tr("Opacity:"), {CanvasTool::Gradient});
  gradient_opacity_spin_ = new QSpinBox(toolbar);
  gradient_opacity_spin_->setObjectName(QStringLiteral("gradientOpacitySpin"));
  gradient_opacity_spin_->setRange(0, 100);
  gradient_opacity_spin_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_spin_->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(gradient_opacity_spin_, 52);
  add_option_widget(gradient_opacity_spin_, {CanvasTool::Gradient});
  gradient_opacity_slider_ = new QSlider(Qt::Horizontal, toolbar);
  gradient_opacity_slider_->setObjectName(QStringLiteral("gradientOpacitySlider"));
  gradient_opacity_slider_->setRange(0, 100);
  gradient_opacity_slider_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_slider_->setFixedWidth(110);
  gradient_opacity_slider_->setToolTip(tr("Gradient opacity"));
  bind_tooltip(gradient_opacity_slider_, "Gradient opacity");
  add_option_widget(gradient_opacity_slider_, {CanvasTool::Gradient});
  gradient_reverse_check_ = new CheckGlyphBox(tr("Reverse"), toolbar);
  gradient_reverse_check_->setObjectName(QStringLiteral("gradientReverseCheck"));
  gradient_reverse_check_->setChecked(canvas_defaults->gradient_reverse());
  add_option_widget(gradient_reverse_check_, {CanvasTool::Gradient});
  gradient_preview_button_ = new QPushButton(toolbar);
  gradient_preview_button_->setObjectName(QStringLiteral("gradientPreviewButton"));
  gradient_preview_button_->setToolTip(tr("Gradient preview"));
  bind_tooltip(gradient_preview_button_, "Gradient preview");
  add_option_widget(gradient_preview_button_, {CanvasTool::Gradient});
  gradient_presets_button_ = new QPushButton(toolbar);
  gradient_presets_button_->setObjectName(QStringLiteral("gradientPresetsButton"));
  gradient_presets_button_->setText(tr("Presets"));
  bind_widget_text(gradient_presets_button_, "Presets");
  gradient_presets_button_->setToolTip(tr("Choose a gradient preset"));
  bind_tooltip(gradient_presets_button_, "Choose a gradient preset");
  add_option_widget(gradient_presets_button_, {CanvasTool::Gradient});
  gradient_edit_stops_button_ = new QPushButton(tr("Edit Stops..."), toolbar);
  gradient_edit_stops_button_->setObjectName(QStringLiteral("gradientEditStopsButton"));
  add_option_widget(gradient_edit_stops_button_, {CanvasTool::Gradient});
  refresh_gradient_controls_from_canvas();
  connect(gradient_method_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (canvas_ == nullptr || gradient_method_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_gradient_method(static_cast<GradientMethod>(gradient_method_combo_->itemData(index).toInt()));
    save_tool_settings();
    refresh_document_info();
  });
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, gradient_opacity_slider_, &QSlider::setValue);
  connect(gradient_opacity_slider_, &QSlider::valueChanged, gradient_opacity_spin_, &QSpinBox::setValue);
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_opacity(value);
      refresh_gradient_controls_from_canvas();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_reverse_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_reverse(checked);
      refresh_gradient_controls_from_canvas();
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_preview_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });
  connect(gradient_presets_button_, &QPushButton::clicked, this, [this] { choose_gradient_preset(); });
  connect(gradient_edit_stops_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });

  clone_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  clone_aligned_check_->setObjectName(QStringLiteral("cloneAlignedCheck"));
  clone_aligned_check_->setChecked(canvas_defaults->clone_aligned());
  clone_aligned_check_->setToolTip(tr("Keep sample source offset aligned across strokes"));
  add_option_widget(clone_aligned_check_, {CanvasTool::Clone, CanvasTool::Healing});
  connect(clone_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_clone_aligned(checked);
      save_tool_settings();
    }
  });

  add_option_label(tr("Diffusion:"), {CanvasTool::Healing});
  auto* healing_diffusion = new QSpinBox(toolbar);
  healing_diffusion->setObjectName(QStringLiteral("healingDiffusionSpin"));
  healing_diffusion->setRange(1, 7);
  healing_diffusion->setValue(current_healing_diffusion_);
  healing_diffusion->setToolTip(tr("Lower values preserve fine texture; higher values adapt more quickly"));
  configure_toolbar_spinbox(healing_diffusion, 42);
  add_option_widget(healing_diffusion, {CanvasTool::Healing});
  connect(healing_diffusion, &QSpinBox::valueChanged, this, [this](int value) {
    current_healing_diffusion_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_healing_diffusion(value);
      save_tool_settings();
    }
  });

  add_option_label(tr("Strength:"), {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                                      CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  local_adjustment_strength_spin_ = new QSpinBox(toolbar);
  local_adjustment_strength_spin_->setObjectName(QStringLiteral("localAdjustmentStrengthSpin"));
  local_adjustment_strength_spin_->setRange(1, 100);
  local_adjustment_strength_spin_->setValue(current_local_adjustment_strength_);
  local_adjustment_strength_spin_->setSuffix(QStringLiteral("%"));
  local_adjustment_strength_spin_->setToolTip(tr("Maximum adjustment applied during one stroke"));
  bind_tooltip(local_adjustment_strength_spin_, "Maximum adjustment applied during one stroke");
  configure_toolbar_spinbox(local_adjustment_strength_spin_, 52);
  add_option_widget(local_adjustment_strength_spin_,
                    {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  connect(local_adjustment_strength_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    current_local_adjustment_strength_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_local_adjustment_strength(value);
    }
    schedule_save_tool_settings();
    refresh_document_info();
  });

  add_option_label(tr("Range:"), {CanvasTool::Dodge, CanvasTool::Burn});
  local_tone_range_combo_ = new QComboBox(toolbar);
  local_tone_range_combo_->setObjectName(QStringLiteral("localToneRangeCombo"));
  local_tone_range_combo_->addItem(tr("Shadows"), static_cast<int>(CanvasWidget::LocalToneRange::Shadows));
  local_tone_range_combo_->addItem(tr("Midtones"), static_cast<int>(CanvasWidget::LocalToneRange::Midtones));
  local_tone_range_combo_->addItem(tr("Highlights"), static_cast<int>(CanvasWidget::LocalToneRange::Highlights));
  local_tone_range_combo_->setCurrentIndex(
      std::max(0, local_tone_range_combo_->findData(static_cast<int>(current_local_tone_range_))));
  local_tone_range_combo_->setFixedWidth(92);
  add_option_widget(local_tone_range_combo_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_tone_range_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || local_tone_range_combo_ == nullptr) {
      return;
    }
    current_local_tone_range_ =
        static_cast<CanvasWidget::LocalToneRange>(local_tone_range_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_local_tone_range(current_local_tone_range_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> local_tone_range_combo(local_tone_range_combo_);
  register_retranslation([local_tone_range_combo] {
    if (local_tone_range_combo == nullptr || local_tone_range_combo->count() < 3) {
      return;
    }
    const QSignalBlocker blocker(local_tone_range_combo);
    local_tone_range_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Shadows"));
    local_tone_range_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Midtones"));
    local_tone_range_combo->setItemText(
        2, QCoreApplication::translate(kMainWindowTranslationContext, "Highlights"));
  });

  local_protect_tones_check_ = new CheckGlyphBox(tr("Protect Tones"), toolbar);
  local_protect_tones_check_->setObjectName(QStringLiteral("localProtectTonesCheck"));
  local_protect_tones_check_->setChecked(current_local_protect_tones_);
  local_protect_tones_check_->setToolTip(tr("Preserve local color differences while lightening or darkening"));
  bind_tooltip(local_protect_tones_check_, "Preserve local color differences while lightening or darkening");
  bind_widget_text(local_protect_tones_check_, "Protect Tones");
  add_option_widget(local_protect_tones_check_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_protect_tones_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_local_protect_tones_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_local_protect_tones(checked);
    }
    save_tool_settings();
  });

  add_option_label(tr("Mode:"), {CanvasTool::Sponge});
  sponge_mode_combo_ = new QComboBox(toolbar);
  sponge_mode_combo_->setObjectName(QStringLiteral("spongeModeCombo"));
  sponge_mode_combo_->addItem(tr("Saturate"), static_cast<int>(CanvasWidget::SpongeMode::Saturate));
  sponge_mode_combo_->addItem(tr("Desaturate"), static_cast<int>(CanvasWidget::SpongeMode::Desaturate));
  sponge_mode_combo_->setCurrentIndex(
      std::max(0, sponge_mode_combo_->findData(static_cast<int>(current_sponge_mode_))));
  sponge_mode_combo_->setFixedWidth(94);
  add_option_widget(sponge_mode_combo_, {CanvasTool::Sponge});
  connect(sponge_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || sponge_mode_combo_ == nullptr) {
      return;
    }
    current_sponge_mode_ =
        static_cast<CanvasWidget::SpongeMode>(sponge_mode_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_sponge_mode(current_sponge_mode_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> sponge_mode_combo(sponge_mode_combo_);
  register_retranslation([sponge_mode_combo] {
    if (sponge_mode_combo == nullptr || sponge_mode_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(sponge_mode_combo);
    sponge_mode_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Saturate"));
    sponge_mode_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Desaturate"));
  });

  sponge_vibrance_check_ = new CheckGlyphBox(tr("Vibrance"), toolbar);
  sponge_vibrance_check_->setObjectName(QStringLiteral("spongeVibranceCheck"));
  sponge_vibrance_check_->setChecked(current_sponge_vibrance_);
  sponge_vibrance_check_->setToolTip(tr("Reduce the adjustment on colors that are already strongly saturated"));
  bind_tooltip(sponge_vibrance_check_,
               "Reduce the adjustment on colors that are already strongly saturated");
  bind_widget_text(sponge_vibrance_check_, "Vibrance");
  add_option_widget(sponge_vibrance_check_, {CanvasTool::Sponge});
  connect(sponge_vibrance_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_sponge_vibrance_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_sponge_vibrance(checked);
    }
    save_tool_settings();
  });

  add_option_label(tr("Size:"), {CanvasTool::QuickSelect});
  auto* quick_select_size = new QSpinBox(toolbar);
  quick_select_size->setObjectName(QStringLiteral("quickSelectSizeSpin"));
  quick_select_size->setRange(1, 512);
  quick_select_size->setValue(canvas_defaults->quick_select_size());
  configure_toolbar_spinbox(quick_select_size, 46);
  add_option_widget(quick_select_size, {CanvasTool::QuickSelect});
  auto* quick_select_size_slider = new QSlider(Qt::Horizontal, toolbar);
  quick_select_size_slider->setObjectName(QStringLiteral("quickSelectSizeSlider"));
  quick_select_size_slider->setRange(1, 512);
  quick_select_size_slider->setValue(canvas_defaults->quick_select_size());
  quick_select_size_slider->setFixedWidth(150);
  quick_select_size_slider->setToolTip(tr("Quick Select brush size — press [ or ]"));
  bind_tooltip(quick_select_size_slider, "Quick Select brush size — press [ or ]");
  add_option_widget(quick_select_size_slider, {CanvasTool::QuickSelect});
  connect(quick_select_size, &QSpinBox::valueChanged, quick_select_size_slider, &QSlider::setValue);
  connect(quick_select_size_slider, &QSlider::valueChanged, quick_select_size, &QSpinBox::setValue);
  connect(quick_select_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_size(value);
      canvas_->refresh_tool_cursor();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  quick_select_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  quick_select_sample_all_layers_check_->setObjectName(QStringLiteral("quickSelectSampleAllLayersCheck"));
  quick_select_sample_all_layers_check_->setChecked(canvas_defaults->quick_select_sample_all_layers());
  quick_select_sample_all_layers_check_->setToolTip(tr("Sample the merged document instead of the active layer"));
  add_option_widget(quick_select_sample_all_layers_check_, {CanvasTool::QuickSelect});
  connect(quick_select_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  quick_select_enhance_edge_check_ = new CheckGlyphBox(tr("Enhance Edge"), toolbar);
  quick_select_enhance_edge_check_->setObjectName(QStringLiteral("quickSelectEnhanceEdgeCheck"));
  quick_select_enhance_edge_check_->setChecked(canvas_defaults->quick_select_enhance_edge());
  quick_select_enhance_edge_check_->setToolTip(tr("Smooth the selection boundary after each stroke"));
  add_option_widget(quick_select_enhance_edge_check_, {CanvasTool::QuickSelect});
  connect(quick_select_enhance_edge_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_enhance_edge(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  add_option_label(tr("Width:"), {CanvasTool::MagneticLasso});
  auto* magnetic_width = new QSpinBox(toolbar);
  magnetic_width->setObjectName(QStringLiteral("magneticLassoWidthSpin"));
  magnetic_width->setRange(1, 256);
  magnetic_width->setSuffix(QStringLiteral(" px"));
  magnetic_width->setValue(canvas_defaults->magnetic_lasso_width());
  magnetic_width->setToolTip(tr("Edge search width in document pixels — press [ or ]"));
  bind_tooltip(magnetic_width, "Edge search width in document pixels — press [ or ]");
  configure_toolbar_spinbox(magnetic_width, 64);
  add_option_widget(magnetic_width, {CanvasTool::MagneticLasso});
  connect(magnetic_width, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_width(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(tr("Contrast:"), {CanvasTool::MagneticLasso});
  auto* magnetic_contrast = new QSpinBox(toolbar);
  magnetic_contrast->setObjectName(QStringLiteral("magneticLassoContrastSpin"));
  magnetic_contrast->setRange(1, 100);
  magnetic_contrast->setSuffix(QStringLiteral("%"));
  magnetic_contrast->setValue(canvas_defaults->magnetic_lasso_edge_contrast());
  magnetic_contrast->setToolTip(tr("Minimum edge contrast the trace snaps to"));
  bind_tooltip(magnetic_contrast, "Minimum edge contrast the trace snaps to");
  configure_toolbar_spinbox(magnetic_contrast, 56);
  add_option_widget(magnetic_contrast, {CanvasTool::MagneticLasso});
  connect(magnetic_contrast, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_edge_contrast(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(tr("Frequency:"), {CanvasTool::MagneticLasso});
  auto* magnetic_frequency = new QSpinBox(toolbar);
  magnetic_frequency->setObjectName(QStringLiteral("magneticLassoFrequencySpin"));
  magnetic_frequency->setRange(0, 100);
  magnetic_frequency->setValue(canvas_defaults->magnetic_lasso_frequency());
  magnetic_frequency->setToolTip(tr("How often anchor points are placed while tracing"));
  bind_tooltip(magnetic_frequency, "How often anchor points are placed while tracing");
  configure_toolbar_spinbox(magnetic_frequency, 46);
  add_option_widget(magnetic_frequency, {CanvasTool::MagneticLasso});
  connect(magnetic_frequency, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_frequency(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_smaller_action = new QAction(tr("Brush Smaller"), this);
  auto* brush_larger_action = new QAction(tr("Brush Larger"), this);
  auto* brush_much_smaller_action = new QAction(tr("Brush Much Smaller"), this);
  auto* brush_much_larger_action = new QAction(tr("Brush Much Larger"), this);
  brush_smaller_action->setObjectName(QStringLiteral("brushSmallerAction"));
  brush_larger_action->setObjectName(QStringLiteral("brushLargerAction"));
  brush_much_smaller_action->setObjectName(QStringLiteral("brushMuchSmallerAction"));
  brush_much_larger_action->setObjectName(QStringLiteral("brushMuchLargerAction"));
  register_hotkey(brush_smaller_action, "brush.smaller", QKeySequence(Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_larger_action, "brush.larger", QKeySequence(Qt::Key_BracketRight), QStringLiteral("brush"));
  register_hotkey(brush_much_smaller_action, "brush.much_smaller", QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_much_larger_action, "brush.much_larger", QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), QStringLiteral("brush"));
  addAction(brush_smaller_action);
  addAction(brush_larger_action);
  addAction(brush_much_smaller_action);
  addAction(brush_much_larger_action);
  // The bracket keys resize whichever brush the active tool uses (Quick Select
  // has its own; for the Magnetic Lasso they adjust the edge search width).
  const auto adjust_brush_size = [this, brush_size, quick_select_size, magnetic_width](int direction, bool coarse) {
    const bool quick_select = current_tool_ == CanvasTool::QuickSelect;
    const bool magnetic = current_tool_ == CanvasTool::MagneticLasso;
    auto* spin = quick_select ? quick_select_size : magnetic ? magnetic_width : brush_size;
    const int cap = quick_select ? 512 : magnetic ? 256 : kMaxBrushSize;
    const int value = spin->value();
    const int step = proportional_brush_step(value, direction, coarse);
    spin->setValue(std::clamp(value + direction * step, 1, cap));
  };
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, false); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, false); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, true); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, true); });
  for (auto* action : {brush_smaller_action, brush_larger_action, brush_much_smaller_action,
                       brush_much_larger_action}) {
    register_document_action(action);
  }

  add_option_label(tr("Tol:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_defaults->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_tolerance(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  wand_contiguous_check_ = new CheckGlyphBox(tr("Contiguous"), toolbar);
  wand_contiguous_check_->setObjectName(QStringLiteral("wandContiguousCheck"));
  wand_contiguous_check_->setChecked(canvas_defaults->wand_contiguous());
  wand_contiguous_check_->setToolTip(tr("Limit Magic Wand selection to connected pixels"));
  add_option_widget(wand_contiguous_check_, {CanvasTool::MagicWand});
  connect(wand_contiguous_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_contiguous(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  wand_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  wand_sample_all_layers_check_->setObjectName(QStringLiteral("wandSampleAllLayersCheck"));
  wand_sample_all_layers_check_->setChecked(canvas_defaults->wand_sample_all_layers());
  wand_sample_all_layers_check_->setToolTip(tr("Sample the merged document instead of the active layer"));
  add_option_widget(wand_sample_all_layers_check_, {CanvasTool::MagicWand});
  connect(wand_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  // Shape | Path | Pixels for the vector-capable draw tools (Shape is the
  // Photoshop-parity default; Pixels is the legacy raster behavior). The
  // vector appearance/combine widgets below register for the same tools and
  // refresh_vector_tool_options_visibility() refines them per mode.
  add_option_label(tr("Mode:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape});
  vector_mode_combo_ = new QComboBox(toolbar);
  vector_mode_combo_->setObjectName(QStringLiteral("vectorModeCombo"));
  vector_mode_combo_->addItems({tr("Shape"), tr("Path"), tr("Pixels")});
  vector_mode_combo_->setCurrentIndex(0);
  vector_mode_combo_->setFixedWidth(76);
  vector_mode_combo_->setToolTip(
      tr("What the shape tools create: a shape layer, work-path subpaths, or raster pixels"));
  QPointer<QComboBox> vector_mode_combo_pointer(vector_mode_combo_);
  register_retranslation([vector_mode_combo_pointer] {
    if (vector_mode_combo_pointer == nullptr || vector_mode_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(vector_mode_combo_pointer);
    // MainWindow::tr (not QObject::tr): "Pixels"/"Subtract" exist in the
    // QObject context with unrelated meanings (color mode, blend mode).
    vector_mode_combo_pointer->setItemText(0, MainWindow::tr("Shape"));
    vector_mode_combo_pointer->setItemText(1, MainWindow::tr("Path"));
    vector_mode_combo_pointer->setItemText(2, MainWindow::tr("Pixels"));
  });
  add_option_widget(vector_mode_combo_, {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape});
  connect(vector_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    current_vector_tool_mode_ = index == 1   ? VectorToolMode::Path
                                : index == 2 ? VectorToolMode::Pixels
                                             : VectorToolMode::Shape;
    if (canvas_ != nullptr) {
      canvas_->set_vector_tool_mode(current_vector_tool_mode_);
      schedule_save_tool_settings();
    }
    refresh_options_bar();
  });

  // The appearance controls also register for the path-select tools: there
  // they show only while an editable shape layer is active and live-edit it
  // (refresh_vector_tool_options_visibility refines; Photoshop's behavior).
  const std::initializer_list<CanvasTool> vector_appearance_tools{
      CanvasTool::Line,    CanvasTool::Rectangle,  CanvasTool::Ellipse,
      CanvasTool::Pen,     CanvasTool::Polygon,    CanvasTool::CustomShape,
      CanvasTool::PathSelect, CanvasTool::DirectSelect};
  vector_shape_mode_option_widgets_.push_back(
      add_option_label(tr("Fill:"), vector_appearance_tools));
  vector_fill_swatch_button_ = new QToolButton(toolbar);
  vector_fill_swatch_button_->setObjectName(QStringLiteral("vectorFillSwatchButton"));
  vector_fill_swatch_button_->setToolTip(tr("Shape fill: none, solid color, gradient, or pattern"));
  vector_fill_swatch_button_->setAutoRaise(true);
  vector_fill_swatch_button_->setProperty("optionsBarButton", true);
  add_option_widget(vector_fill_swatch_button_, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_fill_swatch_button_);
  connect(vector_fill_swatch_button_, &QToolButton::clicked, this,
          [this] { show_vector_paint_menu(false); });

  auto* vector_stroke_check = new CheckGlyphBox(tr("Stroke"), toolbar);
  vector_stroke_check->setObjectName(QStringLiteral("vectorStrokeCheck"));
  vector_stroke_check->setChecked(current_vector_stroke_enabled_);
  vector_stroke_check->setToolTip(tr("Stroke the shape outline"));
  add_option_widget(vector_stroke_check, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_stroke_check);
  connect(vector_stroke_check, &QCheckBox::toggled, this, [this](bool checked) {
    current_vector_stroke_enabled_ = checked;
    schedule_save_tool_settings();
    apply_options_bar_appearance_to_active_shape();
  });

  vector_stroke_swatch_button_ = new QToolButton(toolbar);
  vector_stroke_swatch_button_->setObjectName(QStringLiteral("vectorStrokeSwatchButton"));
  vector_stroke_swatch_button_->setToolTip(
      tr("Shape stroke: solid color, gradient, or pattern"));
  vector_stroke_swatch_button_->setAutoRaise(true);
  vector_stroke_swatch_button_->setProperty("optionsBarButton", true);
  add_option_widget(vector_stroke_swatch_button_, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_stroke_swatch_button_);
  connect(vector_stroke_swatch_button_, &QToolButton::clicked, this,
          [this] { show_vector_paint_menu(true); });

  auto* vector_stroke_width = new QDoubleSpinBox(toolbar);
  vector_stroke_width->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
  vector_stroke_width->setRange(0.1, 1000.0);
  vector_stroke_width->setDecimals(1);
  vector_stroke_width->setValue(current_vector_stroke_width_);
  vector_stroke_width->setSuffix(QStringLiteral(" px"));
  vector_stroke_width->setToolTip(tr("Stroke width"));
  configure_toolbar_spinbox(vector_stroke_width, 64);
  add_option_widget(vector_stroke_width, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_stroke_width);
  connect(vector_stroke_width, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    current_vector_stroke_width_ = value;
    schedule_save_tool_settings();
    schedule_vector_appearance_apply();
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(tr("Weight:"), {CanvasTool::Line}));
  auto* vector_line_weight = new QSpinBox(toolbar);
  vector_line_weight->setObjectName(QStringLiteral("vectorLineWeightSpin"));
  vector_line_weight->setRange(1, 1000);
  vector_line_weight->setValue(current_vector_line_weight_);
  vector_line_weight->setSuffix(QStringLiteral(" px"));
  vector_line_weight->setToolTip(tr("Line thickness"));
  configure_toolbar_spinbox(vector_line_weight, 58);
  add_option_widget(vector_line_weight, {CanvasTool::Line});
  vector_vector_mode_option_widgets_.push_back(vector_line_weight);
  connect(vector_line_weight, &QSpinBox::valueChanged, this, [this](int value) {
    current_vector_line_weight_ = value;
    schedule_save_tool_settings();
  });

  vector_vector_mode_option_widgets_.push_back(add_option_label(
      tr("Combine:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                       CanvasTool::Polygon, CanvasTool::CustomShape,
                       CanvasTool::PathSelect, CanvasTool::DirectSelect}));
  auto* vector_combine_combo = new QComboBox(toolbar);
  vector_combine_combo->setObjectName(QStringLiteral("vectorCombineCombo"));
  vector_combine_combo->addItems(
      {tr("New Layer"), tr("Add"), tr("Subtract"), tr("Intersect"), tr("Exclude")});
  vector_combine_combo->setCurrentIndex(0);
  vector_combine_combo->setFixedWidth(96);
  vector_combine_combo->setToolTip(
      tr("How the next shape combines with the active shape layer or work path"));
  QPointer<QComboBox> vector_combine_combo_pointer(vector_combine_combo);
  register_retranslation([vector_combine_combo_pointer] {
    if (vector_combine_combo_pointer == nullptr || vector_combine_combo_pointer->count() < 5) {
      return;
    }
    QSignalBlocker blocker(vector_combine_combo_pointer);
    vector_combine_combo_pointer->setItemText(0, MainWindow::tr("New Layer"));
    vector_combine_combo_pointer->setItemText(1, MainWindow::tr("Add"));
    vector_combine_combo_pointer->setItemText(2, MainWindow::tr("Subtract"));
    vector_combine_combo_pointer->setItemText(3, MainWindow::tr("Intersect"));
    vector_combine_combo_pointer->setItemText(4, MainWindow::tr("Exclude"));
  });
  add_option_widget(vector_combine_combo,
                    {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape,
                     CanvasTool::PathSelect, CanvasTool::DirectSelect});
  vector_vector_mode_option_widgets_.push_back(vector_combine_combo);
  connect(vector_combine_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
    current_vector_combine_index_ = index;
    // With a path-select selection, the combo edits the selected shapes'
    // combine operation in place (indices 1-4; "New Layer" is creation-only).
    if (canvas_ != nullptr && index >= 1 &&
        (canvas_->tool() == CanvasTool::PathSelect ||
         canvas_->tool() == CanvasTool::DirectSelect) &&
        canvas_->path_edit_has_selection()) {
      canvas_->set_selected_subpaths_combine_op(index == 1   ? patchy::PathCombineOp::Add
                                                : index == 2 ? patchy::PathCombineOp::Subtract
                                                : index == 3 ? patchy::PathCombineOp::Intersect
                                                             : patchy::PathCombineOp::Xor);
    }
  });
  update_vector_swatch_icons();

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(tr("Sides:"), {CanvasTool::Polygon}));
  auto* polygon_sides = new QSpinBox(toolbar);
  polygon_sides->setObjectName(QStringLiteral("polygonSidesSpin"));
  polygon_sides->setRange(3, 100);
  polygon_sides->setValue(5);
  configure_toolbar_spinbox(polygon_sides, 52);
  add_option_widget(polygon_sides, {CanvasTool::Polygon});
  vector_vector_mode_option_widgets_.push_back(polygon_sides);
  connect(polygon_sides, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_polygon_sides(value);
      schedule_save_tool_settings();
    }
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(tr("Star inset:"), {CanvasTool::Polygon}));
  auto* polygon_star_inset = new QSpinBox(toolbar);
  polygon_star_inset->setObjectName(QStringLiteral("polygonStarInsetSpin"));
  polygon_star_inset->setRange(0, 99);
  polygon_star_inset->setValue(0);
  polygon_star_inset->setSuffix(QStringLiteral("%"));
  polygon_star_inset->setToolTip(tr("0 makes a plain polygon; higher values pull in star points"));
  configure_toolbar_spinbox(polygon_star_inset, 56);
  add_option_widget(polygon_star_inset, {CanvasTool::Polygon});
  vector_vector_mode_option_widgets_.push_back(polygon_star_inset);
  connect(polygon_star_inset, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_polygon_star_inset(value);
      schedule_save_tool_settings();
    }
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(tr("Shape:"), {CanvasTool::CustomShape}));
  custom_shape_combo_ = new QComboBox(toolbar);
  custom_shape_combo_->setObjectName(QStringLiteral("customShapeCombo"));
  custom_shape_combo_->setIconSize(QSize(24, 24));
  custom_shape_combo_->setFixedWidth(150);
  refresh_custom_shape_combo();
  add_option_widget(custom_shape_combo_, {CanvasTool::CustomShape});
  vector_vector_mode_option_widgets_.push_back(custom_shape_combo_);
  connect(custom_shape_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    apply_custom_shape_selection();
    schedule_save_tool_settings();
  });

  auto* line_arrow_start = new CheckGlyphBox(tr("Arrow start"), toolbar);
  line_arrow_start->setObjectName(QStringLiteral("lineArrowStartCheck"));
  line_arrow_start->setToolTip(tr("Add an arrowhead at the line start"));
  add_option_widget(line_arrow_start, {CanvasTool::Line});
  vector_vector_mode_option_widgets_.push_back(line_arrow_start);
  connect(line_arrow_start, &QCheckBox::toggled, this, [this](bool checked) {
    current_line_arrow_start_ = checked;
    schedule_save_tool_settings();
  });
  auto* line_arrow_end = new CheckGlyphBox(tr("Arrow end"), toolbar);
  line_arrow_end->setObjectName(QStringLiteral("lineArrowEndCheck"));
  line_arrow_end->setToolTip(tr("Add an arrowhead at the line end"));
  add_option_widget(line_arrow_end, {CanvasTool::Line});
  vector_vector_mode_option_widgets_.push_back(line_arrow_end);
  connect(line_arrow_end, &QCheckBox::toggled, this, [this](bool checked) {
    current_line_arrow_end_ = checked;
    schedule_save_tool_settings();
  });

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(fill_shapes);
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) {
    current_fill_shapes_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_fill_shapes(checked);
    }
  });

  add_option_label(tr("Radius:"), {CanvasTool::Rectangle});
  auto* shape_corner_radius = new QSpinBox(toolbar);
  shape_corner_radius->setObjectName(QStringLiteral("shapeCornerRadiusSpin"));
  shape_corner_radius->setRange(0, 512);
  shape_corner_radius->setValue(canvas_defaults->shape_corner_radius());
  shape_corner_radius->setSuffix(QStringLiteral(" px"));
  shape_corner_radius->setToolTip(tr("Rounded-corner radius for the rectangle tool (0 = sharp corners)"));
  configure_toolbar_spinbox(shape_corner_radius, 64);
  add_option_widget(shape_corner_radius, {CanvasTool::Rectangle});
  connect(shape_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_shape_corner_radius_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_shape_corner_radius(value);
      schedule_save_tool_settings();
    }
  });

  // Style / Width / Height for the shape draw tools, mirroring the marquee's
  // Normal / Fixed Ratio / Fixed Size options (session-only, like the marquee's).
  add_option_label(tr("Style:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_style_combo = new QComboBox(toolbar);
  shape_style_combo->setObjectName(QStringLiteral("shapeStyleCombo"));
  shape_style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  shape_style_combo->setCurrentText(tr("Normal"));
  shape_style_combo->setFixedWidth(92);
  QPointer<QComboBox> shape_style_combo_pointer(shape_style_combo);
  register_retranslation([shape_style_combo_pointer] {
    if (shape_style_combo_pointer == nullptr || shape_style_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(shape_style_combo_pointer);
    shape_style_combo_pointer->setItemText(0, QObject::tr("Normal"));
    shape_style_combo_pointer->setItemText(1, QObject::tr("Fixed Ratio"));
    shape_style_combo_pointer->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(shape_style_combo, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Width:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_fixed_width = new QSpinBox(toolbar);
  shape_fixed_width->setObjectName(QStringLiteral("shapeFixedWidthSpin"));
  shape_fixed_width->setRange(1, 30000);
  shape_fixed_width->setValue(has_active_document() ? document().width() : 1024);
  shape_fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(shape_fixed_width, 78);
  add_option_widget(shape_fixed_width, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Height:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_fixed_height = new QSpinBox(toolbar);
  shape_fixed_height->setObjectName(QStringLiteral("shapeFixedHeightSpin"));
  shape_fixed_height->setRange(1, 30000);
  shape_fixed_height->setValue(has_active_document() ? document().height() : 768);
  shape_fixed_height->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(shape_fixed_height, 78);
  add_option_widget(shape_fixed_height, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  const auto apply_shape_style_settings = [this, shape_style_combo, shape_fixed_width, shape_fixed_height] {
    switch (shape_style_combo->currentIndex()) {
      case 1:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_shape_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_shape_width_ = shape_fixed_width->value();
    current_shape_height_ = shape_fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_shape_style(current_shape_style_);
      canvas_->set_shape_fixed_size(current_shape_width_, current_shape_height_);
    }
  };
  connect(shape_style_combo, &QComboBox::currentIndexChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_width, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_height, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });

  // Fill tool / Fill hotkey settings (independent of the brush; default 100% opacity, 0 softness).
  add_option_label(tr("Opacity:"), {CanvasTool::Fill});
  auto* fill_opacity = new QSpinBox(toolbar);
  fill_opacity->setObjectName(QStringLiteral("fillOpacitySpin"));
  fill_opacity->setRange(1, 100);
  fill_opacity->setValue(canvas_defaults->fill_opacity());
  fill_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_opacity, 52);
  add_option_widget(fill_opacity, {CanvasTool::Fill});
  auto* fill_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_opacity_slider->setObjectName(QStringLiteral("fillOpacitySlider"));
  fill_opacity_slider->setRange(1, 100);
  fill_opacity_slider->setValue(canvas_defaults->fill_opacity());
  fill_opacity_slider->setFixedWidth(120);
  fill_opacity_slider->setToolTip(tr("Fill opacity for the Fill tool and Fill shortcut"));
  add_option_widget(fill_opacity_slider, {CanvasTool::Fill});
  add_option_label(tr("Soft:"), {CanvasTool::Fill});
  auto* fill_softness = new QSpinBox(toolbar);
  fill_softness->setObjectName(QStringLiteral("fillSoftnessSpin"));
  fill_softness->setRange(0, 100);
  fill_softness->setValue(canvas_defaults->fill_softness());
  fill_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_softness, 52);
  add_option_widget(fill_softness, {CanvasTool::Fill});
  auto* fill_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_softness_slider->setObjectName(QStringLiteral("fillSoftnessSlider"));
  fill_softness_slider->setRange(0, 100);
  fill_softness_slider->setValue(canvas_defaults->fill_softness());
  fill_softness_slider->setFixedWidth(110);
  fill_softness_slider->setToolTip(tr("Soft edge feather for the Fill tool and Fill shortcut"));
  add_option_widget(fill_softness_slider, {CanvasTool::Fill});
  connect(fill_opacity, &QSpinBox::valueChanged, fill_opacity_slider, &QSlider::setValue);
  connect(fill_opacity_slider, &QSlider::valueChanged, fill_opacity, &QSpinBox::setValue);
  connect(fill_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_opacity(value);
      schedule_save_tool_settings();
    }
  });
  connect(fill_softness, &QSpinBox::valueChanged, fill_softness_slider, &QSlider::setValue);
  connect(fill_softness_slider, &QSlider::valueChanged, fill_softness, &QSpinBox::setValue);
  connect(fill_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_softness(value);
      schedule_save_tool_settings();
    }
  });

  add_option_label(tr("Font:"), {CanvasTool::Text});
  text_font_combo_ = new FontPickerCombo(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  text_font_combo_->setFixedWidth(210);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  add_option_label(tr("Size:"), {CanvasTool::Text});
  text_size_spin_ = new QDoubleSpinBox(toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->setDecimals(3);
  text_size_spin_->setRange(0.01, 10000.0);
  // Typing accepts up to 10000 pt, but the popup slider stays usable at 0..200.
  text_size_spin_->setProperty(kToolbarSpinboxSliderMaxProperty, 200.0);
  text_size_spin_->setSingleStep(0.25);
  // 48 px at the default document's 72 ppi = 48 pt (startup builds the bar with
  // no document open).
  text_size_spin_->setValue(has_active_document() ? text_pixels_to_points(48, document()) : 48.0);
  text_size_spin_->setSuffix(tr(" pt"));
  configure_toolbar_spinbox(text_size_spin_, 74);
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
  add_option_label(tr("Smoothing:"), {CanvasTool::Text});
  text_smoothing_combo_ = new QComboBox(toolbar);
  text_smoothing_combo_->setObjectName(QStringLiteral("textSmoothingCombo"));
  text_smoothing_combo_->setToolTip(tr("Text smoothing"));
  text_smoothing_combo_->addItem(tr("None"), 0);
  text_smoothing_combo_->addItem(tr("Sharp"), 4);
  text_smoothing_combo_->addItem(tr("Crisp"), 2);
  text_smoothing_combo_->addItem(tr("Strong"), 1);
  text_smoothing_combo_->addItem(tr("Smooth"), 3);
  text_smoothing_combo_->addItem(tr("Windows LCD"), 5);
  text_smoothing_combo_->addItem(tr("Windows"), 6);
  text_smoothing_combo_->setFixedWidth(116);
  set_text_smoothing_combo_value(
      text_smoothing_combo_,
      app_settings().value(QStringLiteral("tools/textSmoothing"), kDefaultTextAntiAlias).toInt());
  add_option_widget(text_smoothing_combo_, {CanvasTool::Text});
  add_option_label(tr("Color:"), {CanvasTool::Text});
  text_color_button_ = new QPushButton(tr("T"), toolbar);
  text_color_button_->setObjectName(QStringLiteral("textColorButton"));
  text_color_button_->setToolTip(tr("Text color"));
  text_color_button_->setFixedSize(30, 26);
  add_option_widget(text_color_button_, {CanvasTool::Text});
  add_option_label(tr("Align:"), {CanvasTool::Text});
  auto* text_alignment_group = new QButtonGroup(toolbar);
  text_alignment_group->setExclusive(true);
  text_align_left_button_ = new QPushButton(tr("L"), toolbar);
  text_align_left_button_->setObjectName(QStringLiteral("textAlignLeftButton"));
  text_align_left_button_->setCheckable(true);
  text_align_left_button_->setChecked(true);
  text_align_left_button_->setToolTip(tr("Align Left"));
  text_align_left_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_left_button_);
  add_option_widget(text_align_left_button_, {CanvasTool::Text});
  text_align_center_button_ = new QPushButton(tr("C"), toolbar);
  text_align_center_button_->setObjectName(QStringLiteral("textAlignCenterButton"));
  text_align_center_button_->setCheckable(true);
  text_align_center_button_->setToolTip(tr("Align Center"));
  text_align_center_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_center_button_);
  add_option_widget(text_align_center_button_, {CanvasTool::Text});
  text_align_right_button_ = new QPushButton(tr("R"), toolbar);
  text_align_right_button_->setObjectName(QStringLiteral("textAlignRightButton"));
  text_align_right_button_->setCheckable(true);
  text_align_right_button_->setToolTip(tr("Align Right"));
  text_align_right_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_right_button_);
  add_option_widget(text_align_right_button_, {CanvasTool::Text});
  text_warp_button_ = new QPushButton(tr("Warp..."), toolbar);
  text_warp_button_->setObjectName(QStringLiteral("textWarpButton"));
  text_warp_button_->setToolTip(tr("Warp Text (Photoshop-style styles: arc, flag, fish, ...)"));
  add_option_widget(text_warp_button_, {CanvasTool::Text});
  // Character panel: works on the LIVE editor session, so Qt::NoFocus is load-bearing here
  // exactly like the session apply/cancel buttons (a focus-taking button would fire the
  // editor's focus-loss auto-commit on mouse press).
  text_character_button_ = new QPushButton(tr("Character..."), toolbar);
  text_character_button_->setObjectName(QStringLiteral("textCharacterButton"));
  text_character_button_->setToolTip(tr("Character panel (leading, tracking, glyph scales)"));
  text_character_button_->setFocusPolicy(Qt::NoFocus);
  add_option_widget(text_character_button_, {CanvasTool::Text});
  connect(text_character_button_, &QPushButton::clicked, this, [this] { open_text_character_dialog(); });
  connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) { apply_text_family_to_active_editor(); });
  connect(text_size_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double) {
    apply_text_size_to_active_editor();
    refresh_document_info();
  });
  connect(text_bold_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_bold_to_active_editor(); });
  connect(text_italic_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_italic_to_active_editor(); });
  connect(text_smoothing_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            apply_text_smoothing_to_active_editor();
            save_tool_settings();
            refresh_document_info();
          });
  connect(text_color_button_, &QPushButton::clicked, this, [this] { choose_text_color(); });
  connect(text_align_left_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignLeft); });
  connect(text_align_center_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignHCenter); });
  connect(text_align_right_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignRight); });
  connect(text_warp_button_, &QPushButton::clicked, this, [this] { request_warp_text_dialog(); });
  // Session apply/cancel, shown only while an inline text editor is open (the
  // text controls above stay visible too -- they apply live to the editor, so
  // unlike a transform session the bar keeps them).  Qt::NoFocus is load-bearing:
  // a focus-taking button would fire the editor's focus-loss auto-commit on
  // mouse press, committing the text before a Cancel click could cancel it.
  text_apply_button_ = new QPushButton(toolbar);
  text_apply_button_->setObjectName(QStringLiteral("textApplyButton"));
  text_apply_button_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  text_apply_button_->setToolTip(tr("Apply text edit"));
  text_apply_button_->setFixedWidth(30);
  text_apply_button_->setIconSize(QSize(20, 20));
  text_apply_button_->setProperty("optionsSessionButton", true);
  text_apply_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_apply_button_);
  text_cancel_button_ = new QPushButton(toolbar);
  text_cancel_button_->setObjectName(QStringLiteral("textCancelButton"));
  text_cancel_button_->setIcon(simple_icon(QStringLiteral("clear"), QColor(255, 150, 150)));
  text_cancel_button_->setToolTip(tr("Cancel text edit"));
  text_cancel_button_->setFixedWidth(30);
  text_cancel_button_->setIconSize(QSize(20, 20));
  text_cancel_button_->setProperty("optionsSessionButton", true);
  text_cancel_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_cancel_button_);
  connect(text_apply_button_, &QPushButton::clicked, this, [this] { commit_active_text_editor(); });
  connect(text_cancel_button_, &QPushButton::clicked, this, [this] { cancel_active_text_editor(); });

  // Export the cross-phase locals bind_action_translations() still needs.
  ctx.options_toolbar = toolbar;
  ctx.brush_smaller_action = brush_smaller_action;
  ctx.brush_larger_action = brush_larger_action;
  ctx.brush_much_smaller_action = brush_much_smaller_action;
  ctx.brush_much_larger_action = brush_much_larger_action;
  ctx.fill_shapes = fill_shapes;
}

}  // namespace patchy::ui
