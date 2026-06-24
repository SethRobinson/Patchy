#include "ui/main_window.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_presets.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/print_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
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
#include <QButtonGroup>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
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

namespace patchy::ui {

namespace {

constexpr const char* kLayerContentThumbnailRevisionProperty = "patchyContentRevision";
constexpr const char* kLayerMaskThumbnailRevisionProperty = "patchyMaskRevision";

QString pen_button_action_to_token(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::None:
      return QStringLiteral("none");
    case PenButtonAction::PanCanvas:
      return QStringLiteral("pan");
    case PenButtonAction::ZoomCanvas:
      return QStringLiteral("zoom");
    case PenButtonAction::PickColor:
      return QStringLiteral("pickColor");
    case PenButtonAction::SetCloneSource:
      return QStringLiteral("setCloneSource");
    case PenButtonAction::SwapColors:
      return QStringLiteral("swapColors");
    case PenButtonAction::Undo:
      return QStringLiteral("undo");
    case PenButtonAction::Redo:
      return QStringLiteral("redo");
    case PenButtonAction::ToggleEraser:
      return QStringLiteral("toggleEraser");
    case PenButtonAction::IncreaseBrushSize:
      return QStringLiteral("increaseBrushSize");
    case PenButtonAction::DecreaseBrushSize:
      return QStringLiteral("decreaseBrushSize");
  }
  return QStringLiteral("none");
}

PenButtonAction pen_button_action_from_token(const QString& token) {
  if (token == QStringLiteral("pan")) {
    return PenButtonAction::PanCanvas;
  }
  if (token == QStringLiteral("zoom")) {
    return PenButtonAction::ZoomCanvas;
  }
  if (token == QStringLiteral("pickColor")) {
    return PenButtonAction::PickColor;
  }
  if (token == QStringLiteral("setCloneSource")) {
    return PenButtonAction::SetCloneSource;
  }
  if (token == QStringLiteral("swapColors")) {
    return PenButtonAction::SwapColors;
  }
  if (token == QStringLiteral("undo")) {
    return PenButtonAction::Undo;
  }
  if (token == QStringLiteral("redo")) {
    return PenButtonAction::Redo;
  }
  if (token == QStringLiteral("toggleEraser")) {
    return PenButtonAction::ToggleEraser;
  }
  if (token == QStringLiteral("increaseBrushSize")) {
    return PenButtonAction::IncreaseBrushSize;
  }
  if (token == QStringLiteral("decreaseBrushSize")) {
    return PenButtonAction::DecreaseBrushSize;
  }
  return PenButtonAction::None;
}

bool ui_profile_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_UI_PROFILE");
  return enabled;
}

void log_ui_profile(std::string_view stage, double elapsed_ms, std::string_view detail = {}) {
  if (!ui_profile_enabled()) {
    return;
  }
  std::cerr << "PATCHY_UI_PROFILE stage=" << stage << " elapsed_ms=" << elapsed_ms;
  if (!detail.empty()) {
    std::cerr << " detail=\"" << detail << "\"";
  }
  std::cerr << '\n';
}

int undo_snapshot_test_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", &ok);
  return ok ? std::max(0, value) : 0;
}

constexpr auto kTranslationContextProperty = "patchy.translationContext";
constexpr auto kTranslationTextProperty = "patchy.translationText";
constexpr auto kTranslationToolTipProperty = "patchy.translationToolTip";
constexpr auto kTranslationStatusTipProperty = "patchy.translationStatusTip";
constexpr auto kMainWindowTranslationContext = "patchy::ui::MainWindow";
constexpr int kLayerRowBaseIndent = 6;
constexpr int kLayerFolderDisclosureWidth = 18;
constexpr int kLayerFolderDisclosureHeight = 20;
constexpr int kLayerRowHorizontalSpacing = 6;
constexpr int kLayerChildIndent = 22;
constexpr int kRightDockMinimumWidth = 280;
constexpr int kRightDockResizeHandleWidth = 7;
constexpr int kOpenProgressTitleReservedWidth = 140;
constexpr int kOpenProgressTitleMinimumFileNameWidth = 180;
constexpr int kFilterProgressMinimumDurationMs = 1000;
constexpr int kLayerOpacityApplyDelayMs = 33;
constexpr int kLayerOpacityIdleFinishDelayMs = 250;
constexpr int kMaxRecentFiles = 200;
constexpr int kMaxRecentFolders = 10;
constexpr int kRecentFilesMenuPageSize = 50;
constexpr auto kRecentFilesMenuProperty = "patchy.recentFilesMenu";
constexpr auto kRecentFoldersMenuProperty = "patchy.recentFoldersMenu";

template <typename Request>
struct AsyncPixelPreviewState {
  bool closed{false};
  bool in_flight{false};
  std::uint64_t generation{0};
  std::optional<Request> pending;
  std::function<void(const Request&)> start;
};

template <typename Request>
void enqueue_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state, Request request,
                                 bool immediate = false) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  if (!immediate && state->in_flight) {
    state->pending = std::move(request);
    return;
  }
  state->start(request);
}

template <typename Request>
void close_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  ++state->generation;
  state->pending.reset();
  state->start = {};
}

template <typename Settings>
struct AdjustmentPixelPreviewRequest {
  bool enabled{true};
  Settings settings{};
};

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

QString elided_open_progress_title_file_name(const QWidget& widget, const QString& file_name) {
  const int available_width =
      std::max(kOpenProgressTitleMinimumFileNameWidth, widget.sizeHint().width() - kOpenProgressTitleReservedWidth);
  return widget.fontMetrics().elidedText(file_name, Qt::ElideMiddle, available_width);
}

QString default_startup_brush_preset_id() {
  return QStringLiteral("ink");
}

void apply_brush_preset(CanvasWidget& canvas, const BrushPreset& preset) {
  canvas.set_brush_build_up(preset.build_up);
  canvas.set_brush_size(preset.size);
  canvas.set_brush_opacity(preset.opacity);
  canvas.set_brush_softness(preset.softness);
}

void trim_recent_files(QStringList& recent_files) {
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
}

QColor qcolor_from_edit_color(EditColor color) {
  return QColor(color.r, color.g, color.b, color.a);
}

QString gradient_css_stops(const std::vector<GradientStop>& stops, int opacity, bool reverse) {
  QStringList css_stops;
  const auto normalized = normalized_gradient_stops(stops);
  const auto global_opacity = static_cast<float>(std::clamp(opacity, 0, 100)) / 100.0F;
  for (const auto& stop : normalized) {
    auto color = stop.color;
    color.a = static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<float>(color.a) * global_opacity), 0L, 255L));
    css_stops << QStringLiteral("stop:%1 rgba(%2, %3, %4, %5)")
                     .arg(reverse ? 1.0 - static_cast<double>(stop.location) : static_cast<double>(stop.location),
                          0, 'f', 3)
                     .arg(color.r)
                     .arg(color.g)
                     .arg(color.b)
                     .arg(color.a);
  }
  return css_stops.join(QStringLiteral(", "));
}

QString gradient_preview_button_style(const std::vector<GradientStop>& stops, int opacity, bool reverse) {
  return QStringLiteral(
             "QPushButton { background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, %1); "
             "border: 1px solid #747b86; border-radius: 2px; min-width: 78px; min-height: 22px; padding: 0; }"
             "QPushButton:hover { border: 2px solid #63a6ff; }")
      .arg(gradient_css_stops(stops, opacity, reverse));
}

void update_gradient_preview_label(QLabel* preview, const std::vector<GradientStop>& stops, int opacity,
                                   bool reverse) {
  if (preview == nullptr) {
    return;
  }
  QPixmap pixmap(std::max(260, preview->width()), 32);
  QPainter painter(&pixmap);
  const int checker = 8;
  for (int y = 0; y < pixmap.height(); y += checker) {
    for (int x = 0; x < pixmap.width(); x += checker) {
      painter.fillRect(QRect(x, y, checker, checker),
                       ((x / checker) + (y / checker)) % 2 == 0 ? QColor(210, 215, 222) : QColor(140, 148, 158));
    }
  }
  QLinearGradient gradient(0, 0, pixmap.width(), 0);
  const auto normalized = normalized_gradient_stops(stops);
  const auto global_opacity = static_cast<float>(std::clamp(opacity, 0, 100)) / 100.0F;
  for (const auto& stop : normalized) {
    auto color = stop.color;
    color.a = static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<float>(color.a) * global_opacity), 0L, 255L));
    gradient.setColorAt(reverse ? 1.0 - static_cast<double>(stop.location) : static_cast<double>(stop.location),
                        qcolor_from_edit_color(color));
  }
  painter.fillRect(pixmap.rect(), gradient);
  painter.setPen(QColor(154, 164, 178));
  painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
  preview->setPixmap(pixmap);
}

class GradientStopTableDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    auto item_option = option;
    const auto selected = item_option.state.testFlag(QStyle::State_Selected);
    item_option.state.setFlag(QStyle::State_Selected, false);
    item_option.showDecorationSelected = false;
    QStyledItemDelegate::paint(painter, item_option, index);
    if (!selected) {
      return;
    }

    painter->save();
    QPen pen(QColor(99, 166, 255), 2);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
    painter->restore();
  }
};

std::optional<std::optional<std::vector<GradientStop>>> request_gradient_stops_dialog(
    QWidget* parent, const std::vector<GradientStop>& initial_stops, bool has_custom_stops, QColor foreground,
    QColor background) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("gradientStopsDialog"));
  dialog.resize(560, 430);

  auto* layout = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Edit Gradient Stops"));
  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("gradientStopsPreview"));
  preview->setFixedHeight(34);
  preview->setMinimumWidth(320);
  layout->addWidget(preview);

  auto* table = new QTableWidget(0, 3, &dialog);
  table->setObjectName(QStringLiteral("gradientStopsTable"));
  table->setHorizontalHeaderLabels({QObject::tr("Location %"), QObject::tr("Color"), QObject::tr("Alpha %")});
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setItemDelegate(new GradientStopTableDelegate(table));
  table->setMinimumHeight(180);
  layout->addWidget(table, 1);

  auto* selected_row = new QWidget(&dialog);
  auto* selected_layout = new QHBoxLayout(selected_row);
  selected_layout->setContentsMargins(0, 0, 0, 0);
  selected_layout->setSpacing(6);
  auto* choose_color = new QPushButton(QObject::tr("Choose Color..."), selected_row);
  choose_color->setObjectName(QStringLiteral("gradientChooseStopColorButton"));
  auto* add_stop = new QPushButton(QObject::tr("Add Stop"), selected_row);
  add_stop->setObjectName(QStringLiteral("gradientAddStopButton"));
  auto* remove_stop = new QPushButton(QObject::tr("Remove Stop"), selected_row);
  remove_stop->setObjectName(QStringLiteral("gradientRemoveStopButton"));
  auto* reset_stops = new QPushButton(QObject::tr("Reset to FG/BG"), selected_row);
  reset_stops->setObjectName(QStringLiteral("gradientResetStopsButton"));
  selected_layout->addWidget(choose_color);
  selected_layout->addWidget(add_stop);
  selected_layout->addWidget(remove_stop);
  selected_layout->addStretch(1);
  selected_layout->addWidget(reset_stops);
  layout->addWidget(selected_row);

  bool using_default_stops = !has_custom_stops;
  bool loading = false;

  const auto default_stops = [&foreground, &background] {
    auto primary = edit_color(foreground);
    primary.a = 255;
    auto secondary = edit_color(background);
    secondary.a = 255;
    return normalized_gradient_stops({GradientStop{0.0F, primary}, GradientStop{1.0F, secondary}});
  };
  const auto cell_value = [table](int row, int column, int fallback) {
    const auto* item = table->item(row, column);
    bool ok = false;
    const auto value = item == nullptr ? fallback : item->text().toInt(&ok);
    return ok ? value : fallback;
  };
  const auto row_color = [table](int row) {
    const auto* item = table->item(row, 1);
    if (item == nullptr) {
      return QColor(Qt::black);
    }
    const auto stored = item->data(Qt::UserRole).value<QColor>();
    const auto typed = QColor(item->text().trimmed());
    return typed.isValid() ? typed : stored.isValid() ? stored : QColor(Qt::black);
  };
  const auto set_item = [table](int row, int column, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(column == 1 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter);
    table->setItem(row, column, item);
    return item;
  };
  const auto update_row_color = [table, &row_color](int row) {
    if (row < 0 || row >= table->rowCount()) {
      return;
    }
    const auto color = row_color(row);
    auto* item = table->item(row, 1);
    if (item == nullptr) {
      return;
    }
    item->setText(color.name(QColor::HexRgb).toUpper());
    item->setData(Qt::UserRole, color);
    item->setBackground(color);
    const auto text = color.red() * 3 + color.green() * 6 + color.blue() > 1280 ? QColor(20, 24, 30)
                                                                                : QColor(245, 248, 252);
    item->setForeground(text);
  };
  const auto read_stops = [&] {
    std::vector<GradientStop> stops;
    stops.reserve(static_cast<std::size_t>(table->rowCount()));
    for (int row = 0; row < table->rowCount(); ++row) {
      const auto color = row_color(row);
      stops.push_back(GradientStop{
          std::clamp(static_cast<float>(cell_value(row, 0, row == 0 ? 0 : 100)) / 100.0F, 0.0F, 1.0F),
          EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                    static_cast<std::uint8_t>(color.blue()),
                    static_cast<std::uint8_t>(std::clamp(cell_value(row, 2, 100), 0, 100) * 255 / 100)}});
    }
    auto normalized = normalized_gradient_stops(stops);
    if (normalized.size() < 2U) {
      normalized = default_stops();
    }
    return normalized;
  };
  const auto refresh_preview = [&] {
    const QSignalBlocker blocker(table);
    for (int row = 0; row < table->rowCount(); ++row) {
      update_row_color(row);
    }
    update_gradient_preview_label(preview, read_stops(), 100, false);
    remove_stop->setEnabled(table->rowCount() > 2);
  };
  const auto add_row = [&](const GradientStop& stop) {
    const auto row = table->rowCount();
    table->insertRow(row);
    set_item(row, 0, QString::number(static_cast<int>(std::round(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F))));
    auto* color_item = set_item(row, 1, qcolor_from_edit_color(stop.color).name(QColor::HexRgb).toUpper());
    color_item->setData(Qt::UserRole, QColor(stop.color.r, stop.color.g, stop.color.b));
    set_item(row, 2, QString::number(static_cast<int>(std::round(static_cast<double>(stop.color.a) * 100.0 / 255.0))));
    update_row_color(row);
  };
  const auto load_stops = [&](const std::vector<GradientStop>& stops) {
    loading = true;
    const QSignalBlocker blocker(table);
    table->setRowCount(0);
    for (const auto& stop : normalized_gradient_stops(stops)) {
      add_row(stop);
    }
    if (table->rowCount() > 0) {
      table->setCurrentCell(0, 0);
    }
    loading = false;
    refresh_preview();
  };
  const auto choose_current_color = [&] {
    if (table->rowCount() <= 0) {
      return;
    }
    const auto row = std::clamp(table->currentRow(), 0, table->rowCount() - 1);
    const auto chosen = request_patchy_color(&dialog, row_color(row), QObject::tr("Choose Gradient Stop Color"));
    if (!chosen.has_value()) {
      return;
    }
    auto* item = table->item(row, 1);
    if (item == nullptr) {
      item = set_item(row, 1, QString());
    }
    item->setText(chosen->name(QColor::HexRgb).toUpper());
    item->setData(Qt::UserRole, *chosen);
    using_default_stops = false;
    refresh_preview();
  };

  load_stops(has_custom_stops ? initial_stops : default_stops());
  QObject::connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem*) {
    if (!loading) {
      using_default_stops = false;
    }
    refresh_preview();
  });
  QObject::connect(table, &QTableWidget::currentCellChanged, &dialog, [&](int, int, int, int) { refresh_preview(); });
  QObject::connect(add_stop, &QPushButton::clicked, &dialog, [&] {
    const auto source_row = std::clamp(table->currentRow(), 0, std::max(0, table->rowCount() - 1));
    const auto color = row_color(source_row);
    const auto location = std::clamp(cell_value(source_row, 0, 50) + (table->rowCount() > 0 ? 10 : 0), 0, 100);
    add_row(GradientStop{static_cast<float>(location) / 100.0F,
                         EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                                   static_cast<std::uint8_t>(color.blue()),
                                   static_cast<std::uint8_t>(std::clamp(cell_value(source_row, 2, 100), 0, 100) *
                                                             255 / 100)}});
    table->setCurrentCell(table->rowCount() - 1, 0);
    using_default_stops = false;
    refresh_preview();
  });
  QObject::connect(remove_stop, &QPushButton::clicked, &dialog, [&] {
    if (table->rowCount() <= 2) {
      return;
    }
    const auto row = std::clamp(table->currentRow(), 0, table->rowCount() - 1);
    table->removeRow(row);
    table->setCurrentCell(std::min(row, table->rowCount() - 1), 0);
    using_default_stops = false;
    refresh_preview();
  });
  QObject::connect(choose_color, &QPushButton::clicked, &dialog, choose_current_color);
  QObject::connect(table, &QTableWidget::cellDoubleClicked, &dialog, [table, &choose_current_color](int row, int column) {
    if (column != 1) {
      return;
    }
    table->setCurrentCell(std::clamp(row, 0, table->rowCount() - 1), 1);
    choose_current_color();
  });
  QObject::connect(reset_stops, &QPushButton::clicked, &dialog, [&] {
    load_stops(default_stops());
    using_default_stops = true;
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  remember_dialog_position(dialog);
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  if (using_default_stops) {
    return std::optional<std::vector<GradientStop>>{};
  }
  return read_stops();
}

QByteArray serialize_gradient_stops(const std::vector<GradientStop>& stops) {
  QJsonArray array;
  for (const auto& stop : normalized_gradient_stops(stops)) {
    QJsonObject object;
    object.insert(QStringLiteral("location"), std::clamp(stop.location, 0.0F, 1.0F));
    object.insert(QStringLiteral("r"), static_cast<int>(stop.color.r));
    object.insert(QStringLiteral("g"), static_cast<int>(stop.color.g));
    object.insert(QStringLiteral("b"), static_cast<int>(stop.color.b));
    object.insert(QStringLiteral("a"), static_cast<int>(stop.color.a));
    array.push_back(object);
  }
  return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

std::optional<std::vector<GradientStop>> deserialize_gradient_stops(const QByteArray& json) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isArray()) {
    return std::nullopt;
  }
  std::vector<GradientStop> stops;
  for (const auto& value : document.array()) {
    if (!value.isObject()) {
      return std::nullopt;
    }
    const auto object = value.toObject();
    const auto location = object.value(QStringLiteral("location"));
    const auto r = object.value(QStringLiteral("r"));
    const auto g = object.value(QStringLiteral("g"));
    const auto b = object.value(QStringLiteral("b"));
    const auto a = object.value(QStringLiteral("a"));
    if (!location.isDouble() || !r.isDouble() || !g.isDouble() || !b.isDouble() || !a.isDouble()) {
      return std::nullopt;
    }
    stops.push_back(GradientStop{
        std::clamp(static_cast<float>(location.toDouble()), 0.0F, 1.0F),
        EditColor{static_cast<std::uint8_t>(std::clamp(r.toInt(), 0, 255)),
                  static_cast<std::uint8_t>(std::clamp(g.toInt(), 0, 255)),
                  static_cast<std::uint8_t>(std::clamp(b.toInt(), 0, 255)),
                  static_cast<std::uint8_t>(std::clamp(a.toInt(), 0, 255))}});
  }
  if (stops.size() < 2U) {
    return std::nullopt;
  }
  return normalized_gradient_stops(stops);
}

QString translate_source(const QObject* object, const char* property_name) {
  const auto source = object->property(property_name).toString();
  if (source.isEmpty()) {
    return {};
  }
  auto context = object->property(kTranslationContextProperty).toString();
  if (context.isEmpty()) {
    context = QStringLiteral("patchy::ui::MainWindow");
  }
  const auto context_bytes = context.toUtf8();
  const auto source_bytes = source.toUtf8();
  return QCoreApplication::translate(context_bytes.constData(), source_bytes.constData());
}

void bind_translated_text(QObject* object, const char* source, const char* context = kMainWindowTranslationContext) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationTextProperty, QString::fromLatin1(source));
}

void bind_translated_tooltip(QObject* object, const char* source, const char* context = kMainWindowTranslationContext) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationToolTipProperty, QString::fromLatin1(source));
}

void bind_translated_status_tip(QObject* object, const char* source,
                                const char* context = kMainWindowTranslationContext) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationStatusTipProperty, QString::fromLatin1(source));
}

void apply_bound_translation(QObject* object) {
  if (object == nullptr) {
    return;
  }

  if (object->property(kTranslationTextProperty).isValid()) {
    const auto text = translate_source(object, kTranslationTextProperty);
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setText(text);
    } else if (auto* menu = qobject_cast<QMenu*>(object); menu != nullptr) {
      menu->setTitle(text);
    } else if (auto* dock = qobject_cast<QDockWidget*>(object); dock != nullptr) {
      dock->setWindowTitle(text);
    } else if (auto* toolbar = qobject_cast<QToolBar*>(object); toolbar != nullptr) {
      toolbar->setWindowTitle(text);
    } else if (auto* label = qobject_cast<QLabel*>(object); label != nullptr) {
      label->setText(text);
    } else if (auto* button = qobject_cast<QAbstractButton*>(object); button != nullptr) {
      button->setText(text);
    } else if (auto* group = qobject_cast<QGroupBox*>(object); group != nullptr) {
      group->setTitle(text);
    }
  }

  if (object->property(kTranslationToolTipProperty).isValid()) {
    const auto tooltip = translate_source(object, kTranslationToolTipProperty);
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setToolTip(tooltip);
    } else if (auto* widget = qobject_cast<QWidget*>(object); widget != nullptr) {
      widget->setToolTip(tooltip);
    }
  }

  if (object->property(kTranslationStatusTipProperty).isValid()) {
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setStatusTip(translate_source(object, kTranslationStatusTipProperty));
    }
  }
}

void bind_action_text(QAction* action, const char* source) {
  bind_translated_text(action, source);
  apply_bound_translation(action);
}

void bind_widget_text(QObject* object, const char* source) {
  bind_translated_text(object, source);
  apply_bound_translation(object);
}

void bind_tooltip(QObject* object, const char* source) {
  bind_translated_tooltip(object, source);
  apply_bound_translation(object);
}

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

const char* tool_action_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return "Move";
    case CanvasTool::Marquee:
      return "Marquee";
    case CanvasTool::EllipticalMarquee:
      return "Elliptical Marquee";
    case CanvasTool::Lasso:
      return "Lasso";
    case CanvasTool::MagicWand:
      return "Magic Wand";
    case CanvasTool::Brush:
      return "Brush";
    case CanvasTool::Clone:
      return "Clone";
    case CanvasTool::Smudge:
      return "Smudge";
    case CanvasTool::Eraser:
      return "Eraser";
    case CanvasTool::Gradient:
      return "Gradient";
    case CanvasTool::Line:
      return "Line";
    case CanvasTool::Rectangle:
      return "Rect";
    case CanvasTool::Ellipse:
      return "Ellipse";
    case CanvasTool::Fill:
      return "Fill";
    case CanvasTool::Eyedropper:
      return "Pick";
    case CanvasTool::Text:
      return "Type";
    case CanvasTool::Pan:
      return "Hand";
    case CanvasTool::Zoom:
      return "Zoom";
  }
  return "Tool";
}

QString tool_hotkey_id(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QStringLiteral("tools.move");
    case CanvasTool::Marquee:
      return QStringLiteral("tools.marquee");
    case CanvasTool::EllipticalMarquee:
      return QStringLiteral("tools.elliptical_marquee");
    case CanvasTool::Lasso:
      return QStringLiteral("tools.lasso");
    case CanvasTool::MagicWand:
      return QStringLiteral("tools.magic_wand");
    case CanvasTool::Brush:
      return QStringLiteral("tools.brush");
    case CanvasTool::Clone:
      return QStringLiteral("tools.clone");
    case CanvasTool::Smudge:
      return QStringLiteral("tools.smudge");
    case CanvasTool::Eraser:
      return QStringLiteral("tools.eraser");
    case CanvasTool::Gradient:
      return QStringLiteral("tools.gradient");
    case CanvasTool::Line:
      return QStringLiteral("tools.line");
    case CanvasTool::Rectangle:
      return QStringLiteral("tools.rect");
    case CanvasTool::Ellipse:
      return QStringLiteral("tools.ellipse");
    case CanvasTool::Fill:
      return QStringLiteral("tools.fill");
    case CanvasTool::Eyedropper:
      return QStringLiteral("tools.eyedropper");
    case CanvasTool::Text:
      return QStringLiteral("tools.type");
    case CanvasTool::Pan:
      return QStringLiteral("tools.hand");
    case CanvasTool::Zoom:
      return QStringLiteral("tools.zoom");
  }
  return QStringLiteral("tools.unknown");
}

QString tool_action_object_name(CanvasTool tool) {
  auto name = QString::fromLatin1(tool_action_source(tool));
  name.remove(QLatin1Char(' '));
  return QStringLiteral("tool") + name + QStringLiteral("Action");
}


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

constexpr int kWindowResizeBorder = 10;

Qt::Edges resize_edges_for_window_position(QSize window_size, QPoint position) {
  Qt::Edges edges;
  if (window_size.isEmpty()) {
    return edges;
  }
  if (position.x() >= 0 && position.x() < kWindowResizeBorder) {
    edges |= Qt::LeftEdge;
  } else if (position.x() < window_size.width() && position.x() >= window_size.width() - kWindowResizeBorder) {
    edges |= Qt::RightEdge;
  }
  if (position.y() >= 0 && position.y() < kWindowResizeBorder) {
    edges |= Qt::TopEdge;
  } else if (position.y() < window_size.height() && position.y() >= window_size.height() - kWindowResizeBorder) {
    edges |= Qt::BottomEdge;
  }
  return edges;
}

bool widget_is_or_contains_scroll_bar(const QWidget* widget) {
  for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
    if (qobject_cast<const QScrollBar*>(current) != nullptr) {
      return true;
    }
  }
  return false;
}

QWidget* deepest_child_at(QWidget* root, QPoint position) {
  if (root == nullptr || !root->rect().contains(position)) {
    return nullptr;
  }

  auto* parent = root;
  auto parent_position = position;
  auto* child = parent->childAt(parent_position);
  while (child != nullptr) {
    const auto child_position = child->mapFrom(parent, parent_position);
    auto* next = child->childAt(child_position);
    if (next == nullptr || next == child) {
      return child;
    }
    parent = child;
    parent_position = child_position;
    child = next;
  }
  return nullptr;
}

bool visible_scroll_bar_contains_global_point(QWidget* root, QPoint global_position) {
  if (root == nullptr) {
    return false;
  }
  for (auto* scroll_bar : root->findChildren<QScrollBar*>()) {
    if (scroll_bar == nullptr || !scroll_bar->isVisibleTo(root) || scroll_bar->window() != root->window()) {
      continue;
    }
    const QRect global_rect(scroll_bar->mapToGlobal(QPoint(0, 0)), scroll_bar->size());
    if (global_rect.contains(global_position)) {
      return true;
    }
  }
  return false;
}

bool window_resize_hit_targets_scroll_bar(QWidget* window, QPoint global_position) {
  if (window == nullptr) {
    return false;
  }
  if (visible_scroll_bar_contains_global_point(window, global_position)) {
    return true;
  }
  return widget_is_or_contains_scroll_bar(deepest_child_at(window, window->mapFromGlobal(global_position)));
}

Qt::CursorShape resize_cursor_for_edges(Qt::Edges edges) {
  const bool left = edges.testFlag(Qt::LeftEdge);
  const bool right = edges.testFlag(Qt::RightEdge);
  const bool top = edges.testFlag(Qt::TopEdge);
  const bool bottom = edges.testFlag(Qt::BottomEdge);
  if ((top && left) || (bottom && right)) {
    return Qt::SizeFDiagCursor;
  }
  if ((top && right) || (bottom && left)) {
    return Qt::SizeBDiagCursor;
  }
  if (left || right) {
    return Qt::SizeHorCursor;
  }
  if (top || bottom) {
    return Qt::SizeVerCursor;
  }
  return Qt::ArrowCursor;
}

#ifdef Q_OS_WIN
void apply_windows_frameless_resize_style(WId window_id) {
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (hwnd == nullptr) {
    return;
  }

  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (style == 0) {
    return;
  }
  const LONG_PTR next_style = (style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX) & ~WS_CAPTION;
  if (next_style != style) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, next_style);
  }
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

  // Windows 11 draws a 1px DWM "visible border" around WS_THICKFRAME windows that turns
  // light/white when the window is deactivated. COLOR_NONE removes it in every state; our
  // own dark QSS frame still draws inside the client area. The call is a harmless no-op on
  // pre-22000 builds (it just returns a failure HRESULT), so we call it unconditionally.
  constexpr DWORD kDwmwaBorderColor = 34;        // DWMWA_BORDER_COLOR (Win11 22000+)
  constexpr COLORREF kDwmColorNone = 0xFFFFFFFE; // DWMWA_COLOR_NONE
  DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
}

void apply_windows_pen_feedback_suppression(WId window_id) {
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (hwnd == nullptr) {
    return;
  }
  // Disable the Windows pen "press and hold" ring (and the related tap/barrel
  // feedback and flicks) so drawing does not show the distracting circle. This
  // is the standard MicrosoftTabletPenServiceProperty technique used by drawing
  // applications.
  const DWORD_PTR flags = TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK |
                          TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS;
  const ATOM atom = ::GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
  ::SetProp(hwnd, MICROSOFT_TABLETPENSERVICE_PROPERTY, reinterpret_cast<HANDLE>(flags));
  if (atom != 0) {
    ::GlobalDeleteAtom(atom);
  }
}
#endif

void set_property_label_text(QLabel* label, const QString& text) {
  if (label == nullptr) {
    return;
  }
  label->setText(text);
  label->setVisible(!text.trimmed().isEmpty());
}

void install_collapsible_dock_title(QDockWidget* dock,
                                    QWidget* content,
                                    const QString& object_prefix,
                                    int expanded_minimum_height = 0,
                                    int expanded_maximum_height = QWIDGETSIZE_MAX,
                                    bool initially_expanded = true) {
  dock->setMinimumWidth(kRightDockMinimumWidth);
  content->setMinimumWidth(kRightDockMinimumWidth - 18);
  if (expanded_minimum_height > 0) {
    dock->setMinimumHeight(expanded_minimum_height);
  }
  dock->setMaximumHeight(expanded_maximum_height);

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
  toggle->setChecked(initially_expanded);
  toggle->setText(initially_expanded ? QStringLiteral("v") : QStringLiteral(">"));
  toggle->setFixedSize(18, 18);
  toggle->setToolTip(initially_expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
  layout->addWidget(toggle);

  auto* label = new QLabel(dock->windowTitle(), title);
  label->setObjectName(object_prefix + QStringLiteral("DockTitleLabel"));
  if (dock->property(kTranslationTextProperty).isValid()) {
    label->setProperty(kTranslationContextProperty, dock->property(kTranslationContextProperty));
    label->setProperty(kTranslationTextProperty, dock->property(kTranslationTextProperty));
    apply_bound_translation(label);
  }
  layout->addWidget(label, 1);

  const auto apply_expanded_state = [dock, content, toggle, expanded_minimum_height,
                                     expanded_maximum_height](bool expanded) {
    content->setVisible(expanded);
    toggle->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
    toggle->setToolTip(expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
    const auto collapsed_height = dock->titleBarWidget()->sizeHint().height() + 8;
    if (expanded_minimum_height > 0) {
      dock->setMinimumHeight(expanded ? expanded_minimum_height : collapsed_height);
    }
    dock->setMaximumHeight(expanded ? expanded_maximum_height : collapsed_height);
    dock->updateGeometry();
  };

  QObject::connect(toggle, &QToolButton::toggled, dock, apply_expanded_state);

  dock->setTitleBarWidget(title);
  apply_expanded_state(initially_expanded);
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
    const auto is_group = item->data(kLayerIsGroupRole).toBool();
    const auto background = item->isSelected() ? QStringLiteral("#3c4651")
                            : is_group        ? QStringLiteral("#292d31")
                                              : QStringLiteral("#242628");
    const auto divider = item->isSelected() ? QStringLiteral("#4f91ca") : QStringLiteral("#303338");
    row_widget->setStyleSheet(QStringLiteral(
                                  "QWidget#layerRowWidget { background: %1; border-bottom: 1px solid %2; }")
                                  .arg(background, divider));
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      auto font = name->font();
      font.setBold(item == list->currentItem() || is_group);
      name->setFont(font);
    }
  }
}

void update_layer_target_styles(QListWidget* list, std::optional<LayerId> active_layer,
                                CanvasWidget::LayerEditTarget edit_target) {
  if (list == nullptr) {
    return;
  }

  auto set_target_active = [](QWidget* widget, bool active) {
    if (widget == nullptr) {
      return;
    }
    widget->setProperty("layerTargetActive", active);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  };

  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    auto* row_widget = list->itemWidget(item);
    if (item == nullptr || row_widget == nullptr) {
      continue;
    }
    const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    const auto row_active = active_layer.has_value() && *active_layer == layer_id;
    set_target_active(row_widget->findChild<QWidget*>(QStringLiteral("layerContentThumbnail")),
                      row_active && edit_target == CanvasWidget::LayerEditTarget::Content);
    set_target_active(row_widget->findChild<QWidget*>(QStringLiteral("layerMaskThumbnail")),
                      row_active && edit_target == CanvasWidget::LayerEditTarget::Mask);
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
  }
  return QColor(145, 175, 215);
}

QString localized_adjustment_display_name(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return QObject::tr("Levels");
    case AdjustmentKind::Curves:
      return QObject::tr("Curves");
    case AdjustmentKind::HueSaturation:
      return QObject::tr("Hue/Saturation");
    case AdjustmentKind::ColorBalance:
      return QObject::tr("Color Balance");
  }
  return QObject::tr("Adjustment");
}

QString adjustment_layer_detail(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return QObject::tr("adjustment");
  }
  return QObject::tr("%1 adjustment").arg(localized_adjustment_display_name(settings->kind));
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

LevelsAdjustment sanitized_levels_adjustment(LevelsSettings settings) {
  const auto clamp_record = [](LevelsRecord record) {
    record.black_input = std::clamp(record.black_input, 0, 254);
    record.white_input = std::clamp(record.white_input, record.black_input + 1, 255);
    record.gamma_percent = std::clamp(record.gamma_percent, 10, 999);
    record.black_output = std::clamp(record.black_output, 0, 255);
    record.white_output = std::clamp(record.white_output, record.black_output, 255);
    return record;
  };
  const auto master = clamp_record(LevelsRecord{settings.black_input, settings.white_input, settings.gamma_percent,
                                                settings.black_output, settings.white_output});
  return LevelsAdjustment{master.black_input, master.white_input, master.gamma_percent, master.black_output,
                          master.white_output, settings.channel, clamp_record(settings.red), clamp_record(settings.green),
                          clamp_record(settings.blue)};
}

bool levels_settings_have_effect(LevelsSettings settings) {
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::Levels;
  adjustment.levels = sanitized_levels_adjustment(settings);
  return adjustment_has_effect(adjustment);
}

void set_layer_pixels_preserving_origin(Layer& layer, PixelBuffer pixels, Rect original_bounds) {
  const auto x = original_bounds.x;
  const auto y = original_bounds.y;
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(Rect{x, y, layer.pixels().width(), layer.pixels().height()});
}

// Like set_layer_pixels_preserving_origin, but takes the new origin from
// new_bounds instead of preserving the old one. Used when a filter grows the
// layer (e.g. a blur bleeding into transparency) and the origin must shift.
void set_layer_pixels_with_bounds(Layer& layer, PixelBuffer pixels, Rect new_bounds) {
  const auto x = new_bounds.x;
  const auto y = new_bounds.y;
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(Rect{x, y, layer.pixels().width(), layer.pixels().height()});
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
      return QObject::tr("Curves: shadows %1, midtones %2, highlights %3")
          .arg(settings->curves.shadow_output)
          .arg(settings->curves.midtone_output)
          .arg(settings->curves.highlight_output);
    case AdjustmentKind::HueSaturation:
      return QObject::tr("Hue/Saturation: hue %1, saturation %2, lightness %3")
          .arg(settings->hue_saturation.hue_shift)
          .arg(settings->hue_saturation.saturation_delta)
          .arg(settings->hue_saturation.lightness_delta);
    case AdjustmentKind::ColorBalance:
      return QObject::tr("Color Balance: C/R %1, M/G %2, Y/B %3")
          .arg(settings->color_balance.cyan_red)
          .arg(settings->color_balance.magenta_green)
          .arg(settings->color_balance.yellow_blue);
  }
  return QObject::tr("Adjustment");
}

double text_size_ppi(const Document& document) noexcept {
  const auto ppi = document.print_settings().horizontal_ppi;
  return std::isfinite(ppi) && ppi > 0.0 ? std::clamp(ppi, 1.0, 9999.0) : 300.0;
}

double text_pixels_to_points(int pixels, const Document& document) noexcept {
  return std::max(0.01, static_cast<double>(std::max(1, pixels)) * 72.0 / text_size_ppi(document));
}

int text_points_to_pixels(double points, const Document& document) noexcept {
  if (!std::isfinite(points)) {
    return 1;
  }
  return std::max(1, static_cast<int>(std::lround(std::max(0.01, points) * text_size_ppi(document) / 72.0)));
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
  const auto shadow_y = y_for_output(settings.shadow_output);
  const auto midtone_y = y_for_output(settings.midtone_output);
  const auto highlight_y = y_for_output(settings.highlight_output);
  QPainterPath curve;
  curve.moveTo(QPointF(x_for_input(0), shadow_y));
  curve.cubicTo(QPointF(x_for_input(44), (shadow_y + midtone_y) * 0.5),
                QPointF(x_for_input(84), (shadow_y + midtone_y) * 0.5), QPointF(x_for_input(128), midtone_y));
  curve.cubicTo(QPointF(x_for_input(172), (midtone_y + highlight_y) * 0.5),
                QPointF(x_for_input(211), (midtone_y + highlight_y) * 0.5), QPointF(x_for_input(255), highlight_y));
  painter.setPen(QPen(QColor(8, 13, 11, 160), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(curve.translated(0.0, 1.0));
  painter.setPen(QPen(accent.lighter(135), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(curve);
  painter.setBrush(QColor(238, 246, 241));
  painter.setPen(QPen(QColor(18, 34, 24), 1));
  for (const auto point : {QPointF(x_for_input(0), shadow_y), QPointF(x_for_input(128), midtone_y),
                           QPointF(x_for_input(255), highlight_y)}) {
    painter.drawEllipse(point, 1.7, 1.7);
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
    painter.setPen(QColor(12, 14, 18, 180));
    painter.drawText(QRect(1, 2, kSize, kSize), Qt::AlignCenter, QStringLiteral("T"));
    painter.setPen(text_color);
    painter.drawText(QRect(0, 1, kSize, kSize), Qt::AlignCenter, QStringLiteral("T"));
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

QWidget* make_layer_row_widget(const Layer& layer, QListWidgetItem* item, QWidget* parent, int depth = 0,
                               bool ancestors_visible = true, bool group_expanded = true,
                               LayerLockFlags ancestor_lock_flags = kLayerLockNone,
                               std::function<void(LayerId)> toggle_group_expanded = {},
                               std::function<void(LayerId, bool)> set_mask_linked = {},
                               bool content_target_active = false, bool mask_target_active = false) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("layerRowWidget"));
  row->setAttribute(Qt::WA_StyledBackground, true);
  row->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  auto* list_parent = dynamic_cast<LayerListWidget*>(parent);
  if (list_parent != nullptr) {
    row->installEventFilter(list_parent);
  }
  auto* layout = new QHBoxLayout(row);
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
  thumbnail->setPixmap(layer_content_thumbnail(layer));
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
    link->setIconSize(QSize(15, 15));
    link->setToolTip(link->isChecked() ? QObject::tr("Layer and mask are linked. Click to unlink.")
                                       : QObject::tr("Layer and mask are unlinked. Click to link."));
    link->setFixedSize(20, 20);
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
    mask_preview->setPixmap(layer_mask_thumbnail(*layer.mask()));
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
  auto* name = new QLabel(display_name, row);
  name->setObjectName(QStringLiteral("layerRowName"));
  name->setTextFormat(Qt::PlainText);
  name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  name->setMinimumWidth(0);
  name->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    name->installEventFilter(list_parent);
  }
  text_column->addWidget(name);

  const auto mode = blend_mode_name(layer.blend_mode());
  QStringList badges;
  if (!layer.layer_style().empty()) {
    badges << QObject::tr("fx");
  }
  if (layer.mask().has_value()) {
    badges << QObject::tr("mask");
  }
  auto detail_text = QStringLiteral("%1  %2%")
                         .arg(mode)
                         .arg(static_cast<int>(std::round(layer.opacity() * 100.0F)));
  if (!badges.isEmpty()) {
    detail_text += QStringLiteral("  %1").arg(badges.join(QStringLiteral("  ")));
  }
  auto* details = new QLabel(detail_text, row);
  details->setObjectName(QStringLiteral("layerRowDetails"));
  details->setTextFormat(Qt::PlainText);
  details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  details->setMinimumWidth(0);
  details->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    details->installEventFilter(list_parent);
  }
  text_column->addWidget(details);

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

QString default_file_dialog_directory() {
  auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (path.isEmpty()) {
    path = QDir::homePath();
  }
  return path;
}

QString last_save_directory() {
  auto settings = app_settings();
  const auto path = settings.value(QStringLiteral("lastSaveDirectory")).toString();
  if (!path.isEmpty()) {
    const QFileInfo info(path);
    if (info.isDir()) {
      return info.absoluteFilePath();
    }
  }
  return default_file_dialog_directory();
}

void remember_save_directory_for_path(const QString& path) {
  const QFileInfo info(path);
  const auto directory = info.absoluteDir();
  if (!directory.exists()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastSaveDirectory"), directory.absolutePath());
}

QString last_open_directory() {
  auto settings = app_settings();
  const auto path = settings.value(QStringLiteral("lastOpenDirectory")).toString();
  if (!path.isEmpty()) {
    const QFileInfo info(path);
    if (info.isDir()) {
      return info.absoluteFilePath();
    }
  }
  return default_file_dialog_directory();
}

void remember_open_directory_for_path(const QString& path) {
  const QFileInfo info(path);
  const auto directory = info.absoluteDir();
  if (!directory.exists()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastOpenDirectory"), directory.absolutePath());
}

QString file_dialog_initial_path(const QString& existing_path, const QString& filename) {
  if (!existing_path.isEmpty()) {
    return existing_path;
  }
  return QDir(last_save_directory()).filePath(filename);
}

QString extension_for_path(const QString& path) {
  return QFileInfo(path).suffix().toLower();
}

bool is_photoshop_document_extension(const QString& extension) {
  return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

QString save_file_filter_for_path(const QString& path) {
  const auto extension = extension_for_path(path);
  const auto filters = save_file_filter().split(QStringLiteral(";;"));
  if (is_photoshop_document_extension(extension)) {
    return filters.value(0);
  }
  if (extension == QStringLiteral("png")) {
    return filters.value(1);
  }
  if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) {
    return filters.value(2);
  }
  if (extension == QStringLiteral("bmp")) {
    return filters.value(3);
  }
  if (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) {
    return filters.value(4);
  }
  if (extension == QStringLiteral("webp")) {
    return filters.value(5);
  }
  return {};
}

bool is_supported_image_extension(const QString& extension) {
  static const QStringList supported = {
      QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("bmp"),
      QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp"),
  };
  return supported.contains(extension);
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

struct OpenDocumentResult {
  Document document;
  QString file_name;
  QString extension;
};

OpenDocumentResult load_document_from_path(QString path) {
  const auto info = QFileInfo(path);
  const auto extension = info.suffix().toLower();
  Document opened;
  if (is_photoshop_document_extension(extension)) {
    opened = psd::DocumentIo::read_file(path.toStdString(), psd::ReadOptions{true, false, true});
  } else if (extension == QStringLiteral("bmp")) {
    try {
      opened = bmp::DocumentIo::read_file(path.toStdString());
    } catch (const std::exception&) {
      QImageReader reader(path);
      reader.setAutoTransform(true);
      const auto image = reader.read();
      if (image.isNull()) {
        throw std::runtime_error(reader.errorString().toStdString());
      }
      opened = document_from_qimage(image, info.completeBaseName().toStdString());
    }
  } else {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (image.isNull()) {
      throw std::runtime_error(reader.errorString().toStdString());
    }
    opened = document_from_qimage(image, info.completeBaseName().toStdString());
  }
  // Flat images (BMP/PNG/TIFF and single-layer PSDs that the PSD reader did not already
  // promote) carry their alpha as a per-pixel channel. Move a meaningful alpha into an
  // editable layer mask so it is visible and paintable, matching Photoshop's "Alpha 1".
  promote_flat_alpha_to_layer_mask(opened);
  if (const auto default_layer_id = default_non_group_layer_id(opened.layers()); default_layer_id.has_value()) {
    opened.set_active_layer(*default_layer_id);
  } else {
    opened.clear_active_layer();
  }
  return OpenDocumentResult{std::move(opened), info.fileName(), extension};
}

QString path_with_default_extension(QString path, const QString& selected_filter) {
  if (!QFileInfo(path).suffix().isEmpty()) {
    return path;
  }

  if (selected_filter.contains(QStringLiteral("*.png"))) {
    return path + QStringLiteral(".png");
  }
  if (selected_filter.contains(QStringLiteral("*.jpg")) || selected_filter.contains(QStringLiteral("*.jpeg"))) {
    return path + QStringLiteral(".jpg");
  }
  if (selected_filter.contains(QStringLiteral("*.bmp"))) {
    return path + QStringLiteral(".bmp");
  }
  if (selected_filter.contains(QStringLiteral("*.tif")) || selected_filter.contains(QStringLiteral("*.tiff"))) {
    return path + QStringLiteral(".tif");
  }
  if (selected_filter.contains(QStringLiteral("*.webp"))) {
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

QByteArray clipboard_image_signature(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  QByteArray signature;
  const qint32 width = converted.width();
  const qint32 height = converted.height();
  const auto pixel_bytes = static_cast<qint64>(std::max<qint32>(0, width)) *
                           static_cast<qint64>(std::max<qint32>(0, height)) * 4;
  signature.reserve(static_cast<qsizetype>(sizeof(width) + sizeof(height) + pixel_bytes));
  signature.append(reinterpret_cast<const char*>(&width), static_cast<qsizetype>(sizeof(width)));
  signature.append(reinterpret_cast<const char*>(&height), static_cast<qsizetype>(sizeof(height)));
  for (int y = 0; y < converted.height(); ++y) {
    signature.append(reinterpret_cast<const char*>(converted.constScanLine(y)),
                     static_cast<qsizetype>(converted.width() * 4));
  }
  return signature;
}

struct NewDocumentSettings {
  std::int32_t width{1024};
  std::int32_t height{768};
  QColor background{Qt::white};
  bool from_clipboard{false};
  QImage clipboard_image;
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
  QString html;
  QString family;
  int size{36};
  bool bold{false};
  bool italic{false};
  int anti_alias{3};
  bool boxed{false};
  int box_width{0};
  int box_height{0};
};

constexpr auto kTextEditorFinishedProperty = "patchy.textEditorFinished";
constexpr auto kTextEditorPreviewEnabledProperty = "patchy.textPreviewEnabled";
constexpr auto kTextEditorPreviewExpensiveProperty = "patchy.expensiveTextStylePreview";
constexpr auto kTextEditorPreviewGenerationProperty = "patchy.textPreviewGeneration";
constexpr auto kTextEditorPreviewPaintProperty = "patchy.previewPaintsText";
constexpr auto kTextEditorPreviewCaretProperty = "patchy.previewCaretRect";
constexpr auto kTextEditorPreviewSelectionProperty = "patchy.previewSelectionRects";
constexpr auto kTextEditorTransformedOverlayProperty = "patchy.transformedPreviewOverlayActive";
constexpr auto kTextEditorChangedProperty = "patchy.textEditorChanged";
constexpr auto kTextEditorSourceRasterPreviewProperty = "patchy.sourceRasterPreview";
constexpr auto kTextEditorForceBakedPreviewProperty = "patchy.forceBakedPreview";
constexpr auto kTextEditorTransformOverrideProperty = "patchy.textTransformOverride";
constexpr auto kTextEditorSourceVisibleAnchorProperty = "patchy.sourceVisibleAnchor";
constexpr auto kTextEditorSourceVisibleSizeProperty = "patchy.sourceVisibleSize";
constexpr auto kTextEditorVisibleLocalRectProperty = "patchy.textVisibleLocalRect";
constexpr auto kTextEditorRenderLocalRectProperty = "patchy.textRenderLocalRect";
constexpr auto kTextEditorExtendedBoxPreviewProperty = "patchy.extendedBoxPreview";
constexpr auto kTextEditorLineAwareBoxPreviewProperty = "patchy.lineAwareBoxPreview";
constexpr auto kTextEditorMetricScaleProperty = "patchy.textMetricScale";
constexpr auto kLayerMetadataTextLineAwareBoxPreview = "patchy.text.line_aware_box_preview";
constexpr auto kTransformedTextEditOverlayObjectName = "transformedTextEditOverlay";
constexpr auto kTextFlowPoint = "point";
constexpr auto kTextFlowBox = "box";
constexpr int kTextDisplayFamilyFormatProperty = QTextFormat::UserProperty + 31;
constexpr int kTextLeadingFormatProperty = QTextFormat::UserProperty + 32;
constexpr int kDefaultTextAntiAlias = 3;
constexpr int kTextEditorCaretWidth = 3;
constexpr int kMinimumTextBoxDocumentSize = 16;
constexpr int kTextEditorPreviewDelayMs = 33;
constexpr int kExpensiveTextEditorPreviewDelayMs = 200;
constexpr int kTextEditorCaretBlinkSpeedMultiplier = 3;
constexpr int kMinimumTextEditorCaretBlinkPhaseMs = 80;

QString text_flow_metadata_value(bool boxed) {
  return boxed ? QString::fromLatin1(kTextFlowBox) : QString::fromLatin1(kTextFlowPoint);
}

bool text_flow_is_box(const QString& value) {
  return value.compare(QString::fromLatin1(kTextFlowBox), Qt::CaseInsensitive) == 0;
}

void update_text_editor_preview_caret(QTextEdit& editor, double zoom);
int text_editor_caret_width(const QTextEdit& editor) noexcept;
int text_editor_caret_blink_phase_ms();
QRect text_editor_viewport_caret_rect(const QTextEdit& editor);
std::vector<QRect> text_editor_viewport_selection_rects(const QTextEdit& editor, int start, int end);
double text_editor_metric_scale(const QTextEdit& editor);

// The document a text layer is rasterized from, plus the font/width the layout was built against.
// Built by build_text_render_document -- the single construction shared by the rasterizer and the
// caret/selection layout so glyphs and caret geometry can never diverge.
struct TextRenderDocument {
  std::unique_ptr<QTextDocument> document;
  QFont font;
  int text_width{0};
};
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale);
QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color);
QString paragraph_runs_from_document(const QTextDocument& document);
QTextDocument* text_editor_document_space_layout(const QTextEdit& editor, double& zoom_out);
void configure_text_font_smoothing(QFont& font, int anti_alias);
std::optional<QRectF> valid_text_local_rect(QRectF rect);

QString preferred_latin_text_fallback_family() {
  const auto available = QFontDatabase::families();
  for (const auto& candidate : {QStringLiteral("Adobe Arabic"), QStringLiteral("Adobe Clean Serif"),
                                QStringLiteral("Minion Pro"), QStringLiteral("Times New Roman"),
                                QStringLiteral("Georgia"), QStringLiteral("Noto Serif"), QStringLiteral("Arial")}) {
    if (available.contains(candidate, Qt::CaseInsensitive)) {
      return candidate;
    }
  }
  return QApplication::font().family();
}

bool text_family_uses_photoshop_latin_fallback(const QString& family) {
  return family.simplified().compare(QStringLiteral("Noto Naskh Arabic"), Qt::CaseInsensitive) == 0;
}

QString compact_text_family_key(const QString& value) {
  QString compact;
  compact.reserve(value.size());
  for (const auto ch : value.toCaseFolded()) {
    if (ch.isLetterOrNumber()) {
      compact.append(ch);
    }
  }
  return compact;
}

std::optional<QString> available_text_family_match(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }

  const auto available = QFontDatabase::families();
  for (const auto& candidate : available) {
    if (candidate.compare(requested, Qt::CaseInsensitive) == 0) {
      return candidate;
    }
  }

  const auto requested_key = compact_text_family_key(requested);
  if (requested_key.isEmpty()) {
    return std::nullopt;
  }
  for (const auto& candidate : available) {
    if (compact_text_family_key(candidate) == requested_key) {
      return candidate;
    }
  }
  return std::nullopt;
}

QString canonical_text_display_family(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return QApplication::font().family();
  }
  return available_text_family_match(requested).value_or(requested);
}

struct AvailableTextFamilyStyle {
  QString family;
  QString style;
};

// A GDI-style name such as "Arial Black" may exist on some platforms only as family "Arial" with
// style "Black" -- the OpenType typographic family/subfamily split the FreeType database uses
// (the Windows database exposes the legacy family directly, so this is a fallback).  Find the
// longest available family that prefixes the requested name and whose remaining words name one
// of that family's styles.
std::optional<AvailableTextFamilyStyle> available_text_family_style_match(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }
  std::optional<AvailableTextFamilyStyle> best;
  for (const auto& candidate : QFontDatabase::families()) {
    if (candidate.isEmpty() || requested.size() <= candidate.size() ||
        !requested.startsWith(candidate, Qt::CaseInsensitive)) {
      continue;
    }
    const auto separator = requested.at(candidate.size());
    if (separator != QLatin1Char(' ') && separator != QLatin1Char('-')) {
      continue;
    }
    if (best.has_value() && best->family.size() >= candidate.size()) {
      continue;
    }
    const auto style = requested.mid(candidate.size() + 1).trimmed();
    if (style.isEmpty()) {
      continue;
    }
    for (const auto& available_style : QFontDatabase::styles(candidate)) {
      if (available_style.compare(style, Qt::CaseInsensitive) == 0) {
        best = AvailableTextFamilyStyle{candidate, available_style};
        break;
      }
    }
  }
  return best;
}

void append_missing_text_family(QStringList& missing, const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty() || requested.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    return;
  }
  if (available_text_family_match(requested).has_value() ||
      available_text_family_style_match(requested).has_value()) {
    return;
  }

  const auto requested_key = compact_text_family_key(requested);
  const bool already_listed = std::any_of(missing.begin(), missing.end(), [&requested, &requested_key](const QString& item) {
    return item.compare(requested, Qt::CaseInsensitive) == 0 ||
           (!requested_key.isEmpty() && compact_text_family_key(item) == requested_key);
  });
  if (!already_listed) {
    missing.push_back(requested);
  }
}

QStringList missing_text_families_for_psd_raster_preview(const QString& primary_family, const QString& runs_text) {
  QStringList missing;
  append_missing_text_family(missing, primary_family);

  const auto lines = runs_text.split(QLatin1Char('\n'));
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    append_missing_text_family(missing, QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1())));
  }
  return missing;
}

bool confirm_psd_raster_preview_font_substitution(QWidget* parent, const QStringList& missing_fonts) {
  if (missing_fonts.isEmpty()) {
    return true;
  }

  QMessageBox dialog(QMessageBox::Warning, QObject::tr("Missing Font"), QString(), QMessageBox::NoButton, parent);
  dialog.setObjectName(QStringLiteral("missingPsdTextFontMessageBox"));
  if (missing_fonts.size() == 1) {
    dialog.setText(QObject::tr("Patchy can't locate the font \"%1\". Editing this PSD raster preview will substitute "
                               "another font. Continue?")
                       .arg(missing_fonts.front()));
  } else {
    dialog.setText(QObject::tr("Patchy can't locate these fonts: %1. Editing this PSD raster preview will substitute "
                               "other fonts. Continue?")
                       .arg(missing_fonts.join(QStringLiteral(", "))));
  }
  auto* continue_button = dialog.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
  dialog.addButton(QMessageBox::Cancel);
  dialog.setDefaultButton(continue_button);
  exec_dialog(dialog);
  return dialog.clickedButton() == continue_button;
}

QString display_text_family_from_font(const QFont& font) {
  const auto families = font.families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = font.family().trimmed();
  return family.isEmpty() ? QApplication::font().family() : family;
}

QStringList render_text_families_for_display_family(const QString& family) {
  const auto display_family = canonical_text_display_family(family);
  if (text_family_uses_photoshop_latin_fallback(display_family)) {
    return QStringList{preferred_latin_text_fallback_family(), display_family};
  }
  return QStringList{display_family};
}

QFont render_text_font_for_display_family(const QString& family, int pixel_size, bool bold, bool italic,
                                          int anti_alias) {
  QFont font;
  font.setFamilies(render_text_families_for_display_family(family));
  font.setPixelSize(std::max(1, pixel_size));
  font.setBold(bold);
  font.setItalic(italic);
  // When the requested name is not a family of its own, resolve it as family + style (e.g.
  // "Arial Black" -> "Arial"/"Black") so the proper face renders instead of a regular-weight
  // substitute.  The style name is set last: it takes precedence over the bold flag in matching.
  if (!available_text_family_match(family).has_value()) {
    if (const auto match = available_text_family_style_match(family); match.has_value()) {
      font.setFamilies(QStringList{match->family});
      font.setStyleName(match->style);
    }
  }
  configure_text_font_smoothing(font, anti_alias);
  return font;
}

void set_text_display_family(QTextCharFormat& format, const QString& family) {
  format.setProperty(kTextDisplayFamilyFormatProperty, canonical_text_display_family(family));
}

QString text_display_family_from_format(const QTextCharFormat& format, const QString& fallback) {
  const auto stored = format.property(kTextDisplayFamilyFormatProperty).toString().trimmed();
  if (!stored.isEmpty()) {
    return stored;
  }
  const auto families = format.font().families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = format.font().family().trimmed();
  if (!family.isEmpty()) {
    return family;
  }
  return fallback.trimmed().isEmpty() ? QApplication::font().family() : fallback.trimmed();
}

bool affine_transform_has_non_translation_linear_part(const LayerAffineTransform& transform) {
  constexpr double kEpsilon = 0.000001;
  return std::abs(transform[0] - 1.0) > kEpsilon || std::abs(transform[1]) > kEpsilon ||
         std::abs(transform[2]) > kEpsilon || std::abs(transform[3] - 1.0) > kEpsilon;
}

bool qtransform_has_non_translation_linear_part(const QTransform& transform) {
  constexpr double kEpsilon = 0.000001;
  return std::abs(transform.m11() - 1.0) > kEpsilon || std::abs(transform.m12()) > kEpsilon ||
         std::abs(transform.m21()) > kEpsilon || std::abs(transform.m22() - 1.0) > kEpsilon ||
         std::abs(transform.m13()) > kEpsilon || std::abs(transform.m23()) > kEpsilon;
}

bool layer_has_non_translation_text_transform(const Layer& layer) {
  if (const auto found = layer.metadata().find(kLayerMetadataTextTransform); found != layer.metadata().end()) {
    if (const auto transform = parse_layer_affine_transform(found->second); transform.has_value()) {
      return affine_transform_has_non_translation_linear_part(*transform);
    }
  }
  if (const auto found = layer.metadata().find(kLayerMetadataPsdTextTransform); found != layer.metadata().end()) {
    if (const auto transform = parse_layer_affine_transform(found->second); transform.has_value()) {
      return affine_transform_has_non_translation_linear_part(*transform);
    }
  }
  return false;
}

bool layer_patchy_text_transform_overrides_psd_source(const Layer& layer) {
  const auto patchy_transform = layer.metadata().find(kLayerMetadataTextTransform);
  if (patchy_transform == layer.metadata().end()) {
    return false;
  }
  const auto parsed_patchy = parse_layer_affine_transform(patchy_transform->second);
  if (!parsed_patchy.has_value()) {
    return false;
  }
  const auto psd_transform = layer.metadata().find(kLayerMetadataPsdTextTransform);
  if (psd_transform == layer.metadata().end()) {
    return true;
  }
  const auto parsed_psd = parse_layer_affine_transform(psd_transform->second);
  if (!parsed_psd.has_value()) {
    return true;
  }
  constexpr double kEpsilon = 0.000001;
  for (std::size_t index = 0; index < parsed_patchy->size(); ++index) {
    if (std::abs((*parsed_patchy)[index] - (*parsed_psd)[index]) > kEpsilon) {
      return true;
    }
  }
  return false;
}

bool layer_should_edit_with_psd_text_frame(const Layer& layer, bool boxed_text) {
  return boxed_text && !layer_patchy_text_transform_overrides_psd_source(layer) &&
         layer.metadata().contains(kLayerMetadataPsdTextTransform) &&
         layer.metadata().contains(kLayerMetadataPsdTextBoxBounds);
}

bool layer_requires_text_editor_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || layer.blend_mode() != BlendMode::Normal ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty()) ||
         layer_has_non_translation_text_transform(layer);
}

void reset_text_editor_scroll(QTextEdit* editor) {
  if (editor == nullptr) {
    return;
  }
  for (auto* bar : {editor->horizontalScrollBar(), editor->verticalScrollBar()}) {
    if (bar != nullptr && bar->value() != 0) {
      QSignalBlocker blocker(bar);
      bar->setValue(0);
    }
  }
}

QString paragraph_alignment_name(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return QStringLiteral("center");
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return QStringLiteral("right");
  }
  if ((alignment & Qt::AlignJustify) != 0) {
    return QStringLiteral("justify");
  }
  return QStringLiteral("left");
}

Qt::Alignment paragraph_alignment_from_name(const QString& name) {
  if (name.compare(QStringLiteral("center"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignHCenter;
  }
  if (name.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignRight;
  }
  if (name.compare(QStringLiteral("justify"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignJustify;
  }
  return Qt::AlignLeft;
}

bool mark_text_editor_finished(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return false;
  }
  editor->setProperty(kTextEditorFinishedProperty, true);
  return true;
}

void configure_text_font_smoothing(QFont& font, int anti_alias) {
  anti_alias = std::clamp(anti_alias, 0, 16);
  if (anti_alias <= 0) {
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    return;
  }

  font.setStyleStrategy(QFont::PreferAntialias);
  switch (anti_alias) {
    case 1:
    case 4:
    case 5:
      font.setHintingPreference(QFont::PreferFullHinting);
      break;
    case 2:
    case 6:
      font.setHintingPreference(QFont::PreferVerticalHinting);
      break;
    default:
      font.setHintingPreference(QFont::PreferNoHinting);
      break;
  }
}

QTextCharFormat text_format_with_smoothing(QTextCharFormat format, int anti_alias) {
  auto font = format.font();
  configure_text_font_smoothing(font, anti_alias);
  format.setFont(font);
  return format;
}

void apply_text_smoothing_to_document(QTextDocument& document, int anti_alias) {
  auto default_font = document.defaultFont();
  configure_text_font_smoothing(default_font, anti_alias);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      ranges.push_back(FormatRange{fragment.position(), fragment.length(),
                                    text_format_with_smoothing(fragment.charFormat(), anti_alias)});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

int text_smoothing_combo_value(const QComboBox* combo) {
  if (combo == nullptr) {
    return kDefaultTextAntiAlias;
  }
  bool ok = false;
  const auto value = combo->currentData().toInt(&ok);
  return std::clamp(ok ? value : kDefaultTextAntiAlias, 0, 16);
}

void set_text_smoothing_combo_value(QComboBox* combo, int value) {
  if (combo == nullptr) {
    return;
  }
  value = std::clamp(value, 0, 16);
  const auto index = combo->findData(value);
  const auto fallback_index = combo->findData(kDefaultTextAntiAlias);
  const QSignalBlocker blocker(combo);
  combo->setCurrentIndex(index >= 0 ? index : std::max(0, fallback_index));
}

class InlineTextEdit final : public QTextEdit {
public:
  explicit InlineTextEdit(QWidget* parent = nullptr) : QTextEdit(parent), caret_blink_timer_(this) {
    setCursorWidth(kTextEditorCaretWidth);
    caret_blink_clock_.start();
    const auto flash_time = text_editor_caret_blink_phase_ms();
    caret_blink_timer_.setInterval(std::max(40, flash_time > 0 ? flash_time / 2 : 80));
    connect(&caret_blink_timer_, &QTimer::timeout, this, [this] {
      if (hasFocus() && property(kTextEditorPreviewPaintProperty).toBool()) {
        viewport()->update();
      }
    });
    caret_blink_timer_.start();
  }

protected:
  void focusInEvent(QFocusEvent* event) override {
    caret_blink_clock_.restart();
    QTextEdit::focusInEvent(event);
  }

  bool canInsertFromMimeData(const QMimeData* source) const override {
    return source != nullptr && (source->hasText() || QTextEdit::canInsertFromMimeData(source));
  }

  void insertFromMimeData(const QMimeData* source) override {
    if (source == nullptr) {
      return;
    }
    if (!source->hasText()) {
      QTextEdit::insertFromMimeData(source);
      return;
    }
    auto cursor = textCursor();
    cursor.insertText(source->text(), currentCharFormat());
    setTextCursor(cursor);
    ensureCursorVisible();
  }

  void paintEvent(QPaintEvent* event) override {
    if (!property(kTextEditorPreviewPaintProperty).toBool()) {
      QTextEdit::paintEvent(event);
      return;
    }

    bool zoom_ok = false;
    const auto zoom = property("patchy.editorZoom").toDouble(&zoom_ok);
    update_text_editor_preview_caret(*this, zoom_ok ? zoom : 1.0);
    if (property(kTextEditorTransformedOverlayProperty).toBool()) {
      return;
    }
    QPainter painter(viewport());
    painter.setClipRect(event->rect());
    paint_selection_highlight(painter);
    if (hasFocus() && caret_visible()) {
      auto caret = property(kTextEditorPreviewCaretProperty).toRect();
      if (caret.isNull() || caret.isEmpty()) {
        caret = text_editor_viewport_caret_rect(*this);
        caret.setWidth(text_editor_caret_width(*this));
      }
      painter.fillRect(caret.intersected(viewport()->rect()), palette().color(QPalette::Text));
    }
  }

private:
  bool caret_visible() const {
    const auto flash_time = text_editor_caret_blink_phase_ms();
    if (flash_time <= 0 || !caret_blink_clock_.isValid()) {
      return true;
    }
    return ((caret_blink_clock_.elapsed() / flash_time) % 2) == 0;
  }

  void paint_selection_highlight(QPainter& painter) const {
    const auto selection = textCursor();
    if (!selection.hasSelection()) {
      return;
    }

    QColor highlight = palette().color(QPalette::Highlight);
    highlight.setAlpha(120);
    if (property(kTextEditorPreviewPaintProperty).toBool()) {
      const auto preview_rects = property(kTextEditorPreviewSelectionProperty).toList();
      for (const auto& rect_value : preview_rects) {
        const auto rect = rect_value.toRect().intersected(viewport()->rect());
        if (!rect.isEmpty()) {
          painter.fillRect(rect, highlight);
        }
      }
      return;
    }

    const auto start = std::min(selection.selectionStart(), selection.selectionEnd());
    const auto end = std::max(selection.selectionStart(), selection.selectionEnd());
    const QPointF scroll_offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    auto* layout = document()->documentLayout();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
      const auto* text_layout = block.layout();
      if (layout == nullptr || text_layout == nullptr) {
        continue;
      }
      const auto block_start = block.position();
      const auto block_end = block_start + block.length();
      const auto selected_start = std::max(start, block_start);
      const auto selected_end = std::min(end, block_end);
      if (selected_start >= selected_end) {
        continue;
      }

      const auto block_origin = layout->blockBoundingRect(block).topLeft() + scroll_offset;
      for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
        const auto line = text_layout->lineAt(line_index);
        const auto line_start = block_start + line.textStart();
        const auto line_end = line_start + line.textLength();
        const auto line_selected_start = std::max(selected_start, line_start);
        const auto line_selected_end = std::min(selected_end, line_end);
        if (line_selected_start >= line_selected_end) {
          continue;
        }

        const auto start_x = line.cursorToX(line_selected_start - block_start);
        const auto end_x = line.cursorToX(line_selected_end - block_start);
        QRectF highlight_rect(block_origin.x() + std::min(start_x, end_x), block_origin.y() + line.y(),
                              std::max<qreal>(1.0, std::abs(end_x - start_x)), line.height());
        painter.fillRect(highlight_rect.toAlignedRect().intersected(viewport()->rect()), highlight);
      }
    }
  }

  QElapsedTimer caret_blink_clock_;
  QTimer caret_blink_timer_;
};

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
  QDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* spin = new QSpinBox(&dialog);
  spin->setObjectName(QStringLiteral("integerInputSpin"));
  spin->setRange(minimum, maximum);
  spin->setSingleStep(std::max(1, step));
  spin->setValue(std::clamp(value, minimum, maximum));
  configure_dialog_spinbox(spin);
  form->addRow(label, spin);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  spin->selectAll();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return spin->value();
}

std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent) {
  constexpr int kClipboardPresetRole = Qt::UserRole + 1;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyNewDocumentDialog"));
  dialog.setWindowTitle(QObject::tr("New Document"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  const auto clipboard_image = QApplication::clipboard()->image();
  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("newDocumentPresetCombo"));
  preset->addItem(QObject::tr("Clipboard"), clipboard_image.isNull() ? QSize() : clipboard_image.size());
  preset->setItemData(0, true, kClipboardPresetRole);
  if (clipboard_image.isNull()) {
    if (auto* model = qobject_cast<QStandardItemModel*>(preset->model()); model != nullptr) {
      if (auto* item = model->item(0); item != nullptr) {
        item->setEnabled(false);
      }
    }
  }
  preset->addItem(QObject::tr("1024 x 768"), QSize(1024, 768));
  preset->addItem(QObject::tr("A4 300 ppi"), QSize(2480, 3508));
  preset->addItem(QObject::tr("A3 300 ppi"), QSize(3508, 4961));
  preset->addItem(QObject::tr("1080p"), QSize(1920, 1080));
  preset->addItem(QObject::tr("4K"), QSize(3840, 2160));
  preset->setCurrentIndex(1);
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

  const auto update_for_preset = [preset, width, height, background](int index) {
    const auto size = preset->itemData(index).toSize();
    if (size.isValid()) {
      width->setValue(size.width());
      height->setValue(size.height());
    }
    const bool clipboard_selected = preset->itemData(index, kClipboardPresetRole).toBool();
    width->setEnabled(!clipboard_selected);
    height->setEnabled(!clipboard_selected);
    background->setEnabled(!clipboard_selected);
  };
  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, update_for_preset);
  update_for_preset(preset->currentIndex());

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const bool clipboard_selected = preset->currentData(kClipboardPresetRole).toBool();
  return NewDocumentSettings{width->value(), height->value(), background->currentData().value<QColor>(),
                             clipboard_selected, clipboard_selected ? QApplication::clipboard()->image() : QImage()};
}

QString default_new_layer_name(const Document& document) {
  std::set<std::string> existing_names;
  std::function<void(const std::vector<Layer>&)> collect_names = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      existing_names.insert(layer.name());
      collect_names(layer.children());
    }
  };
  collect_names(document.layers());

  int suffix = static_cast<int>(document.layers().size()) + 1;
  QString name;
  do {
    name = QObject::tr("Layer %1").arg(suffix++);
  } while (existing_names.contains(name.toStdString()));
  return name;
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

QString canvas_color_swatch_style(QColor color) {
  return QStringLiteral("QPushButton#canvasSizeExtensionColorSwatch { background: rgb(%1, %2, %3); "
                        "border: 1px solid #9a9a9a; border-radius: 3px; padding: 0; min-width: 49px; "
                        "max-width: 49px; min-height: 24px; max-height: 24px; } "
                        "QPushButton#canvasSizeExtensionColorSwatch:hover { border-color: #c8c8c8; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}

void fill_alpha_checkerboard(QPainter& painter, const QRect& rect, int cell_size) {
  if (rect.isEmpty() || cell_size <= 0) {
    return;
  }
  const QColor dark(44, 44, 44);
  const QColor light(188, 188, 188);
  for (int y = rect.top(); y <= rect.bottom(); y += cell_size) {
    for (int x = rect.left(); x <= rect.right(); x += cell_size) {
      const QRect cell(x, y, std::min(cell_size, rect.right() - x + 1),
                       std::min(cell_size, rect.bottom() - y + 1));
      const auto parity = ((x - rect.left()) / cell_size + (y - rect.top()) / cell_size) % 2;
      painter.fillRect(cell, parity == 0 ? dark : light);
    }
  }
}

int color_alpha_percent(QColor color) {
  return std::clamp(static_cast<int>(std::lround(color.alphaF() * 100.0)), 0, 100);
}

QString overlay_color_summary_text(QColor color) {
  return QStringLiteral("%1  %2%").arg(color.name(QColor::HexRgb).toUpper()).arg(color_alpha_percent(color));
}

QIcon overlay_color_swatch_icon(QColor color) {
  QPixmap pixmap(48, 24);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  const QRect swatch = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, swatch, 6);
  painter.fillRect(swatch, color);
  painter.setPen(QPen(QColor(12, 12, 12), 1));
  painter.drawRect(swatch.adjusted(0, 0, -1, -1));
  return QIcon(pixmap);
}

QPixmap grid_overlay_preview_pixmap(QColor grid_color, QColor guide_color, int grid_style, int subdivisions) {
  QPixmap pixmap(218, 86);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const QRect preview_rect = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, preview_rect, 14);
  painter.fillRect(QRect(preview_rect.left(), preview_rect.top(), preview_rect.width() / 2, preview_rect.height()),
                   QColor(28, 30, 34, 130));
  painter.fillRect(QRect(preview_rect.left() + preview_rect.width() / 2, preview_rect.top(),
                         preview_rect.width() - preview_rect.width() / 2, preview_rect.height()),
                   QColor(238, 238, 238, 110));

  auto minor_color = grid_color;
  minor_color.setAlpha(std::clamp(grid_color.alpha() / 2, 24, 120));
  auto major_color = grid_color;
  major_color.setAlpha(std::clamp(grid_color.alpha(), 45, 220));

  const int safe_subdivisions = std::clamp(subdivisions, 1, 64);
  constexpr int major_spacing = 48;
  const int minor_spacing = std::max(6, major_spacing / safe_subdivisions);
  const auto draw_grid = [&](int spacing, QColor color, Qt::PenStyle style) {
    if (spacing <= 0) {
      return;
    }
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (int x = preview_rect.left(); x <= preview_rect.right(); x += spacing) {
      painter.drawLine(QPoint(x, preview_rect.top()), QPoint(x, preview_rect.bottom()));
    }
    for (int y = preview_rect.top(); y <= preview_rect.bottom(); y += spacing) {
      painter.drawLine(QPoint(preview_rect.left(), y), QPoint(preview_rect.right(), y));
    }
  };
  if (safe_subdivisions > 1) {
    draw_grid(minor_spacing, minor_color, grid_style == 0 ? Qt::DotLine : Qt::DashLine);
  }
  draw_grid(major_spacing, major_color, grid_style == 0 ? Qt::SolidLine : Qt::DotLine);

  QPen guide_pen(guide_color, 2.0, Qt::SolidLine);
  guide_pen.setCosmetic(true);
  painter.setPen(guide_pen);
  const int vertical_guide = preview_rect.left() + (preview_rect.width() * 2) / 3;
  const int horizontal_guide = preview_rect.top() + preview_rect.height() / 2;
  painter.drawLine(QPoint(vertical_guide, preview_rect.top()), QPoint(vertical_guide, preview_rect.bottom()));
  painter.drawLine(QPoint(preview_rect.left(), horizontal_guide), QPoint(preview_rect.right(), horizontal_guide));

  painter.setPen(QPen(QColor(92, 92, 92), 1));
  painter.drawRect(preview_rect.adjusted(0, 0, -1, -1));
  return pixmap;
}

std::optional<ImageSizeSettings> request_image_size_settings(QWidget* parent, const Document& document) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyImageSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Image Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyImageSizeDialog {
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
  const auto initial_resolution =
      std::isfinite(document.print_settings().horizontal_ppi) && document.print_settings().horizontal_ppi > 0.0
          ? document.print_settings().horizontal_ppi
          : 300.0;
  resolution->setValue(std::clamp(static_cast<int>(std::lround(initial_resolution)), 1, 9999));
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
  dialog.setObjectName(QStringLiteral("patchyCanvasSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Canvas Size"));
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyCanvasSizeDialog {
      background: #555555;
      color: #ffffff;
    }
    QDialog#patchyCanvasSizeDialog QWidget {
      background: #555555;
      color: #ffffff;
    }
    QDialog#patchyCanvasSizeDialog QLabel {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
    }
    QDialog#patchyCanvasSizeDialog QLabel[sectionLabel="true"] {
      font-weight: 700;
    }
    QDialog#patchyCanvasSizeDialog QFrame#canvasSizeSeparator {
      background: #727272;
      color: #727272;
      min-height: 1px;
      max-height: 1px;
      border: 0;
    }
    QDialog#patchyCanvasSizeDialog QSpinBox,
    QDialog#patchyCanvasSizeDialog QComboBox {
      background: #4f4f4f;
      border: 1px solid #767676;
      border-radius: 3px;
      color: #ffffff;
      min-height: 22px;
      padding: 0 8px;
    }
    QDialog#patchyCanvasSizeDialog QSpinBox:focus,
    QDialog#patchyCanvasSizeDialog QComboBox:focus {
      border-color: #9abbe7;
      background: #4a4a4a;
    }
    QDialog#patchyCanvasSizeDialog QComboBox::drop-down {
      border: 0;
      width: 22px;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox {
      background: transparent;
      color: #ffffff;
      font-size: 11px;
      spacing: 7px;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox::indicator {
      width: 11px;
      height: 11px;
      background: #5a5a5a;
      border: 1px solid #8a8a8a;
    }
    QDialog#patchyCanvasSizeDialog QCheckBox::indicator:checked {
      background: #2f75bd;
      border-color: #9abbe7;
      image: url(:/patchy/icons/checkmark.svg);
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton {
      background: #5c5c5c;
      border: 1px solid #777777;
      border-radius: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
      padding: 0;
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton:hover {
      background: #656565;
      border-color: #9a9a9a;
    }
    QDialog#patchyCanvasSizeDialog QToolButton#canvasSizeAnchorButton:checked {
      background: #4a4a4a;
      border-color: #c8c8c8;
    }
    QDialog#patchyCanvasSizeDialog QPushButton {
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
    QDialog#patchyCanvasSizeDialog QPushButton:hover {
      background: #606060;
      border-color: #b7b7b7;
    }
    QDialog#patchyCanvasSizeDialog QPushButton#canvasSizeOkButton {
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
  dialog.setObjectName(QStringLiteral("patchyTransformDialog"));
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
    QMainWindow::separator {
      background: #1e2022;
      width: 7px;
      height: 7px;
    }
    QMainWindow::separator:hover {
      background: #4e6f95;
    }
    QWidget#rightDockResizeHandle {
      background: #1e2022;
    }
    QWidget#rightDockResizeHandle:hover {
      background: #4e6f95;
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
    QMenuBar::item:selected {
      background: #3a3a3a;
    }
    QLabel#patchyBadge {
      background: transparent;
      border: 0;
    }
    QMenu {
      background: #3a3a3a;
      border: 1px solid #1f1f1f;
    }
    QMenu::item {
      padding: 7px 34px 7px 24px;
    }
    QMenu::item:selected {
      background: #4e6f95;
      color: #ffffff;
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
    QToolButton[optionsBarButton="true"] {
      padding: 2px;
      min-width: 18px;
      min-height: 16px;
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
      border-top: 1px solid #5a5a5a;
      border-bottom: 1px solid #292929;
      spacing: 5px;
      padding: 4px 7px;
    }
    QToolBar#Options QFrame#optionSeparator {
      color: #565656;
      max-width: 2px;
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
    QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
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
      image: url(:/patchy/icons/checkmark.svg);
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
    QLabel#layerContentThumbnail[layerTargetActive="true"], QLabel#layerMaskThumbnail[layerTargetActive="true"] {
      border: 2px solid #31a8ff;
      padding: 0;
    }
    QToolButton#maskEditModeChip {
      background: #31a8ff;
      color: #0d1420;
      border: 1px solid #6cc4ff;
      border-radius: 4px;
      padding: 2px 10px;
      font-weight: 600;
    }
    QToolButton#maskEditModeChip:hover {
      background: #5cbcff;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: #d7dde6;
      line-height: 130%;
    }
    QScrollArea#propertiesScrollArea {
      background: #28292b;
      border: 0;
    }
    QWidget#propertiesPanel {
      background: #28292b;
    }
    QLabel#documentInfoLabel, QLabel#activeLayerInfoLabel, QLabel#activeLayerGeometryLabel,
    QLabel#activeLayerMaskLabel, QLabel#activeLayerAdjustmentLabel, QLabel#activeLayerTextLabel,
    QLabel#activeToolInfoLabel {
      background: #24272b;
      border: 1px solid #3e454d;
      padding: 4px;
      color: #d7dde6;
      font-size: 11px;
    }
    QWidget#layersPanel {
      background: #28292b;
    }
    QListWidget#layerList {
      min-height: 120px;
    }
    QToolButton#layerFolderDisclosureButton {
      background: transparent;
      color: #d9e0ea;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover {
      border-color: #59636f;
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
      background: transparent;
      color: #f2f6fb;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
    QToolButton#layerVisibilityCheck:hover {
      background: #30343a;
      border-color: #59636f;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerVisibilityCheck:checked {
      background: transparent;
      border-color: transparent;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:checked:hover {
      background: transparent;
      border-color: transparent;
    }
    QToolButton#layerVisibilityCheck:!checked {
      background: transparent;
      border-color: transparent;
    }
    QLabel#layerLockBadge {
      background: transparent;
      border: 0;
      padding: 0;
    }
    QToolButton[layerLockControl="true"] {
      background: #24272b;
      border: 1px solid #46505b;
      border-radius: 3px;
      padding: 0;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      max-height: 24px;
    }
    QToolButton[layerLockControl="true"]:hover {
      background: #30343a;
      border-color: #687481;
    }
    QToolButton[layerLockControl="true"]:checked {
      background: #3b3420;
      border-color: #c9a944;
    }
    QToolButton[layerLockControl="true"][mixed="true"] {
      background: #2f3136;
      border-color: #7b8490;
    }
    QToolButton#layerMaskLinkButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
    }
    QToolButton#layerMaskLinkButton:hover {
      background: #30343a;
      border-color: #59636f;
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
    QPushButton[compactSymbolButton="true"] {
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
    QPushButton[layerActionButton="true"], QToolButton[layerActionButton="true"] {
      padding: 0;
      min-width: 40px;
      max-width: 40px;
      min-height: 34px;
      max-height: 34px;
    }
    QPushButton[layerDropActive="true"], QToolButton[layerDropActive="true"] {
      background: #2e3f50;
      border: 2px solid #31a8ff;
      padding: 0;
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
      background: #4a4a4a;
      border: 1px solid #8a8a8a;
    }
    QCheckBox::indicator:hover {
      border-color: #9ccfff;
    }
    QCheckBox::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
      image: url(:/patchy/icons/checkmark.svg);
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
  options.progress_callback = [&canvas] {
    canvas.tick_processing_operation();
  };
  if (canvas.selected_document_rect().has_value()) {
    options.selection = to_core_rect(*canvas.selected_document_rect());
    const auto region = canvas.selected_document_region();
    if (!canvas.selection_has_partial_alpha()) {
      options.selection_scan_rects.reserve(static_cast<std::size_t>(region.rectCount()));
      for (const auto& rect : region) {
        options.selection_scan_rects.push_back(to_core_rect(rect));
      }
    }
    options.selection_mask = [region](std::int32_t x, std::int32_t y) { return region.contains(QPoint(x, y)); };
    options.selection_coverage = [&canvas](std::int32_t x, std::int32_t y) {
      return static_cast<float>(canvas.selection_alpha_at(QPoint(x, y))) / 255.0F;
    };
  }
  return options;
}

std::int64_t rect_pixel_count(Rect rect) noexcept {
  return static_cast<std::int64_t>(std::max(0, rect.width)) * static_cast<std::int64_t>(std::max(0, rect.height));
}

std::int64_t clear_scan_pixel_count(const Document& document, const Layer& layer, Rect rect, const EditOptions& options) {
  auto affected = intersect_rect(intersect_rect(rect, Rect::from_size(document.width(), document.height())), layer.bounds());
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (affected.empty()) {
    return 0;
  }
  if (options.selection_scan_rects.empty()) {
    return rect_pixel_count(affected);
  }

  std::int64_t count = 0;
  for (const auto& scan_rect : options.selection_scan_rects) {
    count += rect_pixel_count(intersect_rect(affected, scan_rect));
  }
  return count;
}

FilterProgress progress_dialog_filter_progress(QProgressDialog& progress,
                                               std::function<QString(const QString&)> label_text,
                                               QEventLoop::ProcessEventsFlags event_flags,
                                               std::function<void()> tick_processing = {}) {
  auto last_progress_value = std::make_shared<int>(-1);
  return FilterProgress{[&progress, label_text = std::move(label_text), event_flags,
                         tick_processing = std::move(tick_processing),
                         last_progress_value](int completed, int total, const QString& detail) {
    const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
    if (value != *last_progress_value) {
      progress.setValue(value);
      if (!detail.isEmpty()) {
        progress.setLabelText(label_text(detail));
      }
      *last_progress_value = value;
      QApplication::processEvents(event_flags);
    }
    if (tick_processing) {
      tick_processing();
    }
    return !progress.wasCanceled();
  }};
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

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const CanvasWidget& canvas);

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
      dst[3] = static_cast<std::uint8_t>((static_cast<int>(source_alpha) * static_cast<int>(layer_alpha)) / 255);
    }
  }
  if (canvas != nullptr) {
    apply_selection_mask(copied, document_rect, *canvas);
  }
  return copied;
}

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const CanvasWidget& canvas) {
  if (!canvas.has_selection() || pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      pixels.format().channels < 4) {
    return;
  }

  const QRect local_bounds(0, 0, pixels.width(), pixels.height());
  if (!canvas.selection_has_partial_alpha()) {
    const auto selected =
        canvas.selected_document_region().intersected(QRegion(QRect(document_rect.x, document_rect.y,
                                                                     document_rect.width, document_rect.height)));
    if (selected.isEmpty()) {
      const auto pixel_bytes = bytes_per_pixel(pixels.format());
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        auto row = pixels.row(y);
        for (std::int32_t x = 0; x < pixels.width(); ++x) {
          row[static_cast<std::size_t>(x) * pixel_bytes + 3U] = 0;
        }
      }
      return;
    }

    auto original = pixels;
    const auto pixel_bytes = bytes_per_pixel(pixels.format());
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      auto row = pixels.row(y);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        row[static_cast<std::size_t>(x) * pixel_bytes + 3U] = 0;
      }
    }
    for (const auto& rect : selected) {
      const auto local = QRect(rect.x() - document_rect.x, rect.y() - document_rect.y, rect.width(), rect.height())
                             .intersected(local_bounds);
      if (local.isEmpty()) {
        continue;
      }
      for (int y = local.top(); y <= local.bottom(); ++y) {
        const auto* src = original.pixel(local.left(), y);
        auto* dst = pixels.pixel(local.left(), y);
        const auto bytes = static_cast<std::size_t>(local.width()) * pixel_bytes;
        std::copy(src, src + bytes, dst);
      }
    }
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

PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect) {
  PixelBuffer mask_pixels(selection_rect.width(), selection_rect.height(), PixelFormat::gray8());
  mask_pixels.clear(0);
  if (selection_rect.isEmpty()) {
    return mask_pixels;
  }

  if (!canvas.selection_has_partial_alpha()) {
    const auto selected = canvas.selected_document_region().intersected(QRegion(selection_rect));
    const QRect local_bounds(0, 0, selection_rect.width(), selection_rect.height());
    for (const auto& rect : selected) {
      const auto local =
          QRect(rect.x() - selection_rect.x(), rect.y() - selection_rect.y(), rect.width(), rect.height())
              .intersected(local_bounds);
      if (local.isEmpty()) {
        continue;
      }
      for (int y = local.top(); y <= local.bottom(); ++y) {
        auto row = mask_pixels.row(y);
        std::fill(row.begin() + local.left(), row.begin() + local.left() + local.width(),
                  static_cast<std::uint8_t>(255));
      }
    }
    return mask_pixels;
  }

  for (int y = 0; y < selection_rect.height(); ++y) {
    for (int x = 0; x < selection_rect.width(); ++x) {
      const QPoint document_point(selection_rect.x() + x, selection_rect.y() + y);
      *mask_pixels.pixel(x, y) = canvas.selection_alpha_at(document_point);
    }
  }
  return mask_pixels;
}

struct LayerCopyPixels {
  PixelBuffer pixels;
  QPoint origin;
  Rect document_rect;
  std::vector<LayerId> source_layer_ids;
};

struct LayerGroupingDestination {
  std::vector<Layer>* siblings{nullptr};
  std::size_t insert_index{0};
};

void collect_layer_names(const std::vector<Layer>& layers, std::set<std::string>& names) {
  for (const auto& layer : layers) {
    names.insert(layer.name());
    collect_layer_names(layer.children(), names);
  }
}

std::optional<LayerGroupingDestination> common_sibling_grouping_destination(
    std::vector<Layer>& layers,
    const std::vector<LayerId>& ids_top_to_bottom) {
  if (ids_top_to_bottom.empty()) {
    return std::nullopt;
  }

  std::vector<LayerSiblingLocation> locations;
  locations.reserve(ids_top_to_bottom.size());
  std::vector<Layer>* siblings = nullptr;
  for (const auto id : ids_top_to_bottom) {
    auto location = find_layer_location(layers, id);
    if (!location.has_value() || location->siblings == nullptr) {
      return std::nullopt;
    }
    if (siblings == nullptr) {
      siblings = location->siblings;
    } else if (siblings != location->siblings) {
      return std::nullopt;
    }
    locations.push_back(*location);
  }

  const auto topmost = std::max_element(locations.begin(), locations.end(), [](const auto& left, const auto& right) {
    return left.index < right.index;
  });
  const auto moved_below_topmost = std::count_if(locations.begin(), locations.end(), [topmost](const auto& location) {
    return location.index < topmost->index;
  });
  return LayerGroupingDestination{siblings, topmost->index - static_cast<std::size_t>(moved_below_topmost)};
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

void scale_font_size(QFont& font, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale)) {
    return;
  }
  if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(1, static_cast<int>(std::round(static_cast<double>(font.pixelSize()) * scale))));
  } else if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
  }
}

void scale_font_width(QFont& font, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }
  const auto stretch = font.stretch() > 0 ? font.stretch() : 100;
  font.setStretch(std::clamp(static_cast<int>(std::round(static_cast<double>(stretch) * scale)), 1, 400));
}

void scale_document_font_sizes(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_size(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  struct BlockFormatRange {
    int position{0};
    int length{0};
    QTextBlockFormat format;
  };
  std::vector<FormatRange> ranges;
  std::vector<BlockFormatRange> block_ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto block_format = block.blockFormat();
    auto scaled_block_format = block_format;
    scaled_block_format.setLeftMargin(block_format.leftMargin() * scale);
    scaled_block_format.setRightMargin(block_format.rightMargin() * scale);
    scaled_block_format.setTextIndent(block_format.textIndent() * scale);
    scaled_block_format.setTopMargin(block_format.topMargin() * scale);
    scaled_block_format.setBottomMargin(block_format.bottomMargin() * scale);
    if (std::abs(scaled_block_format.leftMargin() - block_format.leftMargin()) > 0.0001 ||
        std::abs(scaled_block_format.rightMargin() - block_format.rightMargin()) > 0.0001 ||
        std::abs(scaled_block_format.textIndent() - block_format.textIndent()) > 0.0001 ||
        std::abs(scaled_block_format.topMargin() - block_format.topMargin()) > 0.0001 ||
        std::abs(scaled_block_format.bottomMargin() - block_format.bottomMargin()) > 0.0001) {
      block_ranges.push_back(BlockFormatRange{block.position(), std::max(1, block.length()), scaled_block_format});
    }

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_pixel_size = format_font.pixelSize();
      const auto before_point_size = format_font.pointSizeF();
      const auto before_leading = format.hasProperty(kTextLeadingFormatProperty)
                                      ? format.property(kTextLeadingFormatProperty).toDouble()
                                      : 0.0;
      scale_font_size(format_font, scale);
      bool changed = format_font.pixelSize() != before_pixel_size ||
                     std::abs(format_font.pointSizeF() - before_point_size) >= 0.0001;
      if (format.hasProperty(kTextLeadingFormatProperty) && std::isfinite(before_leading) && before_leading > 0.0) {
        const auto scaled_leading = before_leading * scale;
        format.setProperty(kTextLeadingFormatProperty, scaled_leading);
        changed = changed || std::abs(scaled_leading - before_leading) >= 0.0001;
      }
      if (!changed) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : block_ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(range.format);
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

void scale_document_font_widths(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_width(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_stretch = format_font.stretch();
      scale_font_width(format_font, scale);
      if (format_font.stretch() == before_stretch) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

QString normalized_rich_text_html(const QTextDocument& source) {
  QTextDocument copy;
  copy.setDocumentMargin(0);
  copy.setDefaultFont(source.defaultFont());
  copy.setDefaultTextOption(source.defaultTextOption());
  copy.setHtml(source.toHtml());
  return copy.toHtml();
}

std::unique_ptr<QTextDocument> copy_text_document_formats(const QTextDocument& source) {
  auto document = std::make_unique<QTextDocument>();
  document->setDocumentMargin(0);
  document->setDefaultFont(source.defaultFont());
  document->setDefaultTextOption(source.defaultTextOption());
  document->setPlainText(source.toPlainText());

  const int plain_length = static_cast<int>(document->toPlainText().size());
  for (auto block = source.begin(); block.isValid(); block = block.next()) {
    QTextCursor block_cursor(document.get());
    const auto block_start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto block_end = std::clamp(block.position() + std::max(1, block.length()), 0, std::max(0, plain_length));
    block_cursor.setPosition(block_start);
    block_cursor.setPosition(block_end, QTextCursor::KeepAnchor);
    block_cursor.mergeBlockFormat(block.blockFormat());

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto start = std::clamp(fragment.position(), 0, std::max(0, plain_length));
      const auto end = std::clamp(fragment.position() + fragment.length(), 0, std::max(0, plain_length));
      if (start >= end) {
        continue;
      }
      QTextCursor cursor(document.get());
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      cursor.mergeCharFormat(fragment.charFormat());
    }
  }
  return document;
}

std::unique_ptr<QTextDocument> document_from_editor_in_document_units(const QTextEdit& editor, double zoom) {
  auto document = copy_text_document_formats(*editor.document());
  scale_document_font_sizes(*document, zoom > 0.0 ? 1.0 / zoom : 1.0);
  return document;
}

constexpr auto kTextEditorCaretLayoutObjectName = "patchy.caretLayoutDocument";
constexpr auto kTextEditorCaretLayoutGenerationProperty = "patchy.caretLayoutGeneration";
constexpr auto kTextEditorCaretLayoutBuiltGenerationProperty = "patchy.caretLayoutBuiltGeneration";

// Builds the editor's text laid out at document (1:1) resolution -- through the exact same
// construction the rasterizer uses (build_text_render_document), so caret/selection geometry is
// computed on the identical document the on-screen glyphs come from.  Copying the editor
// document's formats directly is NOT equivalent: blank paragraphs carry their line height in
// block char formats that a fragment-level copy misses, so files with empty lines (old PSDs
// separate lines with control characters) drifted the caret off the text.
std::unique_ptr<QTextDocument> build_text_editor_document_space_layout(const QTextEdit& editor) {
  bool zoom_ok = false;
  double zoom = editor.property("patchy.editorZoom").toDouble(&zoom_ok);
  if (!zoom_ok || !(zoom > 0.0)) {
    zoom = 1.0;
  }

  const auto source_units = document_from_editor_in_document_units(editor, zoom);
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto family = editor.property("patchy.documentTextFamily").toString();
  if (family.trimmed().isEmpty()) {
    family = display_text_family_from_font(editor.font());
  }
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto color = stored_color.isValid() ? stored_color : QColor(Qt::black);
  TextToolSettings settings{source_units->toPlainText(),
                            QString(),
                            family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            anti_alias,
                            text_flow_is_box(editor.property("patchy.documentTextFlow").toString()),
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*source_units, settings, color);
  const auto paragraph_runs = paragraph_runs_from_document(*source_units);
  auto built = build_text_render_document(settings, color, text_width, paragraph_runs, rich_text_runs,
                                          text_editor_metric_scale(editor));
  return std::move(built.document);
}

// Returns the editor's text laid out at document (1:1) resolution. Caret and selection geometry must
// be derived from this layout and then scaled by the zoom: the rendered text is composited at document
// resolution, so laying out directly from the zoom-shrunk editor font produces line metrics that do not
// scale linearly and the caret/selection drift away from the text as the user zooms out.
//
// The layout is cached and rebuilt only when the text content actually changes (tracked by a generation
// counter that is bumped on genuine edits but not when the editor is merely re-scaled for a new zoom).
// This keeps the document-space geometry identical across zoom levels, so the caret tracks the text.
QTextDocument* text_editor_document_space_layout(const QTextEdit& editor, double& zoom_out) {
  bool zoom_ok = false;
  double zoom = editor.property("patchy.editorZoom").toDouble(&zoom_ok);
  zoom_out = (!zoom_ok || !(zoom > 0.0)) ? 1.0 : zoom;

  auto& mutable_editor = const_cast<QTextEdit&>(editor);
  const auto generation = editor.property(kTextEditorCaretLayoutGenerationProperty).toInt();
  auto* cached = editor.findChild<QTextDocument*>(QString::fromLatin1(kTextEditorCaretLayoutObjectName),
                                                  Qt::FindDirectChildrenOnly);
  const auto built_generation_value = editor.property(kTextEditorCaretLayoutBuiltGenerationProperty);
  if (cached != nullptr && built_generation_value.isValid() && built_generation_value.toInt() == generation) {
    return cached;
  }

  delete cached;
  auto document = build_text_editor_document_space_layout(editor);
  auto* layout_document = document.release();
  layout_document->setParent(&mutable_editor);
  layout_document->setObjectName(QString::fromLatin1(kTextEditorCaretLayoutObjectName));
  mutable_editor.setProperty(kTextEditorCaretLayoutBuiltGenerationProperty, generation);
  return layout_document;
}

QRectF scale_document_rect_to_viewport(const QRect& document_rect, double zoom, QPointF scroll_offset) {
  return QRectF(static_cast<qreal>(document_rect.x()) * zoom - scroll_offset.x(),
                static_cast<qreal>(document_rect.y()) * zoom - scroll_offset.y(),
                std::max<qreal>(1.0, static_cast<qreal>(document_rect.width()) * zoom),
                std::max<qreal>(1.0, static_cast<qreal>(document_rect.height()) * zoom));
}

QRect text_document_cursor_rect(const QTextDocument& document, int position) {
  const auto maximum_position = std::max(0, document.characterCount() - 1);
  position = std::clamp(position, 0, maximum_position);
  const auto block = document.findBlock(position);
  if (!block.isValid() || block.layout() == nullptr || document.documentLayout() == nullptr) {
    return {};
  }

  const auto* text_layout = block.layout();
  const auto block_origin = document.documentLayout()->blockBoundingRect(block).topLeft();
  const auto relative_position = std::max(0, position - block.position());
  for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
    const auto line = text_layout->lineAt(line_index);
    const auto line_start = line.textStart();
    const auto line_end = line_start + line.textLength();
    if (relative_position < line_start || (relative_position > line_end && line_index + 1 < text_layout->lineCount())) {
      continue;
    }
    const auto x = line.cursorToX(std::clamp(relative_position, line_start, line_end));
    const auto glyph_height =
        std::max<qreal>(1.0, std::ceil(std::max<qreal>(1.0, line.ascent()) + std::max<qreal>(0.0, line.descent())));
    const auto top_padding = std::max<qreal>(0.0, (line.height() - glyph_height) / 2.0);
    return QRectF(block_origin.x() + x, block_origin.y() + line.y() + top_padding, 1.0, glyph_height).toAlignedRect();
  }

  const auto block_rect = document.documentLayout()->blockBoundingRect(block);
  return QRectF(block_rect.left(), block_rect.top(), 1.0, std::max<qreal>(1.0, block_rect.height())).toAlignedRect();
}

std::vector<QRect> text_document_selection_rects(const QTextDocument& document, int start, int end) {
  const auto maximum_position = std::max(0, document.characterCount() - 1);
  start = std::clamp(start, 0, maximum_position);
  end = std::clamp(end, 0, maximum_position);
  if (start > end) {
    std::swap(start, end);
  }
  if (start == end || document.documentLayout() == nullptr) {
    return {};
  }

  std::vector<QRect> rects;
  const auto* layout = document.documentLayout();
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }

    const auto block_start = block.position();
    const auto block_end = block_start + block.length();
    const auto selected_start = std::max(start, block_start);
    const auto selected_end = std::min(end, block_end);
    if (selected_start >= selected_end) {
      continue;
    }

    const auto block_origin = layout->blockBoundingRect(block).topLeft();
    for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
      const auto line = text_layout->lineAt(line_index);
      const auto line_start = block_start + line.textStart();
      const auto line_end = line_start + line.textLength();
      const auto line_selected_start = std::max(selected_start, line_start);
      const auto line_selected_end = std::min(selected_end, line_end);
      if (line_selected_start >= line_selected_end) {
        continue;
      }

      const auto start_x = line.cursorToX(line_selected_start - block_start);
      const auto end_x = line.cursorToX(line_selected_end - block_start);
      const QRectF rect(block_origin.x() + std::min(start_x, end_x), block_origin.y() + line.y(),
                        std::max<qreal>(1.0, std::abs(end_x - start_x)), line.height());
      rects.push_back(rect.toAlignedRect());
    }
  }
  return rects;
}

void clear_text_editor_preview_overlays(QTextEdit& editor) {
  editor.setProperty(kTextEditorPreviewCaretProperty, QVariant());
  editor.setProperty(kTextEditorPreviewSelectionProperty, QVariant());
}

int text_editor_caret_width(const QTextEdit& editor) noexcept {
  if (editor.cursorWidth() <= 0) {
    return 0;
  }
  return std::max(kTextEditorCaretWidth, editor.cursorWidth());
}

int text_editor_caret_blink_phase_ms() {
  const auto flash_time = QApplication::cursorFlashTime();
  if (flash_time <= 0) {
    return flash_time;
  }
  return std::max(kMinimumTextEditorCaretBlinkPhaseMs, flash_time / kTextEditorCaretBlinkSpeedMultiplier);
}

QRect text_editor_viewport_caret_rect(const QTextEdit& editor) {
  double zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, zoom);
  auto caret_document_rect = text_document_cursor_rect(*layout_document, editor.textCursor().position());
  QRect caret;
  if (!caret_document_rect.isEmpty()) {
    const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());
    caret = scale_document_rect_to_viewport(caret_document_rect, zoom, scroll_offset).toAlignedRect();
  }
  if (caret.isEmpty()) {
    caret = editor.cursorRect();
  }
  return caret;
}

std::vector<QRect> text_editor_viewport_selection_rects(const QTextEdit& editor, int start, int end) {
  double zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, zoom);
  const auto document_selection_rects = text_document_selection_rects(*layout_document, start, end);
  const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());
  std::vector<QRect> rects;
  rects.reserve(document_selection_rects.size());
  for (const auto& rect : document_selection_rects) {
    const auto aligned = scale_document_rect_to_viewport(rect, zoom, scroll_offset).toAlignedRect();
    if (!aligned.isEmpty()) {
      rects.push_back(aligned);
    }
  }
  return rects;
}

void update_text_editor_preview_caret(QTextEdit& editor, double zoom) {
  Q_UNUSED(zoom);
  if (!editor.property(kTextEditorPreviewPaintProperty).toBool()) {
    clear_text_editor_preview_overlays(editor);
    return;
  }

  double layout_zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, layout_zoom);
  const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());

  QVariantList selection_rects;
  const auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    for (const auto& rect :
         text_document_selection_rects(*layout_document, cursor.selectionStart(), cursor.selectionEnd())) {
      const auto aligned = scale_document_rect_to_viewport(rect, layout_zoom, scroll_offset).toAlignedRect();
      if (!aligned.isEmpty()) {
        selection_rects.push_back(aligned);
      }
    }
  }
  editor.setProperty(kTextEditorPreviewSelectionProperty, selection_rects);

  const auto caret_document_rect = text_document_cursor_rect(*layout_document, editor.textCursor().position());
  auto caret = caret_document_rect.isEmpty()
                   ? editor.cursorRect()
                   : scale_document_rect_to_viewport(caret_document_rect, layout_zoom, scroll_offset).toAlignedRect();
  const auto caret_width = text_editor_caret_width(editor);
  if (caret.isEmpty() || caret_width <= 0) {
    editor.setProperty(kTextEditorPreviewCaretProperty, QVariant());
    return;
  }
  caret.setWidth(caret_width);
  editor.setProperty(kTextEditorPreviewCaretProperty, caret);
}

class TransformedTextEditOverlay final : public QWidget {
  enum class ResizeHandle { None, TopLeft, TopRight, BottomLeft, BottomRight };

  struct ResizeDragState {
    ResizeHandle handle{ResizeHandle::None};
    QTransform start_transform;
    QPointF start_editor_point;
    int start_width{kMinimumTextBoxDocumentSize};
    int start_height{kMinimumTextBoxDocumentSize};
  };

  struct ResizeGeometry {
    QTransform transform;
    int width{kMinimumTextBoxDocumentSize};
    int height{kMinimumTextBoxDocumentSize};
  };

  static constexpr std::array<ResizeHandle, 4> kResizeHandles = {
      ResizeHandle::TopLeft,
      ResizeHandle::TopRight,
      ResizeHandle::BottomLeft,
      ResizeHandle::BottomRight,
  };

public:
  explicit TransformedTextEditOverlay(CanvasWidget* canvas)
      : QWidget(canvas), canvas_(canvas), caret_blink_timer_(this) {
    setObjectName(QString::fromLatin1(kTransformedTextEditOverlayObjectName));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    caret_blink_clock_.start();
    const auto flash_time = text_editor_caret_blink_phase_ms();
    caret_blink_timer_.setInterval(std::max(40, flash_time > 0 ? flash_time / 2 : 80));
    connect(&caret_blink_timer_, &QTimer::timeout, this, [this] {
      if (editor_ != nullptr && editor_->hasFocus()) {
        update();
      }
    });
    caret_blink_timer_.start();
  }

  [[nodiscard]] QTextEdit* editor() const noexcept {
    return editor_;
  }

  void configure(QTextEdit* editor, QTransform transform, std::function<void(QTextEdit*)> resize_callback) {
    editor_ = editor;
    text_transform_ = std::move(transform);
    resize_callback_ = std::move(resize_callback);
    resize_preview_transform_.reset();
    resize_preview_document_size_.reset();
    sync_geometry();
  }

  void sync_geometry() {
    if (canvas_ == nullptr || editor_ == nullptr || editor_->viewport() == nullptr) {
      hide();
      return;
    }

    const QRectF editor_rect = editor_visual_rect().adjusted(-4.0, -4.0, 4.0, 4.0);
    const auto canvas_polygon = map_editor_rect_to_canvas(editor_rect);
    if (canvas_polygon.isEmpty()) {
      hide();
      return;
    }

    const auto overlay_geometry = canvas_polygon.boundingRect().adjusted(-8.0, -8.0, 8.0, 8.0).toAlignedRect();
    if (overlay_geometry.isEmpty()) {
      hide();
      return;
    }

    setGeometry(overlay_geometry);
    QPolygon mask_polygon;
    QVariantList editor_polygon;
    const QPointF origin(overlay_geometry.topLeft());
    for (const auto& point : canvas_polygon) {
      mask_polygon << (point - origin).toPoint();
      editor_polygon.push_back(point);
    }
    auto mask = QRegion(mask_polygon);
    QVariantList handle_centers;
    for (const auto handle : kResizeHandles) {
      const auto handle_rect = resize_handle_rect(handle);
      mask = mask.united(QRegion(handle_rect.toAlignedRect()));
      handle_centers.push_back(handle_rect.center() + origin);
    }
    setProperty("patchy.transformedTextEditorPolygon", editor_polygon);
    setProperty("patchy.transformedTextResizeHandleCenters", handle_centers);
    setMask(mask);
    show();
    raise();
    update();
  }

  [[nodiscard]] bool wants_canvas_mouse_event(QPointF canvas_point, QEvent::Type event_type,
                                              Qt::MouseButtons buttons) const {
    if (editor_ == nullptr || !isVisible()) {
      return false;
    }
    const QPointF overlay_point = canvas_point - QPointF(geometry().topLeft());
    if (selecting_ || resize_drag_.has_value()) {
      return event_type == QEvent::MouseMove || event_type == QEvent::MouseButtonRelease;
    }
    if (event_type == QEvent::MouseButtonRelease) {
      return false;
    }
    if (event_type == QEvent::MouseMove && (buttons & Qt::LeftButton) != 0) {
      return false;
    }
    if (resize_handle_at(overlay_point) != ResizeHandle::None) {
      return true;
    }
    if (!rect().adjusted(-2, -2, 2, 2).contains(overlay_point.toPoint())) {
      return false;
    }
    const auto editor_point = map_overlay_point_to_editor(overlay_point);
    return editor_point.has_value() && editor_local_rect().adjusted(-4, -4, 4, 4).contains(editor_point->toPoint());
  }

  [[nodiscard]] bool has_resize_handle_at_canvas_point(QPointF canvas_point) const {
    if (editor_ == nullptr || !isVisible()) {
      return false;
    }
    return resize_handle_at(canvas_point - QPointF(pos())) != ResizeHandle::None;
  }

  bool handle_canvas_mouse_event(QMouseEvent* mouse_event) {
    if (mouse_event == nullptr) {
      return false;
    }
    return handle_canvas_mouse_event(mouse_event, mouse_event->position());
  }

  bool handle_canvas_mouse_event(QMouseEvent* mouse_event, QPointF canvas_position) {
    if (mouse_event == nullptr ||
        !wants_canvas_mouse_event(canvas_position, mouse_event->type(), mouse_event->buttons())) {
      return false;
    }

    const QPointF overlay_position = canvas_position - QPointF(pos());
    QMouseEvent forwarded(mouse_event->type(), overlay_position, mouse_event->globalPosition(), mouse_event->button(),
                          mouse_event->buttons(), mouse_event->modifiers());
    switch (mouse_event->type()) {
      case QEvent::MouseButtonPress:
        mousePressEvent(&forwarded);
        break;
      case QEvent::MouseMove:
        mouseMoveEvent(&forwarded);
        break;
      case QEvent::MouseButtonRelease:
        mouseReleaseEvent(&forwarded);
        break;
      case QEvent::MouseButtonDblClick:
        mouseDoubleClickEvent(&forwarded);
        break;
      default:
        break;
    }
    if (!forwarded.isAccepted()) {
      return false;
    }
    mouse_event->accept();
    return true;
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    if (canvas_ == nullptr || editor_ == nullptr || editor_->viewport() == nullptr) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint_selection(painter);
    paint_caret(painter);
    paint_text_box_controls(painter);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || editor_ == nullptr) {
      event->ignore();
      return;
    }
    if (const auto handle = resize_handle_at(event->position()); handle != ResizeHandle::None) {
      editor_->setFocus(Qt::MouseFocusReason);
      resize_drag_ = ResizeDragState{handle,
                                     text_transform_,
                                     editor_point_for_handle(handle),
                                     std::max(kMinimumTextBoxDocumentSize,
                                              editor_->property("patchy.documentTextWidth").toInt()),
                                     std::max(kMinimumTextBoxDocumentSize,
                                              editor_->property("patchy.documentTextHeight").toInt())};
      event->accept();
      return;
    }

    const auto position = cursor_position_for_overlay_point(event->position());
    if (!position.has_value()) {
      event->ignore();
      return;
    }

    editor_->setFocus(Qt::MouseFocusReason);
    auto cursor = editor_->textCursor();
    if ((event->modifiers() & Qt::ShiftModifier) != 0) {
      selection_anchor_ = cursor.position();
      cursor.setPosition(selection_anchor_);
      cursor.setPosition(*position, QTextCursor::KeepAnchor);
    } else {
      selection_anchor_ = *position;
      cursor.setPosition(*position);
    }
    editor_->setTextCursor(cursor);
    selecting_ = true;
    event->accept();
    update();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (resize_drag_.has_value()) {
      if (const auto geometry = resize_geometry_for_event(event); geometry.has_value()) {
        resize_preview_transform_ = geometry->transform;
        resize_preview_document_size_ = QSizeF(geometry->width, geometry->height);
        sync_geometry();
      }
      event->accept();
      return;
    }
    if (!selecting_ || (event->buttons() & Qt::LeftButton) == 0 || editor_ == nullptr) {
      update_hover_cursor(event->position());
      event->ignore();
      return;
    }
    const auto position = cursor_position_for_overlay_point(event->position());
    if (!position.has_value()) {
      event->ignore();
      return;
    }

    auto cursor = editor_->textCursor();
    cursor.setPosition(selection_anchor_);
    cursor.setPosition(*position, QTextCursor::KeepAnchor);
    editor_->setTextCursor(cursor);
    event->accept();
    update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && resize_drag_.has_value()) {
      resize_text_box(event);
      resize_drag_.reset();
      update_hover_cursor(event->position());
      schedule_resize_callback();
      event->accept();
      return;
    }
    if (event->button() == Qt::LeftButton && selecting_) {
      selecting_ = false;
      event->accept();
      return;
    }
    event->ignore();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || editor_ == nullptr) {
      event->ignore();
      return;
    }
    const auto editor_point = map_overlay_point_to_editor(event->position());
    if (!editor_point.has_value() || !editor_local_rect().contains(editor_point->toPoint())) {
      event->ignore();
      return;
    }

    editor_->setFocus(Qt::MouseFocusReason);
    auto cursor = editor_->cursorForPosition(editor_point->toPoint());
    cursor.select(QTextCursor::WordUnderCursor);
    selection_anchor_ = cursor.selectionStart();
    editor_->setTextCursor(cursor);
    selecting_ = false;
    event->accept();
    update();
  }

private:
  [[nodiscard]] double zoom() const noexcept {
    return canvas_ == nullptr ? 1.0 : std::max(0.05, canvas_->zoom());
  }

  [[nodiscard]] QPointF document_origin_on_canvas() const {
    return canvas_ == nullptr ? QPointF() : QPointF(canvas_->widget_position_for_document_point(QPoint(0, 0)));
  }

  [[nodiscard]] QPointF map_editor_point_to_canvas(QPointF editor_point) const {
    const auto current_zoom = zoom();
    const QPointF document_local(editor_point.x() / current_zoom, editor_point.y() / current_zoom);
    const auto document_point = active_text_transform().map(document_local);
    const auto origin = document_origin_on_canvas();
    return QPointF(origin.x() + document_point.x() * current_zoom,
                   origin.y() + document_point.y() * current_zoom);
  }

  [[nodiscard]] std::optional<QPointF> map_canvas_point_to_editor(QPointF canvas_point,
                                                                  const QTransform& transform) const {
    bool invertible = false;
    const auto inverse = transform.inverted(&invertible);
    if (!invertible) {
      return std::nullopt;
    }

    const auto current_zoom = zoom();
    const auto origin = document_origin_on_canvas();
    const QPointF document_point((canvas_point.x() - origin.x()) / current_zoom,
                                 (canvas_point.y() - origin.y()) / current_zoom);
    const auto document_local = inverse.map(document_point);
    return QPointF(document_local.x() * current_zoom, document_local.y() * current_zoom);
  }

  [[nodiscard]] std::optional<QPointF> map_canvas_point_to_editor(QPointF canvas_point) const {
    return map_canvas_point_to_editor(canvas_point, active_text_transform());
  }

  [[nodiscard]] std::optional<QPointF> map_overlay_point_to_editor(QPointF overlay_point) const {
    return map_canvas_point_to_editor(overlay_point + QPointF(geometry().topLeft()));
  }

  [[nodiscard]] QRect editor_local_rect() const {
    return editor_ == nullptr || editor_->viewport() == nullptr ? QRect() : editor_->viewport()->rect();
  }

  [[nodiscard]] QRectF editor_visual_rect() const {
    if (resize_preview_document_size_.has_value()) {
      const auto current_zoom = zoom();
      return QRectF(0.0, 0.0, resize_preview_document_size_->width() * current_zoom,
                    resize_preview_document_size_->height() * current_zoom);
    }
    const auto visible_rect_value =
        editor_ == nullptr ? QVariant() : editor_->property(kTextEditorVisibleLocalRectProperty);
    if (visible_rect_value.isValid()) {
      const auto visible_rect = visible_rect_value.toRectF();
      if (visible_rect.isValid() && !visible_rect.isEmpty()) {
        const auto current_zoom = zoom();
        return QRectF(visible_rect.left() * current_zoom,
                      visible_rect.top() * current_zoom,
                      std::max<qreal>(1.0, visible_rect.width() * current_zoom),
                      std::max<qreal>(1.0, visible_rect.height() * current_zoom));
      }
    }
    return QRectF(editor_local_rect());
  }

  [[nodiscard]] QTransform active_text_transform() const {
    return resize_preview_transform_.value_or(text_transform_);
  }

  [[nodiscard]] std::optional<int> cursor_position_for_overlay_point(QPointF overlay_point) const {
    if (editor_ == nullptr) {
      return std::nullopt;
    }
    const auto editor_point = map_overlay_point_to_editor(overlay_point);
    if (!editor_point.has_value() || !editor_local_rect().adjusted(-4, -4, 4, 4).contains(editor_point->toPoint())) {
      return std::nullopt;
    }
    return editor_->cursorForPosition(editor_point->toPoint()).position();
  }

  [[nodiscard]] QPolygonF map_editor_rect_to_canvas(QRectF rect) const {
    QPolygonF polygon;
    polygon << map_editor_point_to_canvas(rect.topLeft()) << map_editor_point_to_canvas(rect.topRight())
            << map_editor_point_to_canvas(rect.bottomRight()) << map_editor_point_to_canvas(rect.bottomLeft());
    return polygon;
  }

  [[nodiscard]] QPolygonF map_editor_rect_to_overlay(QRectF rect) const {
    const QPointF overlay_origin(geometry().topLeft());
    auto polygon = map_editor_rect_to_canvas(rect);
    for (auto& point : polygon) {
      point -= overlay_origin;
    }
    return polygon;
  }

  [[nodiscard]] QPointF editor_point_for_handle(ResizeHandle handle) const {
    const auto rect = editor_visual_rect();
    switch (handle) {
      case ResizeHandle::TopLeft:
        return rect.topLeft();
      case ResizeHandle::TopRight:
        return rect.topRight();
      case ResizeHandle::BottomLeft:
        return rect.bottomLeft();
      case ResizeHandle::BottomRight:
        return rect.bottomRight();
      case ResizeHandle::None:
        break;
    }
    return QPointF(rect.center());
  }

  [[nodiscard]] QRectF resize_handle_rect(ResizeHandle handle) const {
    constexpr double kHandleSize = 6.0;
    const auto center = map_editor_rect_to_overlay(QRectF(editor_point_for_handle(handle), QSizeF(1.0, 1.0))).at(0);
    return QRectF(center.x() - kHandleSize / 2.0, center.y() - kHandleSize / 2.0, kHandleSize, kHandleSize);
  }

  [[nodiscard]] ResizeHandle resize_handle_at(QPointF point) const {
    for (const auto handle : kResizeHandles) {
      if (resize_handle_rect(handle).adjusted(-5.0, -5.0, 5.0, 5.0).contains(point)) {
        return handle;
      }
    }
    return ResizeHandle::None;
  }

  [[nodiscard]] Qt::CursorShape cursor_for_handle(ResizeHandle handle) const {
    switch (handle) {
      case ResizeHandle::TopLeft:
      case ResizeHandle::BottomRight:
        return Qt::SizeFDiagCursor;
      case ResizeHandle::TopRight:
      case ResizeHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
      case ResizeHandle::None:
        return Qt::IBeamCursor;
    }
    return Qt::IBeamCursor;
  }

  void update_hover_cursor(QPointF point) {
    setCursor(cursor_for_handle(resize_handle_at(point)));
  }

  [[nodiscard]] bool caret_visible() const {
    const auto flash_time = text_editor_caret_blink_phase_ms();
    if (flash_time <= 0 || !caret_blink_clock_.isValid()) {
      return true;
    }
    return ((caret_blink_clock_.elapsed() / flash_time) % 2) == 0;
  }

  void paint_selection(QPainter& painter) const {
    const auto cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
      return;
    }

    QColor highlight = editor_->palette().color(QPalette::Highlight);
    highlight.setAlpha(120);
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    const auto selection_rects =
        text_editor_viewport_selection_rects(*editor_, cursor.selectionStart(), cursor.selectionEnd());
    for (const auto& rect : selection_rects) {
      painter.drawPolygon(map_editor_rect_to_overlay(QRectF(rect)));
    }
  }

  void paint_caret(QPainter& painter) const {
    if (!editor_->hasFocus() || !caret_visible()) {
      return;
    }

    auto caret = text_editor_viewport_caret_rect(*editor_);
    const auto caret_width = text_editor_caret_width(*editor_);
    if (caret.isEmpty() || caret_width <= 0) {
      return;
    }
    caret.setWidth(caret_width);
    painter.setPen(Qt::NoPen);
    painter.setBrush(editor_->palette().color(QPalette::Text));
    painter.drawPolygon(map_editor_rect_to_overlay(QRectF(caret)));
  }

  void paint_text_box_controls(QPainter& painter) const {
    const auto rect = editor_visual_rect();
    if (rect.isEmpty()) {
      return;
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(99, 168, 255), 1.0, Qt::DashLine));
    painter.drawPolygon(map_editor_rect_to_overlay(rect));

    painter.setPen(QPen(QColor(35, 38, 44), 1.0));
    painter.setBrush(QColor(245, 248, 252));
    for (const auto handle : kResizeHandles) {
      painter.drawRect(resize_handle_rect(handle));
    }
  }

  [[nodiscard]] std::optional<ResizeGeometry> resize_geometry_for_event(QMouseEvent* event) const {
    if (canvas_ == nullptr || editor_ == nullptr || !resize_drag_.has_value()) {
      return std::nullopt;
    }

    const auto canvas_point = QPointF(canvas_->mapFromGlobal(event->globalPosition().toPoint()));
    const auto current_editor_point = map_canvas_point_to_editor(canvas_point, resize_drag_->start_transform);
    if (!current_editor_point.has_value()) {
      return std::nullopt;
    }

    const auto current_zoom = zoom();
    const QPointF delta((current_editor_point->x() - resize_drag_->start_editor_point.x()) / current_zoom,
                        (current_editor_point->y() - resize_drag_->start_editor_point.y()) / current_zoom);
    double left = 0.0;
    double top = 0.0;
    double right = static_cast<double>(resize_drag_->start_width);
    double bottom = static_cast<double>(resize_drag_->start_height);

    switch (resize_drag_->handle) {
      case ResizeHandle::TopLeft:
        left = std::min(right - kMinimumTextBoxDocumentSize, delta.x());
        top = std::min(bottom - kMinimumTextBoxDocumentSize, delta.y());
        break;
      case ResizeHandle::TopRight:
        right = std::max(left + kMinimumTextBoxDocumentSize,
                         static_cast<double>(resize_drag_->start_width) + delta.x());
        top = std::min(bottom - kMinimumTextBoxDocumentSize, delta.y());
        break;
      case ResizeHandle::BottomLeft:
        left = std::min(right - kMinimumTextBoxDocumentSize, delta.x());
        bottom = std::max(top + kMinimumTextBoxDocumentSize,
                          static_cast<double>(resize_drag_->start_height) + delta.y());
        break;
      case ResizeHandle::BottomRight:
        right = std::max(left + kMinimumTextBoxDocumentSize,
                         static_cast<double>(resize_drag_->start_width) + delta.x());
        bottom = std::max(top + kMinimumTextBoxDocumentSize,
                          static_cast<double>(resize_drag_->start_height) + delta.y());
        break;
      case ResizeHandle::None:
        return std::nullopt;
    }

    const auto new_origin = resize_drag_->start_transform.map(QPointF(left, top));
    QTransform adjusted(resize_drag_->start_transform.m11(),
                        resize_drag_->start_transform.m12(),
                        resize_drag_->start_transform.m21(),
                        resize_drag_->start_transform.m22(),
                        new_origin.x(),
                        new_origin.y());
    return ResizeGeometry{adjusted,
                          std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::round(right - left))),
                          std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::round(bottom - top)))};
  }

  void resize_text_box(QMouseEvent* event) {
    const auto geometry = resize_geometry_for_event(event);
    if (!geometry.has_value() || editor_ == nullptr) {
      return;
    }
    resize_preview_transform_ = geometry->transform;
    resize_preview_document_size_ = QSizeF(geometry->width, geometry->height);

    const auto& adjusted = geometry->transform;
    const LayerAffineTransform affine{adjusted.m11(), adjusted.m12(), adjusted.m21(),
                                      adjusted.m22(), adjusted.dx(),  adjusted.dy()};
    editor_->setProperty(kTextEditorTransformOverrideProperty,
                         QString::fromStdString(serialize_layer_affine_transform(affine)));
    editor_->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant());
    editor_->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant());
    editor_->setProperty(kTextEditorVisibleLocalRectProperty, QVariant());
    // Box geometry changed: drop the import-frame render rect so glyphs lay out against the live
    // box instead of the original Photoshop frame (free helper isn't visible inside this class).
    editor_->setProperty(kTextEditorRenderLocalRectProperty, QVariant());
    editor_->setProperty("patchy.documentTextX", static_cast<int>(std::floor(adjusted.dx())));
    editor_->setProperty("patchy.documentTextY", static_cast<int>(std::floor(adjusted.dy())));
    editor_->setProperty("patchy.documentTextFlow", QString::fromLatin1(kTextFlowBox));
    editor_->setProperty("patchy.documentTextWidth", geometry->width);
    editor_->setProperty("patchy.documentTextHeight", geometry->height);
    sync_geometry();
  }

  void schedule_resize_callback() {
    if (resize_callback_pending_ || editor_ == nullptr || !resize_callback_) {
      return;
    }
    resize_callback_pending_ = true;
    QTimer::singleShot(0, this, [this, editor = QPointer<QTextEdit>(editor_)] {
      resize_callback_pending_ = false;
      if (editor != nullptr && resize_callback_) {
        resize_callback_(editor);
      }
    });
  }

  CanvasWidget* canvas_{nullptr};
  QPointer<QTextEdit> editor_;
  QTransform text_transform_;
  std::function<void(QTextEdit*)> resize_callback_;
  bool selecting_{false};
  bool resize_callback_pending_{false};
  std::optional<QTransform> resize_preview_transform_;
  std::optional<QSizeF> resize_preview_document_size_;
  std::optional<ResizeDragState> resize_drag_;
  int selection_anchor_{0};
  QElapsedTimer caret_blink_clock_;
  QTimer caret_blink_timer_;
};

TransformedTextEditOverlay* transformed_text_edit_overlay_for_canvas(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return nullptr;
  }
  auto* widget = canvas->findChild<QWidget*>(QString::fromLatin1(kTransformedTextEditOverlayObjectName),
                                             Qt::FindDirectChildrenOnly);
  return widget == nullptr ? nullptr : static_cast<TransformedTextEditOverlay*>(widget);
}

QString document_html_for_editor(const QString& document_html, const QFont& editor_font, double zoom) {
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(editor_font);
  document.setHtml(document_html);
  scale_document_font_sizes(document, zoom);
  return document.toHtml();
}

QString document_html_from_editor(const QTextEdit& editor, double zoom) {
  return normalized_rich_text_html(*document_from_editor_in_document_units(editor, zoom));
}

QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color) {
  QStringList lines;
  lines << QStringLiteral("v1");
  bool includes_leading = false;
  const auto fallback_family = fallback.family.isEmpty() ? QApplication::font().family() : fallback.family;
  const auto fallback_size = std::max(1, fallback.size);
  const auto fallback_color_name = (fallback_color.isValid() ? fallback_color : QColor(Qt::black)).name(QColor::HexRgb);

  const auto append_run = [&lines, &includes_leading, &fallback_family, fallback_size, &fallback_color_name](
                              int start, int length, const QTextCharFormat& format) {
    if (length <= 0) {
      return;
    }
    auto format_font = format.font();
    auto family = text_display_family_from_format(format, fallback_family);
    int size = format_font.pixelSize();
    if (size <= 0) {
      size = format_font.pointSizeF() > 0.0 ? static_cast<int>(std::round(format_font.pointSizeF())) : fallback_size;
    }
    QColor color = format.foreground().color();
    if (!color.isValid()) {
      color = QColor(fallback_color_name);
    }
    const auto leading = format.hasProperty(kTextLeadingFormatProperty)
                             ? format.property(kTextLeadingFormatProperty).toDouble()
                             : 0.0;
    const auto encoded_family = QString::fromLatin1(family.toUtf8().toPercentEncoding());
    auto line = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                    .arg(start)
                    .arg(length)
                    .arg(std::max(1, size))
                    .arg(format_font.weight() >= QFont::Bold ? 1 : 0)
                    .arg(format_font.italic() ? 1 : 0)
                    .arg(color.name(QColor::HexRgb))
                    .arg(encoded_family);
    if (std::isfinite(leading) && leading > 0.0) {
      includes_leading = true;
      line += QStringLiteral("\t%1").arg(QString::number(leading, 'g', 17));
    }
    lines << line;
  };

  auto fallback_format = [&fallback_family, fallback_size, &fallback_color_name, &fallback]() {
    QTextCharFormat format;
    auto font = render_text_font_for_display_family(fallback_family, fallback_size, fallback.bold,
                                                    fallback.italic, fallback.anti_alias);
    format.setFont(font);
    set_text_display_family(format, fallback_family);
    format.setForeground(QBrush(QColor(fallback_color_name)));
    return format;
  }();

  bool found_run = false;
  QTextCharFormat last_format = fallback_format;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    bool block_has_fragment = false;
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      block_has_fragment = true;
      found_run = true;
      last_format = fragment.charFormat();
      append_run(fragment.position(), fragment.length(), fragment.charFormat());
    }
    if (block.next().isValid()) {
      const auto separator_position = block.position() + block.length() - 1;
      if (separator_position >= 0 && separator_position < plain_length) {
        const auto separator_format = block_has_fragment ? last_format
                                                         : (block.charFormat().isValid() ? block.charFormat()
                                                                                         : last_format);
        found_run = true;
        append_run(separator_position, 1, separator_format);
        last_format = separator_format;
      }
    }
  }

  if (!found_run) {
    append_run(0, document.toPlainText().size(), fallback_format);
  }
  if (includes_leading) {
    lines[0] = QStringLiteral("v2");
  }
  return lines.join(QLatin1Char('\n'));
}

QString paragraph_runs_from_document(const QTextDocument& document) {
  struct ParagraphRunLine {
    int start{0};
    int length{0};
    QString alignment;
    double first_line_indent{0.0};
    double start_indent{0.0};
    double end_indent{0.0};
    double space_before{0.0};
    double space_after{0.0};
  };
  const auto normalized_metric = [](double value) {
    return std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
  };
  const auto metric_text = [](double value) {
    value = std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
    return QString::number(value, 'g', 17);
  };

  std::vector<ParagraphRunLine> run_lines;
  bool include_layout = false;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto length = std::max(0, std::min(block.length(), std::max(0, plain_length - start)));
    const auto format = block.blockFormat();
    ParagraphRunLine run;
    run.start = start;
    run.length = length;
    run.alignment = paragraph_alignment_name(format.alignment());
    run.first_line_indent = normalized_metric(format.textIndent());
    run.start_indent = normalized_metric(format.leftMargin());
    run.end_indent = normalized_metric(format.rightMargin());
    run.space_before = normalized_metric(format.topMargin());
    run.space_after = normalized_metric(format.bottomMargin());
    include_layout = include_layout || run.first_line_indent != 0.0 || run.start_indent != 0.0 ||
                     run.end_indent != 0.0 || run.space_before != 0.0 || run.space_after != 0.0;
    run_lines.push_back(std::move(run));
  }

  QStringList lines;
  lines << (include_layout ? QStringLiteral("v2") : QStringLiteral("v1"));
  for (const auto& run : run_lines) {
    auto line = QStringLiteral("%1\t%2\t%3").arg(run.start).arg(run.length).arg(run.alignment);
    if (include_layout) {
      line += QStringLiteral("\t%1\t%2\t%3\t%4\t%5")
                  .arg(metric_text(run.first_line_indent))
                  .arg(metric_text(run.start_indent))
                  .arg(metric_text(run.end_indent))
                  .arg(metric_text(run.space_before))
                  .arg(metric_text(run.space_after));
    }
    lines << line;
  }
  return lines.join(QLatin1Char('\n'));
}

void apply_paragraph_runs_to_document(QTextDocument& document, const QString& paragraph_runs, double scale = 1.0) {
  const auto lines = paragraph_runs.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  const auto metric_value = [scale](const QString& field) {
    bool ok = false;
    const auto value = field.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
      return 0.0;
    }
    return value * std::max(0.0, scale);
  };
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    if (!start_ok || !length_ok || start > plain_length) {
      continue;
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + std::max(1, length)), QTextCursor::KeepAnchor);
    QTextBlockFormat format;
    format.setAlignment(paragraph_alignment_from_name(fields[2]));
    if (fields.size() >= 8) {
      format.setTextIndent(metric_value(fields[3]));
      format.setLeftMargin(metric_value(fields[4]));
      format.setRightMargin(metric_value(fields[5]));
      format.setTopMargin(metric_value(fields[6]));
      format.setBottomMargin(metric_value(fields[7]));
    }
    cursor.mergeBlockFormat(format);
  }
}

void apply_patchy_text_runs_to_document(QTextDocument& document, const QString& runs_text, const QFont& fallback_font,
                                        QColor fallback_color, double scale, int anti_alias) {
  const auto lines = runs_text.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  bool applied_run = false;
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    bool size_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    const auto document_size = std::max(1, fields[2].toInt(&size_ok));
    if (!start_ok || !length_ok || !size_ok || length <= 0 || start >= plain_length) {
      continue;
    }

    auto family = QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1()));
    if (family.trimmed().isEmpty()) {
      family = display_text_family_from_font(fallback_font);
    } else {
      family = canonical_text_display_family(family);
    }
    auto font = render_text_font_for_display_family(
        family, std::max(1, static_cast<int>(std::round(static_cast<double>(document_size) * scale))),
        fields[3].toInt() != 0, fields[4].toInt() != 0, anti_alias);

    QColor color(fields[5]);
    if (!color.isValid()) {
      color = fallback_color.isValid() ? fallback_color : QColor(Qt::black);
    }

    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, family);
    format.setForeground(QBrush(color));
    if (fields.size() >= 8) {
      bool leading_ok = false;
      const auto leading = fields[7].toDouble(&leading_ok);
      if (leading_ok && std::isfinite(leading) && leading > 0.0) {
        format.setProperty(kTextLeadingFormatProperty, leading * std::max(0.0, scale));
      }
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + length), QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
    applied_run = true;
  }

  if (!applied_run && plain_length > 0) {
    auto font = fallback_font;
    scale_font_size(font, scale);
    configure_text_font_smoothing(font, anti_alias);
    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, display_text_family_from_font(font));
    format.setForeground(QBrush(fallback_color.isValid() ? fallback_color : QColor(Qt::black)));
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(format);
  }
}

QString document_html_from_text_runs(const QString& text, const QString& rich_text_runs,
                                     const TextToolSettings& fallback, QColor fallback_color) {
  const auto fallback_family = fallback.family.isEmpty() ? QApplication::font().family() : fallback.family;
  auto fallback_font = render_text_font_for_display_family(fallback_family, std::max(1, fallback.size),
                                                          fallback.bold, fallback.italic, fallback.anti_alias);

  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(fallback_font);
  document.setPlainText(text);
  apply_patchy_text_runs_to_document(document, rich_text_runs, fallback_font, fallback_color, 1.0,
                                     fallback.anti_alias);
  apply_text_smoothing_to_document(document, fallback.anti_alias);
  return normalized_rich_text_html(document);
}

void apply_plain_text_format(QTextDocument& document, const QFont& font, QColor color) {
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(font);
  set_text_display_family(format, display_text_family_from_font(font));
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);
}

// Re-resolve character formats whose primary family is not an available family but matches a
// family + style pair (see available_text_family_style_match).  HTML round-trips drop the style
// name, so documents built from settings.html would otherwise fall back to a regular-weight face
// on platforms whose font database splits GDI-style names.
void resolve_document_font_styles(QTextDocument& document) {
  struct StylePatch {
    int position{0};
    int length{0};
    QFont font;
  };
  std::vector<StylePatch> patches;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const auto families = format.font().families();
      if (families.isEmpty()) {
        continue;
      }
      const auto& primary = families.front();
      if (available_text_family_match(primary).has_value()) {
        continue;
      }
      const auto match = available_text_family_style_match(primary);
      if (!match.has_value()) {
        continue;
      }
      auto font = format.font();
      font.setFamilies(QStringList{match->family});
      font.setStyleName(match->style);
      patches.push_back(StylePatch{fragment.position(), fragment.length(), std::move(font)});
    }
  }
  for (const auto& patch : patches) {
    QTextCharFormat format;
    format.setFont(patch.font);
    QTextCursor cursor(&document);
    cursor.setPosition(patch.position);
    cursor.setPosition(patch.position + patch.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
  }
}

QTextCharFormat text_editor_typing_format(const QFont& font, QColor color) {
  QTextCharFormat format;
  format.setFont(font);
  set_text_display_family(format, display_text_family_from_font(font));
  format.setForeground(QBrush(color.isValid() ? color : QColor(Qt::black)));
  return format;
}

QTextCharFormat text_editor_reference_format(const QTextEdit& editor) {
  auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    const auto start = std::min(cursor.selectionStart(), cursor.selectionEnd());
    const auto end = std::max(cursor.selectionStart(), cursor.selectionEnd());
    cursor.setPosition(start);
    if (start < end) {
      cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
    }
  }
  return cursor.charFormat();
}

std::optional<double> text_leading_from_document_formats(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      if (!format.hasProperty(kTextLeadingFormatProperty)) {
        continue;
      }
      const auto leading = format.property(kTextLeadingFormatProperty).toDouble();
      if (std::isfinite(leading) && leading > 0.0) {
        return leading;
      }
    }
  }
  return std::nullopt;
}

int document_text_size_from_editor_format(const QTextCharFormat& format, double zoom, int fallback) noexcept {
  const auto font = format.font();
  int editor_pixel_size = font.pixelSize();
  if (editor_pixel_size <= 0 && font.pointSizeF() > 0.0) {
    editor_pixel_size = static_cast<int>(std::round(font.pointSizeF()));
  }
  if (editor_pixel_size <= 0) {
    return std::max(1, fallback);
  }
  return std::max(1, static_cast<int>(std::round(static_cast<double>(editor_pixel_size) / std::max(0.001, zoom))));
}

std::optional<QColor> text_color_from_format(const QTextCharFormat& format) {
  if (format.foreground().style() == Qt::NoBrush) {
    return std::nullopt;
  }
  const auto color = format.foreground().color();
  if (!color.isValid()) {
    return std::nullopt;
  }
  return QColor(color.red(), color.green(), color.blue());
}

void merge_text_char_format(QTextEdit& editor, const QTextCharFormat& format) {
  if (editor.toPlainText().isEmpty()) {
    auto current = editor.currentCharFormat();
    current.merge(format);
    editor.setCurrentCharFormat(current);
    return;
  }

  auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    cursor.mergeCharFormat(format);
    editor.setTextCursor(cursor);
    return;
  }

  editor.mergeCurrentCharFormat(format);
}

void preserve_text_editor_typing_format(QTextEdit& editor, const QFont& font, QColor color) {
  const auto block = editor.textCursor().block();
  const auto empty_document = editor.toPlainText().isEmpty();
  if (!empty_document && editor.textCursor().hasSelection()) {
    return;
  }
  const auto should_preserve_format = empty_document || (block.isValid() && block.text().isEmpty());
  if (!should_preserve_format) {
    return;
  }

  editor.document()->setDefaultFont(font);
  editor.setCurrentCharFormat(text_editor_typing_format(font, color));
  editor.viewport()->update();
}

struct RenderedTextPixels {
  PixelBuffer pixels;
  QRectF local_rect;
};

struct BoxTextLineRenderItem {
  QTextLine line;
  QPointF block_origin;
  QRectF clip_rect;
};

struct BoxTextRenderPlan {
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> lines;
};

std::vector<BoxTextLineRenderItem> boxed_text_line_render_items(const QTextDocument& document, QRectF gate_rect,
                                                                qreal top_bleed, qreal bottom_bleed,
                                                                qreal horizontal_bleed) {
  gate_rect = gate_rect.normalized();
  std::vector<BoxTextLineRenderItem> items;
  if (!std::isfinite(gate_rect.left()) || !std::isfinite(gate_rect.top()) ||
      !std::isfinite(gate_rect.right()) || !std::isfinite(gate_rect.bottom()) ||
      gate_rect.width() <= 0.0 || gate_rect.height() <= 0.0) {
    return items;
  }

  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return items;
  }

  constexpr qreal kLineGateTolerance = 0.01;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto line_rect = line.rect().translated(block_rect.topLeft());
      const auto line_top = line_rect.top();
      const auto line_bottom = line_rect.bottom();
      if (!std::isfinite(line_top) || !std::isfinite(line_bottom)) {
        continue;
      }
      if (line_top >= gate_rect.bottom() - kLineGateTolerance ||
          line_bottom <= gate_rect.top() - kLineGateTolerance) {
        continue;
      }
      items.push_back(BoxTextLineRenderItem{
          line,
          block_rect.topLeft(),
          QRectF(gate_rect.left() - horizontal_bleed,
                 line_top - top_bleed,
                 gate_rect.width() + horizontal_bleed * 2.0,
                 std::max<qreal>(1.0, line_rect.height() + top_bleed + bottom_bleed))});
    }
  }
  return items;
}

BoxTextRenderPlan boxed_text_render_plan(const QTextDocument& document, const QFont& font, QRectF frame_rect,
                                         std::optional<QRectF> requested_local_rect) {
  frame_rect = frame_rect.normalized();
  QRectF gate_rect = frame_rect;
  if (requested_local_rect.has_value()) {
    gate_rect = gate_rect.united(requested_local_rect->normalized());
  }

  const QFontMetricsF metrics(font);
  const auto top_bleed = 2.0;
  const auto bottom_bleed =
      std::max<qreal>(2.0, std::ceil(std::max<qreal>(metrics.descent(), metrics.leading())) + 2.0);
  constexpr qreal kHorizontalBleed = 2.0;

  BoxTextRenderPlan plan{gate_rect, boxed_text_line_render_items(document, gate_rect, top_bleed, bottom_bleed,
                                                                 kHorizontalBleed)};
  if (plan.lines.empty()) {
    return plan;
  }
  for (const auto& item : plan.lines) {
    plan.local_rect = plan.local_rect.united(item.clip_rect);
  }
  return plan;
}

// Build the QTextDocument a text layer is rasterized from.  The caret/selection layout is built
// through this same function (build_text_editor_document_space_layout), so the painted glyphs and
// the caret geometry always come from one identical document -- any divergence in construction
// (formats, blank-line heights, wrapping width) shows up as the caret drifting off the text.
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale) {
  TextRenderDocument result;
  metric_scale = std::clamp(std::isfinite(metric_scale) ? metric_scale : 1.0, 0.5, 1.5);
  result.font = render_text_font_for_display_family(settings.family, std::max(1, settings.size), settings.bold,
                                                    settings.italic, settings.anti_alias);
  scale_font_width(result.font, metric_scale);

  result.text_width = settings.boxed ? std::max(kMinimumTextBoxDocumentSize, settings.box_width)
                                     : std::max(64, max_width);
  result.document = std::make_unique<QTextDocument>();
  auto& document = *result.document;
  document.setDocumentMargin(0);
  document.setDefaultFont(result.font);
  QTextOption option;
  option.setWrapMode(settings.boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  document.setDefaultTextOption(option);
  if (!rich_text_runs.trimmed().isEmpty()) {
    // Build from the plain text + serialized runs -- the same construction the editor uses.  The
    // HTML round-trip below loses character formats on empty paragraphs (old PSDs separate lines
    // with control characters that import as blank paragraphs), which made the drawn text and the
    // selection highlights drift apart.
    document.setPlainText(settings.text);
    apply_patchy_text_runs_to_document(document, rich_text_runs, result.font, color, 1.0, settings.anti_alias);
    scale_document_font_widths(document, metric_scale);
  } else if (!settings.html.trimmed().isEmpty()) {
    document.setHtml(settings.html);
    document.setDocumentMargin(0);
    document.setDefaultTextOption(option);
    // HTML carries family names only; re-resolve names that exist as family + style (e.g.
    // "Arial Black" on platforms whose database splits them) so the correct faces render.
    resolve_document_font_styles(document);
    scale_document_font_widths(document, metric_scale);
  } else {
    document.setPlainText(settings.text);
    apply_plain_text_format(document, result.font, color);
  }
  // NoWrap point text must size to the tight glyph idealWidth.  Passing an explicit textWidth makes
  // Qt report THAT width from document.size().width() (not the ideal width) whenever it exceeds the
  // content, which -- when max_width is a layer's already-scaled pixel bounds -- inflated the rendered
  // image and, after the document transform re-scaled it, produced a free-transform rect several times
  // wider than the glyphs.  Use -1 (auto) for point text so size().width() is the ideal content width.
  document.setTextWidth(settings.boxed ? static_cast<qreal>(result.text_width) : -1.0);
  if (!paragraph_runs.trimmed().isEmpty()) {
    apply_paragraph_runs_to_document(document, paragraph_runs);
  }
  apply_text_smoothing_to_document(document, settings.anti_alias);
  if (!settings.boxed) {
    // Qt only honors paragraph alignment against a finite layout width; leaving point text
    // unconstrained (-1) silently lays every line out flush-left, so centered/right-justified
    // multi-line point text (Photoshop aligns each line around the type anchor) collapsed to
    // left-justified on commit.  The ideal width is the tight content width, so this keeps the
    // glyph-snug bounds the -1 form was chosen for while letting alignment position the lines.
    document.setTextWidth(document.idealWidth());
  }
  return result;
}

RenderedTextPixels render_text_pixels_with_local_rect(const TextToolSettings& settings, QColor color,
                                                      std::int32_t max_width,
                                                      const QString& paragraph_runs = QString(),
                                                      const QString& rich_text_runs = QString(),
                                                      std::optional<QRectF> requested_local_rect = std::nullopt,
                                                      double metric_scale = 1.0,
                                                      const QTransform& document_transform = QTransform()) {
  auto built = build_text_render_document(settings, color, max_width, paragraph_runs, rich_text_runs,
                                          metric_scale);
  auto& document = *built.document;
  const auto& font = built.font;
  const auto text_width = built.text_width;

  const auto size = document.size();
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> line_render_items;
  if (settings.boxed) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(text_width),
                        static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, settings.box_height)));
    if (requested_local_rect.has_value()) {
      auto plan = boxed_text_render_plan(document, font, local_rect, requested_local_rect);
      local_rect = plan.local_rect;
      line_render_items = std::move(plan.lines);
    }
  } else {
    local_rect = QRectF(0.0, 0.0, std::max<qreal>(1.0, std::ceil(size.width()) + 2.0),
                        std::max<qreal>(1.0, std::ceil(size.height()) + 2.0));
  }
  if (!std::isfinite(local_rect.left()) || !std::isfinite(local_rect.top()) ||
      !std::isfinite(local_rect.right()) || !std::isfinite(local_rect.bottom()) ||
      local_rect.width() <= 0.0 || local_rect.height() <= 0.0) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(std::max(1, text_width)),
                        static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, settings.box_height)));
  }
  // When a document transform is supplied the glyphs are rasterized *through* the affine (scale,
  // rotation, shear), so scaled-up text stays crisp instead of resampling an already-rendered bitmap.
  // The output image is sized to the transformed bounds and `local_rect` is returned in document space.
  const bool has_document_transform = !document_transform.isIdentity();
  const auto target_rect = has_document_transform ? document_transform.mapRect(local_rect) : local_rect;
  const auto image_left = static_cast<int>(std::floor(target_rect.left()));
  const auto image_top = static_cast<int>(std::floor(target_rect.top()));
  const auto image_right = static_cast<int>(std::ceil(target_rect.right()));
  const auto image_bottom = static_cast<int>(std::ceil(target_rect.bottom()));
  // Guard against a degenerate transform ballooning the dimensions: an over-large QImage allocates a
  // null image, which would silently drop the crisp render and fall back to the blocky resample.
  constexpr int kMaxTextRasterDimension = 16384;
  const auto image_width = std::clamp(image_right - image_left, 1, kMaxTextRasterDimension);
  const auto image_height = std::clamp(image_bottom - image_top, 1, kMaxTextRasterDimension);
  QImage image(image_width, image_height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return RenderedTextPixels{PixelBuffer{}, local_rect};
  }
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::TextAntialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, settings.anti_alias > 0);
  painter.translate(-static_cast<qreal>(image_left), -static_cast<qreal>(image_top));
  if (has_document_transform) {
    painter.setTransform(document_transform, true);
  }
  if (!line_render_items.empty()) {
    for (const auto& item : line_render_items) {
      painter.save();
      painter.setClipRect(item.clip_rect);
      item.line.draw(&painter, item.block_origin);
      painter.restore();
    }
  } else {
    document.drawContents(&painter, local_rect);
  }
  painter.end();
  if (settings.anti_alias <= 0) {
    for (int y = 0; y < image.height(); ++y) {
      auto* line = image.scanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        auto* pixel = line + static_cast<std::size_t>(x) * 4U;
        pixel[3] = pixel[3] >= 128 ? 255 : 0;
      }
    }
  }
  return RenderedTextPixels{pixels_from_image_rgba(image),
                            QRectF(static_cast<qreal>(image_left), static_cast<qreal>(image_top),
                                   static_cast<qreal>(image_width), static_cast<qreal>(image_height))};
}

PixelBuffer render_text_pixels(const TextToolSettings& settings, QColor color, std::int32_t max_width,
                               const QString& paragraph_runs = QString(),
                               const QString& rich_text_runs = QString()) {
  return render_text_pixels_with_local_rect(settings, color, max_width, paragraph_runs, rich_text_runs).pixels;
}

struct TransformedTextPixels {
  PixelBuffer pixels;
  Rect bounds{};
};

std::optional<LayerAffineTransform> canonical_text_affine_transform_for_layer(const Layer& layer) {
  if (const auto found = layer.metadata().find(kLayerMetadataTextTransform); found != layer.metadata().end()) {
    return parse_layer_affine_transform(found->second);
  }
  if (const auto found = layer.metadata().find(kLayerMetadataPsdTextTransform); found != layer.metadata().end()) {
    return parse_layer_affine_transform(found->second);
  }
  return std::nullopt;
}

std::optional<QTransform> patchy_text_transform_for_layer(const Layer& layer) {
  const auto parsed = canonical_text_affine_transform_for_layer(layer);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  return QTransform((*parsed)[0], (*parsed)[1], (*parsed)[2], (*parsed)[3], (*parsed)[4], (*parsed)[5]);
}

QTransform qtransform_from_affine(const LayerAffineTransform& transform) {
  return QTransform(transform[0], transform[1], transform[2], transform[3], transform[4], transform[5]);
}

std::optional<LayerAffineTransform> text_editor_transform_override(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorTransformOverrideProperty).toString().trimmed();
  if (value.isEmpty()) {
    return std::nullopt;
  }
  return parse_layer_affine_transform(value.toStdString());
}

void set_text_editor_transform_override(QTextEdit& editor, const LayerAffineTransform& transform) {
  editor.setProperty(kTextEditorTransformOverrideProperty,
                     QString::fromStdString(serialize_layer_affine_transform(transform)));
  editor.setProperty("patchy.documentTextX", static_cast<int>(std::floor(transform[4])));
  editor.setProperty("patchy.documentTextY", static_cast<int>(std::floor(transform[5])));
}

void set_text_editor_visible_local_rect(QTextEdit& editor, const Rect& bounds) {
  editor.setProperty(kTextEditorVisibleLocalRectProperty,
                     QRectF(bounds.x, bounds.y, std::max(1, bounds.width), std::max(1, bounds.height)));
}

void clear_text_editor_visible_local_rect(QTextEdit& editor) {
  editor.setProperty(kTextEditorVisibleLocalRectProperty, QVariant());
}

void set_text_editor_render_local_rect(QTextEdit& editor, const QRectF& rect) {
  if (const auto valid = valid_text_local_rect(rect); valid.has_value()) {
    editor.setProperty(kTextEditorRenderLocalRectProperty, *valid);
  }
}

void clear_text_editor_render_local_rect(QTextEdit& editor) {
  editor.setProperty(kTextEditorRenderLocalRectProperty, QVariant());
}

std::optional<QRectF> text_editor_render_local_rect(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorRenderLocalRectProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  return valid_text_local_rect(value.toRectF());
}

std::optional<QPointF> text_editor_source_visible_anchor(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorSourceVisibleAnchorProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  const auto point = value.toPointF();
  if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
    return std::nullopt;
  }
  return point;
}

std::optional<QSizeF> text_editor_source_visible_size(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorSourceVisibleSizeProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  const auto size = value.toSizeF();
  if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0.0 ||
      size.height() <= 0.0) {
    return std::nullopt;
  }
  return size;
}

// The horizontal fraction of a text block's width that its justification pins in place: Photoshop
// point text keeps the anchor at the line start for left text, the line middle for centered text,
// and the line end for right-justified text.
double horizontal_alignment_anchor_factor(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return 0.5;
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return 1.0;
  }
  return 0.0;
}

// Multi-paragraph point text can mix justifications, but the rendered raster is placed as one
// block; the first paragraph's justification decides which point of the block stays fixed.
double text_editor_anchor_alignment_factor(const QTextEdit& editor) {
  const auto block = editor.document()->begin();
  return block.isValid() ? horizontal_alignment_anchor_factor(block.blockFormat().alignment()) : 0.0;
}

// Same as text_editor_anchor_alignment_factor, derived from a layer's stored paragraph runs for
// paths that re-render text without an open editor (e.g. free transform of a text layer).
double layer_anchor_alignment_factor(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataTextParagraphRuns);
  if (found == layer.metadata().end()) {
    return 0.0;
  }
  const auto lines = QString::fromStdString(found->second).split(QLatin1Char('\n'));
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    return horizontal_alignment_anchor_factor(paragraph_alignment_from_name(fields[2]));
  }
  return 0.0;
}

bool text_document_has_non_left_alignment(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    if ((block.blockFormat().alignment() & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignJustify)) != 0) {
      return true;
    }
  }
  return false;
}

double text_editor_metric_scale(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorMetricScaleProperty);
  if (!value.isValid()) {
    return 1.0;
  }
  const auto scale = value.toDouble();
  return std::clamp(std::isfinite(scale) ? scale : 1.0, 0.5, 1.5);
}

void set_text_editor_metric_scale(QTextEdit& editor, double scale) {
  scale = std::clamp(std::isfinite(scale) ? scale : 1.0, 0.5, 1.5);
  if (std::abs(scale - 1.0) < 0.001) {
    editor.setProperty(kTextEditorMetricScaleProperty, QVariant());
    return;
  }
  editor.setProperty(kTextEditorMetricScaleProperty, scale);
}

struct AlphaRowBand {
  int top{0};
  int bottom{0};
};

std::vector<AlphaRowBand> alpha_row_bands(const PixelBuffer& pixels) {
  std::vector<AlphaRowBand> bands;
  if (pixels.empty() || pixels.width() <= 0 || pixels.height() <= 0) {
    return bands;
  }
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return bands;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  const auto active_threshold = std::max(2, pixels.width() / 180);
  constexpr int kAlphaThreshold = 12;
  constexpr int kMergeGap = 2;

  bool in_band = false;
  int band_top = 0;
  int last_active = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    int active_pixels = 0;
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset < bytes.size() && bytes[offset] >= kAlphaThreshold) {
        ++active_pixels;
      }
    }
    const bool active = active_pixels >= active_threshold;
    if (active) {
      if (!in_band) {
        in_band = true;
        band_top = y;
      }
      last_active = y;
    } else if (in_band && last_active >= 0 && y - last_active > kMergeGap) {
      if (last_active + 1 - band_top >= 2) {
        bands.push_back(AlphaRowBand{band_top, last_active + 1});
      }
      in_band = false;
      last_active = -1;
    }
  }
  if (in_band && last_active + 1 - band_top >= 2) {
    bands.push_back(AlphaRowBand{band_top, last_active + 1});
  }
  return bands;
}

int alpha_row_band_span(const std::vector<AlphaRowBand>& bands) {
  if (bands.empty()) {
    return 0;
  }
  return std::max(0, bands.back().bottom - bands.front().top);
}

double alpha_row_band_score(const std::vector<AlphaRowBand>& source_bands,
                            const std::vector<AlphaRowBand>& candidate_bands,
                            double scale) {
  if (source_bands.empty() || candidate_bands.empty()) {
    return std::numeric_limits<double>::infinity();
  }
  const auto source_count = static_cast<int>(source_bands.size());
  const auto candidate_count = static_cast<int>(candidate_bands.size());
  const auto count_delta = std::abs(candidate_count - source_count);
  double score = static_cast<double>(count_delta) * (candidate_count > source_count ? 1000.0 : 650.0);

  const auto source_span = std::max(1, alpha_row_band_span(source_bands));
  const auto candidate_span = std::max(1, alpha_row_band_span(candidate_bands));
  score += std::abs(static_cast<double>(candidate_span - source_span)) /
           static_cast<double>(source_span) * 180.0;

  const auto compared = std::min(source_count, candidate_count);
  for (int index = 0; index < compared; ++index) {
    const auto source_height = std::max(1, source_bands[static_cast<std::size_t>(index)].bottom -
                                              source_bands[static_cast<std::size_t>(index)].top);
    const auto candidate_height = std::max(1, candidate_bands[static_cast<std::size_t>(index)].bottom -
                                                 candidate_bands[static_cast<std::size_t>(index)].top);
    score += std::abs(static_cast<double>(candidate_height - source_height)) /
             static_cast<double>(source_height) * 12.0;
  }

  score += std::max(0.0, 1.0 - scale) * 20.0;
  return score;
}

std::optional<double> calibrated_box_text_metric_scale(const Layer& source_layer,
                                                       const TextToolSettings& settings,
                                                       QColor text_color,
                                                       int text_width,
                                                       const QString& paragraph_runs,
                                                       const QString& rich_text_runs,
                                                       std::optional<QRectF> render_local_rect) {
  if (!settings.boxed || source_layer.pixels().empty()) {
    return std::nullopt;
  }
  const auto source_bands = alpha_row_bands(source_layer.pixels());
  if (source_bands.size() < 2U) {
    return std::nullopt;
  }

  const auto baseline = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                           rich_text_runs, render_local_rect, 1.0);
  const auto baseline_bands = alpha_row_bands(baseline.pixels);
  const auto baseline_score = alpha_row_band_score(source_bands, baseline_bands, 1.0);
  if (!std::isfinite(baseline_score)) {
    return std::nullopt;
  }
  const auto source_count = static_cast<int>(source_bands.size());
  const auto baseline_count = static_cast<int>(baseline_bands.size());
  const auto source_span = std::max(1, alpha_row_band_span(source_bands));
  const auto baseline_span = std::max(1, alpha_row_band_span(baseline_bands));
  if (baseline_count <= source_count &&
      std::abs(static_cast<double>(baseline_span - source_span)) <= static_cast<double>(source_span) * 0.10) {
    return std::nullopt;
  }

  double best_scale = 1.0;
  double best_score = baseline_score;
  for (int step = 1; step <= 35; ++step) {
    const auto scale = 1.0 - static_cast<double>(step) * 0.01;
    if (scale < 0.65) {
      break;
    }
    const auto candidate = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                              rich_text_runs, render_local_rect, scale);
    const auto candidate_bands = alpha_row_bands(candidate.pixels);
    const auto score = alpha_row_band_score(source_bands, candidate_bands, scale);
    if (score < best_score) {
      best_score = score;
      best_scale = scale;
    }
  }

  if (best_scale < 0.995 && best_score + 20.0 < baseline_score) {
    return best_scale;
  }
  return std::nullopt;
}

std::optional<double> calibrated_box_text_metric_scale_for_editor(const QTextEdit& editor,
                                                                  const Layer& source_layer,
                                                                  double zoom,
                                                                  QColor fallback_color) {
  // The layer text is stored untrimmed: rich-text-run and paragraph-run offsets index the full
  // document text, so trimming here would shift every run when the text starts or ends with
  // whitespace (old PSDs often begin with a blank line).  Trim only for the emptiness check.
  const auto text = editor.toPlainText();
  if (text.trimmed().isEmpty() || !text_flow_is_box(editor.property("patchy.documentTextFlow").toString())) {
    return std::nullopt;
  }
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto text_color = stored_color.isValid() ? stored_color : (fallback_color.isValid() ? fallback_color
                                                                                           : QColor(Qt::black));
  const auto text_anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor.property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(editor),
                                                  display_text_family_from_font(editor.font()));
  }

  auto document_text = document_from_editor_in_document_units(editor, zoom);
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            text_anti_alias,
                            true,
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  return calibrated_box_text_metric_scale(source_layer, settings, text_color, text_width, paragraph_runs,
                                          rich_text_runs, text_editor_render_local_rect(editor));
}

Rect rendered_text_bounds_for_editor(const QTextEdit& editor, QPoint document_point,
                                     const RenderedTextPixels& rendered) {
  if (text_editor_render_local_rect(editor).has_value()) {
    if (const auto anchor = text_editor_source_visible_anchor(editor); anchor.has_value()) {
      if (const auto visible_bounds = visible_alpha_local_bounds(rendered.pixels); visible_bounds.has_value()) {
        return Rect{static_cast<std::int32_t>(std::floor(anchor->x() - static_cast<double>(visible_bounds->x))),
                    static_cast<std::int32_t>(std::floor(anchor->y() - static_cast<double>(visible_bounds->y))),
                    rendered.pixels.width(),
                    rendered.pixels.height()};
      }
    }
    return Rect{static_cast<std::int32_t>(std::floor(static_cast<double>(document_point.x()) +
                                                     rendered.local_rect.left())),
                static_cast<std::int32_t>(std::floor(static_cast<double>(document_point.y()) +
                                                     rendered.local_rect.top())),
                rendered.pixels.width(),
                rendered.pixels.height()};
  }
  return Rect{document_point.x(), document_point.y(), rendered.pixels.width(), rendered.pixels.height()};
}

std::optional<LayerAffineTransform> anchored_text_transform_for_pixels(const QTextEdit& editor,
                                                                       const PixelBuffer& pixels) {
  const auto anchor = text_editor_source_visible_anchor(editor);
  if (!anchor.has_value()) {
    return std::nullopt;
  }
  const auto visible_bounds = visible_alpha_local_bounds(pixels);
  if (!visible_bounds.has_value()) {
    return std::nullopt;
  }
  // The anchor pins the source raster's visible top-left.  When the text is centered or
  // right-justified the justification point -- not the left edge -- must stay fixed (Photoshop
  // keeps each line aligned around the type anchor), so shift by the alignment fraction of the
  // width difference between the source raster and this render (font substitution or edits
  // change the width).  The source visible size is captured alongside the anchor on edit start.
  auto anchor_x = anchor->x();
  if (const auto factor = text_editor_anchor_alignment_factor(editor); factor > 0.0) {
    if (const auto source_size = text_editor_source_visible_size(editor); source_size.has_value()) {
      anchor_x += factor * (source_size->width() - static_cast<double>(visible_bounds->width));
    }
  }
  return LayerAffineTransform{1.0,
                              0.0,
                              0.0,
                              1.0,
                              anchor_x - static_cast<double>(visible_bounds->x),
                              anchor->y() - static_cast<double>(visible_bounds->y)};
}

bool update_text_editor_transform_from_source_anchor(QTextEdit& editor, const PixelBuffer& pixels) {
  const auto transform = anchored_text_transform_for_pixels(editor, pixels);
  if (!transform.has_value()) {
    return false;
  }
  set_text_editor_transform_override(editor, *transform);
  clear_text_editor_visible_local_rect(editor);
  return true;
}

std::optional<PixelBuffer> render_text_editor_pixels_for_source_anchor(const QTextEdit& editor, double zoom,
                                                                       QColor fallback_color) {
  // Untrimmed: run offsets index the full document text (see calibrated_box_text_metric_scale_for_editor).
  const auto text = editor.toPlainText();
  if (text.trimmed().isEmpty()) {
    return std::nullopt;
  }
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto boxed_text = text_flow_is_box(editor.property("patchy.documentTextFlow").toString());
  if (boxed_text || editor.property("patchy.usesPsdTextFrame").toBool()) {
    return std::nullopt;
  }
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto text_color = stored_color.isValid() ? stored_color : (fallback_color.isValid() ? fallback_color
                                                                                           : QColor(Qt::black));
  const auto text_anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor.property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(editor),
                                                  display_text_family_from_font(editor.font()));
  }
  auto document_text = document_from_editor_in_document_units(editor, zoom);
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  auto pixels = render_text_pixels(settings, text_color, text_width, paragraph_runs, rich_text_runs);
  if (pixels.empty()) {
    return std::nullopt;
  }
  return pixels;
}

bool update_text_editor_transform_from_source_anchor(QTextEdit& editor, double zoom, QColor fallback_color) {
  const auto pixels = render_text_editor_pixels_for_source_anchor(editor, zoom, fallback_color);
  if (!pixels.has_value()) {
    return false;
  }
  return update_text_editor_transform_from_source_anchor(editor, *pixels);
}

std::optional<QTransform> text_transform_for_editor_or_layer(const QTextEdit& editor, const Layer& layer) {
  if (const auto override_transform = text_editor_transform_override(editor); override_transform.has_value()) {
    return qtransform_from_affine(*override_transform);
  }
  return patchy_text_transform_for_layer(layer);
}

LayerAffineTransform affine_from_qtransform(const QTransform& transform) {
  return LayerAffineTransform{transform.m11(), transform.m12(), transform.m21(),
                              transform.m22(), transform.dx(),  transform.dy()};
}

TransformedTextPixels apply_text_transform_to_pixels(const PixelBuffer& pixels, const QTransform& transform) {
  const auto source = image_from_pixels(pixels).convertToFormat(QImage::Format_RGBA8888);
  const auto mapped = transform.mapRect(QRectF(0.0, 0.0, source.width(), source.height()));
  const auto left = static_cast<int>(std::floor(mapped.left()));
  const auto top = static_cast<int>(std::floor(mapped.top()));
  const auto right = static_cast<int>(std::ceil(mapped.right()));
  const auto bottom = static_cast<int>(std::ceil(mapped.bottom()));
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top), QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);

  QPainter painter(&transformed);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.translate(-left, -top);
  painter.setTransform(transform, true);
  painter.drawImage(QPointF(0.0, 0.0), source);
  painter.end();

  return TransformedTextPixels{pixels_from_image_rgba(transformed),
                               Rect{left, top, transformed.width(), transformed.height()}};
}

// Re-rasterize a text editor session's glyphs *through* the layer transform -- vector glyphs
// rasterized at the final scale -- so scaled/rotated point text stays crisp instead of resampling
// the base-size bitmap.  Shared by commit_text_editor and the live edit preview so the user sees
// the same pixels while typing that the commit will produce.  Returns std::nullopt when the
// bitmap-resample path must be kept: box text, layouts that depend on a local-rect offset, a
// transform with no scale/rotation, or a PSD-anchored layer whose font is missing (re-rasterizing
// would substitute a face) or whose glyph-top-aligned override is absent (rendering through the
// raw PSD baseline transform would drop the text ~one ascent).
std::optional<TransformedTextPixels> render_crisp_transformed_text_for_editor(
    const QTextEdit& editor, bool psd_anchored_text, bool boxed_text, bool has_local_offset,
    const TextToolSettings& settings, QColor text_color, int text_width, const QString& paragraph_runs,
    const QString& rich_text_runs, const QTransform& text_transform, const QTransform& transform_for_pixels) {
  if (boxed_text || has_local_offset || !qtransform_has_non_translation_linear_part(text_transform)) {
    return std::nullopt;
  }
  if (psd_anchored_text) {
    const bool font_installed =
        !settings.family.trimmed().isEmpty() &&
        settings.family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) != 0 &&
        missing_text_families_for_psd_raster_preview(settings.family, rich_text_runs).isEmpty();
    if (!font_installed || !text_editor_transform_override(editor).has_value()) {
      return std::nullopt;
    }
  }
  auto crisp = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                  rich_text_runs, text_editor_render_local_rect(editor),
                                                  text_editor_metric_scale(editor), transform_for_pixels);
  if (crisp.pixels.empty()) {
    return std::nullopt;
  }
  const Rect bounds{static_cast<std::int32_t>(std::floor(crisp.local_rect.left())),
                    static_cast<std::int32_t>(std::floor(crisp.local_rect.top())), crisp.pixels.width(),
                    crisp.pixels.height()};
  return TransformedTextPixels{std::move(crisp.pixels), bounds};
}

std::vector<double> parse_space_separated_doubles(std::string_view text) {
  std::vector<double> values;
  std::istringstream stream{std::string(text)};
  double value = 0.0;
  while (stream >> value) {
    if (std::isfinite(value)) {
      values.push_back(value);
    }
  }
  return values;
}

std::optional<QRectF> psd_text_metadata_local_rect(const Layer& layer, const char* bounds_key) {
  const auto& metadata = layer.metadata();
  const auto bounds_found = metadata.find(bounds_key);
  if (bounds_found == metadata.end()) {
    return std::nullopt;
  }
  const auto bounds = parse_space_separated_doubles(bounds_found->second);
  if (bounds.size() < 4U) {
    return std::nullopt;
  }
  const auto left = bounds[0];
  const auto top = bounds[1];
  const auto right = bounds[2];
  const auto bottom = bounds[3];
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom) ||
      right <= left || bottom <= top) {
    return std::nullopt;
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

std::optional<QRectF> valid_text_local_rect(QRectF rect) {
  rect = rect.normalized();
  if (!std::isfinite(rect.left()) || !std::isfinite(rect.top()) || !std::isfinite(rect.right()) ||
      !std::isfinite(rect.bottom()) || rect.width() <= 0.0 || rect.height() <= 0.0) {
    return std::nullopt;
  }
  return rect;
}

bool rect_extends_beyond(QRectF outer, QRectF inner) {
  outer = outer.normalized();
  inner = inner.normalized();
  constexpr qreal kEpsilon = 0.5;
  return outer.left() < inner.left() - kEpsilon || outer.top() < inner.top() - kEpsilon ||
         outer.right() > inner.right() + kEpsilon || outer.bottom() > inner.bottom() + kEpsilon;
}

std::optional<QRectF> psd_box_text_editor_render_local_rect(const Layer& layer) {
  const auto box = psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoxBounds);
  if (!box.has_value()) {
    return std::nullopt;
  }

  QRectF editor_local(QPointF(0.0, 0.0), box->size());
  if (const auto visual = psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoundingBox); visual.has_value()) {
    editor_local = editor_local.united(visual->translated(-box->topLeft()));
  }
  return valid_text_local_rect(editor_local);
}

std::optional<QRectF> layer_document_rect_text_local_rect(const Layer& layer, QRectF document_rect) {
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value()) {
    return std::nullopt;
  }
  document_rect = document_rect.normalized();
  if (!std::isfinite(document_rect.left()) || !std::isfinite(document_rect.top()) ||
      !std::isfinite(document_rect.right()) || !std::isfinite(document_rect.bottom()) ||
      document_rect.width() <= 0.0 || document_rect.height() <= 0.0) {
    return std::nullopt;
  }
  const auto determinant = (*transform)[0] * (*transform)[3] - (*transform)[1] * (*transform)[2];
  if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001) {
    return std::nullopt;
  }
  const auto map_doc_to_local = [transform, determinant](double x, double y) {
    const auto dx = x - (*transform)[4];
    const auto dy = y - (*transform)[5];
    return QPointF(((*transform)[3] * dx - (*transform)[2] * dy) / determinant,
                   (-(*transform)[1] * dx + (*transform)[0] * dy) / determinant);
  };

  const auto left = document_rect.left();
  const auto top = document_rect.top();
  const auto right = document_rect.right();
  const auto bottom = document_rect.bottom();
  const std::array<QPointF, 4> points = {
      map_doc_to_local(left, top),
      map_doc_to_local(right, top),
      map_doc_to_local(right, bottom),
      map_doc_to_local(left, bottom),
  };
  auto min_x = points.front().x();
  auto max_x = points.front().x();
  auto min_y = points.front().y();
  auto max_y = points.front().y();
  for (const auto& point : points) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
      return std::nullopt;
    }
    min_x = std::min(min_x, point.x());
    max_x = std::max(max_x, point.x());
    min_y = std::min(min_y, point.y());
    max_y = std::max(max_y, point.y());
  }
  return valid_text_local_rect(QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)));
}

std::optional<QRectF> layer_visible_alpha_text_local_rect(const Layer& layer) {
  const auto visible = visible_alpha_local_bounds(layer.pixels());
  if (!visible.has_value()) {
    return std::nullopt;
  }
  return layer_document_rect_text_local_rect(
      layer, QRectF(static_cast<qreal>(layer.bounds().x + visible->x),
                    static_cast<qreal>(layer.bounds().y + visible->y),
                    static_cast<qreal>(visible->width),
                    static_cast<qreal>(visible->height)));
}

std::optional<QRectF> patchy_box_text_editor_render_local_rect(const Layer& layer, int box_width, int box_height) {
  const auto raster_status = layer.metadata().find(kLayerMetadataTextRasterStatus);
  if (raster_status == layer.metadata().end() || raster_status->second != "patchy_raster") {
    return std::nullopt;
  }
  const auto local_rect = layer_visible_alpha_text_local_rect(layer);
  if (!local_rect.has_value()) {
    return std::nullopt;
  }
  const QRectF frame_rect(0.0, 0.0, static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, box_width)),
                          static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, box_height)));
  if (!rect_extends_beyond(*local_rect, frame_rect)) {
    return std::nullopt;
  }
  return valid_text_local_rect(frame_rect.united(*local_rect));
}

std::optional<QRectF> transformed_psd_text_metadata_rect(const Layer& layer, const char* bounds_key) {
  const auto& metadata = layer.metadata();
  const auto transform_found = metadata.find(kLayerMetadataPsdTextTransform);
  const auto bounds_found = metadata.find(bounds_key);
  if (transform_found == metadata.end() || bounds_found == metadata.end()) {
    return std::nullopt;
  }
  const auto transform = parse_space_separated_doubles(transform_found->second);
  const auto bounds = parse_space_separated_doubles(bounds_found->second);
  if (transform.size() < 6U || bounds.size() < 4U) {
    return std::nullopt;
  }

  const auto map_point = [&transform](double x, double y) {
    return QPointF(transform[0] * x + transform[2] * y + transform[4],
                   transform[1] * x + transform[3] * y + transform[5]);
  };
  const std::array<QPointF, 4> points = {
      map_point(bounds[0], bounds[1]),
      map_point(bounds[2], bounds[1]),
      map_point(bounds[2], bounds[3]),
      map_point(bounds[0], bounds[3]),
  };
  auto min_x = points.front().x();
  auto max_x = points.front().x();
  auto min_y = points.front().y();
  auto max_y = points.front().y();
  for (const auto& point : points) {
    min_x = std::min(min_x, point.x());
    max_x = std::max(max_x, point.x());
    min_y = std::min(min_y, point.y());
    max_y = std::max(max_y, point.y());
  }
  if (max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
}

std::optional<QRectF> psd_text_frame_rect(const Layer& layer) {
  return transformed_psd_text_metadata_rect(layer, kLayerMetadataPsdTextBoxBounds);
}

std::optional<QRectF> psd_point_text_visual_rect(const Layer& layer) {
  return transformed_psd_text_metadata_rect(layer, kLayerMetadataPsdTextBoundingBox);
}

std::optional<QRectF> psd_point_text_local_visual_rect(const Layer& layer) {
  return psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoundingBox);
}

QPointF rect_corner(QRectF rect, int corner_index) {
  switch (corner_index) {
    case 0:
      return rect.topLeft();
    case 1:
      return rect.topRight();
    case 2:
      return rect.bottomRight();
    case 3:
      return rect.bottomLeft();
    default:
      break;
  }
  return rect.topLeft();
}

int visual_top_left_corner_index(QRectF local_rect, const QTransform& transform) {
  int best_index = 0;
  auto best_point = transform.map(rect_corner(local_rect, best_index));
  constexpr double kEpsilon = 0.000001;
  for (int index = 1; index < 4; ++index) {
    const auto point = transform.map(rect_corner(local_rect, index));
    if (point.y() < best_point.y() - kEpsilon ||
        (std::abs(point.y() - best_point.y()) <= kEpsilon && point.x() < best_point.x())) {
      best_index = index;
      best_point = point;
    }
  }
  return best_index;
}

LayerAffineTransform affine_with_local_translation(const LayerAffineTransform& transform, QPointF delta) {
  return LayerAffineTransform{
      transform[0],
      transform[1],
      transform[2],
      transform[3],
      transform[0] * delta.x() + transform[2] * delta.y() + transform[4],
      transform[1] * delta.x() + transform[3] * delta.y() + transform[5],
  };
}

std::optional<LayerAffineTransform> psd_point_text_local_bounds_transform_for_pixels(const Layer& layer,
                                                                                    const PixelBuffer& pixels,
                                                                                    bool boxed_text,
                                                                                    double alignment_factor) {
  if (boxed_text || !layer.metadata().contains(kLayerMetadataPsdTextTransform) ||
      !layer.metadata().contains(kLayerMetadataPsdTextBoundingBox)) {
    return std::nullopt;
  }
  // A free transform (no text-content change) composes a new patchy text transform that diverges
  // from the PSD source, so layer_patchy_text_transform_overrides_psd_source() becomes true.  We
  // still want to align the editor to the live glyphs here: the PSD glyph metrics (psd_local_rect)
  // remain valid because committing an actual text edit clears the PSD source entirely (see
  // store_patchy_text_metadata -> clear_layer_psd_text_source), so reaching this point with the PSD
  // transform still present means only the position/scale changed.  Anchoring to the PSD baseline
  // instead would make a scaled/rotated PSD point-text layer jump down ~one ascent on edit.
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value() || !affine_transform_has_non_translation_linear_part(*transform)) {
    return std::nullopt;
  }
  const auto psd_local_rect = psd_point_text_local_visual_rect(layer);
  const auto visible_bounds = visible_alpha_local_bounds(pixels);
  if (!psd_local_rect.has_value() || !visible_bounds.has_value()) {
    return std::nullopt;
  }

  const QRectF visible_rect(visible_bounds->x, visible_bounds->y, visible_bounds->width, visible_bounds->height);
  const auto transform_qt = qtransform_from_affine(*transform);
  const auto anchor_index = visual_top_left_corner_index(*psd_local_rect, transform_qt);
  auto delta = rect_corner(*psd_local_rect, anchor_index) - rect_corner(visible_rect, anchor_index);
  if (alignment_factor > 0.0) {
    // Centered/right-justified text pins its justification point rather than the left edge.  Both
    // rects live in the same local text space, so interpolating between their left and right edges
    // by the alignment fraction stays correct under the layer's linear transform (including flips);
    // the vertical component keeps the visual-top-corner anchoring chosen above.
    delta.setX((psd_local_rect->left() + alignment_factor * psd_local_rect->width()) -
               (visible_rect.left() + alignment_factor * visible_rect.width()));
  }
  if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())) {
    return std::nullopt;
  }
  return affine_with_local_translation(*transform, delta);
}

// Fallback for PSDs whose TySh descriptor predates the bounds/boundingBox fields (Photoshop
// CS-era files import them as a degenerate "0 0 0 0"): without the local glyph rect, align in
// document space instead.  Pin the bounding box of the re-rendered glyphs -- mapped through the
// layer transform -- to the imported raster's visible bounds: the justification fraction
// horizontally and the visible top vertically (matching the local-bounds variant's anchoring).
// A local translation shifts the mapped bounding box rigidly, so the pin is exact for any
// invertible affine.
std::optional<LayerAffineTransform> psd_point_text_document_bounds_transform_for_pixels(
    const Layer& layer, const PixelBuffer& pixels, bool boxed_text, double alignment_factor) {
  if (boxed_text || !layer.metadata().contains(kLayerMetadataPsdTextTransform)) {
    return std::nullopt;
  }
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value() || !affine_transform_has_non_translation_linear_part(*transform)) {
    return std::nullopt;
  }
  const auto source_visible = visible_alpha_local_bounds(layer.pixels());
  const auto render_visible = visible_alpha_local_bounds(pixels);
  if (!source_visible.has_value() || !render_visible.has_value()) {
    return std::nullopt;
  }
  const QRectF source_doc(static_cast<double>(layer.bounds().x + source_visible->x),
                          static_cast<double>(layer.bounds().y + source_visible->y),
                          static_cast<double>(source_visible->width),
                          static_cast<double>(source_visible->height));
  const QRectF render_local(render_visible->x, render_visible->y, render_visible->width,
                            render_visible->height);
  const auto mapped = qtransform_from_affine(*transform).mapRect(render_local);
  if (!(mapped.width() > 0.0) || !(mapped.height() > 0.0)) {
    return std::nullopt;
  }
  const QPointF document_delta(
      source_doc.left() + alignment_factor * (source_doc.width() - mapped.width()) - mapped.left(),
      source_doc.top() - mapped.top());
  // affine_with_local_translation applies the transform's linear part to the delta, so convert
  // the document-space displacement back into local text space.
  bool invertible = false;
  const auto linear_inverse =
      QTransform((*transform)[0], (*transform)[1], (*transform)[2], (*transform)[3], 0.0, 0.0)
          .inverted(&invertible);
  if (!invertible) {
    return std::nullopt;
  }
  const auto local_delta = linear_inverse.map(document_delta);
  if (!std::isfinite(local_delta.x()) || !std::isfinite(local_delta.y())) {
    return std::nullopt;
  }
  return affine_with_local_translation(*transform, local_delta);
}

bool update_text_editor_transform_from_psd_local_bounds(QTextEdit& editor, const Layer& layer,
                                                        const PixelBuffer& pixels, bool boxed_text) {
  const auto alignment_factor = text_editor_anchor_alignment_factor(editor);
  auto transform =
      psd_point_text_local_bounds_transform_for_pixels(layer, pixels, boxed_text, alignment_factor);
  if (!transform.has_value()) {
    transform =
        psd_point_text_document_bounds_transform_for_pixels(layer, pixels, boxed_text, alignment_factor);
  }
  if (!transform.has_value()) {
    return false;
  }
  set_text_editor_transform_override(editor, *transform);
  if (const auto visible_bounds = visible_alpha_local_bounds(pixels); visible_bounds.has_value()) {
    set_text_editor_visible_local_rect(editor, *visible_bounds);
  }
  return true;
}

std::optional<QPointF> layer_source_visible_anchor(const Layer& layer) {
  if (const auto visible_bounds = visible_alpha_local_bounds(layer.pixels()); visible_bounds.has_value()) {
    return QPointF(static_cast<double>(layer.bounds().x + visible_bounds->x),
                   static_cast<double>(layer.bounds().y + visible_bounds->y));
  }
  return std::nullopt;
}

std::optional<QRectF> psd_point_text_source_visible_rect(const Layer& layer) {
  if (const auto visible_bounds = visible_alpha_local_bounds(layer.pixels()); visible_bounds.has_value()) {
    return QRectF(static_cast<double>(layer.bounds().x + visible_bounds->x),
                  static_cast<double>(layer.bounds().y + visible_bounds->y),
                  static_cast<double>(visible_bounds->width), static_cast<double>(visible_bounds->height));
  }
  if (const auto visual_rect = psd_point_text_visual_rect(layer); visual_rect.has_value()) {
    return visual_rect;
  }
  return std::nullopt;
}

struct LayerTextRenderInputs {
  TextToolSettings settings;
  QColor color;
  std::int32_t max_width{320};
  QString paragraph_runs;
  QString rich_text_runs;
};

std::optional<LayerTextRenderInputs> text_render_inputs_from_layer(const Layer& layer) {
  const auto& metadata = layer.metadata();
  // Untrimmed: the stored run offsets index the full text (leading blank lines included).
  const auto text = [&metadata] {
    const auto found = metadata.find(kLayerMetadataText);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  }();
  if (text.trimmed().isEmpty()) {
    return std::nullopt;
  }

  const auto value = [&metadata](const char* key) -> std::optional<QString> {
    const auto found = metadata.find(key);
    if (found == metadata.end()) {
      return std::nullopt;
    }
    return QString::fromStdString(found->second);
  };

  const auto html = value(kLayerMetadataTextHtml).value_or(QString());
  const auto paragraph_runs = value(kLayerMetadataTextParagraphRuns).value_or(QString());
  const auto rich_text_runs = value(kLayerMetadataTextRuns).value_or(QString());
  auto family = value(kLayerMetadataTextFont).value_or(QApplication::font().family());
  if (family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    family = QApplication::font().family();
  }
  const auto size = value(kLayerMetadataTextSize).has_value()
                        ? std::max(1, std::atoi(value(kLayerMetadataTextSize)->toUtf8().constData()))
                        : 36;
  const auto anti_alias =
      value(kLayerMetadataTextAntiAlias).has_value()
          ? std::clamp(std::atoi(value(kLayerMetadataTextAntiAlias)->toUtf8().constData()), 0, 16)
          : kDefaultTextAntiAlias;
  const auto color_value = value(kLayerMetadataTextColor).value_or(QStringLiteral("#000000"));
  const QColor color(color_value);
  const auto boxed = text_flow_is_box(value(kLayerMetadataTextFlow).value_or(QString::fromLatin1(kTextFlowPoint)));
  const auto box_width =
      value(kLayerMetadataTextBoxWidth).has_value()
          ? std::max(kMinimumTextBoxDocumentSize, std::atoi(value(kLayerMetadataTextBoxWidth)->toUtf8().constData()))
          : std::max(kMinimumTextBoxDocumentSize, layer.bounds().width);
  const auto box_height =
      value(kLayerMetadataTextBoxHeight).has_value()
          ? std::max(kMinimumTextBoxDocumentSize, std::atoi(value(kLayerMetadataTextBoxHeight)->toUtf8().constData()))
          : std::max(kMinimumTextBoxDocumentSize, layer.bounds().height);
  TextToolSettings settings{text, html, family, size,
                            value(kLayerMetadataTextBold).value_or(QString()) == QStringLiteral("true"),
                            value(kLayerMetadataTextItalic).value_or(QString()) == QStringLiteral("true"),
                            anti_alias,
                            boxed,
                            box_width,
                            box_height};
  return LayerTextRenderInputs{std::move(settings), color.isValid() ? color : QColor(Qt::black),
                               layer.bounds().width > 0 ? layer.bounds().width : 320, paragraph_runs,
                               rich_text_runs};
}

std::optional<PixelBuffer> render_text_layer_pixels_from_metadata(const Layer& layer) {
  const auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value()) {
    return std::nullopt;
  }
  return render_text_pixels(inputs->settings, inputs->color, inputs->max_width, inputs->paragraph_runs,
                            inputs->rich_text_runs);
}

// Re-render a point-text layer's glyphs *through* the layer transform so a scaled/rotated layer stays
// crisp (vector glyphs rasterized at the final scale) instead of resampling an already-baked bitmap.
// Returns std::nullopt for box text, which has its own transform-aware layout path.
std::optional<TransformedTextPixels> render_text_layer_pixels_through_transform(const Layer& layer,
                                                                               const QTransform& transform) {
  auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value() || inputs->settings.boxed) {
    return std::nullopt;
  }
  const auto rendered = render_text_pixels_with_local_rect(inputs->settings, inputs->color, inputs->max_width,
                                                           inputs->paragraph_runs, inputs->rich_text_runs,
                                                           std::nullopt, 1.0, transform);
  if (rendered.pixels.empty()) {
    return std::nullopt;
  }
  const auto origin_x = static_cast<std::int32_t>(std::floor(rendered.local_rect.left()));
  const auto origin_y = static_cast<std::int32_t>(std::floor(rendered.local_rect.top()));
  // Trim transparent margins (e.g. the bounding box of rotated glyphs, or any residual padding) so the
  // layer bounds -- and therefore the free-transform handles -- hug the inked glyphs.
  if (const auto visible = visible_alpha_local_bounds(rendered.pixels);
      visible.has_value() && (visible->x > 0 || visible->y > 0 || visible->width < rendered.pixels.width() ||
                              visible->height < rendered.pixels.height())) {
    const auto cropped = image_from_pixels(rendered.pixels)
                             .convertToFormat(QImage::Format_RGBA8888)
                             .copy(visible->x, visible->y, visible->width, visible->height);
    return TransformedTextPixels{pixels_from_image_rgba(cropped),
                                 Rect{origin_x + visible->x, origin_y + visible->y, visible->width, visible->height}};
  }
  return TransformedTextPixels{rendered.pixels,
                               Rect{origin_x, origin_y, rendered.pixels.width(), rendered.pixels.height()}};
}

void clear_layer_text_metadata(Layer& layer) {
  static constexpr std::array<const char*, 22> kTextMetadataKeys = {
      kLayerMetadataText,
      kLayerMetadataTextHtml,
      kLayerMetadataTextRuns,
      kLayerMetadataTextParagraphRuns,
      kLayerMetadataTextFlow,
      kLayerMetadataTextBoxWidth,
      kLayerMetadataTextBoxHeight,
      kLayerMetadataTextFont,
      kLayerMetadataTextSize,
      kLayerMetadataTextColor,
      kLayerMetadataTextBold,
      kLayerMetadataTextItalic,
      kLayerMetadataTextAntiAlias,
      kLayerMetadataTextRasterStatus,
      kLayerMetadataTextTransform,
      kLayerMetadataPsdTextTransform,
      kLayerMetadataPsdTextBounds,
      kLayerMetadataPsdTextBoundingBox,
      kLayerMetadataPsdTextBoxBounds,
      kLayerMetadataPsdTextTailBounds,
      kLayerMetadataPsdTextIndex,
      kLayerMetadataTextLineAwareBoxPreview,
  };
  for (const auto* key : kTextMetadataKeys) {
    layer.metadata().erase(key);
  }
  layer.metadata().erase(kLayerMetadataTextSourceBlock);
  auto& blocks = layer.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "TySh" || block.key == "tySh";
  });
}

void clear_layer_psd_text_source(Layer& layer) {
  layer.metadata().erase(kLayerMetadataTextSourceBlock);
  layer.metadata().erase(kLayerMetadataPsdTextTransform);
  layer.metadata().erase(kLayerMetadataPsdTextBounds);
  layer.metadata().erase(kLayerMetadataPsdTextBoundingBox);
  layer.metadata().erase(kLayerMetadataPsdTextBoxBounds);
  layer.metadata().erase(kLayerMetadataPsdTextTailBounds);
  layer.metadata().erase(kLayerMetadataPsdTextIndex);
}

void clear_layer_psd_style_source(Layer& layer) {
  auto& blocks = layer.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "lfx2" || block.key == "lrFX" || block.key == "plFX";
  });
}

std::vector<Layer>* layer_siblings_containing(std::vector<Layer>& layers, LayerId id, std::size_t& index) {
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].id() == id) {
      index = i;
      return &layers;
    }
    if (auto* found = layer_siblings_containing(layers[i].children(), id, index); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

// Flattens the layer tree into compositor paint order (depth-first pre-order). doc.layers()[0] is the
// bottom of the stack, so an id's position in this list is its global bottom-to-top stacking order --
// used to order an arbitrary multi-selection (across folders) and pick the bottom-most merge target.
void collect_layer_render_order(const std::vector<Layer>& layers, std::vector<LayerId>& out) {
  for (const auto& layer : layers) {
    out.push_back(layer.id());
    collect_layer_render_order(layer.children(), out);
  }
}

bool layer_is_effectively_visible(const std::vector<Layer>& layers, LayerId id, bool ancestor_visible = true) {
  for (const auto& layer : layers) {
    const auto visible = ancestor_visible && layer.visible();
    if (layer.id() == id) {
      return visible;
    }
    if (layer.kind() == LayerKind::Group && layer_is_effectively_visible(layer.children(), id, visible)) {
      return true;
    }
  }
  return false;
}

void insert_layer_after_anchor(Document& document, Layer layer, std::optional<LayerId> anchor_id) {
  if (anchor_id.has_value()) {
    std::size_t index = 0;
    if (auto* siblings = layer_siblings_containing(document.layers(), *anchor_id, index); siblings != nullptr) {
      siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(index + 1U), std::move(layer));
      return;
    }
  }
  document.add_layer(std::move(layer));
}

void store_patchy_text_metadata(Layer& layer, const TextToolSettings& settings, QColor color,
                                const QString& rich_text_runs, const QString& paragraph_runs, int text_width,
                                int text_height) {
  layer.metadata()[kLayerMetadataText] = settings.text.toStdString();
  layer.metadata()[kLayerMetadataTextHtml] = settings.html.toStdString();
  layer.metadata()[kLayerMetadataTextRuns] = rich_text_runs.toStdString();
  layer.metadata()[kLayerMetadataTextParagraphRuns] = paragraph_runs.toStdString();
  layer.metadata()[kLayerMetadataTextFlow] = text_flow_metadata_value(settings.boxed).toStdString();
  layer.metadata()[kLayerMetadataTextBoxWidth] = std::to_string(std::max(1, text_width));
  layer.metadata()[kLayerMetadataTextBoxHeight] = std::to_string(std::max(1, text_height));
  layer.metadata()[kLayerMetadataTextFont] = settings.family.toStdString();
  layer.metadata()[kLayerMetadataTextSize] = std::to_string(settings.size);
  layer.metadata()[kLayerMetadataTextColor] = color.name(QColor::HexRgb).toStdString();
  layer.metadata()[kLayerMetadataTextBold] = settings.bold ? "true" : "false";
  layer.metadata()[kLayerMetadataTextItalic] = settings.italic ? "true" : "false";
  layer.metadata()[kLayerMetadataTextAntiAlias] = std::to_string(std::clamp(settings.anti_alias, 0, 16));
  layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  clear_layer_psd_text_source(layer);
}

// Scale the per-run font sizes (and any explicit leading) in a serialized rich-text-runs string.
// Format (see rich_text_runs_from_document): line 0 is the version tag, each subsequent line is
// "start\tlength\tsize\tbold\titalic\tcolor\tfamily[\tleading]".
QString scale_rich_text_runs(const QString& runs, double scale) {
  if (runs.trimmed().isEmpty() || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return runs;
  }
  auto lines = runs.split(QLatin1Char('\n'));
  for (int i = 1; i < lines.size(); ++i) {
    auto fields = lines[i].split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    bool ok = false;
    if (const int size = fields[2].toInt(&ok); ok) {
      fields[2] = QString::number(std::clamp(static_cast<int>(std::lround(size * scale)), 1, 8192));
    }
    if (fields.size() >= 8) {
      if (const double leading = fields[7].toDouble(&ok); ok && std::isfinite(leading) && leading > 0.0) {
        fields[7] = QString::number(leading * scale, 'g', 17);
      }
    }
    lines[i] = fields.join(QLatin1Char('\t'));
  }
  return lines.join(QLatin1Char('\n'));
}

// Fold the vertical scale of a Patchy point-text layer's transform into its font size -- matching how
// imported PSD text records the *visual* size -- and return the residual matrix (vertical scale
// removed, rotation/aspect/translation kept) to render and store.  Updates the size-bearing metadata
// (base size, per-run sizes, regenerated HTML) so the Type panel and PSD export show the new point
// size.  Returns the input transform unchanged when there is no meaningful scale to fold.
QTransform fold_text_transform_scale_into_font_size(Layer& layer, const QTransform& transform) {
  auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value() || inputs->settings.boxed) {
    return transform;
  }
  const double vertical_scale = std::hypot(transform.m21(), transform.m22());
  if (!std::isfinite(vertical_scale) || vertical_scale <= 0.01) {
    return transform;
  }
  const int old_size = std::max(1, inputs->settings.size);
  const int new_size = std::clamp(static_cast<int>(std::lround(old_size * vertical_scale)), 1, 8192);
  if (new_size == old_size) {
    return transform;
  }
  const double applied_scale = static_cast<double>(new_size) / static_cast<double>(old_size);
  const QTransform residual(transform.m11() / applied_scale, transform.m12() / applied_scale,
                            transform.m21() / applied_scale, transform.m22() / applied_scale, transform.dx(),
                            transform.dy());

  const auto& metadata = layer.metadata();
  const auto string_value = [&metadata](const char* key) -> QString {
    const auto found = metadata.find(key);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  };
  const auto int_value = [&metadata](const char* key, int fallback) -> int {
    const auto found = metadata.find(key);
    return found == metadata.end() ? fallback : std::max(1, std::atoi(found->second.c_str()));
  };
  const auto rich_runs = string_value(kLayerMetadataTextRuns);
  const auto paragraph_runs = string_value(kLayerMetadataTextParagraphRuns);
  const auto box_width = int_value(kLayerMetadataTextBoxWidth, inputs->settings.box_width);
  const auto box_height = int_value(kLayerMetadataTextBoxHeight, inputs->settings.box_height);
  auto settings = inputs->settings;
  settings.size = new_size;
  const auto scaled_runs = scale_rich_text_runs(rich_runs, applied_scale);
  settings.html = document_html_from_text_runs(settings.text, scaled_runs, settings, inputs->color);
  store_patchy_text_metadata(layer, settings, inputs->color, scaled_runs, paragraph_runs, box_width, box_height);
  layer.metadata()[kLayerMetadataTextTransform] =
      serialize_layer_affine_transform(affine_from_qtransform(residual));
  return residual;
}

LayerAffineTransform committed_text_transform(QPoint document_point,
                                              const std::optional<LayerAffineTransform>& active_transform) {
  auto transform = active_transform.value_or(LayerAffineTransform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0});
  transform[4] = static_cast<double>(document_point.x());
  transform[5] = static_cast<double>(document_point.y());
  return transform;
}

bool layer_has_rasterizable_content(const Layer& layer) {
  return layer.kind() == LayerKind::Pixel || layer.kind() == LayerKind::Text || layer_is_text(layer);
}

bool layer_can_rasterize(const Layer& layer) {
  return layer.kind() == LayerKind::Text || layer_is_text(layer);
}

bool layer_can_rasterize_layer_style(const Layer& layer) {
  return layer_has_rasterizable_content(layer) && !layer.layer_style().empty();
}

struct RasterizedLayerPixels {
  PixelBuffer pixels;
  Rect bounds;
};

std::optional<RasterizedLayerPixels> render_rasterized_layer_pixels(const Document& document, const Layer& source,
                                                                    bool include_layer_style) {
  if (!layer_has_rasterizable_content(source)) {
    return std::nullopt;
  }

  auto layer = source;
  layer.set_visible(true);
  layer.set_opacity(1.0F);
  layer.set_blend_mode(BlendMode::Normal);
  layer.clear_mask();
  if (!include_layer_style) {
    layer.layer_style() = {};
  }

  if ((layer.kind() == LayerKind::Text || layer.pixels().empty()) && layer_is_text(layer)) {
    auto text_pixels = render_text_layer_pixels_from_metadata(layer);
    if (!text_pixels.has_value() || text_pixels->empty()) {
      return std::nullopt;
    }
    const auto origin = layer.bounds().empty() ? Rect{} : layer.bounds();
    layer.set_pixels(std::move(*text_pixels));
    layer.set_bounds(Rect{origin.x, origin.y, layer.pixels().width(), layer.pixels().height()});
  }
  if (layer.pixels().empty()) {
    return std::nullopt;
  }

  auto bounds = include_layer_style ? layer_render_bounds(layer) : layer_pixel_bounds(layer);
  bounds = intersect_rect(bounds, Rect::from_size(document.width(), document.height()));
  if (bounds.empty()) {
    return std::nullopt;
  }

  Document raster_document(document.width(), document.height(), document.format());
  raster_document.add_layer(std::move(layer));
  const auto image =
      qimage_from_document_rect(raster_document, QRect(bounds.x, bounds.y, bounds.width, bounds.height), true);
  if (image.isNull()) {
    return std::nullopt;
  }
  return RasterizedLayerPixels{pixels_from_image_rgba(image), bounds};
}

std::optional<Layer> renderable_merge_layer_copy(const Layer& source) {
  if (!layer_has_rasterizable_content(source)) {
    return std::nullopt;
  }

  auto layer = source;
  if ((layer.kind() == LayerKind::Text || layer.pixels().empty()) && layer_is_text(layer)) {
    auto text_pixels = render_text_layer_pixels_from_metadata(layer);
    if (!text_pixels.has_value() || text_pixels->empty()) {
      return std::nullopt;
    }
    const auto origin = layer.bounds().empty() ? Rect{} : layer.bounds();
    layer.set_pixels(std::move(*text_pixels));
    layer.set_bounds(Rect{origin.x, origin.y, layer.pixels().width(), layer.pixels().height()});
  }
  if (layer.kind() != LayerKind::Pixel || layer.pixels().empty()) {
    return std::nullopt;
  }
  return layer;
}

QString rasterized_text_layer_name(const Layer& layer) {
  auto name = QString::fromStdString(layer.name()).trimmed();
  const QStringList prefixes = {
      QStringLiteral("Text: "),
      QCoreApplication::translate(kMainWindowTranslationContext, "Text: %1").arg(QString()),
  };
  for (const auto& prefix : prefixes) {
    if (prefix.isEmpty() || !name.startsWith(prefix, Qt::CaseInsensitive)) {
      continue;
    }
    const auto stripped = name.mid(prefix.size()).trimmed();
    if (!stripped.isEmpty()) {
      return stripped;
    }
  }
  return name;
}

QString text_layer_auto_name(const QString& text) {
  auto name = text.simplified();
  if (name.size() > 24) {
    name = name.left(24) + QStringLiteral("...");
  }
  return name;
}

// True when the layer's current name was derived from its text content -- bare, or with the
// legacy "Text: " prefix older Patchy builds added -- rather than chosen by the user (or carried
// in from the source PSD).  Auto-derived names follow the text on commit; chosen names stay.
bool text_layer_name_is_auto(const Layer& layer) {
  const auto current = QString::fromStdString(layer.name()).trimmed();
  if (current.isEmpty()) {
    return true;
  }
  const auto found = layer.metadata().find(kLayerMetadataText);
  const auto auto_name = text_layer_auto_name(
      found == layer.metadata().end() ? QString() : QString::fromStdString(found->second));
  if (auto_name.isEmpty()) {
    return false;
  }
  if (current == auto_name) {
    return true;
  }
  const QStringList prefixes = {
      QStringLiteral("Text: "),
      QCoreApplication::translate(kMainWindowTranslationContext, "Text: %1").arg(QString()),
  };
  for (const auto& prefix : prefixes) {
    if (!prefix.isEmpty() && current.compare(prefix + auto_name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
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
  print_page_layout_ = default_print_page_layout();
  setWindowFlag(Qt::FramelessWindowHint, true);

  document_tabs_ = new QTabWidget(this);
  document_tabs_->setObjectName(QStringLiteral("documentTabs"));
  document_tabs_->setDocumentMode(true);
  document_tabs_->setTabsClosable(true);
  document_tabs_->setMovable(true);
  setAcceptDrops(true);
  document_tabs_->setAcceptDrops(true);
  document_tabs_->installEventFilter(this);
  if (auto* tab_bar = document_tabs_->findChild<QTabBar*>(); tab_bar != nullptr) {
    tab_bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tab_bar, &QWidget::customContextMenuRequested, this, &MainWindow::show_document_tab_context_menu);
  }
  setCentralWidget(document_tabs_);
  connect(document_tabs_, &QTabWidget::currentChanged, this, [this](int index) { activate_document_tab(index); });
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_document_tab(index); });
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
          [this] { clear_internal_clipboard_on_external_change(); });
  layer_opacity_apply_timer_ = new QTimer(this);
  layer_opacity_apply_timer_->setSingleShot(true);
  layer_opacity_apply_timer_->setInterval(kLayerOpacityApplyDelayMs);
  connect(layer_opacity_apply_timer_, &QTimer::timeout, this, [this] { apply_pending_layer_opacity(); });
  layer_opacity_idle_timer_ = new QTimer(this);
  layer_opacity_idle_timer_->setSingleShot(true);
  layer_opacity_idle_timer_->setInterval(kLayerOpacityIdleFinishDelayMs);
  connect(layer_opacity_idle_timer_, &QTimer::timeout, this, [this] { finish_pending_layer_opacity_edit(); });
  load_pen_input_settings();
  reset_document(1024, 768, Qt::white, tr("New document"));
  load_tool_settings();
  if (canvas_ != nullptr) {
    if (const auto* preset = find_brush_preset(default_startup_brush_preset_id()); preset != nullptr) {
      apply_brush_preset(*canvas_, *preset);
    }
  }

  create_actions();
  load_view_settings();
  if (canvas_ != nullptr && document().guides().empty() && document().grid_settings().horizontal_cycle_32 == 576 &&
      document().grid_settings().vertical_cycle_32 == 576) {
    document().grid_settings().horizontal_cycle_32 = view_grid_spacing_32_;
    document().grid_settings().vertical_cycle_32 = view_grid_spacing_32_;
  }
  apply_canvas_aid_settings(canvas_);
  configure_window_chrome();
  load_recent_files();
  rebuild_recent_files_menu();
  load_recent_folders();
  rebuild_recent_folders_menu();
  load_bundled_legacy_plugins();
  create_docks();
  hotkey_registry_.apply_to_actions();
  refresh_layer_list();
  refresh_layer_controls();
  update_document_action_state();
  update_file_path_actions();
  update_undo_redo_actions();
  qApp->installEventFilter(this);

  refresh_document_window_title();
  setWindowIcon(patchy_app_icon());
  if (!restore_window_geometry()) {
    resize(1280, 860);
    clamp_window_to_available_screen();
  }
  setStyleSheet(photoshop_style());
  ensure_native_resizable_frame();
  mask_edit_mode_chip_ = new QToolButton(statusBar());
  mask_edit_mode_chip_->setObjectName(QStringLiteral("maskEditModeChip"));
  mask_edit_mode_chip_->setCursor(Qt::PointingHandCursor);
  mask_edit_mode_chip_->setFocusPolicy(Qt::NoFocus);
  bind_widget_text(mask_edit_mode_chip_, "Editing layer mask (click to exit)");
  bind_tooltip(mask_edit_mode_chip_, "Paint tools are editing the layer mask. Click to edit the layer pixels again.");
  connect(mask_edit_mode_chip_, &QToolButton::clicked, this,
          [this] { set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true); });
  statusBar()->addPermanentWidget(mask_edit_mode_chip_);
  mask_edit_mode_chip_->hide();
  statusBar()->showMessage(tr("Ready"));
}

bool MainWindow::handle_layer_action_button_drag_event(QObject* watched, QEvent* event) {
  if (preview_dialog_edit_locked()) {
    if (auto* drop_event = dynamic_cast<QDropEvent*>(event); drop_event != nullptr) {
      drop_event->ignore();
    }
    return event != nullptr && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
                                event->type() == QEvent::Drop || event->type() == QEvent::DragLeave);
  }
  auto* button = qobject_cast<QWidget*>(watched);
  if (button == nullptr || !button->property("layerDropAction").isValid()) {
    return false;
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

void MainWindow::update_right_dock_resize_handle_geometry(QWidget* host) {
  if (host == nullptr) {
    return;
  }
  auto* handle = host->findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"), Qt::FindDirectChildrenOnly);
  if (handle == nullptr) {
    return;
  }
  handle->setGeometry(0, 0, kRightDockResizeHandleWidth, host->height());
  handle->raise();
}

void MainWindow::set_right_dock_stack_width(int width) {
  const auto max_width = std::max(kRightDockMinimumWidth, this->width() - 260);
  const auto target_width = std::clamp(width, kRightDockMinimumWidth, max_width);
  for (const auto& object_name : {QStringLiteral("layersDock"), QStringLiteral("historyDock"),
                                  QStringLiteral("propertiesDock"), QStringLiteral("infoDock"),
                                  QStringLiteral("swatchesDock")}) {
    auto* dock = findChild<QDockWidget*>(object_name);
    if (dock == nullptr) {
      continue;
    }
    dock->setFixedWidth(target_width);
    dock->updateGeometry();
  }
}

bool MainWindow::handle_right_dock_resize_event(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr) {
    return false;
  }

  if (widget->property("patchy.rightDockResizeHost").toBool()) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      update_right_dock_resize_handle_geometry(widget);
    }
    return false;
  }

  if (!widget->property("patchy.rightDockResizeHandle").toBool()) {
    return false;
  }

  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      auto* dock = qobject_cast<QDockWidget*>(widget->parentWidget());
      if (dock == nullptr) {
        return false;
      }
      right_dock_resizing_ = true;
      right_dock_resize_start_global_ = mouse_event->globalPosition().toPoint();
      right_dock_resize_start_width_ = dock->width();
      widget->grabMouse();
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!right_dock_resizing_ || (mouse_event->buttons() & Qt::LeftButton) == 0) {
        return false;
      }
      const auto delta = right_dock_resize_start_global_.x() - mouse_event->globalPosition().toPoint().x();
      set_right_dock_stack_width(right_dock_resize_start_width_ + delta);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!right_dock_resizing_ || mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      const auto delta = right_dock_resize_start_global_.x() - mouse_event->globalPosition().toPoint().x();
      set_right_dock_stack_width(right_dock_resize_start_width_ + delta);
      right_dock_resizing_ = false;
      widget->releaseMouse();
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }

  return false;
}

bool MainWindow::handle_spacebar_canvas_pan_event(QObject* watched, QEvent* event) {
  if (canvas_ == nullptr || event == nullptr) {
    return false;
  }

  // Cancel an in-progress spacebar pan when focus genuinely leaves the app, but
  // not for intra-app window switches. Clicking the canvas while a non-modal
  // child dialog is focused deactivates that dialog and activates the main
  // window; that WindowDeactivate must be ignored, otherwise the pan we armed on
  // the keypress is disarmed the instant the canvas press arrives and the drag
  // never grabs. The application stays Active across intra-app transitions, so
  // gate WindowDeactivate on the app no longer being active.
  if ((spacebar_canvas_pan_down_ || spacebar_canvas_pan_dragging_) &&
      (event->type() == QEvent::ApplicationDeactivate ||
       (event->type() == QEvent::WindowDeactivate &&
        QGuiApplication::applicationState() != Qt::ApplicationActive))) {
    reset_spacebar_canvas_pan();
    return false;
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr) {
    widget = QApplication::focusWidget();
  }

  const auto target_in_window = spacebar_canvas_pan_target_in_window(widget);
  const auto target_is_canvas = spacebar_canvas_pan_target_is_canvas(widget);
  switch (event->type()) {
    case QEvent::KeyPress: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->key() != Qt::Key_Space || !target_in_window || target_is_canvas ||
          spacebar_canvas_pan_blocked_by_text_input(widget)) {
        return false;
      }
      if (!key_event->isAutoRepeat()) {
        spacebar_canvas_pan_down_ = true;
        canvas_->set_spacebar_panning(true);
        update_spacebar_canvas_pan_cursor(spacebar_canvas_pan_dragging_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
      }
      key_event->accept();
      return true;
    }
    case QEvent::KeyRelease: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->key() != Qt::Key_Space || (!spacebar_canvas_pan_down_ && !spacebar_canvas_pan_dragging_)) {
        return false;
      }
      if (!key_event->isAutoRepeat()) {
        spacebar_canvas_pan_down_ = false;
        canvas_->set_spacebar_panning(false);
        if (spacebar_canvas_pan_dragging_) {
          update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
        } else {
          clear_spacebar_canvas_pan_cursor();
        }
      }
      key_event->accept();
      return true;
    }
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_down_ || mouse_event->button() != Qt::LeftButton || !target_in_window) {
        return false;
      }
      if (target_is_canvas) {
        spacebar_canvas_pan_dragging_ = true;
        update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
        return false;
      }
      if (!canvas_->begin_pan_at_global_position(mouse_event->globalPosition().toPoint())) {
        return false;
      }
      spacebar_canvas_pan_dragging_ = true;
      update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_dragging_ || (mouse_event->buttons() & Qt::LeftButton) == 0 || !target_in_window) {
        return false;
      }
      update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
      if (target_is_canvas) {
        return false;
      }
      if (!canvas_->pan_to_global_position(mouse_event->globalPosition().toPoint())) {
        return false;
      }
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_dragging_ || mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      spacebar_canvas_pan_dragging_ = false;
      if (target_is_canvas) {
        if (spacebar_canvas_pan_down_) {
          update_spacebar_canvas_pan_cursor(Qt::OpenHandCursor);
        } else {
          clear_spacebar_canvas_pan_cursor();
        }
        return false;
      }
      static_cast<void>(canvas_->end_pan());
      if (spacebar_canvas_pan_down_) {
        update_spacebar_canvas_pan_cursor(Qt::OpenHandCursor);
      } else {
        clear_spacebar_canvas_pan_cursor();
      }
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }

  return false;
}

bool MainWindow::spacebar_canvas_pan_target_in_window(QWidget* widget) const noexcept {
  if (widget == nullptr) {
    return false;
  }
  if (widget == this || isAncestorOf(widget) || widget->window() == this) {
    return true;
  }
  // Non-modal child dialogs (layer style, adjustment, filter, ...) are separate
  // top-level windows, so the checks above miss them and spacebar panning would
  // not engage until the canvas is clicked. Walk the dialog's owner chain: if it
  // is ultimately parented to this window, treat its focus widget as in-window so
  // holding Space pans the canvas behind the dialog (Photoshop behavior). The
  // text-input guard still keeps Space typing into the dialog's fields.
  for (QWidget* owner = widget->window(); owner != nullptr;) {
    if (owner == this) {
      return true;
    }
    QWidget* parent = owner->parentWidget();
    owner = parent != nullptr ? parent->window() : nullptr;
  }
  return false;
}

bool MainWindow::spacebar_canvas_pan_target_is_canvas(QWidget* widget) const noexcept {
  return canvas_ != nullptr && widget != nullptr && (widget == canvas_ || canvas_->isAncestorOf(widget));
}

bool MainWindow::spacebar_canvas_pan_blocked_by_text_input(QWidget* widget) const noexcept {
  for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
    if (qobject_cast<QLineEdit*>(current) != nullptr || qobject_cast<QTextEdit*>(current) != nullptr ||
        qobject_cast<QPlainTextEdit*>(current) != nullptr || qobject_cast<QAbstractSpinBox*>(current) != nullptr) {
      return true;
    }
    if (const auto* combo = qobject_cast<QComboBox*>(current); combo != nullptr && combo->isEditable()) {
      return true;
    }
    if (current == this) {
      break;
    }
  }
  return false;
}

void MainWindow::update_spacebar_canvas_pan_cursor(Qt::CursorShape cursor) {
  // The spacebar pan and the pen-hover override share the single application
  // override-cursor slot; release the pen override before taking it over.
  set_canvas_pen_cursor_override(false);
  const QCursor next_cursor(cursor);
  if (spacebar_canvas_pan_cursor_active_) {
    QApplication::changeOverrideCursor(next_cursor);
  } else {
    QApplication::setOverrideCursor(next_cursor);
    spacebar_canvas_pan_cursor_active_ = true;
  }
}

void MainWindow::set_canvas_pen_cursor_override(bool active) {
  if (active) {
    // Qt does not reliably deliver enter/leave events for a pen crossing widget
    // borders (QTBUG-65199), so a plain setCursor on the canvas is not applied
    // until a real mouse event arrives. Drive the tool cursor through the
    // application override cursor instead, which updates immediately while the
    // pen hovers over the canvas. Never stack on top of the spacebar override.
    if (canvas_ == nullptr || spacebar_canvas_pan_cursor_active_) {
      return;
    }
    const QCursor cursor = canvas_->cursor();
    if (canvas_pen_cursor_active_) {
      QApplication::changeOverrideCursor(cursor);
    } else {
      QApplication::setOverrideCursor(cursor);
      canvas_pen_cursor_active_ = true;
    }
  } else {
    if (!canvas_pen_cursor_active_) {
      return;
    }
    canvas_pen_cursor_active_ = false;
    QApplication::restoreOverrideCursor();
  }
}

void MainWindow::update_pen_cursor_override(QObject* watched, QEvent* event) {
  if (canvas_ == nullptr) {
    return;
  }
  switch (event->type()) {
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease: {
      // Each tablet event is dispatched both to the native window object (a
      // QWindow) and to the target widget. Ignore the non-widget delivery;
      // otherwise the override would be cleared by the window event and re-set
      // by the widget event, making the cursor flicker between arrow and brush.
      auto* widget = qobject_cast<QWidget*>(watched);
      if (widget == nullptr) {
        break;
      }
      if (spacebar_canvas_pan_target_is_canvas(widget)) {
        set_canvas_pen_cursor_override(true);
      } else {
        set_canvas_pen_cursor_override(false);
      }
      break;
    }
    case QEvent::TabletLeaveProximity:
      set_canvas_pen_cursor_override(false);
      break;
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
      // Only a genuine mouse event hands control back to Qt's per-widget cursor
      // handling. While a pen hovers, Windows also emits companion mouse-move
      // events flagged as synthesized; clearing on those would make the cursor
      // flip-flop between the arrow and the brush, so they are ignored here.
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->source() == Qt::MouseEventNotSynthesized) {
        set_canvas_pen_cursor_override(false);
      }
      break;
    }
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
      set_canvas_pen_cursor_override(false);
      break;
    default:
      break;
  }
}

void MainWindow::update_pen_hover_tooltip(QObject* watched, QEvent* event) {
  // Qt only wakes its tooltip machinery on genuine mouse hover, so a stylus
  // hovering over a toolbar button never shows the tip. While a pen hovers,
  // Windows emits companion mouse-move events flagged as synthesized; we use
  // those to drive a tooltip ourselves, mirroring Qt's hover-delay behavior.
  switch (event->type()) {
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->source() == Qt::MouseEventNotSynthesized) {
        // A real mouse hands tooltips back to Qt; drop any pen-driven tip.
        cancel_pen_hover_tooltip();
        break;
      }
      const QPoint global_pos = mouse_event->globalPosition().toPoint();
      auto* widget = qApp->widgetAt(global_pos);
      if (widget == nullptr || widget->toolTip().isEmpty()) {
        cancel_pen_hover_tooltip();
        break;
      }
      // Restart the delay whenever the pen moves to a different target so the
      // tip only appears once the pen settles, like the mouse behavior.
      if (widget != pen_hover_tooltip_widget_.data()) {
        pen_hover_tooltip_widget_ = widget;
        pen_hover_tooltip_global_pos_ = global_pos;
        if (pen_hover_tooltip_timer_ == nullptr) {
          pen_hover_tooltip_timer_ = new QTimer(this);
          pen_hover_tooltip_timer_->setSingleShot(true);
          connect(pen_hover_tooltip_timer_, &QTimer::timeout, this, &MainWindow::show_pen_hover_tooltip);
        }
        const int delay = std::max(1, style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay));
        pen_hover_tooltip_timer_->start(delay);
      } else {
        pen_hover_tooltip_global_pos_ = global_pos;
      }
      break;
    }
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
    case QEvent::TabletLeaveProximity:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
      cancel_pen_hover_tooltip();
      break;
    default:
      break;
  }
  Q_UNUSED(watched);
}

void MainWindow::show_pen_hover_tooltip() {
  auto* widget = pen_hover_tooltip_widget_.data();
  if (widget == nullptr) {
    return;
  }
  // Only show if the pen is still resting over the same widget.
  if (qApp->widgetAt(pen_hover_tooltip_global_pos_) != widget) {
    return;
  }
  const QString text = widget->toolTip();
  if (text.isEmpty()) {
    return;
  }
  QToolTip::showText(pen_hover_tooltip_global_pos_, text, widget);
}

void MainWindow::cancel_pen_hover_tooltip() {
  if (pen_hover_tooltip_timer_ != nullptr) {
    pen_hover_tooltip_timer_->stop();
  }
  pen_hover_tooltip_widget_.clear();
}

void MainWindow::clear_spacebar_canvas_pan_cursor() {
  if (!spacebar_canvas_pan_cursor_active_) {
    return;
  }
  spacebar_canvas_pan_cursor_active_ = false;
  QApplication::restoreOverrideCursor();
}

void MainWindow::reset_spacebar_canvas_pan() {
  spacebar_canvas_pan_down_ = false;
  spacebar_canvas_pan_dragging_ = false;
  if (canvas_ != nullptr) {
    static_cast<void>(canvas_->end_pan());
    canvas_->set_spacebar_panning(false);
  }
  clear_spacebar_canvas_pan_cursor();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (handle_spacebar_canvas_pan_event(watched, event)) {
    return true;
  }

  update_pen_cursor_override(watched, event);
  update_pen_hover_tooltip(watched, event);

  if (event->type() == QEvent::KeyPress) {
    if (auto* editor = qobject_cast<QTextEdit*>(watched);
        editor != nullptr && editor->objectName() == QStringLiteral("inlineTextEditor")) {
      auto* key_event = static_cast<QKeyEvent*>(event);
      std::optional<LayerId> layer_id;
      if (editor->property("patchy.editingLayerId").isValid()) {
        layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
      }
      if (key_event->key() == Qt::Key_Escape) {
        cancel_text_editor(editor, layer_id);
        key_event->accept();
        return true;
      }
      if ((key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
          (key_event->modifiers() & Qt::ControlModifier) != 0) {
        const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                                    editor->property("patchy.documentTextY").toInt());
        commit_text_editor(editor, document_point, layer_id);
        key_event->accept();
        return true;
      }
    }
  }

  if (auto* recent_menu = qobject_cast<QMenu*>(watched);
      recent_menu != nullptr && recent_menu->property(kRecentFilesMenuProperty).toBool()) {
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::RightButton) {
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::RightButton) {
          show_recent_file_context_menu(recent_menu, mouse_event->pos());
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::ContextMenu: {
        auto* context_event = static_cast<QContextMenuEvent*>(event);
        show_recent_file_context_menu(recent_menu, context_event->pos());
        context_event->accept();
        return true;
      }
      default:
        break;
    }
  }

  if (handle_layer_action_button_drag_event(watched, event)) {
    return true;
  }

  if (handle_right_dock_resize_event(watched, event)) {
    return true;
  }

  if (auto* editor = qobject_cast<QTextEdit*>(watched);
      editor != nullptr && editor->objectName() == QStringLiteral("inlineTextEditor") &&
      event->type() == QEvent::Wheel) {
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    const auto wheel_delta = !wheel_event->pixelDelta().isNull() ? wheel_event->pixelDelta() : wheel_event->angleDelta();
    const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
    if (canvas_ != nullptr && primary_delta != 0 && (wheel_event->modifiers() & Qt::AltModifier) != 0) {
      canvas_->zoom_at_widget_point(canvas_->mapFromGlobal(wheel_event->globalPosition().toPoint()),
                                    primary_delta > 0 ? 1.1 : 0.9);
      refresh_document_info();
    }
    reset_text_editor_scroll(editor);
    wheel_event->accept();
    return true;
  }
  if (auto* viewport = qobject_cast<QWidget*>(watched);
      viewport != nullptr && viewport->parentWidget() != nullptr &&
      viewport->parentWidget()->objectName() == QStringLiteral("inlineTextEditor") &&
      event->type() == QEvent::Wheel) {
    auto* editor = qobject_cast<QTextEdit*>(viewport->parentWidget());
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    const auto wheel_delta = !wheel_event->pixelDelta().isNull() ? wheel_event->pixelDelta() : wheel_event->angleDelta();
    const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
    if (canvas_ != nullptr && primary_delta != 0 && (wheel_event->modifiers() & Qt::AltModifier) != 0) {
      canvas_->zoom_at_widget_point(canvas_->mapFromGlobal(wheel_event->globalPosition().toPoint()),
                                    primary_delta > 0 ? 1.1 : 0.9);
      refresh_document_info();
    }
    reset_text_editor_scroll(editor);
    wheel_event->accept();
    return true;
  }

  if (canvas_ != nullptr) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && (widget == canvas_ || canvas_->isAncestorOf(widget))) {
      auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
      if (editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool() &&
          handle_text_editor_transform_overlay_event(editor, event)) {
        return true;
      }
    }
  }

  if (watched == canvas_ && canvas_ != nullptr) {
    if (swallow_next_canvas_left_press_) {
      if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        swallow_next_canvas_left_press_ = false;
        if (mouse_event->button() == Qt::LeftButton) {
          mouse_event->accept();
          return true;
        }
      } else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) {
        swallow_next_canvas_left_press_ = false;
      }
    }
    auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool()) {
      switch (event->type()) {
        case QEvent::MouseButtonPress: {
          auto* mouse_event = static_cast<QMouseEvent*>(event);
          if (mouse_event->button() == Qt::LeftButton) {
            if (auto* handle = text_editor_resize_handle_at(mouse_event->pos()); handle != nullptr) {
              return handle_text_editor_resize_event(handle, editor, event);
            }
            if (commit_active_text_editor()) {
              mouse_event->accept();
              return true;
            }
          }
          break;
        }
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
          const auto handle_name = editor->property("patchy.activeTextResizeHandleName").toString();
          if (!handle_name.isEmpty()) {
            if (auto* handle = canvas_->findChild<QWidget*>(handle_name, Qt::FindDirectChildrenOnly);
                handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
              return handle_text_editor_resize_event(handle, editor, event);
            }
          }
          break;
        }
        default:
          break;
      }
    }
  }

  if (auto* handle = qobject_cast<QWidget*>(watched);
      handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
    auto* editor = canvas_ == nullptr ? nullptr : canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor == nullptr) {
      return false;
    }
    return handle_text_editor_resize_event(handle, editor, event);
  }

  if (handle_window_resize_event(watched, event)) {
    return true;
  }

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
          // Dragging the title bar of a maximized window must restore it first. Letting the
          // OS system-move a maximized window leaves Qt's isMaximized() stale, which then
          // disables our edge-resize hit-testing until the next explicit state change. Restore
          // under the cursor (like a native title bar) so the state stays in sync.
          if (isMaximized()) {
            restore_maximized_under_cursor(mouse_event->globalPosition().toPoint());
          }
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

bool MainWindow::handle_window_resize_event(QObject* watched, QEvent* event) {
  if (isMaximized() || isFullScreen()) {
    if (!chrome_resizing_) {
      clear_window_resize_cursor();
    }
    return false;
  }

  if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease &&
      event->type() != QEvent::MouseMove) {
    return false;
  }

  auto* mouse_event = static_cast<QMouseEvent*>(event);
  if (auto* widget = qobject_cast<QWidget*>(watched);
      widget_is_or_contains_scroll_bar(widget) ||
      window_resize_hit_targets_scroll_bar(this, mouse_event->globalPosition().toPoint())) {
    if (chrome_resizing_ && event->type() == QEvent::MouseButtonRelease && mouse_event->button() == Qt::LeftButton) {
      chrome_resizing_ = false;
      chrome_resize_edges_ = Qt::Edges{};
      clear_window_resize_cursor();
    }
    return false;
  }

  if (chrome_resizing_) {
    if (event->type() == QEvent::MouseMove && (mouse_event->buttons() & Qt::LeftButton) != 0) {
      resize_window_from_global_point(mouse_event->globalPosition().toPoint());
      mouse_event->accept();
      return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && mouse_event->button() == Qt::LeftButton) {
      resize_window_from_global_point(mouse_event->globalPosition().toPoint());
      chrome_resizing_ = false;
      chrome_resize_edges_ = Qt::Edges{};
      releaseMouse();
      clear_window_resize_cursor();
      mouse_event->accept();
      return true;
    }
    return false;
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr || widget->window() != this) {
    return false;
  }
  if (widget_is_or_contains_scroll_bar(widget)) {
    clear_window_resize_cursor();
    return false;
  }

  const auto edges = resize_edges_for_window_position(size(), mapFromGlobal(mouse_event->globalPosition().toPoint()));
  if (event->type() == QEvent::MouseMove && mouse_event->buttons() == Qt::NoButton) {
    update_window_resize_cursor(edges);
    return false;
  }
  if (event->type() != QEvent::MouseButtonPress || mouse_event->button() != Qt::LeftButton ||
      edges == Qt::Edges{}) {
    return false;
  }

  chrome_resize_edges_ = edges;
  chrome_resize_start_global_ = mouse_event->globalPosition().toPoint();
  chrome_resize_start_geometry_ = geometry();
  chrome_resizing_ = true;
  chrome_dragging_ = false;
  update_window_resize_cursor(edges);
  grabMouse();
  mouse_event->accept();
  return true;
}

void MainWindow::update_window_resize_cursor(Qt::Edges edges) {
  if (edges == Qt::Edges{}) {
    clear_window_resize_cursor();
    return;
  }

  const QCursor cursor(resize_cursor_for_edges(edges));
  if (chrome_resize_cursor_active_) {
    QApplication::changeOverrideCursor(cursor);
  } else {
    QApplication::setOverrideCursor(cursor);
    chrome_resize_cursor_active_ = true;
  }
}

void MainWindow::clear_window_resize_cursor() {
  if (!chrome_resize_cursor_active_) {
    return;
  }
  QApplication::restoreOverrideCursor();
  chrome_resize_cursor_active_ = false;
}

void MainWindow::resize_window_from_global_point(QPoint global_position) {
  QRect next = chrome_resize_start_geometry_;
  const QPoint delta = global_position - chrome_resize_start_global_;
  const QSize minimum = minimumSize().expandedTo(minimumSizeHint());
  const QSize maximum = maximumSize();

  if (chrome_resize_edges_.testFlag(Qt::LeftEdge)) {
    int left = chrome_resize_start_geometry_.left() + delta.x();
    left = std::min(left, chrome_resize_start_geometry_.right() - minimum.width() + 1);
    if (maximum.width() < QWIDGETSIZE_MAX) {
      left = std::max(left, chrome_resize_start_geometry_.right() - maximum.width() + 1);
    }
    next.setLeft(left);
  } else if (chrome_resize_edges_.testFlag(Qt::RightEdge)) {
    int right = chrome_resize_start_geometry_.right() + delta.x();
    right = std::max(right, chrome_resize_start_geometry_.left() + minimum.width() - 1);
    if (maximum.width() < QWIDGETSIZE_MAX) {
      right = std::min(right, chrome_resize_start_geometry_.left() + maximum.width() - 1);
    }
    next.setRight(right);
  }

  if (chrome_resize_edges_.testFlag(Qt::TopEdge)) {
    int top = chrome_resize_start_geometry_.top() + delta.y();
    top = std::min(top, chrome_resize_start_geometry_.bottom() - minimum.height() + 1);
    if (maximum.height() < QWIDGETSIZE_MAX) {
      top = std::max(top, chrome_resize_start_geometry_.bottom() - maximum.height() + 1);
    }
    next.setTop(top);
  } else if (chrome_resize_edges_.testFlag(Qt::BottomEdge)) {
    int bottom = chrome_resize_start_geometry_.bottom() + delta.y();
    bottom = std::max(bottom, chrome_resize_start_geometry_.top() + minimum.height() - 1);
    if (maximum.height() < QWIDGETSIZE_MAX) {
      bottom = std::min(bottom, chrome_resize_start_geometry_.top() + maximum.height() - 1);
    }
    next.setBottom(bottom);
  }

  if (next.isValid() && next != geometry()) {
    setGeometry(next);
  }
}

void MainWindow::set_window_screen_size(QSize physical_size) {
  if (isMaximized() || isFullScreen()) {
    showNormal();
  }
#ifdef Q_OS_WIN
  // Size the window in physical pixels so screen recordings capture exactly the
  // advertised resolution regardless of display scaling. The window is
  // frameless, so the outer window rect already includes the custom title bar
  // and resize borders.
  auto* hwnd = reinterpret_cast<HWND>(winId());
  if (hwnd != nullptr) {
    RECT window_rect{};
    if (GetWindowRect(hwnd, &window_rect) == 0) {
      return;
    }
    int x = window_rect.left;
    int y = window_rect.top;
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != 0) {
      const RECT& screen = monitor_info.rcMonitor;
      x = std::clamp(x, static_cast<int>(screen.left),
                     std::max(static_cast<int>(screen.left), static_cast<int>(screen.right) - physical_size.width()));
      y = std::clamp(y, static_cast<int>(screen.top),
                     std::max(static_cast<int>(screen.top), static_cast<int>(screen.bottom) - physical_size.height()));
    }
    SetWindowPos(hwnd, nullptr, x, y, physical_size.width(), physical_size.height(), SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
#endif
  const qreal ratio = devicePixelRatioF();
  resize(qRound(physical_size.width() / ratio), qRound(physical_size.height() / ratio));
}

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
  if (message != nullptr && result != nullptr) {
    auto* nc_message = static_cast<MSG*>(message);
    if (nc_message->message == WM_NCACTIVATE) {
      // lParam == -1 tells DefWindowProc not to repaint the non-client border to reflect
      // the active-state change, preventing the white inactive-border flash. Return TRUE so
      // the window still activates/deactivates normally (taskbar, focus). This runs in every
      // window state (including maximized/fullscreen), where the border also appears.
      *result = DefWindowProcW(nc_message->hwnd, WM_NCACTIVATE, nc_message->wParam, -1);
      return true;
    }
    if (nc_message->message == WM_NCCALCSIZE && nc_message->wParam != FALSE) {
      // Strip the entire non-client area so the client fills the window (no native frame,
      // hence no white top line). When maximized, Windows expands the window past the work
      // area by the resize-frame thickness on every side; left unadjusted the top frame
      // shows as a white line and the bottom hangs past the work area (the gap above the
      // taskbar). Inset the client rect by that frame so the maximized client is exactly the
      // work area. Normal and fullscreen states keep the full window (no inset).
      if (IsZoomed(nc_message->hwnd) != 0) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(nc_message->lParam);
        const int border_x = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const int border_y = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        params->rgrc[0].left += border_x;
        params->rgrc[0].top += border_y;
        params->rgrc[0].right -= border_x;
        params->rgrc[0].bottom -= border_y;
      }
      *result = 0;
      return true;
    }
  }
  if (message != nullptr && result != nullptr && !isMaximized() && !isFullScreen()) {
    auto* native_message = static_cast<MSG*>(message);
    if (native_message->message == WM_NCHITTEST) {
      RECT window_rect;
      if (native_message->hwnd != nullptr && GetWindowRect(native_message->hwnd, &window_rect) != 0) {
        const auto x = GET_X_LPARAM(native_message->lParam);
        const auto y = GET_Y_LPARAM(native_message->lParam);
        const bool left = x >= window_rect.left && x < window_rect.left + kWindowResizeBorder;
        const bool right = x < window_rect.right && x >= window_rect.right - kWindowResizeBorder;
        const bool top = y >= window_rect.top && y < window_rect.top + kWindowResizeBorder;
        const bool bottom = y < window_rect.bottom && y >= window_rect.bottom - kWindowResizeBorder;

        if ((left || right || top || bottom) &&
            window_resize_hit_targets_scroll_bar(this, QPoint(x, y))) {
          *result = HTCLIENT;
          return true;
        }

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

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslate_ui();
  }
  if (event->type() == QEvent::ActivationChange && isActiveWindow() && canvas_ != nullptr) {
    // Windows resets the cursor to an arrow when the window is re-activated;
    // re-apply the tool cursor so a hovering pen shows the brush immediately
    // instead of waiting for the first move event.
    canvas_->refresh_tool_cursor();
  }
  if (event->type() == QEvent::WindowStateChange) {
    // Maximize/restore/fullscreen each use a different frameless client rect (see
    // nativeEvent's WM_NCCALCSIZE handling). Drop any in-flight edge-resize state left over
    // from the transition and re-sync the frame so edge-resize hit-testing and the dock
    // separators line up with the new geometry.
    chrome_resizing_ = false;
    chrome_resize_edges_ = Qt::Edges{};
    clear_window_resize_cursor();
    resync_native_frame_geometry();
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    event->ignore();
    return;
  }
  // Tear down any active inline text editor before shutdown.  Left alive, its
  // QTextEdit (caret-blink timer, event filters, signal lambdas referencing
  // canvas_ and the document) is destroyed after the session's Document is
  // freed, dereferencing freed memory.  Committing matches every other path
  // that leaves an edit (focus loss, tool change, tab switch) and preserves
  // the typed text so it counts toward the unsaved-changes prompt below.
  finish_active_text_editor();
  for (auto& target_session : sessions_) {
    if (target_session != nullptr && !confirm_close_session(*target_session)) {
      event->ignore();
      return;
    }
  }
  save_window_geometry();
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

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  ensure_native_resizable_frame();
  if (!native_frame_geometry_resynced_) {
    native_frame_geometry_resynced_ = true;
    // Defer to the next event-loop turn so the frameless WM_NCCALCSIZE frame change has fully
    // settled before we re-sync Qt's geometry to the real client rect.
    QTimer::singleShot(0, this, [this] { resync_native_frame_geometry(); });
  }
}

void MainWindow::restore_maximized_under_cursor(QPoint global_cursor) {
  if (!isMaximized()) {
    return;
  }
  const QRect maximized = geometry();
  const QRect restored = normalGeometry().isValid() ? normalGeometry() : QRect(maximized.topLeft(), maximized.size() / 2);
  // Keep the cursor at the same fractional X along the title bar after restoring, and place
  // the (shorter) restored title bar under the cursor vertically.
  const double x_fraction = maximized.width() > 0
                                ? double(global_cursor.x() - maximized.x()) / double(maximized.width())
                                : 0.5;
  showNormal();
  const int new_left = global_cursor.x() - static_cast<int>(x_fraction * restored.width());
  const int new_top = global_cursor.y() - std::min(restored.height() / 2, menuBar()->height() / 2);
  move(new_left, new_top);
}

void MainWindow::resync_native_frame_geometry() {
#ifdef Q_OS_WIN
  // The frameless window removes its native frame via WM_NCCALCSIZE, which enlarges the client area.
  // Qt's cached frame margins can lag behind that change, so the central content is laid out slightly
  // smaller than the window and the never-repainted backing store shows through as a grey band along
  // the bottom and sides until the user resizes. Replicate that resize programmatically: a momentary
  // 1px grow-then-restore forces a layout pass and a full repaint of the client area.
  if (isMaximized() || isFullScreen() || isMinimized() || !isVisible()) {
    update();
    return;
  }
  const QSize current = size();
  if (current.width() > 0 && current.height() > 0) {
    QMainWindow::resize(current.width(), current.height() + 1);
    QMainWindow::resize(current);
  }
  update();
#endif
}

void MainWindow::refresh_native_frame_after_overlay() {
  // Closing an owned top-level overlay (the startup splash) re-activates this window and can leave
  // the frameless client area out of sync, re-introducing the grey edge band. Re-sync on the next
  // event-loop turn, once the overlay window is fully gone.
  QTimer::singleShot(0, this, [this] { resync_native_frame_geometry(); });
}

void MainWindow::position_window_chrome_controls() {
  if (window_chrome_controls_ == nullptr || menuBar() == nullptr) {
    return;
  }
  window_chrome_controls_->move(std::max(0, menuBar()->width() - window_chrome_controls_->width()), 0);
  window_chrome_controls_->raise();
}

void MainWindow::ensure_native_resizable_frame() {
#ifdef Q_OS_WIN
  apply_windows_frameless_resize_style(winId());
  apply_windows_pen_feedback_suppression(winId());
  native_resizable_frame_applied_ = true;
#else
  native_resizable_frame_applied_ = true;
#endif
}

void MainWindow::clamp_window_to_available_screen() {
  // A large interface scale (QT_SCALE_FACTOR) shrinks the logical desktop, so the default window
  // can be larger than the screen or land partly off it. Shrink to fit and nudge it fully on-screen.
  const QScreen* target_screen = screen();
  if (target_screen == nullptr) {
    target_screen = QGuiApplication::primaryScreen();
  }
  if (target_screen == nullptr) {
    return;
  }
  const QRect available = target_screen->availableGeometry();
  if (!available.isValid()) {
    return;
  }

  const int target_width = std::min(width(), available.width());
  const int target_height = std::min(height(), available.height());
  if (target_width != width() || target_height != height()) {
    resize(target_width, target_height);
  }

  QRect frame = frameGeometry();
  frame.setSize(QSize(target_width, target_height));
  if (frame.right() > available.right()) {
    frame.moveRight(available.right());
  }
  if (frame.bottom() > available.bottom()) {
    frame.moveBottom(available.bottom());
  }
  if (frame.left() < available.left()) {
    frame.moveLeft(available.left());
  }
  if (frame.top() < available.top()) {
    frame.moveTop(available.top());
  }
  move(frame.topLeft());
}

void MainWindow::save_window_geometry() const {
  auto settings = app_settings();
  // normalGeometry() reports the restored (non-maximized) bounds, so the window returns to a sensible
  // size and position when the user un-maximizes after relaunch.
  const QRect normal = normalGeometry();
  if (normal.isValid() && normal.width() > 0 && normal.height() > 0) {
    settings.setValue(QStringLiteral("window/normalGeometry"), normal);
  }
  settings.setValue(QStringLiteral("window/maximized"), isMaximized());
}

bool MainWindow::restore_window_geometry() {
  auto settings = app_settings();
  const auto stored = settings.value(QStringLiteral("window/normalGeometry"));
  if (!stored.canConvert<QRect>()) {
    return false;
  }
  const QRect normal = stored.toRect();
  if (!normal.isValid() || normal.width() <= 0 || normal.height() <= 0) {
    return false;
  }
  setGeometry(normal);
  clamp_window_to_available_screen();
  if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
    // Defer to the windowing system on show; the clamped geometry above becomes the restore bounds.
    setWindowState(windowState() | Qt::WindowMaximized);
  }
  return true;
}

void MainWindow::register_retranslation(std::function<void()> callback) {
  if (!callback) {
    return;
  }
  callback();
  retranslation_callbacks_.push_back(std::move(callback));
}

void MainWindow::retranslate_bound_children() {
  apply_bound_translation(this);
  const auto children = findChildren<QObject*>();
  for (auto* child : children) {
    apply_bound_translation(child);
  }
}

void MainWindow::retranslate_blend_combo() {
  if (blend_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(blend_combo_);
  for (int index = 0; index < blend_combo_->count(); ++index) {
    blend_combo_->setItemText(index, blend_mode_name(static_cast<BlendMode>(blend_combo_->itemData(index).toInt())));
  }
}

void MainWindow::retranslate_brush_preset_combo() {
  if (brush_preset_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(brush_preset_combo_);
  for (int index = 0; index < brush_preset_combo_->count(); ++index) {
    if (const auto* preset = find_brush_preset(brush_preset_combo_->itemData(index).toString()); preset != nullptr) {
      brush_preset_combo_->setItemText(index, brush_preset_display_name(*preset));
    }
  }
}

void MainWindow::refresh_language_actions() {
  const auto current = LocalizationManager::instance().current_language();
  if (language_english_action_ != nullptr) {
    language_english_action_->setChecked(current == QStringLiteral("en"));
  }
  if (language_japanese_action_ != nullptr) {
    language_japanese_action_->setChecked(current == QStringLiteral("ja"));
  }
}

void MainWindow::retranslate_ui() {
  retranslate_bound_children();
  if (menuBar() != nullptr) {
    for (auto* action : menuBar()->actions()) {
      apply_bound_translation(action);
    }
  }
  for (const auto& callback : retranslation_callbacks_) {
    callback();
  }
  refresh_language_actions();
  retranslate_blend_combo();
  retranslate_brush_preset_combo();
  if (text_size_spin_ != nullptr) {
    text_size_spin_->setSuffix(tr(" pt"));
  }
  const auto actions = findChildren<QAction*>();
  for (auto* action : actions) {
    refresh_action_tooltip(action);
  }
  rebuild_recent_files_menu();
  rebuild_recent_folders_menu();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  refresh_color_buttons();
  refresh_text_color_button();
  update_undo_redo_actions();
  update_document_action_state();
  if (statusBar() != nullptr) {
    statusBar()->showMessage(tr("Ready"));
  }
}

void MainWindow::configure_window_chrome() {
  auto* bar = menuBar();
  bar->setNativeMenuBar(false);
  bar->setFixedHeight(34);
  bar->installEventFilter(this);

  auto* badge = new QLabel(bar);
  badge->setObjectName(QStringLiteral("patchyBadge"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setAttribute(Qt::WA_TransparentForMouseEvents);
  badge->setFixedSize(18, 18);
  badge->setPixmap(patchy_app_icon().pixmap(18, 18));
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
  bind_tooltip(minimize_button, "Minimize");
  maximize_button_ =
      add_chrome_button(QStringLiteral("windowMaximizeButton"), window_chrome_icon(QStringLiteral("maximize")),
                        tr("Maximize / Restore"));
  bind_tooltip(maximize_button_, "Maximize / Restore");
  auto* close_button =
      add_chrome_button(QStringLiteral("windowCloseButton"), window_chrome_icon(QStringLiteral("close")), tr("Close"));
  bind_tooltip(close_button, "Close");
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
  bind_action_text(file_menu->menuAction(), "&File");
  bind_action_text(edit_menu->menuAction(), "&Edit");
  bind_action_text(image_menu->menuAction(), "&Image");
  bind_action_text(layer_menu->menuAction(), "&Layer");
  bind_action_text(type_menu->menuAction(), "&Type");
  bind_action_text(select_menu->menuAction(), "&Select");
  bind_action_text(filter_menu->menuAction(), "&Filter");
  bind_action_text(plugins_menu->menuAction(), "&Plugins");
  bind_action_text(view_menu->menuAction(), "&View");
  bind_action_text(window_menu->menuAction(), "&Window");
  bind_action_text(help_menu->menuAction(), "&Help");
  filter_menu->setObjectName(QStringLiteral("filterMenu"));

  auto* new_action = file_menu->addAction(tr("&New"));
  auto* open_action = file_menu->addAction(tr("&Open..."));
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent File"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  configure_recent_files_context_menu(recent_files_menu_);
  recent_folders_menu_ = file_menu->addMenu(tr("Open Recent &Folder"));
  recent_folders_menu_->setObjectName(QStringLiteral("fileOpenRecentFolderMenu"));
  configure_recent_files_context_menu(recent_folders_menu_);
  recent_folders_menu_->setProperty(kRecentFoldersMenuProperty, true);
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  auto* export_flat_action = file_menu->addAction(tr("Export &Flat Image..."));
  auto* page_setup_action = file_menu->addAction(tr("Page Set&up..."));
  auto* print_action = file_menu->addAction(tr("&Print..."));
  file_menu->addSeparator();
  auto* close_action = file_menu->addAction(tr("&Close"));
  auto* close_all_action = file_menu->addAction(tr("Close &All"));
  file_menu->addSeparator();
  auto* preferences_action = file_menu->addAction(tr("&Preferences..."));
  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("&Quit"));
  new_action->setObjectName(QStringLiteral("fileNewAction"));
  open_action->setObjectName(QStringLiteral("fileOpenAction"));
  save_action->setObjectName(QStringLiteral("fileSaveAction"));
  save_as_action->setObjectName(QStringLiteral("fileSaveAsAction"));
  export_flat_action->setObjectName(QStringLiteral("fileExportFlatAction"));
  page_setup_action->setObjectName(QStringLiteral("filePageSetupAction"));
  print_action->setObjectName(QStringLiteral("filePrintAction"));
  close_action->setObjectName(QStringLiteral("fileCloseAction"));
  close_all_action->setObjectName(QStringLiteral("fileCloseAllAction"));
  preferences_action->setObjectName(QStringLiteral("filePreferencesAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  page_setup_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  print_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
  close_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  close_all_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  preferences_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  register_hotkey(new_action, "file.new", QKeySequence(Qt::CTRL | Qt::Key_N));
  register_hotkey(open_action, "file.open", QKeySequence(Qt::CTRL | Qt::Key_O));
  register_hotkey(save_action, "file.save", QKeySequence(Qt::CTRL | Qt::Key_S));
  register_hotkey(save_as_action, "file.save_as", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  register_hotkey(print_action, "file.print", QKeySequence(Qt::CTRL | Qt::Key_P));
  register_hotkey(close_action, "file.close", QKeySequence(Qt::CTRL | Qt::Key_W));
  register_hotkey(close_all_action, "file.close_all", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  register_hotkey(quit_action, "file.quit", QKeySequence(Qt::CTRL | Qt::Key_Q));
  register_hotkey(export_flat_action, "file.export_flat");
  register_hotkey(page_setup_action, "file.page_setup");
  register_hotkey(preferences_action, "file.preferences");

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(page_setup_action, &QAction::triggered, this, [this] { page_setup(); });
  connect(print_action, &QAction::triggered, this, [this] { print_document(); });
  connect(close_action, &QAction::triggered, this, [this] { close_document_tab(document_tabs_->currentIndex()); });
  connect(close_all_action, &QAction::triggered, this, [this] { close_all_document_tabs(); });
  connect(preferences_action, &QAction::triggered, this, [this] { show_preferences(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);
  for (auto* action :
       {save_action, save_as_action, export_flat_action, page_setup_action, print_action, close_action, close_all_action}) {
    register_document_action(action);
  }

  undo_action_ = edit_menu->addAction(tr("&Undo"));
  redo_action_ = edit_menu->addAction(tr("&Redo"));
  undo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  redo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  // Ctrl+Alt+Z is Photoshop's "step backward" muscle memory; Ctrl+Y matches Windows redo.
  register_hotkey(undo_action_, "edit.undo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z)});
  register_hotkey(redo_action_, "edit.redo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
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
  paste_action->setIcon(simple_icon(QStringLiteral("paste")));
  transform_action->setIcon(simple_icon(QStringLiteral("TR")));
  register_hotkey(cut_action, "edit.cut", QKeySequence(Qt::CTRL | Qt::Key_X));
  register_hotkey(copy_action, "edit.copy", QKeySequence(Qt::CTRL | Qt::Key_C));
  register_hotkey(copy_merged_action, "edit.copy_merged", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  register_hotkey(paste_action, "edit.paste", QKeySequence(Qt::CTRL | Qt::Key_V));
  register_hotkey(transform_action, "edit.free_transform", QKeySequence(Qt::CTRL | Qt::Key_T));
  connect(cut_action, &QAction::triggered, this, [this] { cut_selection(); });
  connect(copy_action, &QAction::triggered, this, [this] { copy_selection(); });
  connect(copy_merged_action, &QAction::triggered, this, [this] { copy_merged(); });
  connect(paste_action, &QAction::triggered, this, [this] { paste_clipboard(); });
  connect(transform_action, &QAction::triggered, this, [this] { transform_active_layer_dialog(); });
  for (auto* action : {cut_action, copy_action, copy_merged_action, paste_action, transform_action}) {
    register_document_action(action);
  }
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
  register_hotkey(select_all_action, "select.all", QKeySequence(Qt::CTRL | Qt::Key_A));
  register_hotkey(clear_selection_action, "select.deselect", QKeySequence(Qt::CTRL | Qt::Key_D));
  register_hotkey(reselect_action, "select.reselect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  register_hotkey(inverse_selection_action, "select.inverse", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  register_hotkey(grow_selection_action, "select.grow");
  register_hotkey(similar_selection_action, "select.similar");
  register_hotkey(expand_selection_action, "select.expand");
  register_hotkey(contract_selection_action, "select.contract");
  register_hotkey(border_selection_action, "select.border");
  register_hotkey(layer_transparency_action, "select.layer_transparency");
  register_hotkey(stroke_selection_action, "edit.stroke_selection");
  connect(select_all_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Select All"), [this] { canvas_->select_all(); }); });
  connect(clear_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Deselect"), [this] { canvas_->clear_selection(); }); });
  connect(reselect_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Reselect"), [this] { canvas_->reselect(); }); });
  connect(inverse_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Inverse Selection"), [this] { canvas_->invert_selection(); }); });
  connect(grow_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Grow Selection"), [this] { canvas_->grow_selection(); }); });
  connect(similar_selection_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Select Similar"), [this] { canvas_->select_similar_to_selection(); });
  });
  connect(expand_selection_action, &QAction::triggered, this, [this] { expand_selection_dialog(); });
  connect(contract_selection_action, &QAction::triggered, this, [this] { contract_selection_dialog(); });
  connect(border_selection_action, &QAction::triggered, this, [this] { border_selection_dialog(); });
  connect(layer_transparency_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Load Layer Transparency"),
                                   [this] { canvas_->select_active_layer_opaque_pixels(); });
  });
  connect(stroke_selection_action, &QAction::triggered, this, [this] { stroke_selection(); });
  for (auto* action : {select_all_action, clear_selection_action, reselect_action, inverse_selection_action,
                       grow_selection_action, similar_selection_action, expand_selection_action,
                       contract_selection_action, border_selection_action, layer_transparency_action,
                       stroke_selection_action}) {
    register_document_action(action);
  }
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
  auto* add_mask_action = layer_menu->addAction(tr("Add Layer &Mask"));
  edit_layer_mask_action_ = layer_menu->addAction(tr("&Edit Layer Mask"));
  mask_overlay_action_ = layer_menu->addAction(tr("Show Mask &Overlay"));
  delete_layer_mask_action_ = layer_menu->addAction(tr("&Delete Layer Mask"));
  link_layer_mask_action_ = layer_menu->addAction(tr("Link Layer &Mask"));
  disable_layer_mask_action_ = layer_menu->addAction(tr("&Disable Layer Mask"));
  invert_layer_mask_action_ = layer_menu->addAction(tr("&Invert Layer Mask"));
  apply_layer_mask_action_ = layer_menu->addAction(tr("&Apply Layer Mask"));
  layer_menu->addSeparator();
  auto* edit_adjustment_action = layer_menu->addAction(tr("&Edit Adjustment..."));
  layer_blending_options_action_ = layer_menu->addAction(tr("Edit Layer &Styles..."));
  layer_copy_style_action_ = new QAction(tr("Copy Layer Style"), this);
  layer_paste_style_action_ = new QAction(tr("Paste Layer Style"), this);
  layer_delete_style_action_ = new QAction(tr("Delete Layer Style"), this);
  layer_rasterize_action_ = new QAction(tr("Rasterize"), this);
  layer_rasterize_layer_style_action_ = new QAction(tr("Rasterize (including layer style)"), this);
  layer_menu->addSeparator();
  auto* duplicate_layer_action = layer_menu->addAction(tr("&Duplicate Layer"));
  auto* merge_visible_action = layer_menu->addAction(tr("Merge &Visible to New Layer"));
  merge_visible_action->setObjectName(QStringLiteral("layerMergeVisibleAction"));
  auto* merge_down_action = layer_menu->addAction(tr("Merge &Down"));
  merge_down_action->setObjectName(QStringLiteral("layerMergeDownAction"));
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
  add_mask_action->setObjectName(QStringLiteral("layerAddMaskAction"));
  edit_layer_mask_action_->setObjectName(QStringLiteral("layerEditMaskAction"));
  mask_overlay_action_->setObjectName(QStringLiteral("layerMaskOverlayAction"));
  delete_layer_mask_action_->setObjectName(QStringLiteral("layerDeleteMaskAction"));
  link_layer_mask_action_->setObjectName(QStringLiteral("layerLinkMaskAction"));
  disable_layer_mask_action_->setObjectName(QStringLiteral("layerDisableMaskAction"));
  invert_layer_mask_action_->setObjectName(QStringLiteral("layerInvertMaskAction"));
  apply_layer_mask_action_->setObjectName(QStringLiteral("layerApplyMaskAction"));
  edit_adjustment_action->setObjectName(QStringLiteral("layerEditAdjustmentAction"));
  layer_blending_options_action_->setObjectName(QStringLiteral("layerBlendingOptionsAction"));
  layer_copy_style_action_->setObjectName(QStringLiteral("layerCopyStyleAction"));
  layer_paste_style_action_->setObjectName(QStringLiteral("layerPasteStyleAction"));
  layer_delete_style_action_->setObjectName(QStringLiteral("layerDeleteStyleAction"));
  layer_rasterize_action_->setObjectName(QStringLiteral("layerRasterizeAction"));
  layer_rasterize_layer_style_action_->setObjectName(QStringLiteral("layerRasterizeLayerStyleAction"));
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
  edit_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(150, 205, 255)));
  mask_overlay_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 120, 120)));
  delete_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)));
  link_layer_mask_action_->setIcon(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)));
  disable_layer_mask_action_->setIcon(simple_icon(QStringLiteral("off"), QColor(255, 190, 120)));
  invert_layer_mask_action_->setIcon(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)));
  apply_layer_mask_action_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  link_layer_mask_action_->setCheckable(true);
  disable_layer_mask_action_->setCheckable(true);
  edit_layer_mask_action_->setCheckable(true);
  mask_overlay_action_->setCheckable(true);
  edit_adjustment_action->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_copy_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_paste_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_delete_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(255, 150, 150)));
  layer_rasterize_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  layer_rasterize_layer_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  duplicate_layer_action->setIcon(simple_icon(QStringLiteral("dup")));
  merge_visible_action->setIcon(simple_icon(QStringLiteral("merge")));
  merge_down_action->setIcon(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)));
  rename_layer_action->setIcon(simple_icon(QStringLiteral("RN")));
  delete_layer_action->setIcon(simple_icon(QStringLiteral("trash")));
  fill_layer_action->setIcon(simple_icon(QStringLiteral("fill")));
  fill_background_action->setIcon(simple_icon(QStringLiteral("fill"), QColor(160, 190, 255)));
  clear_layer_action->setIcon(simple_icon(QStringLiteral("clear")));
  flip_h_action->setIcon(simple_icon(QStringLiteral("FH")));
  flip_v_action->setIcon(simple_icon(QStringLiteral("FV")));
  layer_up_action->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
  layer_down_action->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  register_hotkey(add_layer_action, "layer.new", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  register_hotkey(layer_via_copy_action, "layer.via_copy", QKeySequence(Qt::CTRL | Qt::Key_J));
  register_hotkey(layer_via_cut_action, "layer.via_cut", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  register_hotkey(merge_visible_action, "layer.merge_visible", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  register_hotkey(merge_down_action, "layer.merge_down", QKeySequence(Qt::CTRL | Qt::Key_E));
  register_hotkey(edit_layer_mask_action_, "layer.edit_mask", QKeySequence(Qt::CTRL | Qt::Key_Backslash));
  register_hotkey(mask_overlay_action_, "layer.mask_overlay", QKeySequence(Qt::Key_Backslash));
  register_hotkey(fill_layer_action, "layer.fill", QKeySequence(Qt::ALT | Qt::Key_Backspace));
  register_hotkey(fill_background_action, "layer.fill_background", QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  register_hotkey(clear_layer_action, "layer.clear", QKeySequence(Qt::Key_Delete));
  register_hotkey(add_folder_action, "layer.new_folder");
  register_hotkey(add_mask_action, "layer.add_mask");
  register_hotkey(delete_layer_mask_action_, "layer.delete_mask");
  register_hotkey(invert_layer_mask_action_, "layer.invert_mask");
  register_hotkey(apply_layer_mask_action_, "layer.apply_mask");
  register_hotkey(edit_adjustment_action, "layer.edit_adjustment");
  register_hotkey(layer_blending_options_action_, "layer.styles");
  register_hotkey(duplicate_layer_action, "layer.duplicate");
  register_hotkey(rename_layer_action, "layer.rename");
  register_hotkey(delete_layer_action, "layer.delete");
  register_hotkey(flip_h_action, "layer.flip_horizontal");
  register_hotkey(flip_v_action, "layer.flip_vertical");
  register_hotkey(layer_up_action, "layer.move_up");
  register_hotkey(layer_down_action, "layer.move_down");
  connect(add_layer_action, &QAction::triggered, this, [this] { add_layer(); });
  connect(add_folder_action, &QAction::triggered, this, [this] { create_layer_folder(); });
  connect(layer_via_copy_action, &QAction::triggered, this, [this] { layer_via_copy(); });
  connect(layer_via_cut_action, &QAction::triggered, this, [this] { layer_via_cut(); });
  connect(add_mask_action, &QAction::triggered, this, [this] { add_layer_mask(); });
  connect(edit_layer_mask_action_, &QAction::triggered, this, [this](bool checked) {
    set_layer_edit_target_ui(checked ? CanvasWidget::LayerEditTarget::Mask : CanvasWidget::LayerEditTarget::Content,
                             true);
  });
  connect(mask_overlay_action_, &QAction::triggered, this, [this](bool checked) { set_mask_overlay_shown(checked); });
  connect(delete_layer_mask_action_, &QAction::triggered, this, [this] { delete_active_layer_mask(); });
  connect(link_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_linked(checked); });
  connect(disable_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_disabled(checked); });
  connect(invert_layer_mask_action_, &QAction::triggered, this, [this] { invert_active_layer_mask(); });
  connect(apply_layer_mask_action_, &QAction::triggered, this, [this] { apply_active_layer_mask(); });
  connect(edit_adjustment_action, &QAction::triggered, this, [this] { edit_active_adjustment_layer(); });
  connect(layer_blending_options_action_, &QAction::triggered, this, [this] { edit_active_layer_style(); });
  connect(layer_copy_style_action_, &QAction::triggered, this, [this] { copy_active_layer_style(); });
  connect(layer_paste_style_action_, &QAction::triggered, this, [this] { paste_layer_style_to_selected_layers(); });
  connect(layer_delete_style_action_, &QAction::triggered, this, [this] { delete_selected_layer_styles(); });
  connect(layer_rasterize_action_, &QAction::triggered, this, [this] { rasterize_active_layers(); });
  connect(layer_rasterize_layer_style_action_, &QAction::triggered, this,
          [this] { rasterize_active_layer_styles(); });
  connect(duplicate_layer_action, &QAction::triggered, this, [this] { duplicate_active_layer(); });
  connect(merge_visible_action, &QAction::triggered, this, [this] { merge_visible_to_new_layer(); });
  connect(merge_down_action, &QAction::triggered, this, [this] { merge_down(); });
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
  for (auto* action : {add_layer_action, add_folder_action, new_adjustment_layer_menu->menuAction(),
                       layer_via_copy_action, layer_via_cut_action, add_mask_action, duplicate_layer_action,
                       merge_visible_action, merge_down_action, rename_layer_action, delete_layer_action,
                       fill_layer_action, fill_background_action, clear_layer_action, flip_h_action, flip_v_action,
                       layer_up_action, layer_down_action}) {
    register_document_action(action);
  }

  auto* adjustments_menu = image_menu->addMenu(tr("&Adjustments"));
  adjustments_menu->setObjectName(QStringLiteral("imageAdjustmentsMenu"));
  const auto add_adjustment_action = [this, adjustments_menu](const QString& label, const QString& object_name,
                                                              const QString& identifier,
                                                              const QKeySequence& shortcut = {}) {
    auto* action = adjustments_menu->addAction(label);
    action->setObjectName(object_name);
    action->setIcon(simple_icon(label.left(3).toUpper()));
    register_hotkey(action, identifier, shortcut);
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
    return action;
  };
  add_adjustment_action(tr("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("patchy.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  register_hotkey(levels_action, "image.levels", QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  register_document_action(levels_action);
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  register_hotkey(curves_action, "image.curves", QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  register_document_action(curves_action);
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  register_hotkey(hue_saturation_action, "image.hue_saturation", QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  register_document_action(hue_saturation_action);
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  register_hotkey(color_balance_action, "image.color_balance", QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  register_document_action(color_balance_action);
  add_adjustment_action(tr("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("patchy.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(tr("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("patchy.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  adjustments_menu->addSeparator();
  add_adjustment_action(tr("&Brightness/Contrast..."), QStringLiteral("imageAdjustBrightnessContrastAction"),
                        QStringLiteral("patchy.filters.brightness_contrast"));
  add_adjustment_action(tr("&Threshold"), QStringLiteral("imageAdjustThresholdAction"),
                        QStringLiteral("patchy.filters.threshold"));
  add_adjustment_action(tr("&Posterize"), QStringLiteral("imageAdjustPosterizeAction"),
                        QStringLiteral("patchy.filters.posterize"));
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
  register_hotkey(image_size_action, "image.size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  register_hotkey(canvas_size_action, "image.canvas_size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  register_hotkey(crop_action, "image.crop_to_selection", QKeySequence(Qt::Key_C));
  register_hotkey(rotate_cw_action, "image.rotate_cw", QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  register_hotkey(rotate_ccw_action, "image.rotate_ccw", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  connect(image_size_action, &QAction::triggered, this, [this] { resize_image_dialog(); });
  connect(canvas_size_action, &QAction::triggered, this, [this] { resize_canvas_dialog(); });
  connect(crop_action, &QAction::triggered, this, [this] { crop_to_selection(); });
  connect(rotate_cw_action, &QAction::triggered, this, [this] { rotate_canvas_clockwise(); });
  connect(rotate_ccw_action, &QAction::triggered, this, [this] { rotate_canvas_counterclockwise(); });
  for (auto* action : {adjustments_menu->menuAction(), image_size_action, canvas_size_action, crop_action,
                       rotate_cw_action, rotate_ccw_action}) {
    register_document_action(action);
  }

  const auto add_filter_submenu = [this, filter_menu](const char* object_name, const char* source) {
    auto* menu = filter_menu->addMenu(tr(source));
    menu->setObjectName(QString::fromLatin1(object_name));
    bind_action_text(menu->menuAction(), source);
    register_document_action(menu->menuAction());
    return menu;
  };
  auto* filter_photo_looks_menu = add_filter_submenu("filterPhotoLooksMenu", "Photo Looks");
  auto* filter_blur_menu = add_filter_submenu("filterBlurMenu", "Blur");
  auto* filter_sharpen_menu = add_filter_submenu("filterSharpenMenu", "Sharpen");
  auto* filter_distort_menu = add_filter_submenu("filterDistortMenu", "Distort");
  auto* filter_noise_menu = add_filter_submenu("filterNoiseMenu", "Noise");
  auto* filter_pixelate_menu = add_filter_submenu("filterPixelateMenu", "Pixelate");
  auto* filter_stylize_menu = add_filter_submenu("filterStylizeMenu", "Stylize");
  auto* filter_render_menu = add_filter_submenu("filterRenderMenu", "Render");
  const auto menu_for_filter = [filter_menu, filter_photo_looks_menu, filter_blur_menu, filter_sharpen_menu,
                               filter_distort_menu, filter_noise_menu, filter_pixelate_menu, filter_stylize_menu,
                               filter_render_menu](const QString& identifier) {
    if (identifier == QStringLiteral("patchy.filters.soft_glow") ||
        identifier == QStringLiteral("patchy.filters.punchy_color") ||
        identifier == QStringLiteral("patchy.filters.noir") ||
        identifier == QStringLiteral("patchy.filters.cinematic_matte") ||
        identifier == QStringLiteral("patchy.filters.vintage_fade") ||
        identifier == QStringLiteral("patchy.filters.sepia") ||
        identifier == QStringLiteral("patchy.filters.vignette")) {
      return filter_photo_looks_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.box_blur") ||
        identifier == QStringLiteral("patchy.filters.gaussian_blur") ||
        identifier == QStringLiteral("patchy.filters.motion_blur") ||
        identifier == QStringLiteral("patchy.filters.radial_blur")) {
      return filter_blur_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.sharpen") ||
        identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
      return filter_sharpen_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.twirl") || identifier == QStringLiteral("patchy.filters.wave") ||
        identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
      return filter_distort_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.film_grain")) {
      return filter_noise_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.pixelate") ||
        identifier == QStringLiteral("patchy.filters.color_halftone")) {
      return filter_pixelate_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.edge_detect") ||
        identifier == QStringLiteral("patchy.filters.emboss") ||
        identifier == QStringLiteral("patchy.filters.glowing_edges")) {
      return filter_stylize_menu;
    }
    if (identifier == QStringLiteral("patchy.filters.clouds")) {
      return filter_render_menu;
    }
    return filter_menu;
  };

  for (const auto& filter : filters_.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    if (is_adjustment_only_filter(identifier)) {
      continue;
    }
    const auto display_name = filter_display_name(filter);
    auto* action = menu_for_filter(identifier)->addAction(display_name);
    action->setObjectName(filter_action_object_name(identifier));
    action->setIcon(simple_icon(display_name.left(3).toUpper()));
    action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
    refresh_action_tooltip(action);
    QPointer<QAction> filter_action(action);
    register_retranslation([this, filter_action, identifier] {
      if (filter_action == nullptr) {
        return;
      }
      const auto* filter = filters_.find(identifier.toStdString());
      if (filter == nullptr) {
        return;
      }
      const auto display_name = filter_display_name(*filter);
      filter_action->setText(display_name);
      filter_action->setIcon(simple_icon(display_name.left(3).toUpper()));
      filter_action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
      refresh_action_tooltip(filter_action);
    });
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
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
  view_menu->addSeparator();
  view_rulers_action_ = view_menu->addAction(tr("&Rulers"));
  view_grid_action_ = view_menu->addAction(tr("&Grid"));
  view_guides_action_ = view_menu->addAction(tr("&Guides"));
  view_snap_action_ = view_menu->addAction(tr("&Snap"));
  view_lock_guides_action_ = view_menu->addAction(tr("Lock Guides"));
  auto* snap_to_menu = view_menu->addMenu(tr("Snap &To"));
  view_snap_guides_action_ = snap_to_menu->addAction(tr("Guides"));
  view_snap_grid_action_ = snap_to_menu->addAction(tr("Grid"));
  view_snap_document_action_ = snap_to_menu->addAction(tr("Document Bounds and Center"));
  view_snap_layers_action_ = snap_to_menu->addAction(tr("Layer Bounds and Centers"));
  view_snap_selection_action_ = snap_to_menu->addAction(tr("Selection Bounds and Center"));
  auto* guides_menu = view_menu->addMenu(tr("Guide Operations"));
  auto* new_guide_action = guides_menu->addAction(tr("New Guide..."));
  auto* new_guide_layout_action = guides_menu->addAction(tr("New Guide Layout..."));
  auto* clear_selected_guides_action = guides_menu->addAction(tr("Clear Selected Guides"));
  auto* clear_guides_action = guides_menu->addAction(tr("Clear Guides"));
  zoom_in->setObjectName(QStringLiteral("viewZoomInAction"));
  zoom_out->setObjectName(QStringLiteral("viewZoomOutAction"));
  fit_on_screen->setObjectName(QStringLiteral("viewFitOnScreenAction"));
  zoom_reset->setObjectName(QStringLiteral("viewActualPixelsAction"));
  selection_edges_action->setObjectName(QStringLiteral("viewToggleSelectionEdgesAction"));
  view_rulers_action_->setObjectName(QStringLiteral("viewToggleRulersAction"));
  view_grid_action_->setObjectName(QStringLiteral("viewToggleGridAction"));
  view_guides_action_->setObjectName(QStringLiteral("viewToggleGuidesAction"));
  view_snap_action_->setObjectName(QStringLiteral("viewToggleSnapAction"));
  view_lock_guides_action_->setObjectName(QStringLiteral("viewLockGuidesAction"));
  snap_to_menu->setObjectName(QStringLiteral("viewSnapToMenu"));
  guides_menu->setObjectName(QStringLiteral("viewGuideOperationsMenu"));
  view_snap_guides_action_->setObjectName(QStringLiteral("viewSnapToGuidesAction"));
  view_snap_grid_action_->setObjectName(QStringLiteral("viewSnapToGridAction"));
  view_snap_document_action_->setObjectName(QStringLiteral("viewSnapToDocumentAction"));
  view_snap_layers_action_->setObjectName(QStringLiteral("viewSnapToLayersAction"));
  view_snap_selection_action_->setObjectName(QStringLiteral("viewSnapToSelectionAction"));
  new_guide_action->setObjectName(QStringLiteral("viewNewGuideAction"));
  new_guide_layout_action->setObjectName(QStringLiteral("viewNewGuideLayoutAction"));
  clear_selected_guides_action->setObjectName(QStringLiteral("viewClearSelectedGuidesAction"));
  clear_guides_action->setObjectName(QStringLiteral("viewClearGuidesAction"));
  zoom_in->setIcon(simple_icon(QStringLiteral("zoomIn")));
  zoom_out->setIcon(simple_icon(QStringLiteral("zoomOut")));
  fit_on_screen->setIcon(simple_icon(QStringLiteral("fit")));
  zoom_reset->setIcon(simple_icon(QStringLiteral("1x")));
  selection_edges_action->setIcon(simple_icon(QStringLiteral("SE")));
  view_rulers_action_->setIcon(simple_icon(QStringLiteral("RU")));
  view_grid_action_->setIcon(simple_icon(QStringLiteral("GRD")));
  view_guides_action_->setIcon(simple_icon(QStringLiteral("GDE")));
  view_snap_action_->setIcon(simple_icon(QStringLiteral("SN")));
  view_lock_guides_action_->setIcon(simple_icon(QStringLiteral("LK")));
  new_guide_action->setIcon(simple_icon(QStringLiteral("NG")));
  new_guide_layout_action->setIcon(simple_icon(QStringLiteral("NGL")));
  clear_selected_guides_action->setIcon(simple_icon(QStringLiteral("CSG")));
  clear_guides_action->setIcon(simple_icon(QStringLiteral("CG")));
  view_rulers_action_->setCheckable(true);
  view_grid_action_->setCheckable(true);
  view_guides_action_->setCheckable(true);
  view_snap_action_->setCheckable(true);
  view_lock_guides_action_->setCheckable(true);
  view_snap_guides_action_->setCheckable(true);
  view_snap_grid_action_->setCheckable(true);
  view_snap_document_action_->setCheckable(true);
  view_snap_layers_action_->setCheckable(true);
  view_snap_selection_action_->setCheckable(true);
  view_rulers_action_->setChecked(view_rulers_visible_);
  view_grid_action_->setChecked(view_grid_visible_);
  view_guides_action_->setChecked(view_guides_visible_);
  view_snap_action_->setChecked(view_snap_enabled_);
  view_lock_guides_action_->setChecked(view_guides_locked_);
  view_snap_guides_action_->setChecked(view_snap_to_guides_);
  view_snap_grid_action_->setChecked(view_snap_to_grid_);
  view_snap_document_action_->setChecked(view_snap_to_document_);
  view_snap_layers_action_->setChecked(view_snap_to_layers_);
  view_snap_selection_action_->setChecked(view_snap_to_selection_);
  auto zoom_in_defaults = QKeySequence::keyBindings(QKeySequence::ZoomIn);
  if (!zoom_in_defaults.contains(QKeySequence(Qt::CTRL | Qt::Key_Equal))) {
    zoom_in_defaults << QKeySequence(Qt::CTRL | Qt::Key_Equal);
  }
  register_hotkey(zoom_in, "view.zoom_in", zoom_in_defaults);
  register_hotkey(zoom_out, "view.zoom_out", QKeySequence::keyBindings(QKeySequence::ZoomOut));
  register_hotkey(fit_on_screen, "view.fit_on_screen", QKeySequence(Qt::CTRL | Qt::Key_0));
  register_hotkey(zoom_reset, "view.actual_pixels", QKeySequence(Qt::CTRL | Qt::Key_1));
  register_hotkey(selection_edges_action, "view.selection_edges", QKeySequence(Qt::CTRL | Qt::Key_H));
  register_hotkey(view_rulers_action_, "view.rulers", QKeySequence(Qt::CTRL | Qt::Key_R));
  register_hotkey(view_grid_action_, "view.grid", QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  register_hotkey(view_guides_action_, "view.guides", QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  register_hotkey(view_snap_action_, "view.snap", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  register_hotkey(view_lock_guides_action_, "view.lock_guides", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  register_hotkey(new_guide_action, "view.new_guide");
  register_hotkey(new_guide_layout_action, "view.new_guide_layout");
  register_hotkey(clear_selected_guides_action, "view.clear_selected_guides");
  register_hotkey(clear_guides_action, "view.clear_guides");
  connect(zoom_in, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 1.25); });
  connect(zoom_out, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 0.8); });
  connect(fit_on_screen, &QAction::triggered, this, [this] { canvas_->fit_to_view(); });
  connect(zoom_reset, &QAction::triggered, this, [this] { canvas_->set_zoom(1.0); });
  connect(selection_edges_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->toggle_selection_edges_visible();
    }
  });
  const auto apply_view_settings = [this] {
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
    }
    save_view_settings();
  };
  connect(view_rulers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_rulers_visible_ = checked;
    apply_view_settings();
  });
  connect(view_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_grid_visible_ = checked;
    apply_view_settings();
  });
  connect(view_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_visible_ = checked;
    apply_view_settings();
  });
  connect(view_snap_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_enabled_ = checked;
    apply_view_settings();
  });
  connect(view_lock_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_locked_ = checked;
    apply_view_settings();
  });
  connect(view_snap_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_guides_ = checked;
    apply_view_settings();
  });
  connect(view_snap_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_grid_ = checked;
    apply_view_settings();
  });
  connect(view_snap_document_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_document_ = checked;
    apply_view_settings();
  });
  connect(view_snap_layers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_layers_ = checked;
    apply_view_settings();
  });
  connect(view_snap_selection_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_selection_ = checked;
    apply_view_settings();
  });
  connect(new_guide_action, &QAction::triggered, this, [this] { new_guide_dialog(); });
  connect(new_guide_layout_action, &QAction::triggered, this, [this] { new_guide_layout_dialog(); });
  connect(clear_selected_guides_action, &QAction::triggered, this, [this] { clear_selected_guides(); });
  connect(clear_guides_action, &QAction::triggered, this, [this] { clear_guides(); });
  for (auto* action : {zoom_in, zoom_out, fit_on_screen, zoom_reset, selection_edges_action, view_rulers_action_,
                       view_grid_action_, view_guides_action_, view_snap_action_, view_lock_guides_action_,
                       snap_to_menu->menuAction(), view_snap_guides_action_, view_snap_grid_action_,
                       view_snap_document_action_, view_snap_layers_action_, view_snap_selection_action_,
                       guides_menu->menuAction(), new_guide_action, new_guide_layout_action,
                       clear_selected_guides_action, clear_guides_action}) {
    register_document_action(action);
  }

  auto* language_group = new QActionGroup(this);
  language_group->setExclusive(true);
  language_english_action_ = new QAction(tr("&English"), this);
  language_japanese_action_ = new QAction(QStringLiteral("日本語"), this);
  language_english_action_->setObjectName(QStringLiteral("preferencesLanguageEnglishAction"));
  language_japanese_action_->setObjectName(QStringLiteral("preferencesLanguageJapaneseAction"));
  language_english_action_->setCheckable(true);
  language_japanese_action_->setCheckable(true);
  language_group->addAction(language_english_action_);
  language_group->addAction(language_japanese_action_);
  bind_action_text(language_english_action_, "&English");
  connect(language_english_action_, &QAction::triggered, this, [this] {
    LocalizationManager::instance().set_language(QStringLiteral("en"));
    refresh_language_actions();
  });
  connect(language_japanese_action_, &QAction::triggered, this, [this] {
    LocalizationManager::instance().set_language(QStringLiteral("ja"));
    refresh_language_actions();
  });
  refresh_language_actions();

  auto* screen_size_menu = window_menu->addMenu(tr("Set Screen Size"));
  screen_size_menu->setObjectName(QStringLiteral("windowSetScreenSizeMenu"));
  struct ScreenSizePreset {
    int width;
    int height;
    const char* label;
  };
  static constexpr ScreenSizePreset kScreenSizePresets[] = {
      {1280, 720, "1280 x 720 (HD)"},     {1366, 768, "1366 x 768"},
      {1600, 900, "1600 x 900"},          {1920, 1080, "1920 x 1080 (Full HD)"},
      {2560, 1440, "2560 x 1440 (QHD)"},  {3840, 2160, "3840 x 2160 (4K UHD)"},
  };
  for (const auto& preset : kScreenSizePresets) {
    auto* action = screen_size_menu->addAction(QString());
    action->setObjectName(QStringLiteral("windowSetScreenSize%1x%2Action").arg(preset.width).arg(preset.height));
    bind_action_text(action, preset.label);
    connect(action, &QAction::triggered, this,
            [this, preset] { set_window_screen_size(QSize(preset.width, preset.height)); });
  }

  auto* force_refresh_action = window_menu->addAction(tr("Force Refresh"));
  force_refresh_action->setObjectName(QStringLiteral("windowForceRefreshAction"));
  force_refresh_action->setIcon(simple_icon(QStringLiteral("RF")));
  register_hotkey(force_refresh_action, "window.force_refresh", QKeySequence(Qt::Key_F5));
  connect(force_refresh_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->force_refresh();
    }
  });
  register_document_action(force_refresh_action);

  auto* about_action = help_menu->addAction(tr("&About Patchy"));
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
  tool_action_group_ = tool_group;
  move_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Move"), CanvasTool::Move, QKeySequence(Qt::Key_V));
  auto* marquee_menu = new QMenu(tr("Marquee Tools"), tool_palette);
  marquee_menu->setObjectName(QStringLiteral("marqueeToolMenu"));
  bind_widget_text(marquee_menu, "Marquee Tools");
  const auto create_marquee_action = [this, tool_group, marquee_menu](const QString& label, CanvasTool tool,
                                                                      QKeySequence shortcut) {
    auto* action = new QAction(label, this);
    bind_action_text(action, tool_action_source(tool));
    action->setIcon(tool_icon(tool));
    action->setCheckable(true);
    action->setData(static_cast<int>(tool));
    action->setObjectName(tool_action_object_name(tool));
    register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
    tool_group->addAction(action);
    marquee_menu->addAction(action);
    addAction(action);
    register_document_action(action);
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
  bind_widget_text(shape_menu, "Shape Tools");
  const auto create_shape_action = [this, tool_group, shape_menu](const QString& label, CanvasTool tool,
                                                                  QKeySequence shortcut) {
    auto* action = new QAction(label, this);
    bind_action_text(action, tool_action_source(tool));
    action->setIcon(tool_icon(tool));
    action->setCheckable(true);
    action->setData(static_cast<int>(tool));
    action->setObjectName(tool_action_object_name(tool));
    register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
    tool_group->addAction(action);
    shape_menu->addAction(action);
    addAction(action);
    register_document_action(action);
    return action;
  };
  auto* line_tool_action =
      create_shape_action(tr("Line"), CanvasTool::Line, QKeySequence());  // Ctrl+Shift+U belongs to Desaturate
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
  type_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Type"), CanvasTool::Text, QKeySequence(Qt::Key_T));
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
    if (canvas_ == nullptr) {
      return;
    }
    const auto selected = static_cast<CanvasTool>(action->data().toInt());
    if (canvas_->free_transform_active()) {
      canvas_->finish_free_transform();
    }
    if (selected != CanvasTool::Text) {
      finish_active_text_editor();
    }
    current_tool_ = selected;
    canvas_->set_tool(selected);
    set_eraser_brush_settings_active(selected == CanvasTool::Eraser);
    if (selected != CanvasTool::Text ||
        canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr) {
      canvas_->setFocus(Qt::OtherFocusReason);
    }
    refresh_options_bar();
    refresh_document_info();
    statusBar()->showMessage(tool_name(selected));
  });
  type_menu->addAction(type_tool_action_);

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
  register_hotkey(default_colors_action, "color.default", QKeySequence(Qt::Key_D), QStringLiteral("color"));
  register_hotkey(swap_colors_action, "color.swap", QKeySequence(Qt::Key_X), QStringLiteral("color"));
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
  register_document_action(default_colors_action);
  register_document_action(swap_colors_action);

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
  const auto add_transform_option_separator = [this, make_option_separator] {
    auto* line = make_option_separator();
    transform_option_actions_.push_back(line);
    return line;
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
  move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
  });
  move_show_transform_controls_check_ = new CheckGlyphBox(tr("Show Transform Controls"), toolbar);
  move_show_transform_controls_check_->setObjectName(QStringLiteral("moveShowTransformControlsCheck"));
  move_show_transform_controls_check_->setToolTip(tr("Show transform controls when selecting a layer with Move"));
  move_show_transform_controls_check_->setChecked(canvas_->show_transform_controls());
  add_option_widget(move_show_transform_controls_check_, {CanvasTool::Move});
  connect(move_show_transform_controls_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_show_transform_controls(checked);
    }
  });

  add_transform_option_separator();
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
  transform_apply_button_ = new QPushButton(toolbar);
  transform_apply_button_->setObjectName(QStringLiteral("freeTransformApplyButton"));
  transform_apply_button_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  transform_apply_button_->setToolTip(tr("Apply transform"));
  transform_apply_button_->setFixedWidth(30);
  add_transform_option_widget(transform_apply_button_);
  transform_cancel_button_ = new QPushButton(toolbar);
  transform_cancel_button_->setObjectName(QStringLiteral("freeTransformCancelButton"));
  transform_cancel_button_->setIcon(simple_icon(QStringLiteral("clear"), QColor(255, 150, 150)));
  transform_cancel_button_->setToolTip(tr("Cancel transform"));
  transform_cancel_button_->setFixedWidth(30);
  add_transform_option_widget(transform_cancel_button_);

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
  connect(transform_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->finish_free_transform();
    }
  });
  connect(transform_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->cancel_free_transform();
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
    refresh_document_info();
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
    brush_preset_combo_->addItem(brush_preset_display_name(preset), preset.id);
  }
  {
    const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
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
  brush_size_slider->setToolTip(tr("Brush size — press [ or ], or Alt+Right-drag on the canvas"));
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
  brush_opacity_slider->setToolTip(tr("Brush opacity — press number keys (5 = 50%, 0 = 100%)"));
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
  brush_softness_slider->setToolTip(tr("Brush edge softness — Alt+Right-drag up or down on the canvas"));
  add_option_widget(brush_softness_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line,
                     CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(brush_size, &QSpinBox::valueChanged, brush_size_slider, &QSlider::setValue);
  connect(brush_size_slider, &QSlider::valueChanged, brush_size, &QSpinBox::setValue);
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_size(value);
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_opacity, &QSpinBox::valueChanged, brush_opacity_slider, &QSlider::setValue);
  connect(brush_opacity_slider, &QSlider::valueChanged, brush_opacity, &QSpinBox::setValue);
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_opacity(value);
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_softness, &QSpinBox::valueChanged, brush_softness_slider, &QSlider::setValue);
  connect(brush_softness_slider, &QSlider::valueChanged, brush_softness, &QSpinBox::setValue);
  connect(brush_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_softness(value);
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_preset_combo_, &QComboBox::currentIndexChanged, this,
          [this, brush_size, brush_opacity, brush_softness](int index) {
    if (brush_preset_combo_ == nullptr || canvas_ == nullptr || index < 0) {
      return;
    }
    const auto preset_id = brush_preset_combo_->itemData(index).toString();
    const auto* preset = find_brush_preset(preset_id);
    if (preset == nullptr) {
      return;
    }
    apply_brush_preset(*canvas_, *preset);
    brush_size->setValue(preset->size);
    brush_opacity->setValue(preset->opacity);
    brush_softness->setValue(preset->softness);
    save_tool_settings();
    refresh_document_info();
    statusBar()->showMessage(tr("Brush preset: %1").arg(brush_preset_display_name(*preset)));
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
  gradient_opacity_spin_->setValue(canvas_->gradient_opacity());
  gradient_opacity_spin_->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(gradient_opacity_spin_, 52);
  add_option_widget(gradient_opacity_spin_, {CanvasTool::Gradient});
  gradient_opacity_slider_ = new QSlider(Qt::Horizontal, toolbar);
  gradient_opacity_slider_->setObjectName(QStringLiteral("gradientOpacitySlider"));
  gradient_opacity_slider_->setRange(0, 100);
  gradient_opacity_slider_->setValue(canvas_->gradient_opacity());
  gradient_opacity_slider_->setFixedWidth(110);
  gradient_opacity_slider_->setToolTip(tr("Gradient opacity"));
  bind_tooltip(gradient_opacity_slider_, "Gradient opacity");
  add_option_widget(gradient_opacity_slider_, {CanvasTool::Gradient});
  gradient_reverse_check_ = new CheckGlyphBox(tr("Reverse"), toolbar);
  gradient_reverse_check_->setObjectName(QStringLiteral("gradientReverseCheck"));
  gradient_reverse_check_->setChecked(canvas_->gradient_reverse());
  add_option_widget(gradient_reverse_check_, {CanvasTool::Gradient});
  gradient_preview_button_ = new QPushButton(toolbar);
  gradient_preview_button_->setObjectName(QStringLiteral("gradientPreviewButton"));
  gradient_preview_button_->setToolTip(tr("Gradient preview"));
  bind_tooltip(gradient_preview_button_, "Gradient preview");
  add_option_widget(gradient_preview_button_, {CanvasTool::Gradient});
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
      save_tool_settings();
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
  connect(gradient_edit_stops_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });

  clone_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  clone_aligned_check_->setObjectName(QStringLiteral("cloneAlignedCheck"));
  clone_aligned_check_->setChecked(canvas_->clone_aligned());
  clone_aligned_check_->setToolTip(tr("Keep clone source offset aligned across strokes"));
  add_option_widget(clone_aligned_check_, {CanvasTool::Clone});
  connect(clone_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_clone_aligned(checked);
      save_tool_settings();
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
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 1)); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 1)); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 10)); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 10)); });
  for (auto* action : {brush_smaller_action, brush_larger_action, brush_much_smaller_action,
                       brush_much_larger_action}) {
    register_document_action(action);
  }

  add_option_label(tr("Tol:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_tolerance(value);
      save_tool_settings();
      refresh_document_info();
    }
  });

  wand_contiguous_check_ = new CheckGlyphBox(tr("Contiguous"), toolbar);
  wand_contiguous_check_->setObjectName(QStringLiteral("wandContiguousCheck"));
  wand_contiguous_check_->setChecked(canvas_->wand_contiguous());
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
  wand_sample_all_layers_check_->setChecked(canvas_->wand_sample_all_layers());
  wand_sample_all_layers_check_->setToolTip(tr("Sample the merged document instead of the active layer"));
  add_option_widget(wand_sample_all_layers_check_, {CanvasTool::MagicWand});
  connect(wand_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_shapes(checked);
    }
  });

  add_option_label(tr("Radius:"), {CanvasTool::Rectangle});
  auto* shape_corner_radius = new QSpinBox(toolbar);
  shape_corner_radius->setObjectName(QStringLiteral("shapeCornerRadiusSpin"));
  shape_corner_radius->setRange(0, 512);
  shape_corner_radius->setValue(canvas_->shape_corner_radius());
  shape_corner_radius->setSuffix(QStringLiteral(" px"));
  shape_corner_radius->setToolTip(tr("Rounded-corner radius for the rectangle tool (0 = sharp corners)"));
  configure_toolbar_spinbox(shape_corner_radius, 64);
  add_option_widget(shape_corner_radius, {CanvasTool::Rectangle});
  connect(shape_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_shape_corner_radius(value);
      save_tool_settings();
    }
  });

  // Fill tool / Fill hotkey settings (independent of the brush; default 100% opacity, 0 softness).
  add_option_label(tr("Opacity:"), {CanvasTool::Fill});
  auto* fill_opacity = new QSpinBox(toolbar);
  fill_opacity->setObjectName(QStringLiteral("fillOpacitySpin"));
  fill_opacity->setRange(1, 100);
  fill_opacity->setValue(canvas_->fill_opacity());
  fill_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_opacity, 52);
  add_option_widget(fill_opacity, {CanvasTool::Fill});
  auto* fill_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_opacity_slider->setObjectName(QStringLiteral("fillOpacitySlider"));
  fill_opacity_slider->setRange(1, 100);
  fill_opacity_slider->setValue(canvas_->fill_opacity());
  fill_opacity_slider->setFixedWidth(120);
  fill_opacity_slider->setToolTip(tr("Fill opacity for the Fill tool and Fill shortcut"));
  add_option_widget(fill_opacity_slider, {CanvasTool::Fill});
  add_option_label(tr("Soft:"), {CanvasTool::Fill});
  auto* fill_softness = new QSpinBox(toolbar);
  fill_softness->setObjectName(QStringLiteral("fillSoftnessSpin"));
  fill_softness->setRange(0, 100);
  fill_softness->setValue(canvas_->fill_softness());
  fill_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_softness, 52);
  add_option_widget(fill_softness, {CanvasTool::Fill});
  auto* fill_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_softness_slider->setObjectName(QStringLiteral("fillSoftnessSlider"));
  fill_softness_slider->setRange(0, 100);
  fill_softness_slider->setValue(canvas_->fill_softness());
  fill_softness_slider->setFixedWidth(110);
  fill_softness_slider->setToolTip(tr("Soft edge feather for the Fill tool and Fill shortcut"));
  add_option_widget(fill_softness_slider, {CanvasTool::Fill});
  connect(fill_opacity, &QSpinBox::valueChanged, fill_opacity_slider, &QSlider::setValue);
  connect(fill_opacity_slider, &QSlider::valueChanged, fill_opacity, &QSpinBox::setValue);
  connect(fill_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_opacity(value);
      save_tool_settings();
    }
  });
  connect(fill_softness, &QSpinBox::valueChanged, fill_softness_slider, &QSlider::setValue);
  connect(fill_softness_slider, &QSlider::valueChanged, fill_softness, &QSpinBox::setValue);
  connect(fill_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_softness(value);
      save_tool_settings();
    }
  });

  add_option_label(tr("Font:"), {CanvasTool::Text});
  text_font_combo_ = new QFontComboBox(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  text_font_combo_->setFixedWidth(210);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  add_option_label(tr("Size:"), {CanvasTool::Text});
  text_size_spin_ = new QDoubleSpinBox(toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->setDecimals(3);
  text_size_spin_->setRange(0.01, 10000.0);
  text_size_spin_->setSingleStep(0.25);
  text_size_spin_->setValue(text_pixels_to_points(48, document()));
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

  window_menu->addAction(tool_palette->toggleViewAction());
  window_menu->addAction(toolbar->toggleViewAction());
  bind_widget_text(tool_palette, "Tool Palette");
  bind_widget_text(toolbar, "Options");
  const std::vector<std::pair<QAction*, const char*>> translated_actions = {
      {new_action, "&New"},
      {open_action, "&Open..."},
      {recent_files_menu_->menuAction(), "Open &Recent File"},
      {recent_folders_menu_->menuAction(), "Open Recent &Folder"},
      {save_action, "&Save"},
      {save_as_action, "Save &As..."},
      {export_flat_action, "Export &Flat Image..."},
      {page_setup_action, "Page Set&up..."},
      {print_action, "&Print..."},
      {close_action, "&Close"},
      {close_all_action, "Close &All"},
      {preferences_action, "&Preferences..."},
      {quit_action, "&Quit"},
      {undo_action_, "&Undo"},
      {redo_action_, "&Redo"},
      {cut_action, "Cu&t"},
      {copy_action, "&Copy"},
      {copy_merged_action, "Copy Merged"},
      {paste_action, "&Paste"},
      {transform_action, "Free &Transform..."},
      {select_all_action, "Select &All"},
      {clear_selection_action, "&Clear Selection"},
      {reselect_action, "&Reselect"},
      {inverse_selection_action, "&Inverse"},
      {grow_selection_action, "&Grow"},
      {similar_selection_action, "Simi&lar"},
      {expand_selection_action, "&Expand..."},
      {contract_selection_action, "Con&tract..."},
      {border_selection_action, "&Border..."},
      {layer_transparency_action, "Load Layer &Transparency"},
      {stroke_selection_action, "&Stroke Selection"},
      {add_layer_action, "&New Layer"},
      {add_folder_action, "New &Folder"},
      {new_adjustment_layer_menu->menuAction(), "New &Adjustment Layer"},
      {layer_via_copy_action, "Layer Via &Copy"},
      {layer_via_cut_action, "Layer Via Cu&t"},
      {add_mask_action, "Add Layer &Mask"},
      {edit_layer_mask_action_, "&Edit Layer Mask"},
      {mask_overlay_action_, "Show Mask &Overlay"},
      {delete_layer_mask_action_, "&Delete Layer Mask"},
      {link_layer_mask_action_, "Link Layer &Mask"},
      {disable_layer_mask_action_, "&Disable Layer Mask"},
      {invert_layer_mask_action_, "&Invert Layer Mask"},
      {apply_layer_mask_action_, "&Apply Layer Mask"},
      {edit_adjustment_action, "&Edit Adjustment..."},
      {layer_blending_options_action_, "Edit Layer &Styles..."},
      {layer_copy_style_action_, "Copy Layer Style"},
      {layer_paste_style_action_, "Paste Layer Style"},
      {layer_delete_style_action_, "Delete Layer Style"},
      {layer_rasterize_action_, "Rasterize"},
      {layer_rasterize_layer_style_action_, "Rasterize (including layer style)"},
      {duplicate_layer_action, "&Duplicate Layer"},
      {merge_visible_action, "Merge &Visible to New Layer"},
      {merge_down_action, "Merge &Down"},
      {rename_layer_action, "&Rename Layer..."},
      {delete_layer_action, "&Delete Layer"},
      {fill_layer_action, "&Fill Layer / Selection"},
      {fill_background_action, "Fill With &Background Color"},
      {clear_layer_action, "&Clear Layer / Selection"},
      {flip_h_action, "Flip Layer &Horizontal"},
      {flip_v_action, "Flip Layer &Vertical"},
      {layer_up_action, "Move Layer &Up"},
      {layer_down_action, "Move Layer &Down"},
      {adjustments_menu->menuAction(), "&Adjustments"},
      {levels_action, "&Levels..."},
      {curves_action, "&Curves..."},
      {hue_saturation_action, "&Hue/Saturation..."},
      {color_balance_action, "Color &Balance..."},
      {image_size_action, "&Image Size..."},
      {canvas_size_action, "&Canvas Size..."},
      {crop_action, "&Crop to Selection"},
      {rotate_cw_action, "Rotate 90 &Clockwise"},
      {rotate_ccw_action, "Rotate 90 Counterclockwise"},
      {scan_legacy_plugins_action, "&Scan Legacy Photoshop Plug-ins..."},
      {legacy_plugins_menu_->menuAction(), "Legacy Photoshop Plug-ins"},
      {zoom_in, "Zoom &In"},
      {zoom_out, "Zoom &Out"},
      {fit_on_screen, "&Fit on Screen"},
      {zoom_reset, "&Actual Pixels"},
      {selection_edges_action, "Show Selection &Edges"},
      {view_rulers_action_, "&Rulers"},
      {view_grid_action_, "&Grid"},
      {view_guides_action_, "&Guides"},
      {view_snap_action_, "&Snap"},
      {view_lock_guides_action_, "Lock Guides"},
      {snap_to_menu->menuAction(), "Snap &To"},
      {view_snap_guides_action_, "Guides"},
      {view_snap_grid_action_, "Grid"},
      {view_snap_document_action_, "Document Bounds and Center"},
      {view_snap_layers_action_, "Layer Bounds and Centers"},
      {view_snap_selection_action_, "Selection Bounds and Center"},
      {guides_menu->menuAction(), "Guide Operations"},
      {new_guide_action, "New Guide..."},
      {new_guide_layout_action, "New Guide Layout..."},
      {clear_selected_guides_action, "Clear Selected Guides"},
      {clear_guides_action, "Clear Guides"},
      {screen_size_menu->menuAction(), "Set Screen Size"},
      {force_refresh_action, "Force Refresh"},
      {language_english_action_, "&English"},
      {about_action, "&About Patchy"},
      {default_colors_action, "Default Colors"},
      {swap_colors_action, "Swap Colors"},
      {brush_smaller_action, "Brush Smaller"},
      {brush_larger_action, "Brush Larger"},
      {brush_much_smaller_action, "Brush Much Smaller"},
      {brush_much_larger_action, "Brush Much Larger"},
  };
  for (const auto& [action, source] : translated_actions) {
    bind_action_text(action, source);
    refresh_action_tooltip(action);
  }
  const std::vector<std::pair<QObject*, const char*>> translated_widgets = {
      {primary_color_button_, "FG"},
      {secondary_color_button_, "BG"},
      {move_auto_select_check_, "Auto-Select"},
      {move_show_transform_controls_check_, "Show Transform Controls"},
      {clone_aligned_check_, "Aligned"},
      {gradient_reverse_check_, "Reverse"},
      {gradient_edit_stops_button_, "Edit Stops..."},
      {wand_contiguous_check_, "Contiguous"},
      {wand_sample_all_layers_check_, "Sample All Layers"},
      {fill_shapes, "Fill"},
      {text_bold_button_, "B"},
      {text_italic_button_, "I"},
      {text_color_button_, "T"},
      {text_align_left_button_, "L"},
      {text_align_center_button_, "C"},
      {text_align_right_button_, "R"},
  };
  for (const auto& [widget, source] : translated_widgets) {
    bind_widget_text(widget, source);
  }
  retranslate_brush_preset_combo();
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
  bind_widget_text(layers_dock, "Layers");
  layers_dock->setMinimumHeight(300);
  auto* layers_panel = new QWidget(layers_dock);
  layers_panel->setObjectName(QStringLiteral("layersPanel"));
  layers_panel->setMinimumHeight(240);
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
  layer_list->set_thumbnail_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target,
                                                  Qt::KeyboardModifiers modifiers) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    auto* layer = document().find_layer(id);
    if (layer == nullptr) {
      return;
    }
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::ShiftModifier) != 0) {
      const auto disabled = std::as_const(*layer).mask().has_value() && std::as_const(*layer).mask()->disabled;
      document().set_active_layer(id);
      set_active_layer_mask_disabled(!disabled);
      return;
    }
    const auto was_active = document().active_layer_id().has_value() && *document().active_layer_id() == id;
    document().set_active_layer(id);
    restyle_layer_rows(layer_list_);
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::AltModifier) != 0) {
      const auto showing_mask =
          was_active && canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale;
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
      canvas_->set_mask_display_mode(showing_mask ? CanvasWidget::MaskDisplayMode::None
                                                  : CanvasWidget::MaskDisplayMode::Grayscale);
      refresh_layer_controls();
      statusBar()->showMessage(showing_mask
                                   ? tr("Editing layer mask")
                                   : tr("Showing the layer mask. Alt-click the mask thumbnail to return."));
      return;
    }
    const auto editing_mask_already =
        was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask;
    set_layer_edit_target_ui(target == LayerCtrlClickTarget::MaskThumbnail && !editing_mask_already
                                 ? CanvasWidget::LayerEditTarget::Mask
                                 : CanvasWidget::LayerEditTarget::Content,
                             true);
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(250);
  layer_list_->setMinimumHeight(120);
  layer_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  layer_list_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  layer_list_->setTextElideMode(Qt::ElideNone);
  layer_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layer_list_->setDragEnabled(true);
  layer_list_->setAcceptDrops(true);
  layer_list_->setDropIndicatorShown(true);
  layer_list_->setDragDropOverwriteMode(false);
  layer_list_->setDefaultDropAction(Qt::MoveAction);
  layer_list_->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list_, &QListWidget::itemSelectionChanged, this, [this] { set_active_layer_from_selection(); });
  layer_list->set_item_double_click_callback([this](QListWidgetItem*) {
    auto& doc = document();
    const auto active = doc.active_layer_id();
    const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    if (layer != nullptr && layer->kind() == LayerKind::Group) {
      // Layer styles only render for pixel content, so the blending dialog is
      // useless on a folder.
      return;
    }
    if (layer != nullptr && layer->kind() == LayerKind::Adjustment) {
      edit_active_adjustment_layer();
      return;
    }
    edit_active_layer_style();
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
  bind_widget_text(mode_label, "Mode");
  layer_control_grid->addWidget(mode_label, 0, 0);
  blend_combo_ = new QComboBox(layers_panel);
  add_blend_mode_items(blend_combo_);
  blend_combo_->setObjectName(QStringLiteral("layerBlendModeCombo"));
  layer_control_grid->addWidget(blend_combo_, 0, 1, 1, 2);
  connect(blend_combo_, &QComboBox::currentIndexChanged, this, [this](int index) { set_active_layer_blend(index); });
  register_document_widget(blend_combo_);

  auto* opacity_label = new QLabel(tr("Opacity"), layers_panel);
  bind_widget_text(opacity_label, "Opacity");
  layer_control_grid->addWidget(opacity_label, 1, 0);
  opacity_slider_ = new QSlider(Qt::Horizontal, layers_panel);
  opacity_slider_->setObjectName(QStringLiteral("layerOpacitySlider"));
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
  connect(opacity_slider_, &QSlider::sliderReleased, this, [this] { finish_pending_layer_opacity_edit(); });
  connect(opacity_spin_, &QSpinBox::editingFinished, this, [this] { finish_pending_layer_opacity_edit(); });
  register_document_widget(opacity_slider_);
  register_document_widget(opacity_spin_);

  auto* lock_label = new QLabel(tr("Lock"), layers_panel);
  bind_widget_text(lock_label, "Lock");
  layer_control_grid->addWidget(lock_label, 2, 0);
  auto* lock_controls = new QHBoxLayout();
  lock_controls->setContentsMargins(0, 0, 0, 0);
  lock_controls->setSpacing(4);
  const auto make_lock_button = [this, layers_panel](const QString& object_name, const QString& icon_key,
                                                     const QString& tooltip, LayerLockFlags flag) {
    auto* button = new QToolButton(layers_panel);
    button->setObjectName(object_name);
    button->setProperty("layerLockControl", true);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(simple_icon(icon_key, QColor(226, 232, 240)));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(tooltip);
    button->setFixedSize(24, 24);
    connect(button, &QToolButton::toggled, this, [this, flag](bool checked) {
      set_active_layer_lock_flag(flag, checked);
    });
    register_document_widget(button);
    return button;
  };
  lock_transparent_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockTransparentButton"), QStringLiteral("AL"),
                       tr("Lock transparent pixels"), kLayerLockTransparentPixels);
  lock_image_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockPixelsButton"), QStringLiteral("fill"),
                       tr("Lock image pixels"), kLayerLockImagePixels);
  lock_position_button_ =
      make_lock_button(QStringLiteral("layerLockPositionButton"), QStringLiteral("TR"),
                       tr("Lock position"), kLayerLockPosition);
  lock_all_button_ = new QToolButton(layers_panel);
  lock_all_button_->setObjectName(QStringLiteral("layerLockAllButton"));
  lock_all_button_->setProperty("layerLockControl", true);
  lock_all_button_->setCheckable(true);
  lock_all_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  lock_all_button_->setIcon(simple_icon(QStringLiteral("lock"), QColor(226, 232, 240)));
  lock_all_button_->setIconSize(QSize(15, 15));
  lock_all_button_->setToolTip(tr("Lock all"));
  lock_all_button_->setFixedSize(24, 24);
  connect(lock_all_button_, &QToolButton::toggled, this, [this](bool checked) { set_active_layer_lock_all(checked); });
  register_document_widget(lock_all_button_);
  lock_controls->addWidget(lock_transparent_pixels_button_);
  lock_controls->addWidget(lock_image_pixels_button_);
  lock_controls->addWidget(lock_position_button_);
  lock_controls->addWidget(lock_all_button_);
  lock_controls->addStretch(1);
  layer_control_grid->addLayout(lock_controls, 2, 1, 1, 2);
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
  add_button->setObjectName(QStringLiteral("layerNewButton"));
  adjustment_button->setObjectName(QStringLiteral("layerNewAdjustmentButton"));
  add_folder_button->setObjectName(QStringLiteral("layerNewFolderButton"));
  duplicate_button->setObjectName(QStringLiteral("layerDuplicateButton"));
  rename_button->setObjectName(QStringLiteral("layerRenameButton"));
  delete_button->setObjectName(QStringLiteral("layerDeleteButton"));
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
  add_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  add_folder_button->setProperty("layerDropAction", QStringLiteral("folder"));
  duplicate_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  delete_button->setProperty("layerDropAction", QStringLiteral("delete"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, delete_button}) {
    button->setAcceptDrops(true);
    button->installEventFilter(this);
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
  for (auto* widget : {static_cast<QWidget*>(add_button), static_cast<QWidget*>(add_folder_button),
                       static_cast<QWidget*>(adjustment_button), static_cast<QWidget*>(duplicate_button),
                       static_cast<QWidget*>(rename_button), static_cast<QWidget*>(delete_button)}) {
    register_document_widget(widget);
  }

  layers_dock->setWidget(layers_panel);
  install_collapsible_dock_title(layers_dock, layers_panel, QStringLiteral("layers"), 300);
  layers_dock->setProperty("patchy.rightDockResizeHost", true);
  layers_dock->installEventFilter(this);
  auto* right_dock_resize_handle = new QWidget(layers_dock);
  right_dock_resize_handle->setObjectName(QStringLiteral("rightDockResizeHandle"));
  right_dock_resize_handle->setProperty("patchy.rightDockResizeHandle", true);
  right_dock_resize_handle->setAttribute(Qt::WA_StyledBackground, true);
  right_dock_resize_handle->setCursor(Qt::SplitHCursor);
  right_dock_resize_handle->installEventFilter(this);
  addDockWidget(Qt::RightDockWidgetArea, layers_dock);
  update_right_dock_resize_handle_geometry(layers_dock);

  auto* history_dock = new QDockWidget(tr("History"), this);
  history_dock->setObjectName(QStringLiteral("historyDock"));
  bind_widget_text(history_dock, "History");
  history_list_ = new QListWidget(history_dock);
  history_list_->setObjectName(QStringLiteral("historyList"));
  history_dock->setWidget(history_list_);
  install_collapsible_dock_title(history_dock, history_list_, QStringLiteral("history"), 0, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, history_dock);

  auto* properties_dock = new QDockWidget(tr("Properties"), this);
  properties_dock->setObjectName(QStringLiteral("propertiesDock"));
  bind_widget_text(properties_dock, "Properties");
  auto* properties_scroll = new QScrollArea(properties_dock);
  properties_scroll->setObjectName(QStringLiteral("propertiesScrollArea"));
  properties_scroll->setFrameShape(QFrame::NoFrame);
  properties_scroll->setWidgetResizable(true);
  properties_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* properties_panel = new QWidget(properties_dock);
  properties_panel->setObjectName(QStringLiteral("propertiesPanel"));
  auto* properties_layout = new QVBoxLayout(properties_panel);
  properties_layout->setContentsMargins(6, 6, 6, 6);
  properties_layout->setSpacing(4);
  const auto add_properties_label = [properties_panel, properties_layout](const QString& object_name) {
    auto* label = new QLabel(properties_panel);
    label->setObjectName(object_name);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    label->hide();
    properties_layout->addWidget(label);
    return label;
  };
  document_info_label_ = new QLabel(properties_panel);
  document_info_label_->setObjectName(QStringLiteral("documentInfoLabel"));
  document_info_label_->setWordWrap(false);
  document_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  document_info_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  properties_layout->addWidget(document_info_label_);
  active_layer_info_label_ = add_properties_label(QStringLiteral("activeLayerInfoLabel"));
  active_layer_geometry_label_ = add_properties_label(QStringLiteral("activeLayerGeometryLabel"));
  active_layer_mask_label_ = add_properties_label(QStringLiteral("activeLayerMaskLabel"));
  active_layer_adjustment_label_ = add_properties_label(QStringLiteral("activeLayerAdjustmentLabel"));
  active_layer_text_label_ = add_properties_label(QStringLiteral("activeLayerTextLabel"));
  active_tool_info_label_ = add_properties_label(QStringLiteral("activeToolInfoLabel"));
  properties_layout->addStretch(0);
  properties_scroll->setWidget(properties_panel);
  properties_dock->setWidget(properties_scroll);
  install_collapsible_dock_title(properties_dock, properties_scroll, QStringLiteral("properties"), 0, 230, false);
  addDockWidget(Qt::RightDockWidgetArea, properties_dock);

  auto* info_dock = new QDockWidget(tr("Info"), this);
  info_dock->setObjectName(QStringLiteral("infoDock"));
  bind_widget_text(info_dock, "Info");
  auto* info_panel = new QWidget(info_dock);
  auto* info_layout = new QVBoxLayout(info_panel);
  info_layout->setContentsMargins(8, 8, 8, 8);
  canvas_info_label_ = new QLabel(info_panel);
  canvas_info_label_->setObjectName(QStringLiteral("canvasInfoLabel"));
  canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
  canvas_info_label_->setWordWrap(true);
  canvas_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  info_layout->addWidget(canvas_info_label_);
  info_layout->addStretch(1);
  info_dock->setWidget(info_panel);
  install_collapsible_dock_title(info_dock, info_panel, QStringLiteral("info"), 0, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, info_dock);

  create_swatches_dock();
}

void MainWindow::create_swatches_dock() {
  auto* swatches_dock = new QDockWidget(tr("Swatches"), this);
  swatches_dock->setObjectName(QStringLiteral("swatchesDock"));
  bind_widget_text(swatches_dock, "Swatches");
  auto* swatches_panel = new QWidget(swatches_dock);
  auto* swatches_layout = new QGridLayout(swatches_panel);
  swatches_layout->setContentsMargins(6, 6, 6, 6);
  swatches_layout->setSpacing(4);

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
    swatches_layout->addWidget(button, index / 8, index % 8);
    connect(button, &QPushButton::clicked, this, [this, color] {
      if (canvas_ == nullptr) {
        return;
      }
      canvas_->set_primary_color(color);
      apply_primary_color_to_active_text_editor(color);
      refresh_color_buttons();
      statusBar()->showMessage(tr("Foreground color changed"));
    });
    ++index;
  }

  swatches_dock->setWidget(swatches_panel);
  install_collapsible_dock_title(swatches_dock, swatches_panel, QStringLiteral("swatches"), 0, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, swatches_dock);
}

void MainWindow::configure_canvas(CanvasWidget* canvas) {
  canvas->setObjectName(QStringLiteral("canvas"));
  apply_canvas_aid_settings(canvas);
  apply_pen_input_settings(canvas);
  canvas->set_before_edit_callback([this](QString label) { push_undo_snapshot(std::move(label)); });
  canvas->set_selection_history_callback(
      [this](QString label, CanvasWidget::SelectionSnapshot before, bool coalesce) {
        push_selection_history(std::move(label), std::move(before), coalesce);
      });
  canvas->set_selection_mode_changed_callback([this, canvas](CanvasWidget::SelectionMode mode) {
    if (canvas == canvas_) {
      update_selection_mode_buttons(mode);
    }
  });
  canvas->set_color_picked_callback([this, canvas](QColor color) {
    canvas->set_primary_color(color);
    refresh_color_buttons();
    statusBar()->showMessage(tr("Picked color"));
  });
  canvas->set_pen_button_action_callback(
      [this](PenButtonAction action) { handle_pen_button_action(action); });
  canvas->set_brush_settings_changed_callback([this, canvas] {
    if (canvas != canvas_) {
      return;
    }
    sync_brush_controls_from_canvas();
    refresh_gradient_controls_from_canvas();
    save_tool_settings();
    refresh_document_info();
  });
  canvas->set_text_requested_callback([this](QPoint point, QRect requested_text_box) {
    add_text_at(point, requested_text_box);
  });
  canvas->set_active_layer_changed_callback([this](LayerId layer_id) {
    reveal_layer_in_layer_list(layer_id);
    refresh_layer_controls();
    refresh_options_bar();
  });
  canvas->set_status_callback([this](QString message) { statusBar()->showMessage(message); });
  canvas->set_info_callback([this](CanvasInfoState info) { update_canvas_info(std::move(info)); });
  canvas->set_document_changed_callback([this, canvas](CanvasWidget::DocumentChangeReason reason) {
    if (canvas != canvas_) {
      return;
    }
    if (reason == CanvasWidget::DocumentChangeReason::BrushStrokePreview) {
      pending_layer_thumbnail_refresh_ = true;
      return;
    }
    if (pending_layer_thumbnail_refresh_ || reason == CanvasWidget::DocumentChangeReason::BrushStrokeFinished) {
      pending_layer_thumbnail_refresh_ = false;
    }
    refresh_layer_thumbnails();
  });
  canvas->set_view_changed_callback([this, canvas] { handle_canvas_view_changed(canvas); });
  canvas->set_transform_controls_changed_callback([this, canvas] {
    if (canvas == canvas_) {
      refresh_options_bar();
    }
  });
  canvas->set_text_layer_transform_render_callback([this, canvas](LayerId id) -> bool {
    auto* session = session_for_canvas(canvas);
    if (session == nullptr) {
      return false;
    }
    auto* layer = session->document.find_layer(id);
    if (layer == nullptr || !layer_is_text(*layer)) {
      return false;
    }
    const auto transform = canonical_text_affine_transform_for_layer(*layer);
    if (!transform.has_value()) {
      return false;
    }
    if (layer->metadata().contains(kLayerMetadataPsdTextTransform)) {
      // PSD type layers anchor their transform at the typographic baseline, so rendering the glyph
      // raster (top-left origin) through it directly would drop the text ~one ascent.  Render crisp
      // through the glyph-top-aligned transform instead (the same alignment the editor uses), keeping
      // the imported size + matrix representation.  Falls back to the resampled bitmap when it can't be
      // aligned (box text, missing PSD glyph metrics, or a pure move with no scale).
      //
      // Only do this when the original font is installed: re-rasterizing a missing font would
      // substitute a generic face (e.g. Arial), so the decorative type would change appearance on
      // scale.  In that case keep the resampled bitmap, which preserves the imported glyph shapes.
      const auto font_value = layer->metadata().find(kLayerMetadataTextFont);
      const QString family =
          font_value == layer->metadata().end() ? QString() : QString::fromStdString(font_value->second);
      const auto runs_value = layer->metadata().find(kLayerMetadataTextRuns);
      const QString runs =
          runs_value == layer->metadata().end() ? QString() : QString::fromStdString(runs_value->second);
      const bool font_substituted = family.trimmed().isEmpty() ||
                                    family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0 ||
                                    !missing_text_families_for_psd_raster_preview(family, runs).isEmpty();
      if (font_substituted) {
        return false;
      }
      const auto flow = layer->metadata().find(kLayerMetadataTextFlow);
      const bool boxed =
          flow != layer->metadata().end() && text_flow_is_box(QString::fromStdString(flow->second));
      const auto base_pixels = render_text_layer_pixels_from_metadata(*layer);
      if (!base_pixels.has_value()) {
        return false;
      }
      const auto aligned = psd_point_text_local_bounds_transform_for_pixels(*layer, *base_pixels, boxed,
                                                                            layer_anchor_alignment_factor(*layer));
      if (!aligned.has_value()) {
        return false;
      }
      auto rendered = render_text_layer_pixels_through_transform(*layer, qtransform_from_affine(*aligned));
      if (!rendered.has_value()) {
        return false;
      }
      layer->set_pixels(std::move(rendered->pixels));
      layer->set_bounds(rendered->bounds);
      return true;
    }
    // Patchy-authored text: fold the transform's scale into the point size (so the Type panel and a
    // saved PSD show the new size, matching imported text), then re-rasterize through the residual.
    const auto residual = fold_text_transform_scale_into_font_size(*layer, qtransform_from_affine(*transform));
    auto rendered = render_text_layer_pixels_through_transform(*layer, residual);
    if (!rendered.has_value()) {
      return false;
    }
    layer->set_pixels(std::move(rendered->pixels));
    layer->set_bounds(rendered->bounds);
    return true;
  });
}

void MainWindow::add_document_session(Document document, QString title, QString path) {
  auto session = std::make_unique<DocumentSession>();
  session->document = std::move(document);
  if (session->document.guides().empty() && session->document.grid_settings().horizontal_cycle_32 == 576 &&
      session->document.grid_settings().vertical_cycle_32 == 576) {
    session->document.grid_settings().horizontal_cycle_32 = view_grid_spacing_32_;
    session->document.grid_settings().vertical_cycle_32 = view_grid_spacing_32_;
  }
  session->title = std::move(title);
  session->path = std::move(path);
  collect_initially_collapsed_layer_groups(session->document.layers(), session->collapsed_layer_groups);
  session->canvas = new CanvasWidget(document_tabs_);
  session->canvas->setAcceptDrops(true);
  session->canvas->installEventFilter(this);
  configure_canvas(session->canvas);
  session->canvas->set_document(&session->document);
  const bool used_default_tool_settings = canvas_ == nullptr;
  if (!used_default_tool_settings) {
    session->canvas->set_brush_size(canvas_->brush_size());
    session->canvas->set_brush_opacity(canvas_->brush_opacity());
    session->canvas->set_brush_softness(canvas_->brush_softness());
    session->canvas->set_wand_tolerance(canvas_->wand_tolerance());
    session->canvas->set_wand_contiguous(canvas_->wand_contiguous());
    session->canvas->set_wand_sample_all_layers(canvas_->wand_sample_all_layers());
    session->canvas->set_clone_aligned(canvas_->clone_aligned());
    session->canvas->set_gradient_method(canvas_->gradient_method());
    session->canvas->set_gradient_reverse(canvas_->gradient_reverse());
    session->canvas->set_gradient_opacity(canvas_->gradient_opacity());
    session->canvas->set_gradient_stops(canvas_->gradient_stops());
    session->canvas->set_show_transform_controls(canvas_->show_transform_controls());
  } else if (const auto* preset = find_brush_preset(default_startup_brush_preset_id()); preset != nullptr) {
    apply_brush_preset(*session->canvas, *preset);
  }
  if (move_auto_select_check_ != nullptr) {
    session->canvas->set_auto_select_layer(move_auto_select_check_->isChecked());
  }
  if (move_show_transform_controls_check_ != nullptr) {
    session->canvas->set_show_transform_controls(move_show_transform_controls_check_->isChecked());
  }
  if (clone_aligned_check_ != nullptr) {
    session->canvas->set_clone_aligned(clone_aligned_check_->isChecked());
  }
  apply_selection_modes_to_canvas(session->canvas);
  session->canvas->set_tool(current_tool_);
  session->canvas->set_marquee_style(current_marquee_style_);
  session->canvas->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  session->canvas->set_selection_feather_radius(current_selection_feather_radius_);
  session->canvas->set_selection_antialias(current_selection_antialias_);
  apply_canvas_aid_settings(session->canvas);

  auto* canvas = session->canvas;
  const auto tab_title = session->title;
  sessions_.push_back(std::move(session));
  const auto tab_index = document_tabs_->addTab(canvas, tab_title);
  document_tabs_->setCurrentIndex(tab_index);
  canvas_ = canvas;
  pending_layer_thumbnail_refresh_ = false;
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  if (used_default_tool_settings) {
    if (brush_preset_combo_ != nullptr) {
      const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
      if (preset_index >= 0) {
        brush_preset_combo_->setCurrentIndex(preset_index);
      }
    }
    sync_brush_controls_from_canvas();
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_file_path_actions();
  update_undo_redo_actions();
  update_document_action_state();
  refresh_document_tab_titles();
}

void MainWindow::activate_document_tab(int index) {
  if (preview_dialog_edit_locked() && document_tabs_ != nullptr && index != preview_dialog_edit_lock_tab_index_) {
    if (preview_dialog_edit_lock_tab_index_ >= 0 && preview_dialog_edit_lock_tab_index_ < document_tabs_->count()) {
      QSignalBlocker blocker(document_tabs_);
      document_tabs_->setCurrentIndex(preview_dialog_edit_lock_tab_index_);
    }
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto* canvas = index >= 0 ? dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)) : nullptr;
  // Brush settings are application-wide: capture the outgoing canvas's live
  // values so the incoming canvas (whose copies may be stale) inherits them.
  const auto canvas_changed = canvas != canvas_;
  if (canvas_changed) {
    stash_active_brush_settings();
  }
  if (canvas == nullptr || session_for_canvas(canvas) == nullptr) {
    canvas_ = nullptr;
    refresh_options_bar();
    refresh_layer_list();
    refresh_layer_controls();
    refresh_document_info();
    refresh_color_buttons();
    update_file_path_actions();
    update_undo_redo_actions();
    update_document_action_state();
    refresh_document_window_title();
    return;
  }
  canvas_ = canvas;
  pending_layer_thumbnail_refresh_ = false;
  apply_selection_modes_to_canvas(canvas_);
  canvas_->set_tool(current_tool_);
  canvas_->set_marquee_style(current_marquee_style_);
  canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  canvas_->set_selection_feather_radius(current_selection_feather_radius_);
  canvas_->set_selection_antialias(current_selection_antialias_);
  if (canvas_changed) {
    apply_active_brush_settings_to_canvas();
    sync_brush_controls_from_canvas();
  }
  apply_canvas_aid_settings(canvas_);
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->refresh_info_display();
  update_file_path_actions();
  update_undo_redo_actions();
  update_document_action_state();
  refresh_document_window_title();
}

bool MainWindow::close_document_tab(int index) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return false;
  }
  if (document_tabs_ == nullptr || index < 0 || index >= document_tabs_->count()) {
    return false;
  }
  // Commit any in-progress inline text edit while the canvas is still active:
  // the pending text belongs in the save-changes decision below, and an editor
  // that survives into removeTab() auto-commits on the focus change mid
  // teardown, after activate_document_tab() has already cleared canvas_.
  finish_active_text_editor();
  if (index >= document_tabs_->count()) {
    return false;
  }
  auto* widget = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [widget](const auto& candidate) {
    return candidate->canvas == widget;
  });
  if (found == sessions_.end()) {
    return false;
  }
  if (!confirm_close_session(**found)) {
    return false;
  }
  document_tabs_->removeTab(index);
  sessions_.erase(found);
  delete widget;
  activate_document_tab(document_tabs_->currentIndex());
  refresh_document_tab_titles();
  update_document_action_state();
  if (sessions_.empty() && statusBar() != nullptr) {
    statusBar()->showMessage(tr("No document"));
  }
  return true;
}

void MainWindow::close_other_document_tabs(int index) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (document_tabs_ == nullptr || index < 0 || index >= document_tabs_->count()) {
    return;
  }
  auto* keep_widget = document_tabs_->widget(index);
  for (int candidate = document_tabs_->count() - 1; candidate >= 0; --candidate) {
    if (document_tabs_->widget(candidate) == keep_widget) {
      continue;
    }
    if (!close_document_tab(candidate)) {
      break;
    }
  }
  const auto keep_index = document_tabs_->indexOf(keep_widget);
  if (keep_index >= 0) {
    document_tabs_->setCurrentIndex(keep_index);
  }
}

void MainWindow::close_all_document_tabs() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (document_tabs_ == nullptr) {
    return;
  }
  for (int index = document_tabs_->count() - 1; index >= 0; --index) {
    if (!close_document_tab(index)) {
      break;
    }
  }
}

void MainWindow::show_document_tab_context_menu(const QPoint& position) {
  if (document_tabs_ == nullptr) {
    return;
  }
  auto* tab_bar = document_tabs_->findChild<QTabBar*>();
  if (tab_bar == nullptr) {
    return;
  }

  const auto tab_index = tab_bar->tabAt(position);
  if (tab_index < 0 || tab_index >= document_tabs_->count()) {
    return;
  }

  QMenu menu(tab_bar);
  menu.setObjectName(QStringLiteral("documentTabContextMenu"));
  auto* close_action = menu.addAction(tr("Close"));
  auto* close_others_action = menu.addAction(tr("Close Others"));
  auto* close_all_action = menu.addAction(tr("Close All"));
  close_action->setObjectName(QStringLiteral("documentTabCloseAction"));
  close_others_action->setObjectName(QStringLiteral("documentTabCloseOthersAction"));
  close_all_action->setObjectName(QStringLiteral("documentTabCloseAllAction"));
  close_others_action->setEnabled(document_tabs_->count() > 1);

  connect(close_action, &QAction::triggered, this, [this, tab_index] { close_document_tab(tab_index); });
  connect(close_others_action, &QAction::triggered, this,
          [this, tab_index] { close_other_document_tabs(tab_index); });
  connect(close_all_action, &QAction::triggered, this, [this] { close_all_document_tabs(); });
  menu.exec(tab_bar->mapToGlobal(position));
}

bool MainWindow::confirm_close_session(DocumentSession& target_session) {
  if (!session_is_modified(target_session)) {
    return true;
  }

  const auto title = target_session.title.isEmpty() ? tr("Untitled") : target_session.title;
  const auto answer = show_warning_message(this, tr("Save changes?"),
                                           tr("Save changes to %1 before closing?").arg(title),
                                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                           QMessageBox::Save, QStringLiteral("saveChangesMessageBox"));
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
  refresh_document_window_title();
}

void MainWindow::refresh_document_window_title() {
  if (!has_active_document()) {
    setWindowTitle(QStringLiteral("Patchy"));
    return;
  }

  const auto& active_session = session();
  auto title = active_session.title.isEmpty() ? tr("Untitled") : active_session.title;
  if (session_is_modified(active_session)) {
    title.append(QStringLiteral("*"));
  }
  setWindowTitle(title);
}

void MainWindow::set_session_saved(DocumentSession& target_session) {
  target_session.saved_revision = target_session.revision;
  refresh_document_tab_titles();
  update_file_path_actions();
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

bool MainWindow::has_active_document() const noexcept {
  if (document_tabs_ == nullptr) {
    return false;
  }
  return session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget())) != nullptr;
}

void MainWindow::reset_document(std::int32_t width, std::int32_t height, QColor background, QString history_label) {
  Document new_document(width, height, PixelFormat::rgb8());
  const auto background_format = background.alpha() == 0 ? PixelFormat::rgba8() : PixelFormat::rgb8();
  auto& background_layer = new_document.add_pixel_layer("Background",
                                                        make_solid_pixels(new_document.width(), new_document.height(),
                                                                          background, background_format));
  set_layer_locks_position(background_layer, true);
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

void MainWindow::create_clipboard_document(const QImage& image, QString history_label) {
  if (image.isNull()) {
    statusBar()->showMessage(tr("Clipboard does not contain an image"));
    return;
  }

  auto pixels = pixels_from_image_rgba(image);
  Document new_document(pixels.width(), pixels.height(), PixelFormat::rgba8());
  new_document.add_pixel_layer(tr("Clipboard Image").toStdString(), std::move(pixels));
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
  statusBar()->showMessage(tr("Created %1 x %2 document").arg(image.width()).arg(image.height()));
}

void MainWindow::create_new_document() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto settings = request_new_document_settings(this);
  if (!settings.has_value()) {
    return;
  }
  if (settings->from_clipboard) {
    create_clipboard_document(settings->clipboard_image, tr("New document"));
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
    const auto resolution = static_cast<double>(settings->resolution);
    if (std::abs(doc.print_settings().horizontal_ppi - resolution) > 0.01 ||
        std::abs(doc.print_settings().vertical_ppi - resolution) > 0.01) {
      push_undo_snapshot(tr("Print resolution"));
      doc.print_settings().horizontal_ppi = resolution;
      doc.print_settings().vertical_ppi = resolution;
      refresh_document_info();
    }
    statusBar()->showMessage(tr("Image size unchanged; print resolution set to %1 ppi").arg(settings->resolution));
    return;
  }
  const auto resolution = static_cast<double>(settings->resolution);
  const bool dimensions_changed = settings->width != doc.width() || settings->height != doc.height();
  const bool resolution_changed = std::abs(doc.print_settings().horizontal_ppi - resolution) > 0.01 ||
                                  std::abs(doc.print_settings().vertical_ppi - resolution) > 0.01;
  if (!dimensions_changed && !resolution_changed) {
    return;
  }

  push_undo_snapshot(tr("Image size"));
  if (dimensions_changed) {
    resize_image_and_layers(doc, settings->width, settings->height);
    canvas_->clear_selection();
    canvas_->set_document(&doc);
    refresh_layer_list();
    refresh_layer_controls();
  }
  doc.print_settings().horizontal_ppi = resolution;
  doc.print_settings().vertical_ppi = resolution;
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
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto path = get_open_file_name(this, tr("Open"), last_open_directory(), open_file_filter(), nullptr,
                                       QStringLiteral("openFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  open_document_path(path);
}

bool MainWindow::accept_open_file_drag(QDropEvent* event) {
  if (preview_dialog_edit_locked()) {
    if (event != nullptr) {
      event->ignore();
    }
    show_preview_dialog_edit_lock_message();
    return false;
  }
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
  if (preview_dialog_edit_locked()) {
    if (event != nullptr) {
      event->ignore();
    }
    show_preview_dialog_edit_lock_message();
    return false;
  }
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

void MainWindow::open_command_line_files(const QStringList& paths) {
  for (const auto& path : paths) {
    if (path.isEmpty()) {
      continue;
    }
    open_document_path(QFileInfo(path).absoluteFilePath());
  }
}

void MainWindow::activate_for_second_instance(const QStringList& paths) {
  // Restore from a minimized/hidden state and pull the existing window in front so the user sees the
  // file they just double-clicked open in this instance rather than a new process.
  if (isMinimized()) {
    setWindowState(windowState() & ~Qt::WindowMinimized);
  }
  if (!isVisible()) {
    show();
  }
  raise();
  activateWindow();
  open_command_line_files(paths);
}

void MainWindow::open_document_path(QString path) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  try {
    const auto info = QFileInfo(path);

    QProgressDialog progress(tr("Opening %1...").arg(info.fileName()), QString(), 0, 0, this);
    progress.setObjectName(QStringLiteral("openProgressDialog"));
    progress.setWindowTitle(tr("Opening %1").arg(elided_open_progress_title_file_name(progress, info.fileName())));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    remember_dialog_position(progress);
    progress.show();
    progress.raise();
    progress.activateWindow();
    QApplication::processEvents();
    const auto close_progress = qScopeGuard([&progress] {
      progress.close();
      QApplication::processEvents();
    });
    Q_UNUSED(close_progress);

    auto open_future = std::async(std::launch::async, [path] { return load_document_from_path(path); });
    while (open_future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
      QApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    auto loaded = open_future.get();
    progress.close();
    QApplication::processEvents();

    add_document_session(std::move(loaded.document), loaded.file_name, path);
    if (is_photoshop_document_extension(loaded.extension) &&
        app_settings().value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool()) {
      show_compatibility_report(this, document(), loaded.file_name);
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
    remember_open_directory_for_path(path);
    add_recent_folder(QFileInfo(path).absolutePath());
    statusBar()->showMessage(tr("Opened %1").arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Open failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("openFailedMessageBox"));
  }
}

bool MainWindow::save_document() {
  if (!has_active_document()) {
    statusBar()->showMessage(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  if (session().path.isEmpty()) {
    return save_document_as();
  }
  return save_document_to_path(session().path);
}

bool MainWindow::save_document_as() {
  if (!has_active_document()) {
    statusBar()->showMessage(tr("No document"));
    return false;
  }
  finish_active_text_editor();
  const auto fallback_name = session().title.isEmpty() ? tr("Untitled.psd") : session().title;
  const auto initial_path = file_dialog_initial_path(session().path, fallback_name);
  auto selected_filter = save_file_filter_for_path(initial_path);
  auto path = get_save_file_name(this, tr("Save As"), initial_path, save_file_filter(), &selected_filter,
                                 QStringLiteral("saveAsFileDialog"), recent_files_);
  if (path.isEmpty()) {
    return false;
  }
  path = path_with_default_extension(path, selected_filter);
  const auto extension = extension_for_path(path);
  std::optional<ImageSaveOptions> image_options;
  if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
    image_options = prompt_image_save_options(this, extension, load_image_save_option_defaults());
    if (!image_options.has_value()) {
      return false;
    }
  }
  return save_document_to_path(path, image_options);
}

bool MainWindow::save_document_to_path(QString path, std::optional<ImageSaveOptions> image_options) {
  finish_active_text_editor();
  try {
    const auto extension = extension_for_path(path);
    auto effective_image_options = image_options.value_or(load_image_save_option_defaults());
    if (!image_options.has_value() && image_save_options_apply_to_extension(extension)) {
      const auto& active_session = session();
      if (active_session.image_save_options.has_value() && active_session.image_save_options_path == path &&
          active_session.image_save_options_extension == extension) {
        effective_image_options = *active_session.image_save_options;
      }
    }

    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_layered_rgb8_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension, effective_image_options);
    }
    auto& active_session = session();
    active_session.path = path;
    active_session.title = QFileInfo(path).fileName();
    remember_save_directory_for_path(path);
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      active_session.image_save_options = effective_image_options;
      active_session.image_save_options_path = path;
      active_session.image_save_options_extension = extension;
      save_image_save_option_defaults(effective_image_options);
    } else {
      active_session.image_save_options.reset();
      active_session.image_save_options_path.clear();
      active_session.image_save_options_extension.clear();
    }
    set_session_saved(active_session);
    update_history(tr("Save"));
    add_recent_file(path);
    statusBar()->showMessage(tr("Saved %1").arg(path));
    return true;
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Save failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("saveFailedMessageBox"));
  }
  return false;
}

void MainWindow::export_flat_image() {
  if (!has_active_document()) {
    statusBar()->showMessage(tr("No document"));
    return;
  }
  finish_active_text_editor();
  QString selected_filter;
  const auto base_name = QFileInfo(session().title.isEmpty() ? tr("Untitled") : session().title).completeBaseName();
  auto path =
      get_save_file_name(this, tr("Export Flat Image"),
                         file_dialog_initial_path(QString(), base_name + QStringLiteral(".png")),
                         export_image_filter(), &selected_filter, QStringLiteral("exportFlatImageFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);

  try {
    const auto extension = extension_for_path(path);
    std::optional<ImageSaveOptions> image_options;
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      image_options = prompt_image_save_options(this, extension, load_image_save_option_defaults());
      if (!image_options.has_value()) {
        return;
      }
    }
    const auto effective_image_options = image_options.value_or(load_image_save_option_defaults());
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_flat_rgb8_file(document(), path.toStdString());
    } else {
      write_flat_image_file(document(), path, extension, effective_image_options);
    }
    if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
      save_image_save_option_defaults(effective_image_options);
    }
    remember_save_directory_for_path(path);
    update_history(tr("Export flat image"));
    statusBar()->showMessage(tr("Exported %1").arg(path));
  } catch (const std::exception& error) {
    show_critical_message(this, tr("Export failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("exportFailedMessageBox"));
  }
}

void MainWindow::page_setup() {
  run_page_setup_dialog(this, &print_page_layout_);
}

void MainWindow::print_document() {
  if (!has_active_document()) {
    statusBar()->showMessage(tr("No document"));
    return;
  }
  std::optional<QRect> selection_bounds;
  if (canvas_ != nullptr) {
    selection_bounds = canvas_->selected_document_rect();
  }
  if (run_print_dialog(this, document(), selection_bounds, &print_page_layout_)) {
    statusBar()->showMessage(tr("Print output created"));
  }
}

void MainWindow::show_update_available(const UpdateInfo& update) {
  QMessageBox dialog(QMessageBox::Information, tr("Update Available"),
                     tr("Patchy %1 is available. You are using version %2.\n\n"
                        "Save your work and close Patchy before running the installer.")
                         .arg(update.version, QStringLiteral(PATCHY_VERSION)),
                     QMessageBox::NoButton, this);
  dialog.setObjectName(QStringLiteral("updateAvailableMessageBox"));
  auto* download_button = dialog.addButton(tr("Download"), QMessageBox::AcceptRole);
  dialog.addButton(tr("Not Now"), QMessageBox::RejectRole);
  dialog.setDefaultButton(download_button);

  exec_dialog(dialog);
  if (dialog.clickedButton() == download_button && !QDesktopServices::openUrl(update.download_url)) {
    statusBar()->showMessage(tr("Could not open the download link"));
  }
}

void MainWindow::show_preferences() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("patchyPreferencesDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("Preferences"));

  auto settings = app_settings();
  dialog.setMinimumSize(650, 430);
  dialog.resize(700, 560);

  const auto make_tab_page = [](QWidget* parent) {
    // Wrap each tab in a scroll area so a tab whose content is taller than the
    // dialog scrolls instead of overlapping its own controls.
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("preferencesTabScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("preferencesTabPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    scroll->setWidget(page);
    return std::pair<QWidget*, QVBoxLayout*>{scroll, layout};
  };
  const auto configure_panel = [](QFrame* panel) {
    panel->setProperty("preferencesPanel", true);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  };
  const auto configure_form = [](QFormLayout* form) {
    form->setContentsMargins(12, 12, 12, 12);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  };

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("preferencesTabWidget"));
  tabs->setDocumentMode(true);

  auto [application_page, application_layout] = make_tab_page(tabs);
  auto* application_group = new QFrame(application_page);
  application_group->setObjectName(QStringLiteral("preferencesApplicationGroup"));
  configure_panel(application_group);
  auto* application_form = new QFormLayout(application_group);
  configure_form(application_form);

  auto* language_combo = new QComboBox(application_group);
  language_combo->setObjectName(QStringLiteral("preferencesLanguageCombo"));
  language_combo->addItem(tr("English"), QStringLiteral("en"));
  language_combo->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
  const auto current_language = LocalizationManager::instance().current_language();
  const auto current_index = language_combo->findData(current_language);
  language_combo->setCurrentIndex(current_index >= 0 ? current_index : 0);
  application_form->addRow(tr("Language:"), language_combo);

  auto* gui_scale_combo = new QComboBox(application_group);
  gui_scale_combo->setObjectName(QStringLiteral("preferencesGuiScaleCombo"));
  constexpr std::array<int, 5> gui_scale_percents{100, 125, 150, 175, 200};
  for (const int percent : gui_scale_percents) {
    gui_scale_combo->addItem(QStringLiteral("%1%").arg(percent), percent);
  }
  const int current_gui_scale =
      std::clamp(settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt(),
                 gui_scale_percents.front(), gui_scale_percents.back());
  const int gui_scale_index = gui_scale_combo->findData(current_gui_scale);
  gui_scale_combo->setCurrentIndex(gui_scale_index >= 0 ? gui_scale_index : 0);
  application_form->addRow(tr("Interface scale:"), gui_scale_combo);

  auto* update_check = new QCheckBox(tr("Check for updates on startup"), application_group);
  update_check->setObjectName(QStringLiteral("preferencesCheckForUpdatesCheck"));
  update_check->setChecked(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
  application_form->addRow(update_check);
  auto* psd_import_warnings_check =
      new QCheckBox(tr("Show warnings and extra info when importing .psd files"), application_group);
  psd_import_warnings_check->setObjectName(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
  psd_import_warnings_check->setChecked(
      settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
  application_form->addRow(psd_import_warnings_check);
  application_layout->addWidget(application_group);
  application_layout->addStretch(1);
  tabs->addTab(application_page, tr("Application"));

  connect(language_combo, &QComboBox::currentIndexChanged, &dialog, [this, language_combo] {
    const auto code = language_combo->currentData().toString();
    if (!code.isEmpty() && LocalizationManager::instance().set_language(code)) {
      refresh_language_actions();
    }
  });

  auto [pen_page, pen_layout] = make_tab_page(tabs);
  auto* pen_group = new QFrame(pen_page);
  pen_group->setObjectName(QStringLiteral("preferencesPenGroup"));
  configure_panel(pen_group);
  auto* pen_form = new QFormLayout(pen_group);
  configure_form(pen_form);

  auto* pen_enabled_check = new QCheckBox(tr("Enable pen and tablet input"), pen_group);
  pen_enabled_check->setObjectName(QStringLiteral("preferencesPenEnabledCheck"));
  pen_enabled_check->setChecked(pen_input_settings_.enabled);
  auto* pen_pressure_size_check = new QCheckBox(tr("Pressure controls brush size"), pen_group);
  pen_pressure_size_check->setObjectName(QStringLiteral("preferencesPenPressureSizeCheck"));
  pen_pressure_size_check->setChecked(pen_input_settings_.pressure_size);
  auto* pen_pressure_size_min_spin = new QSpinBox(pen_group);
  pen_pressure_size_min_spin->setObjectName(QStringLiteral("preferencesPenPressureSizeMinSpin"));
  pen_pressure_size_min_spin->setRange(1, 100);
  pen_pressure_size_min_spin->setSuffix(QStringLiteral("%"));
  pen_pressure_size_min_spin->setValue(pen_input_settings_.pressure_size_min_percent);
  auto* pen_pressure_opacity_check = new QCheckBox(tr("Pressure controls opacity"), pen_group);
  pen_pressure_opacity_check->setObjectName(QStringLiteral("preferencesPenPressureOpacityCheck"));
  pen_pressure_opacity_check->setChecked(pen_input_settings_.pressure_opacity);
  auto* pen_pressure_opacity_min_spin = new QSpinBox(pen_group);
  pen_pressure_opacity_min_spin->setObjectName(QStringLiteral("preferencesPenPressureOpacityMinSpin"));
  pen_pressure_opacity_min_spin->setRange(1, 100);
  pen_pressure_opacity_min_spin->setSuffix(QStringLiteral("%"));
  pen_pressure_opacity_min_spin->setValue(pen_input_settings_.pressure_opacity_min_percent);
  auto* pen_eraser_check = new QCheckBox(tr("Use eraser tip as Eraser"), pen_group);
  pen_eraser_check->setObjectName(QStringLiteral("preferencesPenEraserTipCheck"));
  pen_eraser_check->setChecked(pen_input_settings_.use_eraser_tip);
  auto* pen_wheel_zoom_check = new QCheckBox(tr("Scroll wheel zooms the canvas"), pen_group);
  pen_wheel_zoom_check->setObjectName(QStringLiteral("preferencesPenWheelZoomCheck"));
  pen_wheel_zoom_check->setChecked(wheel_zooms_);
  pen_wheel_zoom_check->setToolTip(
      tr("Also applies to a pen button set to Scroll. Hold Ctrl or Shift while scrolling to pan."));
  const auto populate_pen_button_combo = [](QComboBox* combo, PenButtonAction current) {
    const std::array<std::pair<PenButtonAction, QString>, 11> entries{{
        {PenButtonAction::None, tr("None")},
        {PenButtonAction::PanCanvas, tr("Pan canvas")},
        {PenButtonAction::ZoomCanvas, tr("Zoom canvas (drag)")},
        {PenButtonAction::PickColor, tr("Pick color")},
        {PenButtonAction::SetCloneSource, tr("Set clone source")},
        {PenButtonAction::SwapColors, tr("Swap colors")},
        {PenButtonAction::Undo, tr("Undo")},
        {PenButtonAction::Redo, tr("Redo")},
        {PenButtonAction::ToggleEraser, tr("Toggle eraser")},
        {PenButtonAction::IncreaseBrushSize, tr("Increase brush size")},
        {PenButtonAction::DecreaseBrushSize, tr("Decrease brush size")},
    }};
    for (const auto& [action, label] : entries) {
      combo->addItem(label, static_cast<int>(action));
    }
    const auto index = combo->findData(static_cast<int>(current));
    combo->setCurrentIndex(index >= 0 ? index : 0);
  };
  auto* pen_primary_button_combo = new QComboBox(pen_group);
  pen_primary_button_combo->setObjectName(QStringLiteral("preferencesPenPrimaryButtonCombo"));
  populate_pen_button_combo(pen_primary_button_combo, pen_input_settings_.primary_button_action);
  auto* pen_secondary_button_combo = new QComboBox(pen_group);
  pen_secondary_button_combo->setObjectName(QStringLiteral("preferencesPenSecondaryButtonCombo"));
  populate_pen_button_combo(pen_secondary_button_combo, pen_input_settings_.secondary_button_action);
  auto* pen_tilt_shape_check = new QCheckBox(tr("Tilt shapes brush dabs"), pen_group);
  pen_tilt_shape_check->setObjectName(QStringLiteral("preferencesPenTiltShapeCheck"));
  pen_tilt_shape_check->setChecked(pen_input_settings_.tilt_shape);
  auto* pen_tilt_roundness_spin = new QSpinBox(pen_group);
  pen_tilt_roundness_spin->setObjectName(QStringLiteral("preferencesPenTiltMinRoundnessSpin"));
  pen_tilt_roundness_spin->setRange(1, 100);
  pen_tilt_roundness_spin->setSuffix(QStringLiteral("%"));
  pen_tilt_roundness_spin->setValue(pen_input_settings_.tilt_min_roundness_percent);

  const auto refresh_pen_controls = [=] {
    const auto pen_enabled = pen_enabled_check->isChecked();
    pen_pressure_size_check->setEnabled(pen_enabled);
    pen_pressure_size_min_spin->setEnabled(pen_enabled && pen_pressure_size_check->isChecked());
    pen_pressure_opacity_check->setEnabled(pen_enabled);
    pen_pressure_opacity_min_spin->setEnabled(pen_enabled && pen_pressure_opacity_check->isChecked());
    pen_eraser_check->setEnabled(pen_enabled);
    pen_primary_button_combo->setEnabled(pen_enabled);
    pen_secondary_button_combo->setEnabled(pen_enabled);
    pen_tilt_shape_check->setEnabled(pen_enabled);
    pen_tilt_roundness_spin->setEnabled(pen_enabled && pen_tilt_shape_check->isChecked());
  };
  connect(pen_enabled_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_size_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_opacity_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_tilt_shape_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  refresh_pen_controls();

  pen_form->addRow(pen_enabled_check);
  pen_form->addRow(pen_pressure_size_check);
  pen_form->addRow(tr("Minimum size:"), pen_pressure_size_min_spin);
  pen_form->addRow(pen_pressure_opacity_check);
  pen_form->addRow(tr("Minimum opacity:"), pen_pressure_opacity_min_spin);
  auto* pen_pad_hint_label = new QLabel(
      tr("Set the pen buttons to Right Mouse Click and Middle Mouse Click in your tablet driver: while the "
         "pen is over the canvas, a right click triggers the Upper action and a middle click the Lower one. "
         "Buttons set to Scroll or Pan are handled by the driver and cannot trigger these actions. Tablet pad "
         "buttons (express keys) are also driver-only: map them to keyboard shortcuts such as Undo, Redo, "
         "[ and ] for brush size, and E for the eraser."),
      pen_group);
  pen_pad_hint_label->setObjectName(QStringLiteral("preferencesPenPadButtonsHint"));
  pen_pad_hint_label->setWordWrap(true);
  pen_pad_hint_label->setEnabled(false);

  pen_form->addRow(pen_eraser_check);
  pen_form->addRow(pen_wheel_zoom_check);
  pen_form->addRow(tr("Upper pen button:"), pen_primary_button_combo);
  pen_form->addRow(tr("Lower pen button:"), pen_secondary_button_combo);
  pen_form->addRow(pen_pad_hint_label);
  pen_form->addRow(pen_tilt_shape_check);
  pen_form->addRow(tr("Minimum tilt roundness:"), pen_tilt_roundness_spin);
  pen_layout->addWidget(pen_group);
  pen_layout->addStretch(1);
  tabs->addTab(pen_page, tr("Pen"));

  auto [view_page, view_layout] = make_tab_page(tabs);
  auto* view_group = new QFrame(view_page);
  view_group->setObjectName(QStringLiteral("preferencesCanvasAidsGroup"));
  configure_panel(view_group);
  auto* view_form = new QFormLayout(view_group);
  configure_form(view_form);

  auto* ruler_units_combo = new QComboBox(view_group);
  ruler_units_combo->setObjectName(QStringLiteral("preferencesRulerUnitsCombo"));
  ruler_units_combo->addItem(tr("Pixels"), QStringLiteral("px"));
  ruler_units_combo->addItem(tr("Inches"), QStringLiteral("in"));
  ruler_units_combo->addItem(tr("Centimeters"), QStringLiteral("cm"));
  const auto ruler_units = settings.value(QStringLiteral("view/rulerUnits"), QStringLiteral("px")).toString();
  const auto ruler_units_index = ruler_units_combo->findData(ruler_units);
  ruler_units_combo->setCurrentIndex(ruler_units_index >= 0 ? ruler_units_index : 0);

  auto* default_rulers_check = new QCheckBox(tr("Show rulers"), view_group);
  default_rulers_check->setObjectName(QStringLiteral("preferencesShowRulersCheck"));
  default_rulers_check->setChecked(view_rulers_visible_);
  auto* default_grid_check = new QCheckBox(tr("Show grid"), view_group);
  default_grid_check->setObjectName(QStringLiteral("preferencesShowGridCheck"));
  default_grid_check->setChecked(view_grid_visible_);
  auto* default_guides_check = new QCheckBox(tr("Show guides"), view_group);
  default_guides_check->setObjectName(QStringLiteral("preferencesShowGuidesCheck"));
  default_guides_check->setChecked(view_guides_visible_);
  auto* lock_guides_check = new QCheckBox(tr("Lock guides"), view_group);
  lock_guides_check->setObjectName(QStringLiteral("preferencesLockGuidesCheck"));
  lock_guides_check->setChecked(view_guides_locked_);
  auto* snap_check = new QCheckBox(tr("Enable snapping"), view_group);
  snap_check->setObjectName(QStringLiteral("preferencesSnapCheck"));
  snap_check->setChecked(view_snap_enabled_);

  auto* grid_spacing_spin = new QDoubleSpinBox(view_group);
  grid_spacing_spin->setObjectName(QStringLiteral("preferencesGridSpacingSpin"));
  grid_spacing_spin->setRange(0.03125, 10000.0);
  grid_spacing_spin->setDecimals(3);
  grid_spacing_spin->setSuffix(tr(" px"));
  grid_spacing_spin->setValue(static_cast<double>(view_grid_spacing_32_) / 32.0);
  auto* grid_subdivisions_spin = new QSpinBox(view_group);
  grid_subdivisions_spin->setObjectName(QStringLiteral("preferencesGridSubdivisionsSpin"));
  grid_subdivisions_spin->setRange(1, 64);
  grid_subdivisions_spin->setValue(view_grid_subdivisions_);
  auto* grid_style_combo = new QComboBox(view_group);
  grid_style_combo->setObjectName(QStringLiteral("preferencesGridStyleCombo"));
  grid_style_combo->addItem(tr("Lines"), 0);
  grid_style_combo->addItem(tr("Dots"), 1);
  grid_style_combo->setCurrentIndex(std::clamp(view_grid_style_, 0, 1));

  auto selected_grid_color = view_grid_color_;
  auto selected_guide_color = view_guide_color_;
  auto* grid_color_button = new QPushButton(view_group);
  grid_color_button->setObjectName(QStringLiteral("preferencesGridColorButton"));
  auto* guide_color_button = new QPushButton(view_group);
  guide_color_button->setObjectName(QStringLiteral("preferencesGuideColorButton"));
  const auto refresh_color_choice_button = [](QPushButton* button, QColor color) {
    button->setText(overlay_color_summary_text(color));
    button->setIcon(overlay_color_swatch_icon(color));
    button->setIconSize(QSize(48, 24));
    button->setToolTip(overlay_color_summary_text(color));
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  };
  auto* overlay_preview = new QLabel(view_group);
  overlay_preview->setObjectName(QStringLiteral("preferencesGridOverlayPreview"));
  overlay_preview->setAlignment(Qt::AlignCenter);
  overlay_preview->setFixedSize(218, 86);
  const auto refresh_overlay_preview = [&] {
    overlay_preview->setPixmap(grid_overlay_preview_pixmap(selected_grid_color, selected_guide_color,
                                                           grid_style_combo->currentData().toInt(),
                                                           grid_subdivisions_spin->value()));
  };
  refresh_color_choice_button(grid_color_button, selected_grid_color);
  refresh_color_choice_button(guide_color_button, selected_guide_color);
  refresh_overlay_preview();
  connect(grid_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_grid_color, &dialog, tr("Grid Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_grid_color = color;
      refresh_color_choice_button(grid_color_button, selected_grid_color);
      refresh_overlay_preview();
    }
  });
  connect(guide_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_guide_color, &dialog, tr("Guide Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_guide_color = color;
      refresh_color_choice_button(guide_color_button, selected_guide_color);
      refresh_overlay_preview();
    }
  });
  connect(grid_subdivisions_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });
  connect(grid_style_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });

  auto* snap_guides_check = new QCheckBox(tr("Guides"), &dialog);
  snap_guides_check->setObjectName(QStringLiteral("preferencesSnapGuidesCheck"));
  snap_guides_check->setChecked(view_snap_to_guides_);
  auto* snap_grid_check = new QCheckBox(tr("Grid"), &dialog);
  snap_grid_check->setObjectName(QStringLiteral("preferencesSnapGridCheck"));
  snap_grid_check->setChecked(view_snap_to_grid_);
  auto* snap_document_check = new QCheckBox(tr("Document bounds and center"), &dialog);
  snap_document_check->setObjectName(QStringLiteral("preferencesSnapDocumentCheck"));
  snap_document_check->setChecked(view_snap_to_document_);
  auto* snap_layers_check = new QCheckBox(tr("Layer bounds and centers"), &dialog);
  snap_layers_check->setObjectName(QStringLiteral("preferencesSnapLayersCheck"));
  snap_layers_check->setChecked(view_snap_to_layers_);
  auto* snap_selection_check = new QCheckBox(tr("Selection bounds and center"), &dialog);
  snap_selection_check->setObjectName(QStringLiteral("preferencesSnapSelectionCheck"));
  snap_selection_check->setChecked(view_snap_to_selection_);

  auto* visibility_row = new QWidget(view_group);
  auto* visibility_layout = new QGridLayout(visibility_row);
  visibility_layout->setContentsMargins(0, 0, 0, 0);
  visibility_layout->setHorizontalSpacing(18);
  visibility_layout->setVerticalSpacing(6);
  visibility_layout->addWidget(default_rulers_check, 0, 0);
  visibility_layout->addWidget(default_grid_check, 0, 1);
  visibility_layout->addWidget(default_guides_check, 1, 0);
  visibility_layout->addWidget(lock_guides_check, 1, 1);
  auto* snap_targets_row = new QWidget(&dialog);
  auto* snap_targets_layout = new QGridLayout(snap_targets_row);
  snap_targets_layout->setContentsMargins(0, 0, 0, 0);
  snap_targets_layout->setHorizontalSpacing(18);
  snap_targets_layout->setVerticalSpacing(6);
  snap_targets_layout->addWidget(snap_guides_check, 0, 0);
  snap_targets_layout->addWidget(snap_grid_check, 0, 1);
  snap_targets_layout->addWidget(snap_document_check, 1, 0, 1, 2);
  snap_targets_layout->addWidget(snap_layers_check, 2, 0, 1, 2);
  snap_targets_layout->addWidget(snap_selection_check, 3, 0, 1, 2);

  view_form->addRow(tr("Ruler units:"), ruler_units_combo);
  view_form->addRow(tr("Default visibility:"), visibility_row);
  view_form->addRow(tr("Grid spacing:"), grid_spacing_spin);
  view_form->addRow(tr("Grid subdivisions:"), grid_subdivisions_spin);
  view_form->addRow(tr("Grid style:"), grid_style_combo);
  view_form->addRow(tr("Grid color:"), grid_color_button);
  view_form->addRow(tr("Guide color:"), guide_color_button);
  view_form->addRow(tr("Overlay preview:"), overlay_preview);
  view_layout->addWidget(view_group);
  view_layout->addStretch(1);
  tabs->addTab(view_page, tr("Grid and Guides"));

  auto [snapping_page, snapping_layout] = make_tab_page(tabs);
  auto* snapping_group = new QFrame(snapping_page);
  snapping_group->setObjectName(QStringLiteral("preferencesSnappingGroup"));
  configure_panel(snapping_group);
  auto* snapping_form = new QFormLayout(snapping_group);
  configure_form(snapping_form);
  snapping_form->addRow(tr("Snap:"), snap_check);
  snapping_form->addRow(tr("Snap targets:"), snap_targets_row);
  snapping_layout->addWidget(snapping_group);
  snapping_layout->addStretch(1);
  tabs->addTab(snapping_page, tr("Snapping"));

  auto [hotkeys_page, hotkeys_layout] = make_tab_page(tabs);
  auto* hotkey_editor = new HotkeyEditorPanel(hotkey_registry_, menuBar(), hotkeys_page);
  hotkeys_layout->addWidget(hotkey_editor);
  hotkeys_layout->addStretch(1);
  tabs->addTab(hotkeys_page, tr("Hotkeys"));

  content->addWidget(tabs, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  buttons->setObjectName(QStringLiteral("preferencesButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);

  // Applied after every child widget exists: Qt does not reliably pick up
  // sub-control rules (QSpinBox::up-button) for widgets created on hidden
  // tab pages after the stylesheet was set.
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyPreferencesDialog QTabWidget::pane {
      border: 1px solid #444444;
      background: #2b2b2b;
      top: -1px;
    }
    QDialog#patchyPreferencesDialog QTabBar::tab {
      background: #383838;
      border: 1px solid #444444;
      border-bottom-color: #2b2b2b;
      color: #dcdcdc;
      padding: 7px 18px;
      min-width: 92px;
    }
    QDialog#patchyPreferencesDialog QTabBar::tab:selected {
      background: #2b2b2b;
      color: #ffffff;
      border-bottom-color: #2b2b2b;
    }
    QDialog#patchyPreferencesDialog QFrame[preferencesPanel="true"] {
      background: #303030;
      border: 1px solid #464646;
      border-radius: 4px;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton {
      background: #3a3a3a;
      border: 1px solid #626262;
      border-radius: 3px;
      color: #f0f0f0;
      min-height: 30px;
      min-width: 158px;
      padding: 3px 9px;
      text-align: left;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton:hover,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton:hover {
      border-color: #80bfff;
      background: #404040;
    }
    QDialog#patchyPreferencesDialog QLabel#preferencesGridOverlayPreview {
      background: #202020;
      border: 1px solid #575757;
      padding: 0;
    }
    QDialog#patchyPreferencesDialog QScrollArea#preferencesTabScroll,
    QDialog#patchyPreferencesDialog QWidget#preferencesTabPage {
      background: transparent;
    }
    /* These spin box rules stay unprefixed: Qt ignores sub-control geometry
       (::up-button / ::down-button) when the selector has a descendant prefix
       such as QDialog#patchyPreferencesDialog. Setting them on this dialog's
       stylesheet already limits them to widgets inside the dialog. */
    QSpinBox,
    QDoubleSpinBox {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
      color: #f0f0f0;
      min-height: 26px;
      padding-left: 6px;
      padding-right: 54px; /* keep text clear of the - / + buttons */
    }
    QSpinBox:disabled,
    QDoubleSpinBox:disabled {
      background: #2c2c2c;
      color: #767676;
    }
    /* The decrement button sits on the left, the increment button on the
       far right, so the right-hand button always raises the value. */
    QSpinBox::down-button,
    QDoubleSpinBox::down-button {
      subcontrol-origin: border;
      subcontrol-position: center right;
      right: 27px;
      width: 24px;
      height: 24px;
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
    }
    QSpinBox::up-button,
    QDoubleSpinBox::up-button {
      subcontrol-origin: border;
      subcontrol-position: center right;
      right: 1px;
      width: 24px;
      height: 24px;
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
    }
    QSpinBox::up-button:hover,
    QSpinBox::down-button:hover,
    QDoubleSpinBox::up-button:hover,
    QDoubleSpinBox::down-button:hover {
      background: #4a4a4a;
      border-color: #696969;
    }
    QSpinBox::up-button:pressed,
    QSpinBox::down-button:pressed,
    QDoubleSpinBox::up-button:pressed,
    QDoubleSpinBox::down-button:pressed {
      background: #2f75bd;
      border-color: #6bb3ff;
    }
    QSpinBox::up-button:disabled,
    QSpinBox::down-button:disabled,
    QDoubleSpinBox::up-button:disabled,
    QDoubleSpinBox::down-button:disabled {
      background: #2e2e2e;
      border-top-color: #444444;
    }
    QSpinBox::up-arrow,
    QDoubleSpinBox::up-arrow {
      image: url(:/patchy/icons/spin-plus.svg);
      width: 12px;
      height: 12px;
    }
    QSpinBox::up-arrow:disabled,
    QSpinBox::up-arrow:off,
    QDoubleSpinBox::up-arrow:disabled,
    QDoubleSpinBox::up-arrow:off {
      image: url(:/patchy/icons/spin-plus-disabled.svg);
    }
    QSpinBox::down-arrow,
    QDoubleSpinBox::down-arrow {
      image: url(:/patchy/icons/spin-minus.svg);
      width: 12px;
      height: 12px;
    }
    QSpinBox::down-arrow:disabled,
    QSpinBox::down-arrow:off,
    QDoubleSpinBox::down-arrow:disabled,
    QDoubleSpinBox::down-arrow:off {
      image: url(:/patchy/icons/spin-minus-disabled.svg);
    }
  )"));

  if (exec_dialog(dialog) == QDialog::Accepted) {
    hotkey_editor->commit();
    const auto new_grid_spacing_32 =
        std::clamp(static_cast<int>(std::lround(grid_spacing_spin->value() * 32.0)), 1, 320000);
    settings.setValue(QStringLiteral("updates/checkOnStartup"), update_check->isChecked());
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), psd_import_warnings_check->isChecked());
    const int selected_gui_scale = gui_scale_combo->currentData().toInt();
    const int previous_gui_scale =
        settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt();
    if (selected_gui_scale != previous_gui_scale) {
      settings.setValue(QStringLiteral("preferences/guiScalePercent"), selected_gui_scale);
      show_information_message(this, tr("Interface Scale"),
                               tr("Restart Patchy for the new interface scale to take effect."),
                               QStringLiteral("preferencesInterfaceScaleMessageBox"));
    }
    settings.setValue(QStringLiteral("view/rulerUnits"), ruler_units_combo->currentData().toString());
    pen_input_settings_.enabled = pen_enabled_check->isChecked();
    pen_input_settings_.pressure_size = pen_pressure_size_check->isChecked();
    pen_input_settings_.pressure_size_min_percent = pen_pressure_size_min_spin->value();
    pen_input_settings_.pressure_opacity = pen_pressure_opacity_check->isChecked();
    pen_input_settings_.pressure_opacity_min_percent = pen_pressure_opacity_min_spin->value();
    pen_input_settings_.use_eraser_tip = pen_eraser_check->isChecked();
    pen_input_settings_.primary_button_action =
        static_cast<PenButtonAction>(pen_primary_button_combo->currentData().toInt());
    pen_input_settings_.secondary_button_action =
        static_cast<PenButtonAction>(pen_secondary_button_combo->currentData().toInt());
    pen_input_settings_.tilt_shape = pen_tilt_shape_check->isChecked();
    pen_input_settings_.tilt_min_roundness_percent = pen_tilt_roundness_spin->value();
    wheel_zooms_ = pen_wheel_zoom_check->isChecked();
    view_rulers_visible_ = default_rulers_check->isChecked();
    view_grid_visible_ = default_grid_check->isChecked();
    view_guides_visible_ = default_guides_check->isChecked();
    view_guides_locked_ = lock_guides_check->isChecked();
    view_snap_enabled_ = snap_check->isChecked();
    view_snap_to_guides_ = snap_guides_check->isChecked();
    view_snap_to_grid_ = snap_grid_check->isChecked();
    view_snap_to_document_ = snap_document_check->isChecked();
    view_snap_to_layers_ = snap_layers_check->isChecked();
    view_snap_to_selection_ = snap_selection_check->isChecked();
    view_grid_spacing_32_ = new_grid_spacing_32;
    view_grid_subdivisions_ = grid_subdivisions_spin->value();
    view_grid_style_ = grid_style_combo->currentData().toInt();
    view_grid_color_ = selected_grid_color;
    view_guide_color_ = selected_guide_color;
    if (canvas_ != nullptr && (document().grid_settings().horizontal_cycle_32 != new_grid_spacing_32 ||
                               document().grid_settings().vertical_cycle_32 != new_grid_spacing_32)) {
      push_undo_snapshot(tr("Grid Preferences"));
      document().grid_settings().horizontal_cycle_32 = new_grid_spacing_32;
      document().grid_settings().vertical_cycle_32 = new_grid_spacing_32;
      canvas_->document_changed();
    }
    if (view_rulers_action_ != nullptr) {
      view_rulers_action_->setChecked(view_rulers_visible_);
    }
    if (view_grid_action_ != nullptr) {
      view_grid_action_->setChecked(view_grid_visible_);
    }
    if (view_guides_action_ != nullptr) {
      view_guides_action_->setChecked(view_guides_visible_);
    }
    if (view_snap_action_ != nullptr) {
      view_snap_action_->setChecked(view_snap_enabled_);
    }
    if (view_lock_guides_action_ != nullptr) {
      view_lock_guides_action_->setChecked(view_guides_locked_);
    }
    if (view_snap_guides_action_ != nullptr) {
      view_snap_guides_action_->setChecked(view_snap_to_guides_);
    }
    if (view_snap_grid_action_ != nullptr) {
      view_snap_grid_action_->setChecked(view_snap_to_grid_);
    }
    if (view_snap_document_action_ != nullptr) {
      view_snap_document_action_->setChecked(view_snap_to_document_);
    }
    if (view_snap_layers_action_ != nullptr) {
      view_snap_layers_action_->setChecked(view_snap_to_layers_);
    }
    if (view_snap_selection_action_ != nullptr) {
      view_snap_selection_action_->setChecked(view_snap_to_selection_);
    }
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
      apply_pen_input_settings(active_session->canvas);
    }
    save_pen_input_settings();
    save_view_settings();
  }
}

void MainWindow::new_guide_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  auto* orientation_combo = new QComboBox(&dialog);
  orientation_combo->setObjectName(QStringLiteral("newGuideOrientationCombo"));
  orientation_combo->addItem(tr("Vertical"), static_cast<int>(GuideOrientation::Vertical));
  orientation_combo->addItem(tr("Horizontal"), static_cast<int>(GuideOrientation::Horizontal));
  auto* position_spin = new QDoubleSpinBox(&dialog);
  position_spin->setObjectName(QStringLiteral("newGuidePositionSpin"));
  position_spin->setRange(0.0, std::max(document().width(), document().height()));
  position_spin->setDecimals(3);
  position_spin->setSuffix(tr(" px"));
  position_spin->setValue(0.0);
  form->addRow(tr("Orientation:"), orientation_combo);
  form->addRow(tr("Position:"), position_spin);
  content->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  const auto orientation = static_cast<GuideOrientation>(orientation_combo->currentData().toInt());
  canvas_->add_guide(orientation, static_cast<std::int32_t>(std::lround(position_spin->value() * 32.0)));
}

void MainWindow::new_guide_layout_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideLayoutDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide Layout"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  auto* columns_spin = new QSpinBox(&dialog);
  columns_spin->setObjectName(QStringLiteral("newGuideLayoutColumnsSpin"));
  columns_spin->setRange(0, 64);
  columns_spin->setValue(2);
  auto* rows_spin = new QSpinBox(&dialog);
  rows_spin->setObjectName(QStringLiteral("newGuideLayoutRowsSpin"));
  rows_spin->setRange(0, 64);
  rows_spin->setValue(2);
  auto* clear_existing = new QCheckBox(tr("Clear existing guides"), &dialog);
  clear_existing->setObjectName(QStringLiteral("newGuideLayoutClearExistingCheck"));
  clear_existing->setChecked(false);
  form->addRow(tr("Columns:"), columns_spin);
  form->addRow(tr("Rows:"), rows_spin);
  form->addRow(QString(), clear_existing);
  content->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideLayoutButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  if (clear_existing->isChecked() && !document().guides().empty()) {
    push_undo_snapshot(tr("New Guide Layout"));
    document().guides().clear();
  } else {
    push_undo_snapshot(tr("New Guide Layout"));
  }
  const auto add_even_guides = [this](GuideOrientation orientation, int count, int span) {
    if (count <= 0 || span <= 0) {
      return;
    }
    for (int index = 1; index < count; ++index) {
      const auto position = static_cast<double>(span) * static_cast<double>(index) / static_cast<double>(count);
      document().guides().push_back(DocumentGuide{orientation, static_cast<std::int32_t>(std::lround(position * 32.0))});
    }
  };
  add_even_guides(GuideOrientation::Vertical, columns_spin->value(), document().width());
  add_even_guides(GuideOrientation::Horizontal, rows_spin->value(), document().height());
  canvas_->document_changed();
}

void MainWindow::clear_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_guides();
}

void MainWindow::clear_selected_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_selected_guides();
}

void MainWindow::scan_legacy_plugins() {
  const auto paths =
      get_open_file_names(this, tr("Scan Legacy Photoshop Plug-ins"), QString(),
                          tr("Photoshop Plug-ins (*.8bf *.8bi *.8li);;All Files (*.*)"), nullptr,
                          QStringLiteral("legacyPluginScanFileDialog"));
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

  show_information_message(this, tr("Legacy Photoshop Plug-ins"),
                           tr("%1 plug-in action(s) available under Plug-ins > Legacy Photoshop Plug-ins.\n\n%2")
                               .arg(available)
                               .arg(report.join('\n')),
                           QStringLiteral("legacyPluginScanMessageBox"));
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
  descriptor.kind = probe.kind == LegacyPhotoshopPluginKind::Format8bi ? PATCHY_PLUGIN_FILE_FORMAT
                                                                        : PATCHY_PLUGIN_FILTER;
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
  register_document_action(action);
  update_document_action_state();
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  const auto name = QString::fromStdString(descriptor->display_name).toLower();
  if (!name.contains(QStringLiteral("greyscale")) && !name.contains(QStringLiteral("white to transparent"))) {
    show_information_message(
        this, tr("Legacy Photoshop Plug-in"),
        tr("%1 was scanned and is available, but this build only has compatibility shims for the "
           "bundled Greyscale and White to Transparent test filters. A full 8BF host still needs "
           "the out-of-process Photoshop SDK adapter.")
            .arg(QString::fromStdString(descriptor->display_name)),
        QStringLiteral("legacyPluginUnavailableMessageBox"));
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

void MainWindow::clear_system_clipboard() {
  if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    const QSignalBlocker blocker(clipboard);
    clipboard->clear();
    patchy_system_clipboard_signature_ = clipboard_image_signature(clipboard->image());
  }
}

void MainWindow::set_system_clipboard_image(const QImage& image) {
  if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    const QSignalBlocker blocker(clipboard);
    clipboard->setImage(image);
    patchy_system_clipboard_signature_ = clipboard_image_signature(clipboard->image());
  }
}

void MainWindow::clear_internal_clipboard_on_external_change() {
  const auto current_signature = clipboard_image_signature(QApplication::clipboard()->image());
  if (patchy_system_clipboard_signature_.has_value() && current_signature == *patchy_system_clipboard_signature_) {
    return;
  }
  clipboard_.reset();
  patchy_system_clipboard_signature_.reset();
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
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible() ||
        layer_id_locks_image_pixels(layer.id())) {
      continue;
    }
    layers_to_cut.push_back(layer.id());
  }
  if (layers_to_cut.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      statusBar()->showMessage(tr("Layer pixels are locked."));
      return;
    }
    clipboard_.reset();
    clear_system_clipboard();
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
    affected = unite_rect(affected, patchy::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Cut %1 layer(s)").arg(static_cast<qulonglong>(layers_to_cut.size())));
}

void MainWindow::copy_selection() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
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
    clear_system_clipboard();
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
    clear_system_clipboard();
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
    clear_system_clipboard();
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
  set_system_clipboard_image(image_from_pixels(clipboard_->pixels));
  update_history(tr("Copy"));
  statusBar()->showMessage(
      tr("Copied %1 layer(s), %2 x %3 px")
          .arg(static_cast<qulonglong>(layers_to_copy.size()))
          .arg(copy_rect.width)
          .arg(copy_rect.height));
}

void MainWindow::copy_merged() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
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
  set_system_clipboard_image(image);
  update_history(tr("Copy merged"));
  statusBar()->showMessage(tr("Copied merged %1 x %2 px").arg(copy_rect.width).arg(copy_rect.height));
}

void MainWindow::paste_clipboard() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
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
  if (canvas_ == nullptr) {
    statusBar()->showMessage(tr("Select a pixel layer to transform"));
    return;
  }
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }
  if (const auto active = document().active_layer_id();
      active.has_value() && layer_id_locks_position(*active)) {
    statusBar()->showMessage(tr("Layer position is locked."));
    return;
  }
  if (!canvas_->begin_free_transform()) {
    statusBar()->showMessage(tr("Select a pixel layer to transform"));
  }
}

void MainWindow::add_text_at(QPoint document_point, QRect requested_text_box) {
  if (canvas_ == nullptr) {
    return;
  }
  if (canvas_->free_transform_active()) {
    canvas_->finish_free_transform();
  }
  if (current_tool_ != CanvasTool::Text) {
    if (type_tool_action_ != nullptr) {
      type_tool_action_->trigger();
    } else {
      current_tool_ = CanvasTool::Text;
      canvas_->set_tool(current_tool_);
      refresh_options_bar();
      refresh_document_info();
    }
  }
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }

  const QPoint requested_document_point = document_point;
  std::optional<LayerId> editing_layer;
  std::optional<bool> editing_layer_was_visible;
  std::optional<LayerId> restore_active_layer = document().active_layer_id();
  bool editing_layer_needs_preview = false;
  bool editing_layer_expensive_preview = false;
  bool editing_layer_uses_source_raster_preview = false;
  bool editing_layer_force_baked_preview = false;
  bool editing_layer_uses_psd_text_frame = false;
  bool editing_layer_has_transformed_preview = false;
  bool editing_layer_uses_extended_box_preview = false;
  bool editing_layer_uses_line_aware_box_preview = false;
  std::optional<QPointF> editing_layer_source_visible_anchor;
  std::optional<QSizeF> editing_layer_source_visible_size;
  std::optional<QRectF> editing_layer_render_local_rect;
  QString initial_text;
  QString initial_html;
  QString initial_rich_text_runs;
  QString initial_paragraph_runs;
  std::optional<QPointF> initial_cursor_local_position;
  QString family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : font().family();
  int document_text_size = text_size_spin_ != nullptr ? text_points_to_pixels(text_size_spin_->value(), document()) : 48;
  bool text_bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  bool text_italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  int text_anti_alias = text_smoothing_combo_value(text_smoothing_combo_);
  QColor text_color = canvas_->primary_color();
  bool boxed_text = requested_text_box.isValid() && requested_text_box.width() >= kMinimumTextBoxDocumentSize &&
                    requested_text_box.height() >= kMinimumTextBoxDocumentSize;
  if (boxed_text) {
    requested_text_box = requested_text_box.normalized();
    document_point = requested_text_box.topLeft();
  }
  int document_editor_width =
      boxed_text ? requested_text_box.width() : std::max(160, std::min(520, document().width() - document_point.x() - 8));
  int document_editor_height = boxed_text ? requested_text_box.height() : 96;
  if (const auto active = document().active_layer_id(); active.has_value()) {
    if (auto* layer = document().find_layer(*active); layer != nullptr && layer_is_text(*layer) &&
        layer->bounds().contains(document_point.x(), document_point.y())) {
      if (layer_id_locks_image_pixels(*active)) {
        statusBar()->showMessage(tr("Layer pixels are locked."));
        return;
      }
      editing_layer = *active;
      editing_layer_was_visible = layer->visible();
      initial_text = QString::fromStdString(layer->metadata().at(kLayerMetadataText));
      if (const auto found = layer->metadata().find(kLayerMetadataTextHtml); found != layer->metadata().end()) {
        initial_html = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextRuns); found != layer->metadata().end()) {
        initial_rich_text_runs = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextParagraphRuns); found != layer->metadata().end()) {
        initial_paragraph_runs = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextFont); found != layer->metadata().end()) {
        const auto stored_family = QString::fromStdString(found->second);
        if (stored_family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) != 0) {
          family = canonical_text_display_family(stored_family);
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
      if (const auto found = layer->metadata().find(kLayerMetadataTextAntiAlias); found != layer->metadata().end()) {
        text_anti_alias = std::clamp(std::atoi(found->second.c_str()), 0, 16);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextRasterStatus);
          found != layer->metadata().end()) {
        editing_layer_uses_source_raster_preview =
            *editing_layer_was_visible && found->second == "psd_raster_preview";
      }
      if (editing_layer_uses_source_raster_preview) {
        const auto missing_fonts = missing_text_families_for_psd_raster_preview(family, initial_rich_text_runs);
        if (!confirm_psd_raster_preview_font_substitution(this, missing_fonts)) {
          statusBar()->showMessage(tr("Canceled text edit"));
          return;
        }
      }
      if (text_font_combo_ != nullptr) {
        text_font_combo_->setCurrentFont(QFont(family));
      }
      if (text_size_spin_ != nullptr) {
        text_size_spin_->setValue(text_pixels_to_points(document_text_size, document()));
      }
      if (text_bold_button_ != nullptr) {
        text_bold_button_->setChecked(text_bold);
      }
      if (text_italic_button_ != nullptr) {
        text_italic_button_->setChecked(text_italic);
      }
      set_text_smoothing_combo_value(text_smoothing_combo_, text_anti_alias);
      boxed_text = text_flow_is_box(
          layer->metadata().contains(kLayerMetadataTextFlow)
              ? QString::fromStdString(layer->metadata().at(kLayerMetadataTextFlow))
              : QString::fromLatin1(kTextFlowPoint));
      const auto text_transform = patchy_text_transform_for_layer(*layer);
      const auto psd_frame = psd_text_frame_rect(*layer);
      const bool using_psd_frame = psd_frame.has_value() && boxed_text;
      editing_layer_uses_psd_text_frame = layer_should_edit_with_psd_text_frame(*layer, boxed_text) && using_psd_frame;
      editing_layer_has_transformed_preview =
          text_transform.has_value() && qtransform_has_non_translation_linear_part(*text_transform) &&
          !editing_layer_uses_psd_text_frame;
      if (text_transform.has_value() && !editing_layer_uses_psd_text_frame) {
        const auto text_affine_transform = canonical_text_affine_transform_for_layer(*layer);
        // A PSD-imported point-text layer stores its transform translation at the typographic
        // baseline, which sits well below the visible glyph top.  When the transform is a pure
        // translation we anchor the editor to the live visible glyph top instead of the transform
        // origin so the caret lands on the glyphs.  The anchor is derived from the current raster,
        // so it stays valid for any translation-only point text -- a fresh PSD import, a layer the
        // user moved, or one already committed by Patchy (whose transform origin would drop the
        // editor onto the baseline and make the text jump down ~one ascent on edit).  Capturing it
        // every session also records the visible width, which the commit needs to keep the
        // justification point of centered/right text pinned across repeated edits.
        const bool can_use_psd_visual_origin =
            !boxed_text && text_affine_transform.has_value() &&
            !affine_transform_has_non_translation_linear_part(*text_affine_transform);
        const auto psd_visible_rect =
            can_use_psd_visual_origin ? psd_point_text_source_visible_rect(*layer) : std::optional<QRectF>{};
        if (psd_visible_rect.has_value()) {
          editing_layer_source_visible_anchor = psd_visible_rect->topLeft();
          editing_layer_source_visible_size = psd_visible_rect->size();
          document_point = QPoint(static_cast<int>(std::floor(psd_visible_rect->left())),
                                  static_cast<int>(std::floor(psd_visible_rect->top())));
        } else {
          bool invertible = false;
          const auto inverse = text_transform->inverted(&invertible);
          if (invertible) {
            initial_cursor_local_position = inverse.map(QPointF(requested_document_point));
          }
          const auto transformed_origin = text_transform->map(QPointF(0.0, 0.0));
          document_point = QPoint(static_cast<int>(std::floor(transformed_origin.x())),
                                  static_cast<int>(std::floor(transformed_origin.y())));
        }
      } else if (using_psd_frame) {
        document_point = QPoint(static_cast<int>(std::floor(psd_frame->left())),
                                static_cast<int>(std::floor(psd_frame->top())));
        document_editor_width =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->width())));
        document_editor_height =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->height())));
      } else {
        document_point = QPoint(layer->bounds().x, layer->bounds().y);
      }
      if (boxed_text && (!using_psd_frame || (text_transform.has_value() && !editing_layer_uses_psd_text_frame))) {
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxWidth); found != layer->metadata().end()) {
          document_editor_width = std::max(kMinimumTextBoxDocumentSize, std::atoi(found->second.c_str()));
        } else {
          document_editor_width = std::max(kMinimumTextBoxDocumentSize, layer->bounds().width);
        }
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxHeight); found != layer->metadata().end()) {
          document_editor_height = std::max(kMinimumTextBoxDocumentSize, std::atoi(found->second.c_str()));
        } else {
          document_editor_height = std::max(kMinimumTextBoxDocumentSize, layer->bounds().height);
        }
      } else if (!boxed_text) {
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxWidth); found != layer->metadata().end()) {
          document_editor_width = std::max(160, std::atoi(found->second.c_str()));
        } else {
          document_editor_width = std::max(160, layer->bounds().width);
        }
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxHeight); found != layer->metadata().end()) {
          document_editor_height = std::max(64, std::atoi(found->second.c_str()));
        } else {
          document_editor_height = std::max(64, layer->bounds().height + document_text_size);
        }
      }
      if (boxed_text) {
        const QRectF frame_rect(0.0, 0.0, static_cast<qreal>(document_editor_width),
                                static_cast<qreal>(document_editor_height));
        if (editing_layer_uses_psd_text_frame) {
          editing_layer_uses_line_aware_box_preview = true;
          editing_layer_render_local_rect = psd_box_text_editor_render_local_rect(*layer).value_or(frame_rect);
          if (rect_extends_beyond(*editing_layer_render_local_rect, frame_rect)) {
            editing_layer_uses_extended_box_preview = true;
            if (!editing_layer_source_visible_anchor.has_value()) {
              editing_layer_source_visible_anchor = layer_source_visible_anchor(*layer);
            }
          }
        } else {
          const auto raster_status = layer->metadata().find(kLayerMetadataTextRasterStatus);
          const bool patchy_box_raster =
              raster_status != layer->metadata().end() && raster_status->second == "patchy_raster";
          const bool patchy_box_raster_uses_line_aware_preview =
              patchy_box_raster && layer->metadata().contains(kLayerMetadataTextLineAwareBoxPreview);
          const bool patchy_box_raster_has_linear_transform =
              text_transform.has_value() && qtransform_has_non_translation_linear_part(*text_transform);
          if (!patchy_box_raster_has_linear_transform) {
            if (const auto render_rect =
                    patchy_box_text_editor_render_local_rect(*layer, document_editor_width, document_editor_height);
                render_rect.has_value()) {
              editing_layer_render_local_rect = *render_rect;
              editing_layer_uses_extended_box_preview = true;
              editing_layer_uses_line_aware_box_preview = true;
              editing_layer_source_visible_anchor = layer_source_visible_anchor(*layer);
            } else if (patchy_box_raster_uses_line_aware_preview) {
              editing_layer_render_local_rect = frame_rect;
              editing_layer_uses_line_aware_box_preview = true;
            }
          } else if (patchy_box_raster_uses_line_aware_preview) {
            editing_layer_render_local_rect = frame_rect;
            editing_layer_uses_line_aware_box_preview = true;
          }
        }
      }
      editing_layer_needs_preview = *editing_layer_was_visible &&
                                    (layer_requires_text_editor_preview(*layer) ||
                                     editing_layer_uses_extended_box_preview ||
                                     editing_layer_uses_line_aware_box_preview);
      // Text edits show the live render from the very start of the session.  Once the user is
      // past the substitution warning (it only appears when a font is missing; cancelling it
      // returned above), keeping Photoshop's raster on screen made the caret and selection
      // unmatchable: they are computed from Patchy's layout, which wraps and meters differently
      // from Photoshop's raster (hyphenation, substituted faces), and the font swap then landed
      // mid-typing.  Showing the live render immediately keeps glyphs, caret, and selection in
      // one geometry.  Applying the session -- even with no text change -- commits that live
      // render (apply keeps what you saw); Escape is the way to leave Photoshop's original
      // pixels untouched.
      if (editing_layer_uses_source_raster_preview && editing_layer_was_visible.value_or(true)) {
        editing_layer_uses_source_raster_preview = false;
      }
      // Plain point text would otherwise render with the editor's own widget glyphs, which are
      // rasterized differently from render_text_pixels (the committed layer's renderer):
      // entering/leaving edit then visibly shifts the text and changes its antialiasing.  Drive
      // the edit through the same live baked-preview path that effect text uses (the glyphs are
      // re-rasterized by render_text_pixels every keystroke), so the on-screen result comes from
      // the exact renderer the commit uses -- no shift, and the caret still lands on the glyphs.
      // Transformed point text never reaches this block: a non-translation transform already
      // counts as requiring a preview (layer_requires_text_editor_preview), so those sessions
      // render live through the regular preview path from the start.
      if (editing_layer_was_visible.value_or(true) && !boxed_text && !editing_layer_needs_preview) {
        editing_layer_needs_preview = true;
        editing_layer_force_baked_preview = true;
      }
      const auto document_bounds = Rect::from_size(document().width(), document().height());
      editing_layer_expensive_preview =
          editing_layer_needs_preview && layer_style_preview_is_expensive(*layer, document_bounds);
      if (!editing_layer_uses_source_raster_preview) {
        layer->set_visible(false);
        canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      }
    }
  }

  auto* editor = new InlineTextEdit(canvas_);
  editor->setObjectName(QStringLiteral("inlineTextEditor"));
  editor->setAcceptRichText(true);
  auto editor_font = render_text_font_for_display_family(
      family, std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))), text_bold,
      text_italic, text_anti_alias);
  editor->setFont(editor_font);
  editor->document()->setDocumentMargin(0);
  editor->document()->setDefaultFont(editor_font);
  editor->setFrameShape(QFrame::NoFrame);
  editor->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAutoFillBackground(false);
  editor->setLineWrapMode(boxed_text ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
  editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setProperty("patchy.documentTextSize", document_text_size);
  editor->setProperty("patchy.documentTextWidth", document_editor_width);
  editor->setProperty("patchy.documentTextHeight", document_editor_height);
  editor->setProperty("patchy.documentTextFlow", text_flow_metadata_value(boxed_text));
  editor->setProperty("patchy.documentTextAntiAlias", text_anti_alias);
  editor->setProperty("patchy.documentTextFamily", family);
  editor->setProperty("patchy.editorZoom", canvas_->zoom());
  editor->setProperty("patchy.documentTextColor", text_color);
  editor->setProperty("patchy.documentTextX", document_point.x());
  editor->setProperty("patchy.documentTextY", document_point.y());
  editor->setProperty(kTextEditorPreviewEnabledProperty, editing_layer_needs_preview);
  editor->setProperty(kTextEditorPreviewExpensiveProperty, editing_layer_expensive_preview);
  editor->setProperty(kTextEditorPreviewPaintProperty,
                      editing_layer_uses_source_raster_preview ||
                          (editing_layer_needs_preview &&
                           (!editing_layer_expensive_preview || editing_layer_has_transformed_preview)));
  editor->setProperty(kTextEditorPreviewGenerationProperty, 0);
  editor->setProperty(kTextEditorChangedProperty, false);
  editor->setProperty(kTextEditorSourceRasterPreviewProperty, editing_layer_uses_source_raster_preview);
  editor->setProperty(kTextEditorForceBakedPreviewProperty, editing_layer_force_baked_preview);
  editor->setProperty(kTextEditorExtendedBoxPreviewProperty, editing_layer_uses_extended_box_preview);
  editor->setProperty(kTextEditorLineAwareBoxPreviewProperty, editing_layer_uses_line_aware_box_preview);
  editor->setProperty("patchy.usesPsdTextFrame", editing_layer_uses_psd_text_frame);
  if (editing_layer_render_local_rect.has_value()) {
    set_text_editor_render_local_rect(*editor, *editing_layer_render_local_rect);
  }
  if (editing_layer_source_visible_anchor.has_value()) {
    editor->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant::fromValue(*editing_layer_source_visible_anchor));
  }
  if (editing_layer_source_visible_size.has_value()) {
    editor->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant::fromValue(*editing_layer_source_visible_size));
  }
  if (restore_active_layer.has_value()) {
    editor->setProperty("patchy.restoreActiveLayerId", QVariant::fromValue<qulonglong>(*restore_active_layer));
  }
  if (editing_layer.has_value()) {
    editor->setProperty("patchy.editingLayerId", QVariant::fromValue<qulonglong>(*editing_layer));
  }
  if (editing_layer_was_visible.has_value()) {
    editor->setProperty("patchy.editingLayerWasVisible", *editing_layer_was_visible);
  }
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  if (!initial_rich_text_runs.trimmed().isEmpty()) {
    auto document_font =
        render_text_font_for_display_family(family, std::max(1, document_text_size), text_bold, text_italic,
                                            text_anti_alias);
    editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
    apply_patchy_text_runs_to_document(*editor->document(), initial_rich_text_runs, document_font, text_color,
                                       canvas_->zoom(), text_anti_alias);
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
    auto typing_format = text_editor_typing_format(editor_font, text_color);
    if (const auto leading = text_leading_from_document_formats(*editor->document()); leading.has_value()) {
      typing_format.setProperty(kTextLeadingFormatProperty, *leading);
    }
    editor->setCurrentCharFormat(typing_format);
  } else if (!initial_html.trimmed().isEmpty()) {
    auto document_font =
        render_text_font_for_display_family(family, std::max(1, document_text_size), text_bold, text_italic,
                                            text_anti_alias);
    editor->setHtml(document_html_for_editor(initial_html, document_font, canvas_->zoom()));
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  } else {
    editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    const auto format = text_editor_typing_format(editor_font, text_color);
    cursor.mergeCharFormat(format);
    editor->setCurrentCharFormat(format);
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  }
  if (editing_layer.has_value() && editing_layer_uses_line_aware_box_preview) {
    if (auto* layer = document().find_layer(*editing_layer); layer != nullptr) {
      if (const auto scale =
              calibrated_box_text_metric_scale_for_editor(*editor, *layer, canvas_->zoom(), text_color);
          scale.has_value()) {
        set_text_editor_metric_scale(*editor, *scale);
      }
    }
  }
  if (editing_layer_source_visible_anchor.has_value()) {
    update_text_editor_transform_from_source_anchor(*editor, canvas_->zoom(), text_color);
    document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                            editor->property("patchy.documentTextY").toInt());
  } else if (editing_layer.has_value()) {
    if (auto* layer = document().find_layer(*editing_layer); layer != nullptr) {
      if (const auto pixels = render_text_editor_pixels_for_source_anchor(*editor, canvas_->zoom(), text_color);
          pixels.has_value() &&
          update_text_editor_transform_from_psd_local_bounds(*editor, *layer, *pixels, boxed_text)) {
        document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                                editor->property("patchy.documentTextY").toInt());
        if (const auto transform = text_editor_transform_override(*editor); transform.has_value()) {
          bool invertible = false;
          const auto inverse = qtransform_from_affine(*transform).inverted(&invertible);
          if (invertible) {
            initial_cursor_local_position = inverse.map(QPointF(requested_document_point));
          }
        }
      }
    }
  }
  if (!editing_layer.has_value()) {
    editor->selectAll();
  }
  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(),
                      std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom()))),
                      std::max(32, static_cast<int>(std::round(document_editor_height * canvas_->zoom()))));
  const auto resize_editor = [editor = QPointer<QTextEdit>(editor), this] {
    if (editor == nullptr) {
      return;
    }
    if (editor->property("patchy.relayoutingTextEditor").toBool()) {
      return;
    }
    relayout_text_editor(editor, true);
    reset_text_editor_scroll(editor);
    schedule_text_editor_preview(editor);
  };
  connect(editor, &QTextEdit::textChanged, editor, [this, editor = QPointer<QTextEdit>(editor), resize_editor] {
    mark_text_editor_changed(editor);
    resize_editor();
  });
  const auto lock_scroll = [editor = QPointer<QTextEdit>(editor)] {
    if (editor != nullptr) {
      reset_text_editor_scroll(editor);
    }
  };
  connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, editor, [lock_scroll](int) { lock_scroll(); });
  connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, editor, [lock_scroll](int) { lock_scroll(); });
  connect(editor->horizontalScrollBar(), &QScrollBar::rangeChanged, editor, [lock_scroll](int, int) { lock_scroll(); });
  connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, editor, [lock_scroll](int, int) { lock_scroll(); });
  connect(editor, &QTextEdit::cursorPositionChanged, editor, [this, editor = QPointer<QTextEdit>(editor)] {
    if (editor == nullptr) {
      return;
    }
    reset_text_editor_scroll(editor);
    sync_text_options_from_active_editor();
    sync_text_alignment_buttons_from_editor();
    update_text_editor_preview_caret(*editor, canvas_ != nullptr ? canvas_->zoom() : 1.0);
    update_text_editor_transform_overlay(editor);
  });
  connect(editor, &QTextEdit::selectionChanged, editor, [this, editor = QPointer<QTextEdit>(editor)] {
    sync_text_options_from_active_editor();
    if (editor != nullptr) {
      update_text_editor_preview_caret(*editor, canvas_ != nullptr ? canvas_->zoom() : 1.0);
      update_text_editor_transform_overlay(editor);
    }
  });
  editor->show();
  editor->installEventFilter(this);
  editor->viewport()->installEventFilter(this);
  resize_editor();
  if (editing_layer.has_value()) {
    QPoint click_viewport_point;
    if (initial_cursor_local_position.has_value()) {
      click_viewport_point =
          QPoint(static_cast<int>(std::round(initial_cursor_local_position->x() * canvas_->zoom())),
                 static_cast<int>(std::round(initial_cursor_local_position->y() * canvas_->zoom())));
    } else {
      const auto click_widget_point = canvas_->widget_position_for_document_point(requested_document_point);
      click_viewport_point = editor->viewport()->mapFrom(canvas_, click_widget_point);
    }
    editor->setTextCursor(editor->cursorForPosition(click_viewport_point));
  }
  update_text_editor_handles(editor);
  if (editor->property(kTextEditorPreviewEnabledProperty).toBool() &&
      (!editor->property(kTextEditorPreviewExpensiveProperty).toBool() || editing_layer_has_transformed_preview)) {
    update_text_editor_preview(editor);
  }
  schedule_text_editor_preview(editor);
  editor->setFocus(Qt::OtherFocusReason);
  sync_text_options_from_active_editor();
  sync_text_alignment_buttons_from_editor();
  update_text_editor_preview_caret(*editor, canvas_->zoom());
  refresh_text_color_button();

  auto committed = std::make_shared<bool>(false);
  const auto commit = [this, editor = QPointer<QTextEdit>(editor), editing_layer, committed] {
    if (*committed || editor == nullptr) {
      return;
    }
    *committed = true;
    const QPoint current_document_point(editor->property("patchy.documentTextX").toInt(),
                                        editor->property("patchy.documentTextY").toInt());
    commit_text_editor(editor, current_document_point, editing_layer);
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
    const auto text_color_dialog_has_focus_change = [this](QWidget* widget) {
      if (color_dialog_ == nullptr) {
        return false;
      }
      const auto target = color_dialog_->property("patchy.colorTarget").toString();
      return (target == QStringLiteral("text") || target == QStringLiteral("foreground")) &&
             (widget == color_dialog_ || color_dialog_->isAncestorOf(widget));
    };
    if (text_color_dialog_has_focus_change(old) || text_color_dialog_has_focus_change(now)) {
      return;
    }
    if (canvas_ != nullptr && now == canvas_ &&
        text_editor_resize_handle_at(canvas_->mapFromGlobal(QCursor::pos())) != nullptr) {
      return;
    }
    if (canvas_ != nullptr && now == canvas_) {
      auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
      if (overlay != nullptr && overlay->editor() == editor &&
          overlay->has_resize_handle_at_canvas_point(QPointF(canvas_->mapFromGlobal(QCursor::pos())))) {
        return;
      }
    }
    if (((left_editor && !entered_editor) || entered_canvas) && !entered_editor && !is_text_option_widget(now)) {
      if (entered_canvas && (QApplication::mouseButtons() & Qt::LeftButton) != 0) {
        swallow_next_canvas_left_press_ = true;
      }
      commit();
    }
  });
}

void MainWindow::cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id) {
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  const auto restore_existing_visibility =
      editor->property("patchy.editingLayerWasVisible").isValid()
          ? editor->property("patchy.editingLayerWasVisible").toBool()
          : true;
  remove_text_editor_preview(editor);
  remove_text_editor_transform_overlay(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();

  if (layer_id.has_value() && canvas_ != nullptr && has_active_document()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  refresh_text_color_button();
  statusBar()->showMessage(tr("Canceled text edit"));
}

void MainWindow::commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id) {
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  if (canvas_ == nullptr || !has_active_document()) {
    // A stray commit with no canvas/document to rasterize into (e.g. a focus
    // change while the owning tab is torn down): drop the edit but still
    // dismantle the editor chrome.  The helpers below tolerate a null
    // canvas_; document() would throw.
    remove_text_editor_preview(editor);
    remove_text_editor_transform_overlay(editor);
    remove_text_editor_handles(editor);
    editor->hide();
    editor->setParent(nullptr);
    editor->deleteLater();
    return;
  }
  // Untrimmed: the stored text must keep indexing the rich-text/paragraph runs, which are
  // extracted from the full editor document -- old PSDs often start with a blank line, and
  // trimming here shifted every run on the next edit session.  Trim only for the emptiness test.
  const auto text = editor->toPlainText();
  // Applying always keeps what the session showed.  A box/PSD-frame session still displays
  // Photoshop's raster (kTextEditorSourceRasterPreviewProperty), so an unchanged apply keeps that
  // raster; a point-text session shows the live render from entry, so applying commits it even
  // with no text change -- snapping back to the raster after the user saw the live layout was
  // jarring.  Escape is the way to leave the original pixels untouched.
  const auto source_raster_unchanged = editor->property(kTextEditorSourceRasterPreviewProperty).toBool() &&
                                       !editor->property(kTextEditorChangedProperty).toBool();
  const auto restore_existing_visibility =
      editor->property("patchy.editingLayerWasVisible").isValid()
          ? editor->property("patchy.editingLayerWasVisible").toBool()
          : true;
  const auto restore_hidden_text_layer = [this, layer_id, restore_existing_visibility] {
    if (!layer_id.has_value()) {
      return;
    }
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  };
  remove_text_editor_preview(editor);
  remove_text_editor_transform_overlay(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();
  if (source_raster_unchanged) {
    restore_hidden_text_layer();
    refresh_text_color_button();
    return;
  }
  if (text.trimmed().isEmpty()) {
    restore_hidden_text_layer();
    return;
  }

  const auto text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());
  const auto boxed_text =
      text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto text_anti_alias = std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor->property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(*editor),
                                                  display_text_family_from_font(editor->font()));
  }
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  auto rendered = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                     rich_text_runs, text_editor_render_local_rect(*editor),
                                                     text_editor_metric_scale(*editor));
  if (rendered.pixels.empty()) {
    restore_hidden_text_layer();
    return;
  }
  if (!boxed_text && !editor->property("patchy.usesPsdTextFrame").toBool()) {
    bool updated_transform = false;
    if (layer_id.has_value()) {
      if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
        updated_transform = update_text_editor_transform_from_psd_local_bounds(*editor, *layer, rendered.pixels,
                                                                              boxed_text);
      }
    }
    if (!updated_transform) {
      update_text_editor_transform_from_source_anchor(*editor, rendered.pixels);
    }
    document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                            editor->property("patchy.documentTextY").toInt());
  }

  auto committed_bounds = rendered_text_bounds_for_editor(*editor, document_point, rendered);
  auto pixels = std::move(rendered.pixels);
  const auto local_text_height = pixels.height();
  std::optional<QTransform> text_transform;
  std::optional<LayerAffineTransform> text_affine_transform;
  bool psd_anchored_text = false;
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      psd_anchored_text = layer->metadata().contains(kLayerMetadataPsdTextTransform);
      if (const auto override_transform = text_editor_transform_override(*editor); override_transform.has_value()) {
        text_affine_transform = *override_transform;
        text_transform = qtransform_from_affine(*override_transform);
      } else {
        text_transform = patchy_text_transform_for_layer(*layer);
        text_affine_transform = canonical_text_affine_transform_for_layer(*layer);
      }
    }
  }
  const auto anchor_places_rendered_pixels =
      text_transform.has_value() && text_editor_render_local_rect(*editor).has_value() &&
      text_editor_source_visible_anchor(*editor).has_value() &&
      !qtransform_has_non_translation_linear_part(*text_transform);
  if (text_transform.has_value() && !editor->property("patchy.usesPsdTextFrame").toBool() &&
      !anchor_places_rendered_pixels) {
    auto transform_for_pixels = *text_transform;
    const bool has_local_offset =
        text_editor_render_local_rect(*editor).has_value() &&
        (std::abs(rendered.local_rect.left()) > 0.0001 || std::abs(rendered.local_rect.top()) > 0.0001);
    if (has_local_offset) {
      transform_for_pixels = qtransform_from_affine(
          affine_with_local_translation(affine_from_qtransform(*text_transform), rendered.local_rect.topLeft()));
    }
    // For a scaled/rotated point-text layer, re-rasterize the glyphs *through* the transform so the
    // committed result stays crisp instead of resampling the base-size bitmap (which is what made an
    // edit of a scaled PSD layer go blocky).  Eligibility rules live on the shared helper.
    bool used_crisp = false;
    if (auto crisp = render_crisp_transformed_text_for_editor(*editor, psd_anchored_text, boxed_text,
                                                              has_local_offset, settings, text_color, text_width,
                                                              paragraph_runs, rich_text_runs, *text_transform,
                                                              transform_for_pixels);
        crisp.has_value()) {
      committed_bounds = crisp->bounds;
      pixels = std::move(crisp->pixels);
      used_crisp = true;
    }
    if (!used_crisp) {
      auto transformed = apply_text_transform_to_pixels(pixels, transform_for_pixels);
      pixels = std::move(transformed.pixels);
      committed_bounds = transformed.bounds;
    }
  }

  if (layer_id.has_value() && layer_id_locks_image_pixels(*layer_id)) {
    restore_hidden_text_layer();
    refresh_text_color_button();
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
    }
  }
  push_undo_snapshot(tr("Type"));
  const auto name = text_layer_auto_name(settings.text);
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      // Follow the text content only for auto-derived names; a name the user (or the source PSD)
      // chose stays put.  No "Text: " prefix -- the layer list already shows a type badge.
      if (text_layer_name_is_auto(*layer)) {
        layer->set_name(name.toStdString());
      }
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(committed_bounds);
      layer->set_visible(restore_existing_visibility);
      store_patchy_text_metadata(*layer, settings, text_color, rich_text_runs, paragraph_runs, text_width,
                                 boxed_text ? text_height : local_text_height);
      if (editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool()) {
        layer->metadata()[kLayerMetadataTextLineAwareBoxPreview] = "true";
      } else {
        layer->metadata().erase(kLayerMetadataTextLineAwareBoxPreview);
      }
      if (boxed_text) {
        layer->metadata()[kLayerMetadataTextTransform] =
            serialize_layer_affine_transform(committed_text_transform(document_point, text_affine_transform));
      } else if (text_affine_transform.has_value()) {
        layer->metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform(*text_affine_transform);
      }
    }
  } else {
    Layer text_layer(document().allocate_layer_id(), name.toStdString(), std::move(pixels));
    text_layer.set_bounds(
        Rect{document_point.x(), document_point.y(), text_layer.pixels().width(), text_layer.pixels().height()});
    store_patchy_text_metadata(text_layer, settings, text_color, rich_text_runs, paragraph_runs, text_width,
                               boxed_text ? text_height : text_layer.pixels().height());
    if (editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool()) {
      text_layer.metadata()[kLayerMetadataTextLineAwareBoxPreview] = "true";
    }
    if (boxed_text) {
      text_layer.metadata()[kLayerMetadataTextTransform] =
          serialize_layer_affine_transform(committed_text_transform(document_point, std::nullopt));
    }
    document().add_layer(std::move(text_layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  refresh_text_color_button();
  statusBar()->showMessage(tr("Created text layer"));
}

bool MainWindow::commit_active_text_editor() {
  if (canvas_ == nullptr) {
    return false;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return false;
  }
  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  std::optional<LayerId> layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  commit_text_editor(editor, document_point, layer_id);
  return true;
}

void MainWindow::finish_active_text_editor() {
  if (!commit_active_text_editor()) {
    return;
  }
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  try {
    const auto identifier_text = identifier.toStdString();
    const auto* filter = filters_.find(identifier_text);
    if (filter == nullptr) {
      throw std::invalid_argument("Unknown filter identifier");
    }
    const auto display_name = filter_display_name(*filter);
    const auto dialog_spec = filter_dialog_spec_for(*filter);
    const auto selection = canvas_->selected_document_region();
    const auto bounds = layer->bounds();
    auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
    // Tracks the bounds the layer currently shows in the preview. Blur-family
    // filters grow the layer, so each swap must repaint the union of the previous
    // and new bounds to erase any stale halo left behind when the layer shrinks.
    auto last_preview_bounds = std::make_shared<Rect>(bounds);
    const auto foreground = canvas_->primary_color();
    const auto background = canvas_->secondary_color();
    auto preview_registry = std::make_shared<FilterRegistry>(filters_);
    auto preview_state = std::make_shared<AsyncPixelPreviewState<FilterPreviewSettings>>();
    preview_state->start =
        [this, preview_state, active, original_pixels, last_preview_bounds, selection, bounds, identifier, foreground,
         background, preview_registry](const FilterPreviewSettings& settings) {
          if (!settings.preview_enabled) {
            preview_state->pending.reset();
            ++preview_state->generation;
            if (auto* preview_layer = document().find_layer(*active); preview_layer != nullptr) {
              set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
              if (canvas_ != nullptr) {
                canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
              }
              *last_preview_bounds = bounds;
            }
            return;
          }

          preview_state->in_flight = true;
          const auto generation = ++preview_state->generation;
          auto result_bounds = std::make_shared<Rect>(bounds);
          auto* app = QCoreApplication::instance();
          auto window = QPointer<MainWindow>(this);
          std::thread([app, window, preview_state, generation, original_pixels, result_bounds, last_preview_bounds,
                       selection, bounds, identifier, settings, foreground, background, preview_registry, active] {
            auto result = std::make_shared<PixelBuffer>();
            auto error = std::make_shared<QString>();
            try {
              *result = build_filter_preview_pixels(*original_pixels, selection, bounds, identifier, *preview_registry,
                                                    settings, foreground, background, nullptr, &*result_bounds);
            } catch (const std::exception& caught) {
              *error = QString::fromUtf8(caught.what());
            }
            if (app == nullptr) {
              return;
            }
            QMetaObject::invokeMethod(
                app,
                [window, preview_state, generation, active, result_bounds, last_preview_bounds, result,
                 error]() mutable {
                  preview_state->in_flight = false;
                  const auto has_pending = preview_state->pending.has_value();
                  if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                      window != nullptr) {
                    if (error->isEmpty()) {
                      if (auto* layer = window->document().find_layer(*active); layer != nullptr) {
                        set_layer_pixels_with_bounds(*layer, std::move(*result), *result_bounds);
                        if (window->canvas_ != nullptr) {
                          window->canvas_->document_changed(
                              to_qrect(*last_preview_bounds).united(to_qrect(*result_bounds)));
                        }
                        *last_preview_bounds = *result_bounds;
                      }
                    } else {
                      window->statusBar()->showMessage(
                          window->tr("Filter preview failed: %1").arg(*error));
                    }
                  }
                  if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
                    auto next = *preview_state->pending;
                    preview_state->pending.reset();
                    preview_state->start(next);
                  }
                },
                Qt::QueuedConnection);
          }).detach();
        };
    const auto preview_changed = [preview_state](FilterPreviewSettings settings) {
      enqueue_async_pixel_preview(preview_state, std::move(settings), !settings.preview_enabled);
    };

    auto preview_edit_lock = lock_preview_dialog_edits();
    const auto settings = request_filter_settings(this, dialog_spec, preview_changed);
    close_async_pixel_preview(preview_state);
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
    canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
    *last_preview_bounds = bounds;
    preview_edit_lock.release();
    if (!settings.has_value()) {
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }

    if (canvas_ != nullptr) {
      canvas_->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("filterProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    int last_progress_value = -1;
    FilterProgress filter_progress{[&](int completed, int total, const QString& detail) {
      const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
      if (value != last_progress_value) {
        progress.setValue(value);
        if (!detail.isEmpty()) {
          progress.setLabelText(tr("Applying %1...\n%2").arg(display_name, detail));
        }
        last_progress_value = value;
        QApplication::processEvents();
      }
      if (canvas_ != nullptr) {
        canvas_->tick_processing_operation();
      }
      return !progress.wasCanceled();
    }};

    PixelBuffer final_pixels;
    Rect final_bounds = bounds;
    try {
      final_pixels = build_filter_preview_pixels(*original_pixels, selection, bounds, identifier, filters_,
                                                 FilterPreviewSettings{true, *settings}, foreground, background,
                                                 &filter_progress, &final_bounds);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(*active);
      if (layer != nullptr) {
        set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
        canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
        *last_preview_bounds = bounds;
      }
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }
    if (pixel_buffers_equal(final_pixels, *original_pixels)) {
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      return;
    }

    push_undo_snapshot(tr("Filter: %1").arg(display_name));
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    set_layer_pixels_with_bounds(*layer, std::move(final_pixels), final_bounds);
    canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(final_bounds)));
    statusBar()->showMessage(tr("Applied %1").arg(display_name));
  } catch (const std::exception& error) {
    if (active.has_value()) {
      if (auto* restore_layer = doc.find_layer(*active); restore_layer != nullptr) {
        canvas_->document_changed(to_qrect(restore_layer->bounds()));
      }
    }
    show_critical_message(this, tr("Filter failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("filterFailedMessageBox"));
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
      register_document_action(action);
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
    settings.levels = sanitized_levels_adjustment(levels);
    update_adjustment_layer_preview(tr("Levels"), settings, enabled, preview_id, restore_active_layer);
  };

  const PixelBuffer* histogram_source = nullptr;
  if (restore_active_layer.has_value()) {
    if (auto* layer = document().find_layer(*restore_active_layer); editable_rgb8_layer(layer)) {
      histogram_source = &layer->pixels();
    }
  }
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_levels_settings(this, preview_changed, {}, histogram_source);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }
  apply_levels_adjustment(*settings, true);
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using LevelsPreviewRequest = AdjustmentPixelPreviewRequest<LevelsSettings>;
  auto preview_state = std::make_shared<AsyncPixelPreviewState<LevelsPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels,
                          selection](const LevelsPreviewRequest& request) {
    if (!request.enabled || !levels_settings_have_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_levels_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state](bool enabled, const LevelsSettings& settings) {
    enqueue_async_pixel_preview(preview_state, LevelsPreviewRequest{enabled, settings},
                                !enabled || !levels_settings_have_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_levels_settings(this, preview_changed, {}, original_pixels.get());
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }

  auto final_pixels = *original_pixels;
  if (levels_settings_have_effect(*settings)) {
    const auto display_name = tr("Levels");
    if (canvas_ != nullptr) {
      canvas_->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("adjustmentProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    auto filter_progress = progress_dialog_filter_progress(
        progress, [this, display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
        QEventLoop::AllEvents, [this] {
          if (canvas_ != nullptr) {
            canvas_->tick_processing_operation();
          }
        });
    try {
      apply_levels_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(active_id);
      if (layer != nullptr) {
        set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
        canvas_->document_changed(to_qrect(bounds));
      }
      statusBar()->showMessage(tr("Cancelled Levels"));
      return;
    }
  }
  if (pixel_buffers_equal(final_pixels, *original_pixels)) {
    statusBar()->showMessage(tr("%1 made no changes").arg(tr("Levels")));
    return;
  }
  push_undo_snapshot(tr("Levels"));
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, std::move(final_pixels), bounds);
  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied %1").arg(tr("Levels")));
}

void MainWindow::apply_levels_adjustment(const LevelsSettings& levels, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Levels;
  settings.levels = sanitized_levels_adjustment(levels);
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

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
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
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using CurvesPreviewRequest = AdjustmentPixelPreviewRequest<CurvesSettings>;
  const auto curves_has_effect = [](const CurvesSettings& settings) {
    return !(settings.shadow_output == 0 && settings.midtone_output == 128 && settings.highlight_output == 255);
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<CurvesPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          curves_has_effect](const CurvesPreviewRequest& request) {
    if (!request.enabled || !curves_has_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_curves_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state, curves_has_effect](bool enabled, const CurvesSettings& settings) {
    enqueue_async_pixel_preview(preview_state, CurvesPreviewRequest{enabled, settings},
                                !enabled || !curves_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
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

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
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
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using HueSaturationPreviewRequest = AdjustmentPixelPreviewRequest<HueSaturationSettings>;
  const auto hue_saturation_has_effect = [](const HueSaturationSettings& settings) {
    return !(settings.hue_shift == 0 && settings.saturation_delta == 0 && settings.lightness_delta == 0);
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<HueSaturationPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          hue_saturation_has_effect](const HueSaturationPreviewRequest& request) {
    if (!request.enabled || !hue_saturation_has_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_hue_saturation_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state, hue_saturation_has_effect](bool enabled,
                                                                         const HueSaturationSettings& settings) {
    enqueue_async_pixel_preview(preview_state, HueSaturationPreviewRequest{enabled, settings},
                                !enabled || !hue_saturation_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
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

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
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
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using ColorBalancePreviewRequest = AdjustmentPixelPreviewRequest<ColorBalanceSettings>;
  const auto color_balance_has_effect = [](const ColorBalanceSettings& settings) {
    return !(settings.cyan_red == 0 && settings.magenta_green == 0 && settings.yellow_blue == 0);
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<ColorBalancePreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          color_balance_has_effect](const ColorBalancePreviewRequest& request) {
    if (!request.enabled || !color_balance_has_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_color_balance_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state, color_balance_has_effect](bool enabled,
                                                                         const ColorBalanceSettings& settings) {
    enqueue_async_pixel_preview(preview_state, ColorBalancePreviewRequest{enabled, settings},
                                !enabled || !color_balance_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
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
    layer.set_mask(LayerMask{to_core_rect(selection_rect), selection_mask_pixels(*canvas_, selection_rect), 0, false});
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
  // Adjustment dialogs are preview dialogs; opening one on top of another
  // preview dialog (e.g. by double-clicking a layer row while one is open)
  // stacks nested event loops and crashes.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Adjustment) {
    statusBar()->showMessage(tr("Select an adjustment layer to edit its settings"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
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
  auto preview_edit_lock = lock_preview_dialog_edits();
  switch (original_settings->kind) {
    case AdjustmentKind::Levels: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled, const LevelsSettings& levels) {
        auto settings = *original_settings;
        settings.levels = sanitized_levels_adjustment(levels);
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_levels_settings(this, preview_changed,
                                                  LevelsSettings{original_settings->levels.black_input,
                                                                 original_settings->levels.white_input,
                                                                 original_settings->levels.gamma_percent,
                                                                 original_settings->levels.black_output,
                                                                 original_settings->levels.white_output,
                                                                 original_settings->levels.channel,
                                                                 original_settings->levels.red,
                                                                 original_settings->levels.green,
                                                                 original_settings->levels.blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->levels = sanitized_levels_adjustment(*result);
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
  preview_edit_lock.release();
  if (!accepted_settings.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cancelled adjustment edit"));
    return;
  }

  push_undo_snapshot(tr("Edit %1 adjustment").arg(localized_adjustment_display_name(original_settings->kind)));
  apply_settings(*accepted_settings);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Updated adjustment layer"));
}

void MainWindow::add_layer() {
  auto& doc = document();
  const auto name = default_new_layer_name(doc);
  auto anchor_id = doc.active_layer_id();
  const auto selected_ids = selected_layer_ids();
  if (!selected_ids.empty()) {
    anchor_id = selected_ids.front();
  }

  push_undo_snapshot(tr("New layer"));
  auto layer_pixels =
      make_solid_pixels(doc.width(), doc.height(), QColor(0, 0, 0, 0), PixelFormat::rgba8());
  Layer layer(doc.allocate_layer_id(), name.toStdString(), std::move(layer_pixels));
  const auto layer_id = layer.id();
  layer.set_opacity(1.0F);
  layer.set_blend_mode(BlendMode::Normal);
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::create_layer_folder() {
  create_layer_folder_from_layers(selected_layer_ids());
}

void MainWindow::create_layer_folder_from_layers(std::vector<LayerId> ids) {
  auto& doc = document();
  std::set<std::string> existing_names;
  collect_layer_names(doc.layers(), existing_names);

  int suffix = 1;
  std::string name;
  do {
    name = tr("Folder %1").arg(suffix++).toStdString();
  } while (existing_names.contains(name));

  auto grouped_ids = root_drop_layer_ids(doc.layers(), ids);
  const auto destination = common_sibling_grouping_destination(doc.layers(), grouped_ids);

  push_undo_snapshot(tr("New folder"));
  Layer folder(doc.allocate_layer_id(), name, LayerKind::Group);
  const auto folder_id = folder.id();
  folder.set_blend_mode(BlendMode::PassThrough);
  if (!grouped_ids.empty()) {
    std::vector<Layer> grouped_top_to_bottom;
    grouped_top_to_bottom.reserve(grouped_ids.size());
    for (const auto id : grouped_ids) {
      if (auto grouped = take_layer_from_tree(doc.layers(), id); grouped.has_value()) {
        grouped_top_to_bottom.push_back(std::move(*grouped));
      }
    }
    for (auto it = grouped_top_to_bottom.rbegin(); it != grouped_top_to_bottom.rend(); ++it) {
      folder.add_child(std::move(*it));
    }
  }

  auto* siblings = destination.has_value() && destination->siblings != nullptr ? destination->siblings : &doc.layers();
  const auto insert_index =
      destination.has_value() ? std::min(destination->insert_index, siblings->size()) : siblings->size();
  siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(insert_index), std::move(folder));
  doc.set_active_layer(folder_id);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created folder"));
}

void MainWindow::layer_via_copy() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
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
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  const auto ids = selected_or_active_layer_ids();
  auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to cut to a new layer"));
    return;
  }
  if (std::any_of(payload->source_layer_ids.begin(), payload->source_layer_ids.end(),
                  [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
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
    affected = unite_rect(affected, patchy::clear_rect(doc, id, clear_area, options));
  }

  Layer cut_layer(doc.allocate_layer_id(), tr("Layer Via Cut").toStdString(), std::move(payload->pixels));
  cut_layer.set_bounds(Rect{payload->origin.x(), payload->origin.y(), cut_layer.pixels().width(), cut_layer.pixels().height()});
  doc.add_layer(std::move(cut_layer));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(tr("Cut selection to a new layer"));
}

void MainWindow::add_layer_mask() {
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  const auto from_selection = !selection.isEmpty() && !selection_rect.isEmpty();
  if (!from_selection && layer->mask().has_value()) {
    statusBar()->showMessage(tr("Layer already has a mask"));
    return;
  }

  push_undo_snapshot(tr("Add layer mask"));
  const auto before = layer_render_bounds(*layer);
  if (from_selection) {
    auto mask_pixels = selection_mask_pixels(*canvas_, selection_rect);
    layer->set_mask(LayerMask{to_core_rect(selection_rect), std::move(mask_pixels), 0, false});
  } else {
    PixelBuffer mask_pixels(doc.width(), doc.height(), PixelFormat::gray8());
    mask_pixels.clear(255);
    layer->set_mask(LayerMask{Rect{0, 0, doc.width(), doc.height()}, std::move(mask_pixels), 255, false});
  }
  const auto after = layer_render_bounds(*layer);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(unite_rect(before, after)));
  refresh_layer_list();
  set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
  statusBar()->showMessage(from_selection
                               ? tr("Added layer mask from selection")
                               : tr("Added layer mask. Paint with black to hide and white to reveal."));
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  push_undo_snapshot(tr("Delete layer mask"));
  const auto affected = layer_render_bounds(*layer);
  layer->clear_mask();
  layer->metadata().erase(kLayerMetadataMaskLinked);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, false);
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    refresh_layer_controls();
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

void MainWindow::set_layer_edit_target_ui(CanvasWidget::LayerEditTarget target, bool announce) {
  if (canvas_ == nullptr) {
    return;
  }
  if (target == CanvasWidget::LayerEditTarget::Mask) {
    const auto active = document().active_layer_id();
    const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
    if (layer == nullptr || !layer->mask().has_value()) {
      target = CanvasWidget::LayerEditTarget::Content;
    }
  }
  if (target == CanvasWidget::LayerEditTarget::Content &&
      canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale) {
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
  }
  canvas_->set_layer_edit_target(target);
  canvas_->update();
  update_layer_target_styles(layer_list_, document().active_layer_id(), target);
  refresh_layer_controls();
  if (announce) {
    statusBar()->showMessage(target == CanvasWidget::LayerEditTarget::Mask ? tr("Editing layer mask")
                                                                           : tr("Editing layer pixels"));
  }
}

void MainWindow::set_mask_overlay_shown(bool shown) {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->set_mask_display_mode(shown ? CanvasWidget::MaskDisplayMode::Overlay
                                       : CanvasWidget::MaskDisplayMode::None);
  refresh_layer_controls();
  statusBar()->showMessage(shown ? tr("Mask overlay shown. Red marks the areas the mask hides.")
                                 : tr("Mask overlay hidden"));
}

void MainWindow::set_active_layer_mask_disabled(bool disabled) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    statusBar()->showMessage(tr("Active layer has no mask"));
    refresh_layer_controls();
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    refresh_layer_controls();
    return;
  }
  if (layer->mask()->disabled == disabled) {
    refresh_layer_controls();
    return;
  }

  push_undo_snapshot(disabled ? tr("Disable layer mask") : tr("Enable layer mask"));
  layer->mask()->disabled = disabled;
  canvas_->document_changed(to_qrect(layer->bounds()));
  // The mask overlay spans the whole canvas, so a partial repaint of the layer
  // bounds is not enough when it appears or disappears.
  canvas_->update();
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(disabled ? tr("Layer mask disabled") : tr("Layer mask enabled"));
}

void MainWindow::invert_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    statusBar()->showMessage(tr("Active layer has no mask"));
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  push_undo_snapshot(tr("Invert layer mask"));
  auto& mask = *layer->mask();
  mask.default_color = static_cast<std::uint8_t>(255 - mask.default_color);
  if (!mask.pixels.empty()) {
    for (auto& value : mask.pixels.data()) {
      value = static_cast<std::uint8_t>(255 - value);
    }
  }
  const auto dirty = unite_rect(layer_render_bounds(*layer), mask.bounds.empty() ? layer->bounds() : mask.bounds);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(dirty));
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Inverted layer mask"));
}

void MainWindow::apply_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    statusBar()->showMessage(tr("Active layer has no mask"));
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  if (layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Apply mask supports editable 8-bit pixel layers"));
    return;
  }

  push_undo_snapshot(tr("Apply layer mask"));
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  if (channels >= 4) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto mask_alpha = layer_mask_value_at(*layer, bounds.x + x, bounds.y + y);
        px[3] = static_cast<std::uint8_t>((static_cast<int>(px[3]) * static_cast<int>(mask_alpha)) / 255);
      }
    }
  } else {
    PixelBuffer rgba(pixels.width(), pixels.height(), PixelFormat::rgba8());
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* src = pixels.pixel(x, y);
        auto* dst = rgba.pixel(x, y);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = layer_mask_value_at(*layer, bounds.x + x, bounds.y + y);
      }
    }
    pixels = std::move(rgba);
  }
  layer->clear_mask();
  layer->metadata().erase(kLayerMetadataMaskLinked);
  if (canvas_ != nullptr) {
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
    canvas_->invalidate_mask_display();
    canvas_->document_changed(to_qrect(bounds));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Applied layer mask"));
}

void MainWindow::duplicate_active_layer() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  duplicate_layers(selected_or_active_layer_ids());
}

void MainWindow::duplicate_layers(std::vector<LayerId> ids) {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
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

  const auto new_name = request_text_input(this, QStringLiteral("patchyRenameLayerDialog"), tr("Rename Layer"),
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
  // A layer-style dialog is itself a preview dialog. Never open a second one on
  // top of an existing one (e.g. by double-clicking another layer in the list
  // while one is open) -- the stacked nested event loops crash.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (canvas_ != nullptr &&
      canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }
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
  auto set_layer_style_settings = [](Layer& target, const LayerStyleSettings& settings) {
    target.set_opacity(static_cast<float>(settings.opacity) / 100.0F);
    target.set_blend_mode(settings.blend_mode);
    target.layer_style() = settings.style;
  };
  auto apply_preview_settings = [this, &doc, layer_id, set_layer_style_settings](const LayerStyleSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    set_layer_style_settings(*target, settings);
    if (canvas_ != nullptr) {
      canvas_->document_changed_async_preview();
    }
  };
  auto apply_committed_settings = [this, &doc, layer_id, set_layer_style_settings](const LayerStyleSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    set_layer_style_settings(*target, settings);
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

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_layer_style_settings(this, *layer, apply_preview_settings);
  if (!settings.has_value()) {
    restore_original();
    preview_edit_lock.release();
    refresh_layer_list();
    refresh_layer_controls();
    return;
  }

  restore_original();
  preview_edit_lock.release();
  push_undo_snapshot(tr("Layer style"));
  if (auto* target = doc.find_layer(layer_id); target != nullptr) {
    clear_layer_psd_style_source(*target);
  }
  apply_committed_settings(*settings);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated layer style"));
}

void MainWindow::copy_active_layer_style() {
  if (!has_active_document()) {
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.size() != 1U) {
    refresh_layer_style_action_states();
    return;
  }

  const auto* layer = document().find_layer(ids.front());
  if (layer == nullptr) {
    refresh_layer_style_action_states();
    return;
  }

  layer_style_clipboard_ = layer->layer_style();
  update_history(tr("Copy layer style"));
  statusBar()->showMessage(tr("Copied layer style"));
  refresh_layer_style_action_states();
}

void MainWindow::paste_layer_style_to_selected_layers() {
  if (!has_active_document() || !layer_style_clipboard_.has_value()) {
    refresh_layer_style_action_states();
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  std::vector<LayerId> targets;
  targets.reserve(ids.size());
  const auto& doc = document();
  for (const auto id : ids) {
    if (doc.find_layer(id) != nullptr) {
      targets.push_back(id);
    }
  }
  if (targets.empty()) {
    refresh_layer_style_action_states();
    return;
  }

  auto& mutable_doc = document();
  push_undo_snapshot(tr("Paste layer style"));
  Rect affected;
  std::size_t pasted_count = 0;
  for (const auto id : targets) {
    auto* layer = mutable_doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = *layer_style_clipboard_;
    affected = unite_rect(affected, layer_render_bounds(*layer));
    ++pasted_count;
  }

  refresh_layer_list();
  refresh_layer_controls();
  if (canvas_ != nullptr) {
    canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  }
  statusBar()->showMessage(
      tr("Pasted layer style to %1 layer(s)").arg(static_cast<qulonglong>(pasted_count)));
}

void MainWindow::delete_selected_layer_styles() {
  if (!has_active_document()) {
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  std::vector<LayerId> targets;
  targets.reserve(ids.size());
  const auto& doc = document();
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer != nullptr && !layer->layer_style().empty()) {
      targets.push_back(id);
    }
  }
  if (targets.empty()) {
    refresh_layer_style_action_states();
    return;
  }

  auto& mutable_doc = document();
  push_undo_snapshot(tr("Delete layer style"));
  Rect affected;
  std::size_t deleted_count = 0;
  for (const auto id : targets) {
    auto* layer = mutable_doc.find_layer(id);
    if (layer == nullptr || layer->layer_style().empty()) {
      continue;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = {};
    affected = unite_rect(affected, layer_render_bounds(*layer));
    ++deleted_count;
  }

  refresh_layer_list();
  refresh_layer_controls();
  if (canvas_ != nullptr) {
    canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  }
  statusBar()->showMessage(
      tr("Deleted layer style from %1 layer(s)").arg(static_cast<qulonglong>(deleted_count)));
}

void MainWindow::refresh_layer_style_action_states() {
  bool can_copy = false;
  bool can_paste = false;
  bool can_delete = false;
  if (has_active_document() && !preview_dialog_edit_locked()) {
    const auto ids = selected_or_active_layer_ids();
    int valid_layer_count = 0;
    for (const auto id : ids) {
      const auto* layer = document().find_layer(id);
      if (layer == nullptr) {
        continue;
      }
      ++valid_layer_count;
      can_delete = can_delete || !layer->layer_style().empty();
    }
    can_copy = ids.size() == 1U && valid_layer_count == 1;
    can_paste = layer_style_clipboard_.has_value() && valid_layer_count > 0;
  }

  if (layer_copy_style_action_ != nullptr) {
    layer_copy_style_action_->setEnabled(can_copy);
  }
  if (layer_paste_style_action_ != nullptr) {
    layer_paste_style_action_->setEnabled(can_paste);
  }
  if (layer_delete_style_action_ != nullptr) {
    layer_delete_style_action_->setEnabled(can_delete);
  }
}

void MainWindow::rasterize_active_layers() {
  finish_active_text_editor();
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  struct PendingRasterize {
    LayerId id{};
    RasterizedLayerPixels pixels;
    Rect before;
    bool clear_text{false};
  };
  auto& doc = document();
  std::vector<PendingRasterize> pending;
  pending.reserve(ids.size());
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer_id_locks_image_pixels(id) || !layer_can_rasterize(*layer)) {
      continue;
    }
    auto rasterized = render_rasterized_layer_pixels(doc, *layer, false);
    if (!rasterized.has_value()) {
      continue;
    }
    pending.push_back(PendingRasterize{id, std::move(*rasterized), layer_render_bounds(*layer),
                                       layer->kind() == LayerKind::Text || layer_is_text(*layer)});
  }
  if (pending.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      statusBar()->showMessage(tr("Layer pixels are locked."));
      return;
    }
    statusBar()->showMessage(tr("No rasterizable layers selected"));
    return;
  }

  push_undo_snapshot(pending.size() == 1U ? tr("Rasterize layer") : tr("Rasterize layers"));
  Rect affected;
  for (auto& change : pending) {
    auto* layer = doc.find_layer(change.id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, change.before);
    layer->set_pixels(std::move(change.pixels.pixels));
    layer->set_bounds(change.pixels.bounds);
    if (change.clear_text) {
      layer->set_name(rasterized_text_layer_name(*layer).toStdString());
      clear_layer_text_metadata(*layer);
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  statusBar()->showMessage(pending.size() == 1U ? tr("Rasterized layer") : tr("Rasterized layers"));
}

void MainWindow::rasterize_active_layer_styles() {
  finish_active_text_editor();
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  struct PendingRasterizeStyle {
    LayerId id{};
    RasterizedLayerPixels pixels;
    Rect before;
    bool clear_text{false};
  };
  auto& doc = document();
  std::vector<PendingRasterizeStyle> pending;
  pending.reserve(ids.size());
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer_id_locks_image_pixels(id) || !layer_can_rasterize_layer_style(*layer)) {
      continue;
    }
    auto rasterized = render_rasterized_layer_pixels(doc, *layer, true);
    if (!rasterized.has_value()) {
      continue;
    }
    pending.push_back(PendingRasterizeStyle{id, std::move(*rasterized), layer_render_bounds(*layer),
                                            layer->kind() == LayerKind::Text || layer_is_text(*layer)});
  }
  if (pending.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      statusBar()->showMessage(tr("Layer pixels are locked."));
      return;
    }
    statusBar()->showMessage(tr("No layer styles to rasterize"));
    return;
  }

  push_undo_snapshot(pending.size() == 1U ? tr("Rasterize layer style") : tr("Rasterize layer styles"));
  Rect affected;
  for (auto& change : pending) {
    auto* layer = doc.find_layer(change.id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, change.before);
    layer->set_pixels(std::move(change.pixels.pixels));
    layer->set_bounds(change.pixels.bounds);
    layer->layer_style() = {};
    if (change.clear_text) {
      layer->set_name(rasterized_text_layer_name(*layer).toStdString());
      clear_layer_text_metadata(*layer);
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  statusBar()->showMessage(pending.size() == 1U ? tr("Rasterized layer style") : tr("Rasterized layer styles"));
}

void MainWindow::delete_active_layer() {
  delete_layers(selected_or_active_layer_ids());
}

void MainWindow::delete_layers(std::vector<LayerId> ids) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
    return;
  }
  ids = root_drop_layer_ids(document().layers(), ids);
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
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
    return;
  }
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

void MainWindow::show_layer_context_menu(QPoint position) {
  if (layer_list_ == nullptr || !has_active_document()) {
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
  const auto has_rasterizable_layer = std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && !layer_id_locks_image_pixels(id) && layer_can_rasterize(*layer);
  });
  const auto has_rasterizable_layer_style = std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && !layer_id_locks_image_pixels(id) && layer_can_rasterize_layer_style(*layer);
  });

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
    refresh_layer_style_action_states();
    if (layer_copy_style_action_ != nullptr) {
      menu.addAction(layer_copy_style_action_);
    }
    if (layer_paste_style_action_ != nullptr) {
      menu.addAction(layer_paste_style_action_);
    }
    if (layer_delete_style_action_ != nullptr) {
      menu.addAction(layer_delete_style_action_);
    }
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
  auto* merge_down_action =
      menu.addAction(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)), tr("Merge Down"));
  auto* merge_visible_action = menu.addAction(simple_icon(QStringLiteral("merge")), tr("Merge Visible to New Layer"));
  if (layer_rasterize_action_ != nullptr) {
    layer_rasterize_action_->setEnabled(has_rasterizable_layer);
    menu.addAction(layer_rasterize_action_);
  }
  if (layer_rasterize_layer_style_action_ != nullptr) {
    layer_rasterize_layer_style_action_->setEnabled(has_rasterizable_layer_style);
    menu.addAction(layer_rasterize_layer_style_action_);
  }
  menu.addSeparator();
  auto* visibility_action = menu.addAction(tr("Visible"));
  visibility_action->setCheckable(true);
  visibility_action->setChecked(active_layer == nullptr || active_layer->visible());
  auto* lock_menu = menu.addMenu(tr("Lock"));
  const auto all_selected_have_lock = [this, &ids](LayerLockFlags flag) {
    return !ids.empty() && std::all_of(ids.begin(), ids.end(), [this, flag](LayerId id) {
      const auto* layer = document().find_layer(id);
      return layer != nullptr && (layer_lock_flags(*layer) & flag) == flag;
    });
  };
  auto* transparent_lock_action = lock_menu->addAction(tr("Lock Transparent Pixels"));
  transparent_lock_action->setCheckable(true);
  transparent_lock_action->setChecked(all_selected_have_lock(kLayerLockTransparentPixels));
  auto* image_lock_action = lock_menu->addAction(tr("Lock Image Pixels"));
  image_lock_action->setCheckable(true);
  image_lock_action->setChecked(all_selected_have_lock(kLayerLockImagePixels));
  auto* position_lock_action = lock_menu->addAction(tr("Lock Position"));
  position_lock_action->setCheckable(true);
  position_lock_action->setChecked(all_selected_have_lock(kLayerLockPosition));
  lock_menu->addSeparator();
  auto* all_lock_action = lock_menu->addAction(tr("Lock All"));
  all_lock_action->setCheckable(true);
  all_lock_action->setChecked(all_selected_have_lock(kLayerLockAll));
  auto* select_opaque_action = menu.addAction(tr("Load Layer Transparency"));
  auto* add_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)),
                                         tr("Add Layer Mask"));
  auto* edit_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(150, 205, 255)),
                                          tr("Edit Layer Mask"));
  edit_mask_action->setCheckable(true);
  edit_mask_action->setChecked(active_layer != nullptr && active_layer->mask().has_value() && canvas_ != nullptr &&
                               canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask);
  auto* overlay_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(255, 120, 120)),
                                             tr("Show Mask Overlay"));
  overlay_mask_action->setCheckable(true);
  overlay_mask_action->setChecked(canvas_ != nullptr &&
                                  canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Overlay);
  auto* delete_mask_action = menu.addAction(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)),
                                            tr("Delete Layer Mask"));
  auto* link_mask_action = menu.addAction(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)),
                                          tr("Link Layer Mask"));
  link_mask_action->setCheckable(true);
  link_mask_action->setChecked(active_layer == nullptr || layer_mask_linked(*active_layer));
  auto* disable_mask_action = menu.addAction(simple_icon(QStringLiteral("off"), QColor(220, 185, 120)),
                                             tr("Disable Layer Mask"));
  disable_mask_action->setCheckable(true);
  disable_mask_action->setChecked(active_layer != nullptr && active_layer->mask().has_value() &&
                                  active_layer->mask()->disabled);
  auto* invert_mask_action = menu.addAction(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)),
                                            tr("Invert Layer Mask"));
  auto* apply_mask_action = menu.addAction(simple_icon(QStringLiteral("ok"), QColor(150, 220, 170)),
                                           tr("Apply Layer Mask"));

  duplicate_action->setEnabled(has_layer);
  rename_action->setEnabled(active_layer != nullptr);
  delete_action->setEnabled(has_layer);
  merge_down_action->setEnabled(has_layer);
  visibility_action->setEnabled(has_layer);
  lock_menu->setEnabled(has_layer);
  select_opaque_action->setEnabled(active_layer != nullptr && canvas_ != nullptr);
  const auto active_pixels_locked = active_layer != nullptr && layer_id_locks_image_pixels(active_layer->id());
  add_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked &&
                              (active_layer->kind() == LayerKind::Pixel ||
                               active_layer->kind() == LayerKind::Adjustment) &&
                              canvas_ != nullptr &&
                              (canvas_->has_selection() || !active_layer->mask().has_value()));
  edit_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  overlay_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  delete_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  link_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  disable_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  invert_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  apply_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value() &&
                                active_layer->kind() == LayerKind::Pixel);

  hide_menu_action_icons(&menu);
  auto* chosen = menu.exec(layer_list_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) {
    return;
  }
  if (chosen == layer_blending_options_action_ || chosen == layer_copy_style_action_ ||
      chosen == layer_paste_style_action_ || chosen == layer_delete_style_action_ ||
      chosen == layer_rasterize_action_ || chosen == layer_rasterize_layer_style_action_) {
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
  } else if (chosen == merge_down_action) {
    merge_down();
  } else if (chosen == merge_visible_action) {
    merge_visible_to_new_layer();
  } else if (chosen == visibility_action) {
    set_active_layer_visible(visibility_action->isChecked());
  } else if (chosen == transparent_lock_action) {
    set_active_layer_lock_flag(kLayerLockTransparentPixels, transparent_lock_action->isChecked());
  } else if (chosen == image_lock_action) {
    set_active_layer_lock_flag(kLayerLockImagePixels, image_lock_action->isChecked());
  } else if (chosen == position_lock_action) {
    set_active_layer_lock_flag(kLayerLockPosition, position_lock_action->isChecked());
  } else if (chosen == all_lock_action) {
    set_active_layer_lock_all(all_lock_action->isChecked());
  } else if (chosen == select_opaque_action && active_layer != nullptr && canvas_ != nullptr) {
    canvas_->select_layer_opaque_pixels(active_layer->id());
  } else if (chosen == add_mask_action) {
    add_layer_mask();
  } else if (chosen == edit_mask_action) {
    set_layer_edit_target_ui(edit_mask_action->isChecked() ? CanvasWidget::LayerEditTarget::Mask
                                                           : CanvasWidget::LayerEditTarget::Content,
                             true);
  } else if (chosen == overlay_mask_action) {
    set_mask_overlay_shown(overlay_mask_action->isChecked());
  } else if (chosen == delete_mask_action) {
    delete_active_layer_mask();
  } else if (chosen == link_mask_action) {
    set_active_layer_mask_linked(link_mask_action->isChecked());
  } else if (chosen == disable_mask_action) {
    set_active_layer_mask_disabled(disable_mask_action->isChecked());
  } else if (chosen == invert_mask_action) {
    invert_active_layer_mask();
  } else if (chosen == apply_mask_action) {
    apply_active_layer_mask();
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

void MainWindow::merge_down() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  finish_active_text_editor();

  auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to merge down"));
    return;
  }

  auto& doc = document();

  // Normalize the selection: dedupe, drop ids missing from the tree, and drop any id whose ancestor
  // group is also selected (the folder already contains it, so merging both would double-count).
  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::set<LayerId> seen_ids;
  std::vector<LayerId> normalized;
  normalized.reserve(ids.size());
  for (const auto id : ids) {
    if (!seen_ids.insert(id).second) {
      continue;
    }
    if (doc.find_layer(id) == nullptr) {
      continue;
    }
    std::vector<LayerId> ancestors;
    if (collect_layer_ancestor_groups(doc.layers(), id, ancestors) &&
        std::any_of(ancestors.begin(), ancestors.end(),
                    [&selected](LayerId ancestor) { return selected.count(ancestor) != 0; })) {
      continue;
    }
    normalized.push_back(id);
  }

  if (normalized.empty()) {
    statusBar()->showMessage(tr("Select a layer to merge down"));
    return;
  }

  // Order the selection bottom-to-top by global compositor paint order so blend modes composite
  // correctly and the bottom-most item becomes the merge target.
  std::vector<LayerId> render_order;
  collect_layer_render_order(doc.layers(), render_order);
  const auto stack_position = [&render_order](LayerId id) {
    return static_cast<std::size_t>(
        std::distance(render_order.begin(), std::find(render_order.begin(), render_order.end(), id)));
  };
  std::sort(normalized.begin(), normalized.end(),
            [&stack_position](LayerId lhs, LayerId rhs) { return stack_position(lhs) < stack_position(rhs); });

  // Build the bottom-to-top merge list. A single folder flattens in place (Photoshop "Merge Group");
  // a single non-folder merges with the layer directly below it ("Merge Down"); several selected
  // items merge the whole selection together.
  std::vector<LayerId> merge_list;
  if (normalized.size() == 1U) {
    const auto id = normalized.front();
    const auto* single = doc.find_layer(id);
    if (single != nullptr && single->kind() == LayerKind::Group) {
      merge_list = {id};
    } else {
      const auto location = find_layer_location(doc.layers(), id);
      if (!location.has_value() || location->index == 0U) {
        statusBar()->showMessage(tr("No layer below to merge"));
        return;
      }
      merge_list = {(*location->siblings)[location->index - 1U].id(), id};
    }
  } else {
    merge_list = normalized;  // already sorted bottom-to-top
  }

  // Render the visible items into a scratch document. Hidden items contribute nothing and are simply
  // dropped from the render -- they are still removed below, matching how Photoshop discards hidden
  // layers when merging. Folders and adjustment layers are added as-is so the compositor flattens or
  // applies them; anything the compositor can't draw is skipped the same way. The merged pixels land
  // in the bottom-most visible item, which keeps its id and name.
  Rect affected;
  Document merge_document(doc.width(), doc.height(), doc.format());
  LayerId target_id = 0;
  for (const auto id : merge_list) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      statusBar()->showMessage(tr("Select a layer to merge down"));
      return;
    }
    if (!layer_is_effectively_visible(doc.layers(), id)) {
      continue;
    }
    std::optional<Layer> renderable;
    if (layer->kind() == LayerKind::Group || layer->kind() == LayerKind::Adjustment) {
      renderable = *layer;
    } else {
      renderable = renderable_merge_layer_copy(*layer);
    }
    if (!renderable.has_value()) {
      continue;
    }
    if (target_id == 0) {
      target_id = id;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    auto& added = merge_document.add_layer(std::move(*renderable));
    affected = unite_rect(affected, layer_render_bounds(added));
  }

  if (target_id == 0) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }
  if (layer_id_locks_image_pixels(target_id)) {
    statusBar()->showMessage(tr("Target layer pixels are locked. Unlock image pixels to merge down."));
    return;
  }

  auto merge_bounds = affected;
  merge_bounds = intersect_rect(merge_bounds, Rect::from_size(doc.width(), doc.height()));
  if (merge_bounds.empty()) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }

  const auto image =
      qimage_from_document_rect(merge_document, QRect(merge_bounds.x, merge_bounds.y, merge_bounds.width, merge_bounds.height), true);
  if (image.isNull()) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }
  auto merged_pixels = pixels_from_image_rgba(image);

  push_undo_snapshot(tr("Merge down"));
  const auto target_location = find_layer_location(doc.layers(), target_id);
  if (!target_location.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Select a layer to merge down"));
    return;
  }

  Layer& target = (*target_location->siblings)[target_location->index];
  if (target.kind() != LayerKind::Pixel) {
    // A folder/adjustment/text target can't simply hold the merged pixels; replace it in place with a
    // fresh pixel layer that keeps its id and name (dropping any now-flattened children/metadata).
    Layer merged(target_id, target.name(), std::move(merged_pixels));
    merged.set_bounds(merge_bounds);
    target = std::move(merged);
  } else {
    target.set_pixels(std::move(merged_pixels));
    target.set_bounds(merge_bounds);
    target.set_opacity(1.0F);
    target.set_blend_mode(BlendMode::Normal);
    target.clear_mask();
    target.layer_style() = {};
    clear_layer_text_metadata(target);
    clear_layer_psd_style_source(target);
    target.set_visible(true);
  }

  // Remove every other merged item by id. remove_layer searches the whole tree, so cross-folder
  // selections are handled; do not touch the `target` reference after this -- erasing siblings can
  // invalidate it.
  for (const auto id : merge_list) {
    if (id != target_id) {
      doc.remove_layer(id);
    }
  }
  doc.set_active_layer(target_id);
  affected = unite_rect(affected, merge_bounds);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(merge_list.size() == 2U ? tr("Merged layer down") : tr("Merged layers down"));
}

void MainWindow::fill_active_layer() {
  fill_active_layer_with_color(canvas_->primary_color(), tr("Fill"));
}

void MainWindow::fill_active_layer_with_color(QColor color, QString label) {
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) {
    if (const auto active = document().active_layer_id();
        active.has_value() && layer_id_locks_image_pixels(*active)) {
      statusBar()->showMessage(tr("Layer pixels are locked."));
      return;
    }
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    push_undo_snapshot(label);
    const auto dirty = canvas_->fill_active_layer_mask(color);
    if (!dirty.isEmpty()) {
      canvas_->document_changed(dirty);
      refresh_layer_thumbnails();
      refresh_document_info();
      statusBar()->showMessage(tr("Filled layer mask"));
    }
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  push_undo_snapshot(label);
  auto options = edit_options(*canvas_);
  options.primary = edit_color(color);
  // Fill honors its own Opacity and Soft settings (Fill tool options bar; default 100% / 0). Opacity
  // scales the fill alpha; Soft feathers the fill inward from the selection edge.
  constexpr double kFillMaxFeatherPixels = 50.0;
  options.primary.a = static_cast<std::uint8_t>(
      std::clamp(std::lround(static_cast<double>(options.primary.a) * canvas_->fill_opacity() / 100.0), 0L, 255L));
  options.fill_softness_feather = std::clamp(canvas_->fill_softness(), 0, 100) / 100.0 * kFillMaxFeatherPixels;
  Rect affected;
  for (const auto id : editable_ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto target = canvas_->has_selection() && canvas_->selected_document_rect().has_value()
                            ? to_core_rect(*canvas_->selected_document_rect())
                            : layer->bounds();
    affected = unite_rect(affected, patchy::fill_rect(doc, id, target, options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::clear_active_layer() {
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) {
    if (const auto active = document().active_layer_id();
        active.has_value() && layer_id_locks_image_pixels(*active)) {
      statusBar()->showMessage(tr("Layer pixels are locked."));
      return;
    }
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    push_undo_snapshot(tr("Clear layer mask"));
    const auto dirty = canvas_->clear_active_layer_mask();
    if (!dirty.isEmpty()) {
      canvas_->document_changed(dirty);
      refresh_layer_thumbnails();
      refresh_document_info();
      statusBar()->showMessage(tr("Cleared layer mask"));
    }
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  struct ClearCandidate {
    LayerId id{};
    Rect bounds{};
    bool lock_transparent_pixels{false};
  };
  struct ClearTarget {
    LayerId id{};
    Rect bounds{};
    bool lock_transparent_pixels{false};
  };
  std::vector<ClearCandidate> candidates;
  std::int64_t scan_pixels = 0;
  auto options = edit_options(*canvas_);
  for (const auto id : editable_ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto pixels_to_scan = clear_scan_pixel_count(doc, *layer, layer->bounds(), options);
    scan_pixels += pixels_to_scan;
    if (pixels_to_scan > 0) {
      candidates.push_back(ClearCandidate{id, layer->bounds(), options.lock_transparent_pixels});
    }
  }

  constexpr std::int64_t kClearProgressPixelThreshold = 250'000;
  std::unique_ptr<QProgressDialog> progress;
  if (scan_pixels >= kClearProgressPixelThreshold) {
    progress = std::make_unique<QProgressDialog>(tr("Clearing..."), QString(), 0, 0, this);
    progress->setObjectName(QStringLiteral("clearProgressDialog"));
    progress->setWindowTitle(tr("Clearing"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    remember_dialog_position(*progress);
    progress->show();
    progress->raise();
    progress->activateWindow();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }
  const auto close_progress = qScopeGuard([&progress] {
    if (progress != nullptr) {
      progress->close();
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
  });
  Q_UNUSED(close_progress);

  std::vector<ClearTarget> targets;
  for (const auto& candidate : candidates) {
    options.lock_transparent_pixels = candidate.lock_transparent_pixels;
    const auto changed = patchy::clear_rect_change_bounds(doc, candidate.id, candidate.bounds, options);
    if (!changed.empty()) {
      targets.push_back(ClearTarget{candidate.id, changed, candidate.lock_transparent_pixels});
    }
  }

  if (targets.empty()) {
    statusBar()->showMessage(tr("Nothing to clear"));
    return;
  }

  push_undo_snapshot(tr("Clear"));
  Rect affected;
  for (const auto& target : targets) {
    options.lock_transparent_pixels = target.lock_transparent_pixels;
    affected = unite_rect(affected, patchy::clear_rect(doc, target.id, target.bounds, options));
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
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
  const auto affected = patchy::fill_rect(doc, *active, to_core_rect(stroke_region.boundingRect()), options);
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
  const auto pixels = request_integer_input(this, QStringLiteral("patchyExpandSelectionDialog"),
                                            tr("Expand Selection"), tr("Expand by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Expand Selection"), [this, pixels] { canvas_->expand_selection(*pixels); });
  }
}

void MainWindow::contract_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before contracting"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("patchyContractSelectionDialog"),
                                            tr("Contract Selection"), tr("Contract by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Contract Selection"), [this, pixels] { canvas_->contract_selection(*pixels); });
  }
}

void MainWindow::border_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before selecting a border"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("patchyBorderSelectionDialog"),
                                            tr("Border Selection"), tr("Width"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Border Selection"), [this, pixels] { canvas_->border_selection(*pixels); });
  }
}

void MainWindow::flip_active_layer_horizontal() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip horizontal"));
  Rect affected;
  for (const auto id : editable_ids) {
    affected = unite_rect(affected, patchy::flip_layer_horizontal(doc, id));
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
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip vertical"));
  Rect affected;
  for (const auto id : editable_ids) {
    affected = unite_rect(affected, patchy::flip_layer_vertical(doc, id));
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
  if (!patchy::crop_document(doc, to_core_rect(*selection))) {
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
  patchy::rotate_document_clockwise(doc);
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
  patchy::rotate_document_counterclockwise(doc);
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

LayerLockFlags MainWindow::layer_id_effective_lock_flags(LayerId id) const {
  return has_active_document() ? patchy::layer_effective_lock_flags(document().layers(), id) : kLayerLockNone;
}

LayerLockFlags MainWindow::layer_id_ancestor_lock_flags(LayerId id) const {
  return has_active_document() ? patchy::layer_ancestor_lock_flags(document().layers(), id) : kLayerLockNone;
}

bool MainWindow::layer_id_locks_image_pixels(LayerId id) const {
  return (layer_id_effective_lock_flags(id) & kLayerLockImagePixels) != kLayerLockNone;
}

bool MainWindow::layer_id_locks_position(LayerId id) const {
  return (layer_id_effective_lock_flags(id) & kLayerLockPosition) != kLayerLockNone;
}

std::vector<LayerId> MainWindow::layer_ids_without_image_pixel_lock(std::vector<LayerId> ids) const {
  ids.erase(std::remove_if(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); }),
            ids.end());
  return ids;
}

bool MainWindow::show_pixel_lock_message_if_all_locked(const std::vector<LayerId>& requested_ids,
                                                       const std::vector<LayerId>& editable_ids) {
  if (!requested_ids.empty() && editable_ids.empty()) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return true;
  }
  return false;
}

void MainWindow::set_active_layer_from_selection() {
  if (updating_layer_controls_) {
    return;
  }
  if (canvas_ != nullptr && layer_list_ != nullptr &&
      canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    // Commit any in-progress text edit before honoring the new selection.
    // Finishing the editor rebuilds the layer list and can reset the current
    // item, so remember which layer the user clicked and restore it afterwards.
    std::optional<LayerId> requested_id;
    if (auto* item = layer_list_->currentItem(); item != nullptr) {
      requested_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    }
    finish_active_text_editor();
    if (requested_id.has_value() && document().find_layer(*requested_id) != nullptr) {
      const QSignalBlocker blocker(layer_list_);
      for (int row = 0; row < layer_list_->count(); ++row) {
        auto* item = layer_list_->item(row);
        if (item != nullptr && static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()) == *requested_id) {
          layer_list_->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
          break;
        }
      }
    }
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    // Restore the selection to the active layer, but defer the rebuild: clearing
    // and repopulating the list from inside this itemSelectionChanged handler
    // deletes the items Qt is still using and crashes.
    QTimer::singleShot(0, this, [this] { refresh_layer_list(); });
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
    const auto previous_active = doc.active_layer_id();
    if (!previous_active.has_value() || *previous_active != id) {
      doc.set_active_layer(id);
      if (canvas_ != nullptr) {
        canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
      }
    }
    if (canvas_ != nullptr) {
      update_layer_target_styles(layer_list_, doc.active_layer_id(), canvas_->layer_edit_target());
    }
    refresh_layer_controls();
    restyle_layer_rows(layer_list_);
  }
}

void MainWindow::set_active_layer_opacity(int value) {
  if (updating_layer_controls_) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
    return;
  }

  if (!pending_layer_opacity_edit_active_) {
    if (!has_active_document()) {
      return;
    }
    auto ids = selected_or_active_layer_ids();
    auto& doc = document();
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&doc](LayerId id) { return doc.find_layer(id) == nullptr; }),
              ids.end());
    if (ids.empty()) {
      return;
    }
    push_undo_snapshot(tr("Opacity"));
    pending_layer_opacity_ids_ = std::move(ids);
    pending_layer_opacity_edit_active_ = true;
  }

  pending_layer_opacity_value_ = std::clamp(value, 0, 100);
  if (layer_opacity_apply_timer_ != nullptr) {
    layer_opacity_apply_timer_->start();
  } else {
    apply_pending_layer_opacity();
  }
  if (layer_opacity_idle_timer_ != nullptr) {
    layer_opacity_idle_timer_->start();
  }
}

void MainWindow::apply_pending_layer_opacity() {
  if (!pending_layer_opacity_value_.has_value()) {
    return;
  }
  const auto value = *pending_layer_opacity_value_;
  pending_layer_opacity_value_.reset();
  if (!has_active_document()) {
    return;
  }

  auto& doc = document();
  bool changed = false;
  for (const auto id : pending_layer_opacity_ids_) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_opacity(static_cast<float>(value) / 100.0F);
    changed = true;
  }
  if (changed && canvas_ != nullptr) {
    canvas_->document_changed_async_preview();
  }
}

void MainWindow::finish_pending_layer_opacity_edit() {
  if (layer_opacity_apply_timer_ != nullptr) {
    layer_opacity_apply_timer_->stop();
  }
  if (layer_opacity_idle_timer_ != nullptr) {
    layer_opacity_idle_timer_->stop();
  }
  apply_pending_layer_opacity();
  reset_pending_layer_opacity_edit();
}

void MainWindow::reset_pending_layer_opacity_edit() {
  pending_layer_opacity_ids_.clear();
  pending_layer_opacity_value_.reset();
  pending_layer_opacity_edit_active_ = false;
}

void MainWindow::set_active_layer_blend(int index) {
  if (updating_layer_controls_ || index < 0) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
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
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
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

void MainWindow::set_layer_lock_flag_state(LayerId id, LayerLockFlags flag, bool locked) {
  if (updating_layer_controls_ || !has_active_document()) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
    return;
  }
  auto* layer = document().find_layer(id);
  if (layer == nullptr || ((layer_lock_flags(*layer) & flag) != kLayerLockNone) == locked) {
    refresh_layer_list();
    refresh_layer_controls();
    return;
  }
  push_undo_snapshot(tr("Lock layer"));
  set_layer_lock_flag(*layer, flag, locked);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(locked ? tr("Layer lock enabled") : tr("Layer lock disabled"));
}

void MainWindow::set_active_layer_lock_flag(LayerLockFlags flag, bool locked) {
  if (updating_layer_controls_) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Lock layer"));
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    set_layer_lock_flag(*layer, flag, locked);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(locked ? tr("Layer lock enabled") : tr("Layer lock disabled"));
}

void MainWindow::set_active_layer_lock_all(bool locked) {
  if (updating_layer_controls_) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Lock layer"));
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    set_layer_locks_all(*layer, locked);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(locked ? tr("Layer locked") : tr("Layer unlocked"));
}

void MainWindow::undo() {
  finish_pending_layer_opacity_edit();
  auto& active_session = session();
  if (active_session.undo_stack.empty()) {
    return;
  }
  active_session.redo_stack.push_back(DocumentSession::HistoryState{
      active_session.document, active_session.revision, canvas_->capture_selection_snapshot()});
  active_session.document = active_session.undo_stack.back().document;
  active_session.revision = active_session.undo_stack.back().revision;
  auto restored_selection = active_session.undo_stack.back().selection;
  active_session.undo_stack.pop_back();
  active_session.selection_move_coalescing = false;
  canvas_->set_document(&active_session.document);
  canvas_->apply_selection_snapshot(restored_selection);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Undo"));
  update_history(tr("Undo"));
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::redo() {
  finish_pending_layer_opacity_edit();
  auto& active_session = session();
  if (active_session.redo_stack.empty()) {
    return;
  }
  active_session.undo_stack.push_back(DocumentSession::HistoryState{
      active_session.document, active_session.revision, canvas_->capture_selection_snapshot()});
  active_session.document = active_session.redo_stack.back().document;
  active_session.revision = active_session.redo_stack.back().revision;
  auto restored_selection = active_session.redo_stack.back().selection;
  active_session.redo_stack.pop_back();
  active_session.selection_move_coalescing = false;
  canvas_->set_document(&active_session.document);
  canvas_->apply_selection_snapshot(restored_selection);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Redo"));
  update_history(tr("Redo"));
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::push_undo_snapshot(QString label) {
  finish_pending_layer_opacity_edit();
  const auto started = std::chrono::steady_clock::now();
  constexpr std::size_t kMaxUndo = 40;
  auto& active_session = session();
  const auto snapshot_revision = active_session.revision;
  auto snapshot_future = std::async(std::launch::async, [&active_session] {
    if (const auto delay = undo_snapshot_test_delay_ms(); delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return active_session.document;
  });
  if (canvas_ != nullptr) {
    canvas_->wait_for_processing_operation([&snapshot_future] {
      return snapshot_future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready;
    });
  } else {
    snapshot_future.wait();
  }
  // Capture the selection that is active right now (before the edit mutates the
  // document) so undoing this edit also restores the selection it ran against.
  auto snapshot_selection =
      canvas_ != nullptr ? canvas_->capture_selection_snapshot() : CanvasWidget::SelectionSnapshot{};
  active_session.undo_stack.push_back(
      DocumentSession::HistoryState{snapshot_future.get(), snapshot_revision, std::move(snapshot_selection)});
  if (active_session.undo_stack.size() > kMaxUndo) {
    active_session.undo_stack.erase(active_session.undo_stack.begin());
  }
  active_session.redo_stack.clear();
  active_session.selection_move_coalescing = false;
  mark_session_modified(active_session);
  update_history(label);
  update_undo_redo_actions();
  statusBar()->showMessage(label);
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  log_ui_profile("push_undo_snapshot", elapsed, label.toStdString());
}

void MainWindow::push_selection_history(QString label, CanvasWidget::SelectionSnapshot before, bool coalesce) {
  finish_pending_layer_opacity_edit();
  constexpr std::size_t kMaxUndo = 40;
  auto& active_session = session();
  // A run of moves/nudges collapses into one undo step: once the first move has
  // pushed an entry holding the pre-run position, later moves leave the live
  // selection updated but add no new entry, so undo returns to where the run
  // began and redo lands on the final position. Any non-coalescing edit (below,
  // or push_undo_snapshot) clears the flag and ends the run.
  if (coalesce && active_session.selection_move_coalescing && !active_session.undo_stack.empty()) {
    statusBar()->showMessage(label);
    return;
  }
  // A selection-only edit leaves the pixels untouched, so the entry holds the
  // current document together with the pre-edit selection. The document is not
  // flagged modified for save purposes (a mere selection is not unsaved work),
  // but the change still joins the undo/redo history.
  active_session.undo_stack.push_back(
      DocumentSession::HistoryState{active_session.document, active_session.revision, std::move(before)});
  if (active_session.undo_stack.size() > kMaxUndo) {
    active_session.undo_stack.erase(active_session.undo_stack.begin());
  }
  active_session.redo_stack.clear();
  active_session.selection_move_coalescing = coalesce;
  update_history(label);
  update_undo_redo_actions();
  statusBar()->showMessage(label);
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
      item->setToolTip(tr("%1\n%2% opacity%3%4%5%6%7")
                           .arg(QString::fromStdString(it->name()))
                           .arg(std::round(it->opacity() * 100.0F))
                           .arg(!ancestors_visible
                                    ? tr("\nHidden by parent folder")
                                    : QString())
                           .arg(effective_lock_flags != kLayerLockNone ? tr("\nLocked") : QString())
                           .arg(layer_locks_transparent_pixels(*it) ? tr("\nTransparent pixels locked") : QString())
                           .arg(it->mask().has_value() ? tr("\nLayer mask") : QString())
                           .arg(folder_detail));
      item->setSizeHint(QSize(0, 44));
      item->setForeground(effective_visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
      if (active.has_value() && *active == it->id()) {
        auto font = item->font();
        font.setBold(true);
        item->setFont(font);
        row_to_select = layer_list_->row(item);
      }
      layer_list_->setItemWidget(
          item, make_layer_row_widget(*it, item, layer_list_, depth, ancestors_visible, group_expanded,
                                      ancestor_lock_flags,
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
                                          edit_target == CanvasWidget::LayerEditTarget::Mask));
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
    const auto revision = static_cast<qulonglong>(layer->content_revision());
    if (auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")); thumbnail != nullptr &&
        (layer->kind() == LayerKind::Pixel || layer_is_text(*layer))) {
      if (thumbnail->property(kLayerContentThumbnailRevisionProperty).toULongLong() != revision) {
        thumbnail->setPixmap(layer_content_thumbnail(*layer));
        thumbnail->setProperty(kLayerContentThumbnailRevisionProperty, QVariant::fromValue<qulonglong>(revision));
        ++content_refreshed;
      } else {
        ++content_skipped;
      }
    }
    if (auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
        mask_thumbnail != nullptr && layer->mask().has_value()) {
      if (mask_thumbnail->property(kLayerMaskThumbnailRevisionProperty).toULongLong() != revision) {
        mask_thumbnail->setPixmap(layer_mask_thumbnail(*layer->mask()));
        mask_thumbnail->setProperty(kLayerMaskThumbnailRevisionProperty, QVariant::fromValue<qulonglong>(revision));
        ++mask_refreshed;
      } else {
        ++mask_skipped;
      }
    }
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  std::ostringstream detail;
  detail << "content_refreshed=" << content_refreshed << " content_skipped=" << content_skipped
         << " mask_refreshed=" << mask_refreshed << " mask_skipped=" << mask_skipped;
  log_ui_profile("refresh_layer_thumbnails", elapsed, detail.str());
}

void MainWindow::refresh_layer_controls() {
  if (!updating_layer_controls_) {
    finish_pending_layer_opacity_edit();
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
    if (edit_layer_mask_action_ != nullptr) {
      edit_layer_mask_action_->setEnabled(false);
      edit_layer_mask_action_->setChecked(false);
    }
    if (mask_overlay_action_ != nullptr) {
      mask_overlay_action_->setEnabled(false);
      mask_overlay_action_->setChecked(false);
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
    disable_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value());
    disable_layer_mask_action_->setChecked(layer->mask().has_value() && layer->mask()->disabled);
  }
  if (invert_layer_mask_action_ != nullptr) {
    invert_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value());
  }
  if (apply_layer_mask_action_ != nullptr) {
    apply_layer_mask_action_->setEnabled(edit_allowed && !active_pixels_locked && layer->mask().has_value() && layer->kind() == LayerKind::Pixel);
  }
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask &&
      !layer->mask().has_value()) {
    // The mask is gone (deleted, applied, or undone away) - fall back to editing pixels.
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
    update_layer_target_styles(layer_list_, active, CanvasWidget::LayerEditTarget::Content);
  }
  if (canvas_ != nullptr && !layer->mask().has_value() &&
      canvas_->mask_display_mode() != CanvasWidget::MaskDisplayMode::None) {
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
  }
  const auto editing_mask = canvas_ != nullptr && layer->mask().has_value() &&
                            canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask;
  if (edit_layer_mask_action_ != nullptr) {
    edit_layer_mask_action_->setEnabled(edit_allowed && layer->mask().has_value());
    edit_layer_mask_action_->setChecked(editing_mask);
  }
  if (mask_overlay_action_ != nullptr) {
    mask_overlay_action_->setEnabled(layer->mask().has_value());
    mask_overlay_action_->setChecked(canvas_ != nullptr &&
                                     canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Overlay);
  }
  if (mask_edit_mode_chip_ != nullptr) {
    mask_edit_mode_chip_->setVisible(editing_mask);
  }
  updating_layer_controls_ = false;
  refresh_document_info();
}

void MainWindow::refresh_document_info() {
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
  set_property_label_text(document_info_label_,
                          tr("Document: %1 x %2 px | %3 ppi | %4 | %5 layers | Zoom %6% | %7")
                              .arg(doc.width())
                              .arg(doc.height())
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
    if (current_tool_ == CanvasTool::Brush || current_tool_ == CanvasTool::Clone ||
        current_tool_ == CanvasTool::Smudge || current_tool_ == CanvasTool::Eraser ||
        current_tool_ == CanvasTool::Line || current_tool_ == CanvasTool::Rectangle ||
        current_tool_ == CanvasTool::Ellipse) {
      lines << tr("Size: %1 px").arg(canvas_->brush_size())
            << tr("Opacity: %1%").arg(canvas_->brush_opacity())
            << tr("Softness: %1%").arg(canvas_->brush_softness());
    } else if (current_tool_ == CanvasTool::MagicWand) {
      lines << tr("Tolerance: %1 | %2 | %3")
                   .arg(canvas_->wand_tolerance())
                   .arg(canvas_->wand_contiguous() ? tr("contiguous") : tr("non-contiguous"))
                   .arg(canvas_->wand_sample_all_layers() ? tr("sample all layers") : tr("active layer"));
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

void MainWindow::choose_primary_color() {
  show_color_panel(true);
}

void MainWindow::choose_secondary_color() {
  show_color_panel(false);
}

void MainWindow::choose_text_color() {
  if (canvas_ == nullptr) {
    return;
  }
  sync_text_options_from_active_editor();
  if (color_dialog_ != nullptr) {
    if (color_dialog_->property("patchy.colorTarget").toString() == QStringLiteral("text")) {
      color_dialog_->show();
      color_dialog_->raise();
      color_dialog_->activateWindow();
      return;
    }
    color_dialog_->close();
  }

  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  auto* dialog = create_patchy_color_panel(this, current_text_color(), tr("Text Color"),
                                           [this, editor = QPointer<QTextEdit>(editor)](QColor color) {
    if (canvas_ == nullptr) {
      return;
    }
    color.setAlpha(255);
    canvas_->set_primary_color(color);
    if (editor != nullptr) {
      editor->setProperty("patchy.documentTextColor", color);
      apply_text_color_to_active_editor();
    }
    refresh_color_buttons();
    statusBar()->showMessage(tr("Text color changed"));
  });
  dialog->setProperty("patchy.colorTarget", QStringLiteral("text"));
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

void MainWindow::show_color_panel(bool foreground) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto color_target = foreground ? QStringLiteral("foreground") : QStringLiteral("background");
  if (color_dialog_ != nullptr) {
    if (color_dialog_->property("patchy.colorTarget").toString() == color_target) {
      color_dialog_->show();
      color_dialog_->raise();
      color_dialog_->activateWindow();
      return;
    }
    color_dialog_->close();
  }

  auto* dialog = create_patchy_color_panel(
      this, foreground ? canvas_->primary_color() : canvas_->secondary_color(),
      foreground ? tr("Foreground Color") : tr("Background Color"),
      [this, foreground](QColor color) {
        color.setAlpha(255);
        if (foreground) {
          canvas_->set_primary_color(color);
          apply_primary_color_to_active_text_editor(color);
          statusBar()->showMessage(tr("Foreground color changed"));
        } else {
          canvas_->set_secondary_color(color);
          statusBar()->showMessage(tr("Background color changed"));
        }
        refresh_color_buttons();
      });
  dialog->setProperty("patchy.colorTarget", color_target);
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
  if (canvas_ == nullptr) {
    return;
  }
  const auto primary = canvas_->primary_color();
  canvas_->set_primary_color(canvas_->secondary_color());
  canvas_->set_secondary_color(primary);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Swapped foreground/background"));
}

void MainWindow::default_colors() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->set_primary_color(Qt::black);
  canvas_->set_secondary_color(Qt::white);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Default colors"));
}

void MainWindow::refresh_color_buttons() {
  const auto primary_color = canvas_ != nullptr ? canvas_->primary_color() : QColor(Qt::black);
  const auto secondary_color = canvas_ != nullptr ? canvas_->secondary_color() : QColor(Qt::white);
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setText(tr("FG"));
    primary_color_button_->setToolTip(tr("Foreground color %1").arg(primary_color.name(QColor::HexRgb).toUpper()));
    primary_color_button_->setStyleSheet(color_button_style(primary_color));
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setText(tr("BG"));
    secondary_color_button_->setToolTip(tr("Background color %1").arg(secondary_color.name(QColor::HexRgb).toUpper()));
    secondary_color_button_->setStyleSheet(color_button_style(secondary_color));
  }
  refresh_text_color_button();
  refresh_gradient_controls_from_canvas();
}

void MainWindow::refresh_text_color_button() {
  if (text_color_button_ == nullptr || canvas_ == nullptr) {
    return;
  }
  const auto color = current_text_color();
  text_color_button_->setText(tr("T"));
  text_color_button_->setToolTip(tr("Text color %1").arg(color.name(QColor::HexRgb).toUpper()));
  text_color_button_->setStyleSheet(color_button_style(color));
}

void MainWindow::edit_gradient_stops() {
  if (canvas_ == nullptr) {
    return;
  }
  const auto result = request_gradient_stops_dialog(this, canvas_->effective_gradient_stops(),
                                                    canvas_->gradient_stops().has_value(), canvas_->primary_color(),
                                                    canvas_->secondary_color());
  if (!result.has_value()) {
    return;
  }
  canvas_->set_gradient_stops(*result);
  refresh_gradient_controls_from_canvas();
  save_tool_settings();
  refresh_document_info();
}

void MainWindow::refresh_gradient_controls_from_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  if (gradient_method_combo_ != nullptr) {
    QSignalBlocker blocker(gradient_method_combo_);
    const auto index = gradient_method_combo_->findData(static_cast<int>(canvas_->gradient_method()));
    gradient_method_combo_->setCurrentIndex(std::max(0, index));
  }
  if (gradient_opacity_spin_ != nullptr) {
    QSignalBlocker blocker(gradient_opacity_spin_);
    gradient_opacity_spin_->setValue(canvas_->gradient_opacity());
  }
  if (gradient_opacity_slider_ != nullptr) {
    QSignalBlocker blocker(gradient_opacity_slider_);
    gradient_opacity_slider_->setValue(canvas_->gradient_opacity());
  }
  if (gradient_reverse_check_ != nullptr) {
    QSignalBlocker blocker(gradient_reverse_check_);
    gradient_reverse_check_->setChecked(canvas_->gradient_reverse());
  }
  if (gradient_preview_button_ != nullptr) {
    gradient_preview_button_->setStyleSheet(gradient_preview_button_style(
        canvas_->effective_gradient_stops(), canvas_->gradient_opacity(), canvas_->gradient_reverse()));
    gradient_preview_button_->setToolTip(tr("Gradient preview"));
  }
}

QColor MainWindow::current_text_color() const {
  if (canvas_ != nullptr) {
    if (const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")); editor != nullptr) {
      if (const auto color = text_color_from_format(text_editor_reference_format(*editor)); color.has_value()) {
        return *color;
      }
      const auto color = editor->property("patchy.documentTextColor").value<QColor>();
      if (color.isValid()) {
        return color;
      }
    }
    return canvas_->primary_color();
  }
  return QColor(Qt::black);
}

void MainWindow::sync_text_options_from_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }

  const auto format = text_editor_reference_format(*editor);
  const auto format_font = format.font();
  const auto display_family = text_display_family_from_format(
      format, editor->property("patchy.documentTextFamily").toString());
  if (!display_family.trimmed().isEmpty() && text_font_combo_ != nullptr) {
    QSignalBlocker blocker(text_font_combo_);
    text_font_combo_->setCurrentFont(QFont(display_family));
  }
  if (!display_family.trimmed().isEmpty()) {
    editor->setProperty("patchy.documentTextFamily", display_family);
  }

  const auto document_text_size = document_text_size_from_editor_format(
      format, canvas_->zoom(), std::max(1, editor->property("patchy.documentTextSize").toInt()));
  if (text_size_spin_ != nullptr) {
    QSignalBlocker blocker(text_size_spin_);
    text_size_spin_->setValue(
        std::clamp(text_pixels_to_points(document_text_size, document()), text_size_spin_->minimum(),
                   text_size_spin_->maximum()));
  }

  if (text_bold_button_ != nullptr) {
    QSignalBlocker blocker(text_bold_button_);
    text_bold_button_->setChecked(format_font.bold());
  }
  if (text_italic_button_ != nullptr) {
    QSignalBlocker blocker(text_italic_button_);
    text_italic_button_->setChecked(format_font.italic());
  }
  set_text_smoothing_combo_value(text_smoothing_combo_,
                                 std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));

  if (const auto color = text_color_from_format(format); color.has_value()) {
    editor->setProperty("patchy.documentTextColor", *color);
  }
  refresh_text_color_button();
}

void MainWindow::apply_canvas_aid_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_rulers_visible(view_rulers_visible_);
  canvas->set_grid_visible(view_grid_visible_);
  canvas->set_guides_visible(view_guides_visible_);
  canvas->set_guides_locked(view_guides_locked_);
  canvas->set_snap_enabled(view_snap_enabled_);
  canvas->set_snap_to_guides(view_snap_to_guides_);
  canvas->set_snap_to_grid(view_snap_to_grid_);
  canvas->set_snap_to_document(view_snap_to_document_);
  canvas->set_snap_to_layers(view_snap_to_layers_);
  canvas->set_snap_to_selection(view_snap_to_selection_);
  canvas->set_grid_subdivisions(view_grid_subdivisions_);
  canvas->set_grid_style(view_grid_style_);
  canvas->set_grid_color(view_grid_color_);
  canvas->set_guide_color(view_guide_color_);
}

void MainWindow::apply_pen_input_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_pen_input_settings(pen_input_settings_);
  canvas->set_wheel_zooms(wheel_zooms_);
}

void MainWindow::activate_tool(CanvasTool tool) {
  if (tool_action_group_ == nullptr) {
    return;
  }
  for (auto* action : tool_action_group_->actions()) {
    if (static_cast<CanvasTool>(action->data().toInt()) == tool) {
      action->trigger();
      return;
    }
  }
}

void MainWindow::handle_pen_button_action(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::Undo:
      undo();
      break;
    case PenButtonAction::Redo:
      redo();
      break;
    case PenButtonAction::ToggleEraser:
      if (current_tool_ == CanvasTool::Eraser) {
        activate_tool(tool_before_eraser_toggle_);
      } else {
        tool_before_eraser_toggle_ = current_tool_;
        activate_tool(CanvasTool::Eraser);
      }
      break;
    case PenButtonAction::IncreaseBrushSize:
    case PenButtonAction::DecreaseBrushSize: {
      if (auto* brush_size = findChild<QSpinBox*>(QStringLiteral("brushSizeSpin")); brush_size != nullptr) {
        const auto step = std::max(1, brush_size->value() / 10);
        const auto delta = action == PenButtonAction::IncreaseBrushSize ? step : -step;
        brush_size->setValue(brush_size->value() + delta);
      }
      break;
    }
    case PenButtonAction::SwapColors:
      if (canvas_ != nullptr) {
        const auto primary = canvas_->primary_color();
        canvas_->set_primary_color(canvas_->secondary_color());
        canvas_->set_secondary_color(primary);
        refresh_color_buttons();
      }
      break;
    case PenButtonAction::PanCanvas:
    case PenButtonAction::ZoomCanvas:
    case PenButtonAction::PickColor:
    case PenButtonAction::SetCloneSource:
    case PenButtonAction::None:
      break;
  }
}

void MainWindow::load_pen_input_settings() {
  auto settings = app_settings();
  pen_input_settings_.enabled = settings.value(QStringLiteral("input/pen/enabled"), true).toBool();
  pen_input_settings_.pressure_size = settings.value(QStringLiteral("input/pen/pressureSize"), true).toBool();
  pen_input_settings_.pressure_size_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureSizeMinPercent"), 20).toInt(), 1, 100);
  pen_input_settings_.pressure_opacity =
      settings.value(QStringLiteral("input/pen/pressureOpacity"), true).toBool();
  pen_input_settings_.pressure_opacity_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureOpacityMinPercent"), 15).toInt(), 1, 100);
  pen_input_settings_.use_eraser_tip = settings.value(QStringLiteral("input/pen/useEraserTip"), true).toBool();
  auto primary_token = settings.value(QStringLiteral("input/pen/primaryButtonAction")).toString();
  auto secondary_token = settings.value(QStringLiteral("input/pen/secondaryButtonAction")).toString();
  if (primary_token.isEmpty()) {
    // Migrate from the legacy "barrel button pans canvas" toggle.
    const auto legacy_pans = settings.value(QStringLiteral("input/pen/barrelButtonPans"), true).toBool();
    primary_token =
        pen_button_action_to_token(legacy_pans ? PenButtonAction::PanCanvas : PenButtonAction::None);
  }
  if (secondary_token.isEmpty()) {
    secondary_token = pen_button_action_to_token(PenButtonAction::PickColor);
  }
  pen_input_settings_.primary_button_action = pen_button_action_from_token(primary_token);
  pen_input_settings_.secondary_button_action = pen_button_action_from_token(secondary_token);
  pen_input_settings_.tilt_shape = settings.value(QStringLiteral("input/pen/tiltShape"), false).toBool();
  pen_input_settings_.tilt_min_roundness_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/tiltMinRoundnessPercent"), 35).toInt(), 1, 100);
  wheel_zooms_ = settings.value(QStringLiteral("input/wheelZooms"), true).toBool();
  apply_pen_input_settings(canvas_);
}

void MainWindow::save_pen_input_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("input/pen/enabled"), pen_input_settings_.enabled);
  settings.setValue(QStringLiteral("input/pen/pressureSize"), pen_input_settings_.pressure_size);
  settings.setValue(QStringLiteral("input/pen/pressureSizeMinPercent"),
                    pen_input_settings_.pressure_size_min_percent);
  settings.setValue(QStringLiteral("input/pen/pressureOpacity"), pen_input_settings_.pressure_opacity);
  settings.setValue(QStringLiteral("input/pen/pressureOpacityMinPercent"),
                    pen_input_settings_.pressure_opacity_min_percent);
  settings.setValue(QStringLiteral("input/pen/useEraserTip"), pen_input_settings_.use_eraser_tip);
  settings.setValue(QStringLiteral("input/pen/primaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.primary_button_action));
  settings.setValue(QStringLiteral("input/pen/secondaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.secondary_button_action));
  settings.setValue(QStringLiteral("input/pen/tiltShape"), pen_input_settings_.tilt_shape);
  settings.setValue(QStringLiteral("input/pen/tiltMinRoundnessPercent"),
                    pen_input_settings_.tilt_min_roundness_percent);
  settings.setValue(QStringLiteral("input/wheelZooms"), wheel_zooms_);
}

void MainWindow::load_view_settings() {
  auto settings = app_settings();
  view_rulers_visible_ = settings.value(QStringLiteral("view/rulersVisible"), view_rulers_visible_).toBool();
  view_grid_visible_ = settings.value(QStringLiteral("view/gridVisible"), view_grid_visible_).toBool();
  view_guides_visible_ = settings.value(QStringLiteral("view/guidesVisible"), view_guides_visible_).toBool();
  view_guides_locked_ = settings.value(QStringLiteral("view/guidesLocked"), view_guides_locked_).toBool();
  view_snap_enabled_ = settings.value(QStringLiteral("view/snapEnabled"), view_snap_enabled_).toBool();
  view_snap_to_guides_ = settings.value(QStringLiteral("view/snapToGuides"), view_snap_to_guides_).toBool();
  view_snap_to_grid_ = settings.value(QStringLiteral("view/snapToGrid"), view_snap_to_grid_).toBool();
  view_snap_to_document_ = settings.value(QStringLiteral("view/snapToDocument"), view_snap_to_document_).toBool();
  view_snap_to_layers_ = settings.value(QStringLiteral("view/snapToLayers"), view_snap_to_layers_).toBool();
  view_snap_to_selection_ = settings.value(QStringLiteral("view/snapToSelection"), view_snap_to_selection_).toBool();
  view_grid_spacing_32_ = std::clamp(settings.value(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_).toInt(),
                                     1, 320000);
  view_grid_subdivisions_ =
      std::clamp(settings.value(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_).toInt(), 1, 64);
  view_grid_style_ = std::clamp(settings.value(QStringLiteral("view/gridStyle"), view_grid_style_).toInt(), 0, 1);
  view_grid_color_ =
      settings.value(QStringLiteral("view/gridColor"), view_grid_color_).value<QColor>();
  view_guide_color_ =
      settings.value(QStringLiteral("view/guideColor"), view_guide_color_).value<QColor>();
  const bool migrate_legacy_guide_default =
      !settings.value(QStringLiteral("view/guideColorDefaultMigrated"), false).toBool();
  if (!view_grid_color_.isValid()) {
    view_grid_color_ = QColor(78, 154, 255, 105);
  }
  if (!view_guide_color_.isValid() ||
      (migrate_legacy_guide_default && view_guide_color_ == QColor(72, 186, 255, 210))) {
    view_guide_color_ = QColor(255, 70, 180, 230);
  }
  if (migrate_legacy_guide_default) {
    settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
  }

  if (view_rulers_action_ != nullptr) {
    view_rulers_action_->setChecked(view_rulers_visible_);
  }
  if (view_grid_action_ != nullptr) {
    view_grid_action_->setChecked(view_grid_visible_);
  }
  if (view_guides_action_ != nullptr) {
    view_guides_action_->setChecked(view_guides_visible_);
  }
  if (view_snap_action_ != nullptr) {
    view_snap_action_->setChecked(view_snap_enabled_);
  }
  if (view_lock_guides_action_ != nullptr) {
    view_lock_guides_action_->setChecked(view_guides_locked_);
  }
  if (view_snap_guides_action_ != nullptr) {
    view_snap_guides_action_->setChecked(view_snap_to_guides_);
  }
  if (view_snap_grid_action_ != nullptr) {
    view_snap_grid_action_->setChecked(view_snap_to_grid_);
  }
  if (view_snap_document_action_ != nullptr) {
    view_snap_document_action_->setChecked(view_snap_to_document_);
  }
  if (view_snap_layers_action_ != nullptr) {
    view_snap_layers_action_->setChecked(view_snap_to_layers_);
  }
  if (view_snap_selection_action_ != nullptr) {
    view_snap_selection_action_->setChecked(view_snap_to_selection_);
  }
  apply_canvas_aid_settings(canvas_);
}

void MainWindow::save_view_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("view/rulersVisible"), view_rulers_visible_);
  settings.setValue(QStringLiteral("view/gridVisible"), view_grid_visible_);
  settings.setValue(QStringLiteral("view/guidesVisible"), view_guides_visible_);
  settings.setValue(QStringLiteral("view/guidesLocked"), view_guides_locked_);
  settings.setValue(QStringLiteral("view/snapEnabled"), view_snap_enabled_);
  settings.setValue(QStringLiteral("view/snapToGuides"), view_snap_to_guides_);
  settings.setValue(QStringLiteral("view/snapToGrid"), view_snap_to_grid_);
  settings.setValue(QStringLiteral("view/snapToDocument"), view_snap_to_document_);
  settings.setValue(QStringLiteral("view/snapToLayers"), view_snap_to_layers_);
  settings.setValue(QStringLiteral("view/snapToSelection"), view_snap_to_selection_);
  settings.setValue(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_);
  settings.setValue(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_);
  settings.setValue(QStringLiteral("view/gridStyle"), view_grid_style_);
  settings.setValue(QStringLiteral("view/gridColor"), view_grid_color_);
  settings.setValue(QStringLiteral("view/guideColor"), view_guide_color_);
  settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
}

void MainWindow::load_tool_settings() {
  if (canvas_ == nullptr) {
    return;
  }
  auto settings = app_settings();
  stored_paint_brush_settings_.size =
      settings.value(QStringLiteral("tools/brushSize"), canvas_->brush_size()).toInt();
  stored_paint_brush_settings_.opacity =
      settings.value(QStringLiteral("tools/brushOpacity"), canvas_->brush_opacity()).toInt();
  stored_paint_brush_settings_.softness =
      settings.value(QStringLiteral("tools/brushSoftness"), canvas_->brush_softness()).toInt();
  stored_eraser_brush_settings_.size =
      settings.value(QStringLiteral("tools/eraserSize"), stored_paint_brush_settings_.size).toInt();
  stored_eraser_brush_settings_.opacity =
      settings.value(QStringLiteral("tools/eraserOpacity"), stored_paint_brush_settings_.opacity).toInt();
  stored_eraser_brush_settings_.softness =
      settings.value(QStringLiteral("tools/eraserSoftness"), stored_paint_brush_settings_.softness).toInt();
  apply_active_brush_settings_to_canvas();
  if (settings.contains(QStringLiteral("tools/brushBuildUp"))) {
    canvas_->set_brush_build_up(settings.value(QStringLiteral("tools/brushBuildUp"), canvas_->brush_build_up()).toBool());
  } else if (const auto* preset =
                 find_brush_preset(settings.value(QStringLiteral("tools/brushPreset"), QString()).toString());
             preset != nullptr) {
    canvas_->set_brush_build_up(preset->build_up);
  }
  canvas_->set_wand_tolerance(settings.value(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance()).toInt());
  canvas_->set_wand_contiguous(settings.value(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous()).toBool());
  canvas_->set_wand_sample_all_layers(
      settings.value(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers()).toBool());
  canvas_->set_show_transform_controls(
      settings.value(QStringLiteral("tools/showTransformControls"), true).toBool());
  const auto transform_interpolation =
      settings.value(QStringLiteral("tools/transformInterpolation"),
                     static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic))
          .toInt();
  switch (static_cast<CanvasWidget::TransformInterpolation>(transform_interpolation)) {
    case CanvasWidget::TransformInterpolation::NearestNeighbor:
      canvas_->set_transform_interpolation(CanvasWidget::TransformInterpolation::NearestNeighbor);
      break;
    case CanvasWidget::TransformInterpolation::Bilinear:
      canvas_->set_transform_interpolation(CanvasWidget::TransformInterpolation::Bilinear);
      break;
    case CanvasWidget::TransformInterpolation::Bicubic:
    default:
      canvas_->set_transform_interpolation(CanvasWidget::TransformInterpolation::Bicubic);
      break;
  }
  canvas_->set_clone_aligned(settings.value(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned()).toBool());
  canvas_->set_shape_corner_radius(
      settings.value(QStringLiteral("tools/shapeCornerRadius"), canvas_->shape_corner_radius()).toInt());
  if (auto* shape_corner_radius = findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
      shape_corner_radius != nullptr) {
    QSignalBlocker blocker(shape_corner_radius);
    shape_corner_radius->setValue(canvas_->shape_corner_radius());
  }
  canvas_->set_fill_opacity(settings.value(QStringLiteral("tools/fillOpacity"), canvas_->fill_opacity()).toInt());
  canvas_->set_fill_softness(settings.value(QStringLiteral("tools/fillSoftness"), canvas_->fill_softness()).toInt());
  const auto sync_fill_widget = [this](const QString& spin_name, const QString& slider_name, int value) {
    if (auto* spin = findChild<QSpinBox*>(spin_name); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
    if (auto* slider = findChild<QSlider*>(slider_name); slider != nullptr) {
      QSignalBlocker blocker(slider);
      slider->setValue(value);
    }
  };
  sync_fill_widget(QStringLiteral("fillOpacitySpin"), QStringLiteral("fillOpacitySlider"), canvas_->fill_opacity());
  sync_fill_widget(QStringLiteral("fillSoftnessSpin"), QStringLiteral("fillSoftnessSlider"), canvas_->fill_softness());
  const auto gradient_method = settings.value(QStringLiteral("tools/gradientMethod"),
                                              static_cast<int>(canvas_->gradient_method()))
                                   .toInt();
  canvas_->set_gradient_method(gradient_method == static_cast<int>(GradientMethod::Radial) ? GradientMethod::Radial
                                                                                           : GradientMethod::Linear);
  canvas_->set_gradient_reverse(settings.value(QStringLiteral("tools/gradientReverse"), canvas_->gradient_reverse()).toBool());
  canvas_->set_gradient_opacity(settings.value(QStringLiteral("tools/gradientOpacity"), canvas_->gradient_opacity()).toInt());
  if (settings.value(QStringLiteral("tools/gradientUseCustomStops"), false).toBool()) {
    const auto stops = deserialize_gradient_stops(settings.value(QStringLiteral("tools/gradientStops")).toByteArray());
    canvas_->set_gradient_stops(stops);
  } else {
    canvas_->set_gradient_stops(std::nullopt);
  }
  if (text_smoothing_combo_ != nullptr) {
    set_text_smoothing_combo_value(
        text_smoothing_combo_,
        settings.value(QStringLiteral("tools/textSmoothing"), kDefaultTextAntiAlias).toInt());
  }
}

void MainWindow::save_tool_settings() const {
  if (canvas_ == nullptr) {
    return;
  }
  auto settings = app_settings();
  auto paint_brush_settings = stored_paint_brush_settings_;
  auto eraser_brush_settings = stored_eraser_brush_settings_;
  auto& live_brush_settings = eraser_brush_settings_active_ ? eraser_brush_settings : paint_brush_settings;
  live_brush_settings =
      BrushToolSettings{canvas_->brush_size(), canvas_->brush_opacity(), canvas_->brush_softness()};
  settings.setValue(QStringLiteral("tools/brushSize"), paint_brush_settings.size);
  settings.setValue(QStringLiteral("tools/brushOpacity"), paint_brush_settings.opacity);
  settings.setValue(QStringLiteral("tools/brushSoftness"), paint_brush_settings.softness);
  settings.setValue(QStringLiteral("tools/eraserSize"), eraser_brush_settings.size);
  settings.setValue(QStringLiteral("tools/eraserOpacity"), eraser_brush_settings.opacity);
  settings.setValue(QStringLiteral("tools/eraserSoftness"), eraser_brush_settings.softness);
  settings.setValue(QStringLiteral("tools/brushBuildUp"), canvas_->brush_build_up());
  settings.setValue(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance());
  settings.setValue(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous());
  settings.setValue(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers());
  settings.setValue(QStringLiteral("tools/showTransformControls"), canvas_->show_transform_controls());
  settings.setValue(QStringLiteral("tools/transformInterpolation"), static_cast<int>(canvas_->transform_interpolation()));
  settings.setValue(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned());
  settings.setValue(QStringLiteral("tools/shapeCornerRadius"), canvas_->shape_corner_radius());
  settings.setValue(QStringLiteral("tools/fillOpacity"), canvas_->fill_opacity());
  settings.setValue(QStringLiteral("tools/fillSoftness"), canvas_->fill_softness());
  settings.setValue(QStringLiteral("tools/gradientMethod"), static_cast<int>(canvas_->gradient_method()));
  settings.setValue(QStringLiteral("tools/gradientReverse"), canvas_->gradient_reverse());
  settings.setValue(QStringLiteral("tools/gradientOpacity"), canvas_->gradient_opacity());
  settings.setValue(QStringLiteral("tools/gradientUseCustomStops"), canvas_->gradient_stops().has_value());
  if (canvas_->gradient_stops().has_value()) {
    settings.setValue(QStringLiteral("tools/gradientStops"), serialize_gradient_stops(*canvas_->gradient_stops()));
  } else {
    settings.remove(QStringLiteral("tools/gradientStops"));
  }
  if (brush_preset_combo_ != nullptr && brush_preset_combo_->currentIndex() >= 0) {
    settings.setValue(QStringLiteral("tools/brushPreset"), brush_preset_combo_->currentData().toString());
  }
  if (text_smoothing_combo_ != nullptr) {
    settings.setValue(QStringLiteral("tools/textSmoothing"), text_smoothing_combo_value(text_smoothing_combo_));
  }
}

MainWindow::BrushToolSettings& MainWindow::active_stored_brush_settings() {
  return eraser_brush_settings_active_ ? stored_eraser_brush_settings_ : stored_paint_brush_settings_;
}

void MainWindow::stash_active_brush_settings() {
  if (canvas_ == nullptr) {
    return;
  }
  active_stored_brush_settings() =
      BrushToolSettings{canvas_->brush_size(), canvas_->brush_opacity(), canvas_->brush_softness()};
}

void MainWindow::apply_active_brush_settings_to_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  const auto values = active_stored_brush_settings();
  canvas_->set_brush_size(values.size);
  canvas_->set_brush_opacity(values.opacity);
  canvas_->set_brush_softness(values.softness);
}

void MainWindow::set_eraser_brush_settings_active(bool active) {
  if (eraser_brush_settings_active_ == active) {
    return;
  }
  stash_active_brush_settings();
  eraser_brush_settings_active_ = active;
  apply_active_brush_settings_to_canvas();
  sync_brush_controls_from_canvas();
  save_tool_settings();
}

void MainWindow::remove_text_editor_transform_overlay(QTextEdit* editor) {
  if (editor != nullptr) {
    const auto was_active = editor->property(kTextEditorTransformedOverlayProperty).toBool();
    editor->setProperty(kTextEditorTransformedOverlayProperty, false);
    if (was_active && editor->viewport() != nullptr) {
      editor->viewport()->update();
    }
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr || (editor != nullptr && overlay->editor() != editor)) {
    return;
  }
  overlay->hide();
  overlay->deleteLater();
}

void MainWindow::update_text_editor_transform_overlay(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool() ||
      !editor->property(kTextEditorPreviewPaintProperty).toBool() ||
      editor->property("patchy.usesPsdTextFrame").toBool() ||
      !editor->property("patchy.editingLayerId").isValid()) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  auto* layer = document().find_layer(layer_id);
  if (layer == nullptr) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  const auto transform = text_transform_for_editor_or_layer(*editor, *layer);
  if (!transform.has_value() || !qtransform_has_non_translation_linear_part(*transform)) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr) {
    overlay = new TransformedTextEditOverlay(canvas_);
  }

  const auto was_active = editor->property(kTextEditorTransformedOverlayProperty).toBool();
  editor->setProperty(kTextEditorTransformedOverlayProperty, true);
  overlay->configure(editor, *transform, [this](QTextEdit* active_editor) {
    mark_text_editor_changed(active_editor);
    relayout_text_editor(active_editor, false);
    schedule_text_editor_preview(active_editor);
  });
  if (!was_active && editor->viewport() != nullptr) {
    editor->viewport()->update();
  }
}

void MainWindow::relayout_text_editor(QTextEdit* editor, bool allow_point_auto_expand) {
  if (canvas_ == nullptr || editor == nullptr) {
    return;
  }
  if (editor->property("patchy.relayoutingTextEditor").toBool()) {
    return;
  }
  struct RelayoutGuard {
    QTextEdit* editor = nullptr;
    ~RelayoutGuard() {
      if (editor != nullptr) {
        editor->setProperty("patchy.relayoutingTextEditor", false);
      }
    }
  } guard{editor};
  editor->setProperty("patchy.relayoutingTextEditor", true);
  const auto zoom = std::max(0.05, canvas_->zoom());
  const auto boxed = text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  bool old_zoom_ok = false;
  const auto old_zoom = editor->property("patchy.editorZoom").toDouble(&old_zoom_ok);
  if (old_zoom_ok && old_zoom > 0.0 && std::abs(old_zoom - zoom) > 0.0001) {
    editor->setProperty("patchy.editorZoom", zoom);
    scale_document_font_sizes(*editor->document(), zoom / old_zoom);
  } else if (!old_zoom_ok) {
    editor->setProperty("patchy.editorZoom", zoom);
  }
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto document_text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  auto editor_font = editor->font();
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * zoom))));
  configure_text_font_smoothing(editor_font, std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  apply_text_smoothing_to_document(*editor->document(),
                                   std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  preserve_text_editor_typing_format(*editor, editor_font, text_color);
  auto document_editor_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  auto document_editor_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());

  QTextOption option;
  option.setWrapMode(boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  editor->document()->setDefaultTextOption(option);
  editor->setLineWrapMode(boxed ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);

  int width = std::max(1, static_cast<int>(std::round(document_editor_width * zoom)));
  int height = std::max(1, static_cast<int>(std::round(document_editor_height * zoom)));
  if (boxed) {
    editor->document()->setTextWidth(width);
  } else {
    editor->document()->setTextWidth(-1);
    const auto minimum_width = std::max(80, width);
    const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
    if (text_document_has_non_left_alignment(*editor->document())) {
      // QTextEdit lays aligned NoWrap text out against the viewport width, so any slack between
      // the widget and the content shifts the editor's glyphs/caret away from the rendered layer
      // pixels, which lay out against the tight ideal width.  Track the content exactly (shrink
      // as well as grow) for centered/right point text; left text keeps the roomier behavior.
      width = std::max(80, content_width);
      document_editor_width =
          std::max(1, static_cast<int>(std::ceil(static_cast<double>(width) / zoom)));
    } else {
      width = allow_point_auto_expand ? std::max(minimum_width, content_width) : minimum_width;
      document_editor_width = std::max(document_editor_width,
                                       static_cast<int>(std::ceil(static_cast<double>(width) / zoom)));
    }
    editor->setProperty("patchy.documentTextWidth", document_editor_width);
    editor->document()->setTextWidth(width);
    const auto text_height =
        std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
    const auto minimum_height =
        std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * zoom * 1.45)));
    height = std::max(text_height, minimum_height);
    document_editor_height = std::max(kMinimumTextBoxDocumentSize,
                                      static_cast<int>(std::ceil(static_cast<double>(height) / zoom)));
    editor->setProperty("patchy.documentTextHeight", document_editor_height);
  }

  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(), std::max(1, width), std::max(1, height));
  update_text_editor_transform_overlay(editor);
  update_text_editor_handles(editor);
  editor->updateGeometry();
  reset_text_editor_scroll(editor);
  update_text_editor_preview_caret(*editor, zoom);
  editor->update();
}

void MainWindow::update_text_editor_handles(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr) {
    return;
  }
  if (editor->property(kTextEditorTransformedOverlayProperty).toBool()) {
    remove_text_editor_handles(editor);
    return;
  }

  struct HandleSpec {
    const char* name;
    const char* corner;
    Qt::CursorShape cursor;
  };
  static constexpr std::array<HandleSpec, 4> kHandles = {{
      {"textBoxResizeHandleTopLeft", "topLeft", Qt::SizeFDiagCursor},
      {"textBoxResizeHandleTopRight", "topRight", Qt::SizeBDiagCursor},
      {"textBoxResizeHandleBottomLeft", "bottomLeft", Qt::SizeBDiagCursor},
      {"textBoxResizeHandleBottomRight", "bottomRight", Qt::SizeFDiagCursor},
  }};

  const auto editor_rect = editor->geometry();
  const auto handle_size = 9;
  for (const auto& spec : kHandles) {
    auto* handle = canvas_->findChild<QWidget*>(QString::fromLatin1(spec.name), Qt::FindDirectChildrenOnly);
    if (handle == nullptr) {
      handle = new QWidget(canvas_);
      handle->setObjectName(QString::fromLatin1(spec.name));
      handle->setProperty("patchy.textResizeHandle", true);
      handle->setProperty("patchy.textResizeCorner", QString::fromLatin1(spec.corner));
      handle->setCursor(spec.cursor);
      handle->setFocusPolicy(Qt::NoFocus);
      handle->setAttribute(Qt::WA_StyledBackground, true);
      handle->setMouseTracking(true);
      handle->setStyleSheet(QStringLiteral(
          "background: rgb(245, 248, 252); border: 1px solid rgb(35, 38, 44); border-radius: 1px;"));
      handle->installEventFilter(this);
    }
    QPoint position;
    const auto corner = QString::fromLatin1(spec.corner);
    if (corner == QStringLiteral("topLeft")) {
      position = editor_rect.topLeft();
    } else if (corner == QStringLiteral("topRight")) {
      position = editor_rect.topRight() + QPoint(1, 0);
    } else if (corner == QStringLiteral("bottomLeft")) {
      position = editor_rect.bottomLeft() + QPoint(0, 1);
    } else {
      position = editor_rect.bottomRight() + QPoint(1, 1);
    }
    handle->setGeometry(position.x() - handle_size / 2, position.y() - handle_size / 2, handle_size, handle_size);
    handle->show();
    handle->raise();
  }
}

QWidget* MainWindow::text_editor_resize_handle_at(QPoint canvas_position) const {
  if (canvas_ == nullptr) {
    return nullptr;
  }
  QWidget* best_handle = nullptr;
  int best_distance = std::numeric_limits<int>::max();
  const auto handles = canvas_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* handle : handles) {
    if (handle == nullptr || !handle->isVisible() || !handle->property("patchy.textResizeHandle").toBool()) {
      continue;
    }
    const auto hit_rect = handle->geometry().adjusted(-8, -8, 8, 8);
    if (!hit_rect.contains(canvas_position)) {
      continue;
    }
    const auto delta = hit_rect.center() - canvas_position;
    const auto distance = delta.x() * delta.x() + delta.y() * delta.y();
    if (distance < best_distance) {
      best_distance = distance;
      best_handle = handle;
    }
  }
  return best_handle;
}

bool MainWindow::handle_text_editor_resize_event(QWidget* handle, QTextEdit* editor, QEvent* event) {
  if (canvas_ == nullptr || handle == nullptr || editor == nullptr) {
    return false;
  }
  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() != Qt::LeftButton) {
        break;
      }
      handle->setProperty("patchy.dragStartGlobal", mouse_event->globalPosition().toPoint());
      handle->setProperty("patchy.dragStartX", editor->property("patchy.documentTextX").toInt());
      handle->setProperty("patchy.dragStartY", editor->property("patchy.documentTextY").toInt());
      handle->setProperty("patchy.dragStartWidth", editor->property("patchy.documentTextWidth").toInt());
      handle->setProperty("patchy.dragStartHeight", editor->property("patchy.documentTextHeight").toInt());
      editor->setProperty("patchy.activeTextResizeHandleName", handle->objectName());
      handle->grabMouse();
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if ((mouse_event->buttons() & Qt::LeftButton) == 0 ||
          !handle->property("patchy.dragStartGlobal").isValid()) {
        break;
      }
      const auto start_global = handle->property("patchy.dragStartGlobal").toPoint();
      const auto delta = mouse_event->globalPosition().toPoint() - start_global;
      const auto zoom = std::max(0.05, canvas_->zoom());
      const auto delta_x = static_cast<int>(std::round(static_cast<double>(delta.x()) / zoom));
      const auto delta_y = static_cast<int>(std::round(static_cast<double>(delta.y()) / zoom));
      const auto start_x = handle->property("patchy.dragStartX").toInt();
      const auto start_y = handle->property("patchy.dragStartY").toInt();
      const auto start_width = std::max(kMinimumTextBoxDocumentSize, handle->property("patchy.dragStartWidth").toInt());
      const auto start_height = std::max(kMinimumTextBoxDocumentSize, handle->property("patchy.dragStartHeight").toInt());
      auto left = start_x;
      auto top = start_y;
      auto right = start_x + start_width;
      auto bottom = start_y + start_height;
      const auto corner = handle->property("patchy.textResizeCorner").toString();
      if (corner.contains(QStringLiteral("Left"))) {
        left = std::min(right - kMinimumTextBoxDocumentSize, start_x + delta_x);
      }
      if (corner.contains(QStringLiteral("Right"))) {
        right = std::max(left + kMinimumTextBoxDocumentSize, start_x + start_width + delta_x);
      }
      if (corner.contains(QStringLiteral("top"), Qt::CaseInsensitive)) {
        top = std::min(bottom - kMinimumTextBoxDocumentSize, start_y + delta_y);
      }
      if (corner.contains(QStringLiteral("bottom"), Qt::CaseInsensitive)) {
        bottom = std::max(top + kMinimumTextBoxDocumentSize, start_y + start_height + delta_y);
      }
      mark_text_editor_changed(editor);
      editor->setProperty("patchy.documentTextFlow", QString::fromLatin1(kTextFlowBox));
      editor->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant());
      editor->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant());
      // Dragging the box invalidates the import-frame caches: drop the anchor/local-rect so the
      // glyphs lay out against the live box rather than the original Photoshop frame.
      clear_text_editor_render_local_rect(*editor);
      clear_text_editor_visible_local_rect(*editor);
      // Retarget (rather than drop) any text transform to the new box origin. Clearing it would let
      // text_transform_for_editor_or_layer() fall back to the layer's original import transform,
      // which re-pins the rendered glyphs at their old position while the caret/markers move -- the
      // regression this fixes. The handle path only runs for translation-only text (rotated/skewed
      // text uses the overlay resize path), so a pure-translation override is sufficient here.
      bool retarget_transform = text_editor_transform_override(*editor).has_value();
      if (!retarget_transform && editor->property("patchy.editingLayerId").isValid()) {
        const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
        if (const auto* layer = document().find_layer(layer_id); layer != nullptr) {
          retarget_transform = patchy_text_transform_for_layer(*layer).has_value();
        }
      }
      if (retarget_transform) {
        set_text_editor_transform_override(
            *editor, LayerAffineTransform{1.0, 0.0, 0.0, 1.0, static_cast<double>(left),
                                          static_cast<double>(top)});
      } else {
        editor->setProperty(kTextEditorTransformOverrideProperty, QVariant());
      }
      editor->setProperty("patchy.documentTextX", left);
      editor->setProperty("patchy.documentTextY", top);
      editor->setProperty("patchy.documentTextWidth", std::max(kMinimumTextBoxDocumentSize, right - left));
      editor->setProperty("patchy.documentTextHeight", std::max(kMinimumTextBoxDocumentSize, bottom - top));
      relayout_text_editor(editor, false);
      schedule_text_editor_preview(editor);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      handle->setProperty("patchy.dragStartGlobal", QVariant());
      editor->setProperty("patchy.activeTextResizeHandleName", QVariant());
      handle->releaseMouse();
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }
  return false;
}

bool MainWindow::handle_text_editor_transform_overlay_event(QTextEdit* editor, QEvent* event) {
  if (canvas_ == nullptr || editor == nullptr) {
    return false;
  }
  if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
      event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick) {
    return false;
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr || overlay->editor() != editor || !overlay->isVisible()) {
    return false;
  }

  auto* mouse_event = static_cast<QMouseEvent*>(event);
  const QPointF canvas_position = QPointF(canvas_->mapFromGlobal(mouse_event->globalPosition().toPoint()));
  return overlay->handle_canvas_mouse_event(mouse_event, canvas_position);
}

void MainWindow::remove_text_editor_handles(QTextEdit* /*editor*/) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto handles = canvas_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* handle : handles) {
    if (handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
      handle->hide();
      handle->deleteLater();
    }
  }
}

void MainWindow::mark_text_editor_changed(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property("patchy.relayoutingTextEditor").toBool()) {
    return;
  }
  editor->setProperty(kTextEditorChangedProperty, true);
  editor->setProperty(kTextEditorCaretLayoutGenerationProperty,
                      editor->property(kTextEditorCaretLayoutGenerationProperty).toInt() + 1);
  if (!editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    return;
  }

  editor->setProperty(kTextEditorSourceRasterPreviewProperty, false);
  // The Photoshop raster can no longer represent the edited text.  Plain point text previously
  // fell back to the editor widget's own glyph painting here, but the widget lays aligned text
  // out against its own width -- centered/right-justified text then shows one layout while
  // editing and a different one after commit.  Hand the session to the live baked preview
  // instead, so the on-screen glyphs come from render_text_pixels, the same rasterizer the
  // commit uses (the installed-font flow already edits this way from the start).
  const bool adopt_baked_preview =
      !text_flow_is_box(editor->property("patchy.documentTextFlow").toString()) &&
      !editor->property("patchy.usesPsdTextFrame").toBool() &&
      !editor->property(kTextEditorPreviewEnabledProperty).toBool();
  if (adopt_baked_preview) {
    editor->setProperty(kTextEditorForceBakedPreviewProperty, true);
    editor->setProperty(kTextEditorPreviewEnabledProperty, true);
  }
  editor->setProperty(kTextEditorPreviewPaintProperty,
                      adopt_baked_preview ||
                          editor->property(kTextEditorExtendedBoxPreviewProperty).toBool() ||
                          editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool());
  update_text_editor_transform_overlay(editor);
  clear_text_editor_preview_overlays(*editor);
  remove_text_editor_preview(editor);

  if (editor->property("patchy.editingLayerId").isValid()) {
    const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
    if (auto* layer = document().find_layer(layer_id); layer != nullptr && layer->visible()) {
      layer->set_visible(false);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  if (adopt_baked_preview) {
    // Render the first live preview immediately: the editor no longer paints its own glyphs and
    // the debounced preview pass would otherwise leave the text invisible for a beat.
    update_text_editor_preview(editor);
  }
}

void MainWindow::schedule_text_editor_preview(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
    return;
  }
  if (!editor->property(kTextEditorPreviewEnabledProperty).toBool()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->setProperty("patchy.textPreviewPending", false);
    remove_text_editor_preview(editor);
    editor->viewport()->update();
    return;
  }
  if (editor->property(kTextEditorExtendedBoxPreviewProperty).toBool() &&
      !editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
    return;
  }

  const auto generation = editor->property(kTextEditorPreviewGenerationProperty).toInt() + 1;
  editor->setProperty(kTextEditorPreviewGenerationProperty, generation);
  const auto expensive_preview = editor->property(kTextEditorPreviewExpensiveProperty).toBool();
  const auto transformed_preview = editor->property(kTextEditorTransformedOverlayProperty).toBool();
  if (expensive_preview && !transformed_preview) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    remove_text_editor_preview(editor);
    editor->viewport()->update();
  }
  editor->setProperty("patchy.textPreviewPending", true);
  QTimer::singleShot(expensive_preview && !transformed_preview ? kExpensiveTextEditorPreviewDelayMs
                                                               : kTextEditorPreviewDelayMs,
                     editor,
                     [this, editor = QPointer<QTextEdit>(editor), generation] {
    if (editor == nullptr) {
      return;
    }
    if (editor->property(kTextEditorFinishedProperty).toBool() ||
        !editor->property(kTextEditorPreviewEnabledProperty).toBool()) {
      editor->setProperty("patchy.textPreviewPending", false);
      return;
    }
    if (editor->property(kTextEditorPreviewGenerationProperty).toInt() != generation) {
      return;
    }
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
  });
}

void MainWindow::update_text_editor_preview(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, true);
    editor->setProperty("patchy.textPreviewPending", false);
    remove_text_editor_preview(editor);
    update_text_editor_preview_caret(*editor, canvas_->zoom());
    update_text_editor_transform_overlay(editor);
    editor->viewport()->update();
    return;
  }
  auto& doc = document();
  std::optional<LayerId> editing_layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    editing_layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  auto* source = editing_layer_id.has_value() ? doc.find_layer(*editing_layer_id) : nullptr;
  const auto source_was_visible = !editor->property("patchy.editingLayerWasVisible").isValid() ||
                                  editor->property("patchy.editingLayerWasVisible").toBool();
  const auto extended_box_preview = editor->property(kTextEditorExtendedBoxPreviewProperty).toBool();
  const auto line_aware_box_preview = editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool();
  // Plain installed-font point text carries no layer effect, so it would not normally need a preview;
  // force one anyway (kTextEditorForceBakedPreviewProperty) so its on-screen glyphs come from the same
  // render_text_pixels rasterizer the committed layer uses -- no antialiasing/position shift on edit.
  const auto force_baked_preview = editor->property(kTextEditorForceBakedPreviewProperty).toBool();
  const auto needs_text_preview =
      source != nullptr && source_was_visible &&
      (layer_requires_text_editor_preview(*source) || extended_box_preview || line_aware_box_preview ||
       force_baked_preview);
  editor->setProperty(kTextEditorPreviewEnabledProperty, needs_text_preview);
  if (!needs_text_preview) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }

  // Untrimmed: run offsets index the full document text (see commit_text_editor).
  const auto text = editor->toPlainText();
  if (text.trimmed().isEmpty()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }

  const auto text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());
  const auto boxed_text =
      text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto text_anti_alias = std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor->property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(*editor),
                                                  display_text_family_from_font(editor->font()));
  }
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  auto rendered = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                     rich_text_runs, text_editor_render_local_rect(*editor),
                                                     text_editor_metric_scale(*editor));
  if (rendered.pixels.empty()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }
  if (!boxed_text && !editor->property("patchy.usesPsdTextFrame").toBool()) {
    bool updated_transform = false;
    if (source != nullptr) {
      updated_transform = update_text_editor_transform_from_psd_local_bounds(*editor, *source, rendered.pixels,
                                                                            boxed_text);
    }
    if (!updated_transform) {
      update_text_editor_transform_from_source_anchor(*editor, rendered.pixels);
    }
  }

  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  auto preview_bounds = rendered_text_bounds_for_editor(*editor, document_point, rendered);
  auto pixels = std::move(rendered.pixels);
  if (source != nullptr) {
    if (const auto transform = text_transform_for_editor_or_layer(*editor, *source);
        transform.has_value() && !editor->property("patchy.usesPsdTextFrame").toBool() &&
        !(text_editor_render_local_rect(*editor).has_value() &&
          text_editor_source_visible_anchor(*editor).has_value() &&
          !qtransform_has_non_translation_linear_part(*transform))) {
      const bool has_local_offset =
          text_editor_render_local_rect(*editor).has_value() &&
          (std::abs(rendered.local_rect.left()) > 0.0001 || std::abs(rendered.local_rect.top()) > 0.0001);
      auto transform_for_pixels = *transform;
      if (has_local_offset) {
        transform_for_pixels = qtransform_from_affine(
            affine_with_local_translation(affine_from_qtransform(*transform), rendered.local_rect.topLeft()));
      }
      // Show the same crisp glyphs the commit will produce: previewing a scaled-up text layer by
      // resampling its base-size bitmap left the text blurry for the whole edit session (then it
      // snapped sharp on commit), which made small-point/large-scale PSD text unreadable while typing.
      if (auto crisp = render_crisp_transformed_text_for_editor(
              *editor, source->metadata().contains(kLayerMetadataPsdTextTransform), boxed_text,
              has_local_offset, settings, text_color, text_width, paragraph_runs, rich_text_runs, *transform,
              transform_for_pixels);
          crisp.has_value()) {
        pixels = std::move(crisp->pixels);
        preview_bounds = crisp->bounds;
      } else {
        auto transformed = apply_text_transform_to_pixels(pixels, transform_for_pixels);
        pixels = std::move(transformed.pixels);
        preview_bounds = transformed.bounds;
      }
    }
  }
  std::optional<LayerId> restore_active_layer;
  if (editor->property("patchy.restoreActiveLayerId").isValid()) {
    restore_active_layer = static_cast<LayerId>(editor->property("patchy.restoreActiveLayerId").toULongLong());
  }

  QRect dirty = to_qrect(preview_bounds);
  if (editor->property("patchy.textPreviewLayerId").isValid()) {
    const auto preview_id = static_cast<LayerId>(editor->property("patchy.textPreviewLayerId").toULongLong());
    if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
      dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(preview_bounds);
      layer->set_opacity(source->opacity());
      layer->set_blend_mode(source->blend_mode());
      layer->layer_style() = source->layer_style();
      dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
      canvas_->document_changed_effect_bounds(dirty);
      editor->setProperty(kTextEditorPreviewPaintProperty, true);
      update_text_editor_preview_caret(*editor, canvas_->zoom());
      update_text_editor_transform_overlay(editor);
      editor->viewport()->update();
      return;
    }
    editor->setProperty("patchy.textPreviewLayerId", QVariant());
  }

  Layer preview(doc.allocate_layer_id(), tr("Text Preview").toStdString(), std::move(pixels));
  preview.set_bounds(preview_bounds);
  preview.metadata()["patchy.internal.text_preview"] = "true";
  preview.set_opacity(source->opacity());
  preview.set_blend_mode(source->blend_mode());
  preview.layer_style() = source->layer_style();
  const auto preview_id = preview.id();
  insert_layer_after_anchor(doc, std::move(preview), editing_layer_id);
  editor->setProperty("patchy.textPreviewLayerId", QVariant::fromValue<qulonglong>(preview_id));
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
    dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
  }
  canvas_->document_changed_effect_bounds(dirty);
  editor->setProperty(kTextEditorPreviewPaintProperty, true);
  update_text_editor_preview_caret(*editor, canvas_->zoom());
  update_text_editor_transform_overlay(editor);
  editor->viewport()->update();
}

void MainWindow::remove_text_editor_preview(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || !editor->property("patchy.textPreviewLayerId").isValid()) {
    return;
  }
  auto& doc = document();
  const auto preview_id = static_cast<LayerId>(editor->property("patchy.textPreviewLayerId").toULongLong());
  QRect dirty;
  if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
    dirty = to_qrect(layer_render_bounds(*layer));
  }
  const auto removed = doc.remove_layer(preview_id);
  editor->setProperty("patchy.textPreviewLayerId", QVariant());
  if (editor->property("patchy.restoreActiveLayerId").isValid()) {
    const auto restore_id = static_cast<LayerId>(editor->property("patchy.restoreActiveLayerId").toULongLong());
    if (doc.find_layer(restore_id) != nullptr) {
      doc.set_active_layer(restore_id);
    }
  }
  if (removed) {
    canvas_->document_changed_effect_bounds(dirty);
  }
}

void MainWindow::handle_canvas_view_changed(CanvasWidget* canvas) {
  if (canvas == nullptr || canvas != canvas_) {
    return;
  }
  refresh_document_info();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  relayout_text_editor(editor, false);
  reset_text_editor_scroll(editor);
}

void MainWindow::apply_text_alignment_to_active_editor(Qt::Alignment alignment) {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }
  editor->setAlignment(alignment);
  mark_text_editor_changed(editor);
  sync_text_alignment_buttons_from_editor();
  schedule_text_editor_preview(editor);
}

void MainWindow::sync_text_alignment_buttons_from_editor() {
  if (text_align_left_button_ == nullptr || text_align_center_button_ == nullptr ||
      text_align_right_button_ == nullptr || canvas_ == nullptr) {
    return;
  }
  const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  const auto alignment = editor != nullptr ? editor->alignment() : Qt::AlignLeft;
  const auto set_checked = [](QPushButton* button, bool checked) {
    QSignalBlocker blocker(button);
    button->setChecked(checked);
  };
  set_checked(text_align_left_button_, (alignment & (Qt::AlignHCenter | Qt::AlignRight)) == 0);
  set_checked(text_align_center_button_, (alignment & Qt::AlignHCenter) != 0);
  set_checked(text_align_right_button_, (alignment & Qt::AlignRight) != 0);
}

void MainWindow::apply_text_family_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto family =
      text_font_combo_ != nullptr
          ? text_font_combo_->currentFont().family()
          : text_display_family_from_format(text_editor_reference_format(*editor),
                                            editor->property("patchy.documentTextFamily").toString());
  QTextCharFormat format;
  format.setFontFamilies(render_text_families_for_display_family(family));
  set_text_display_family(format, family);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setFamilies(render_text_families_for_display_family(family));
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setProperty("patchy.documentTextFamily", family);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
}

void MainWindow::apply_text_size_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto document_text_size =
      text_size_spin_ != nullptr ? text_points_to_pixels(text_size_spin_->value(), document())
                                 : std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto editor_pixel_size = std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom())));
  QTextCharFormat format;
  format.setProperty(QTextFormat::FontPixelSize, editor_pixel_size);
  editor->setProperty("patchy.documentTextSize", document_text_size);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setPixelSize(editor_pixel_size);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_bold_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  QTextCharFormat format;
  format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setBold(bold);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_italic_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  QTextCharFormat format;
  format.setFontItalic(italic);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setItalic(italic);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_color_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  QTextCharFormat format;
  format.setForeground(QBrush(text_color));
  merge_text_char_format(*editor, format);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_primary_color_to_active_text_editor(QColor color) {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }

  color.setAlpha(255);
  editor->setProperty("patchy.documentTextColor", color);
  apply_text_color_to_active_editor();
}

void MainWindow::apply_text_smoothing_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_anti_alias = text_smoothing_combo_value(text_smoothing_combo_);
  editor->setProperty("patchy.documentTextAntiAlias", text_anti_alias);

  auto editor_font = editor->font();
  configure_text_font_smoothing(editor_font, text_anti_alias);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  const auto cursor = editor->textCursor();
  apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  editor->setTextCursor(cursor);
  editor->setCurrentCharFormat(text_format_with_smoothing(editor->currentCharFormat(), text_anti_alias));

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
}

bool MainWindow::is_text_option_widget(QWidget* widget) const {
  if (widget == nullptr) {
    return false;
  }
  if (widget->property("patchy.textResizeHandle").toBool()) {
    return true;
  }
  if (widget->objectName() == QString::fromLatin1(kTransformedTextEditOverlayObjectName)) {
    return true;
  }
  const auto owns = [widget](const QWidget* candidate) {
    return candidate != nullptr && (widget == candidate || candidate->isAncestorOf(widget));
  };
  const auto in_named_ancestor = [widget](QStringView object_name) {
    for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
      if (current->objectName() == object_name) {
        return true;
      }
    }
    return false;
  };
  return owns(text_font_combo_) || owns(text_size_spin_) || owns(text_bold_button_) || owns(text_italic_button_) ||
         owns(text_smoothing_combo_) || owns(text_color_button_) || owns(text_align_left_button_) ||
         owns(text_align_center_button_) || owns(text_align_right_button_) || owns(primary_color_button_) ||
         in_named_ancestor(QStringLiteral("swatchesDock"));
}

void MainWindow::apply_transform_controls_from_ui() {
  if (updating_transform_controls_ || canvas_ == nullptr || transform_x_spin_ == nullptr ||
      transform_y_spin_ == nullptr || transform_scale_x_spin_ == nullptr || transform_scale_y_spin_ == nullptr ||
      transform_rotation_spin_ == nullptr) {
    return;
  }
  canvas_->set_transform_controls_state(QPointF(transform_x_spin_->value(), transform_y_spin_->value()),
                                        transform_scale_x_spin_->value(), transform_scale_y_spin_->value(),
                                        transform_rotation_spin_->value());
}

void MainWindow::sync_transform_controls_from_canvas() {
  if (updating_transform_controls_) {
    return;
  }
  const auto state = canvas_ != nullptr ? canvas_->transform_controls_state() : std::optional<CanvasWidget::TransformControlsState>{};
  updating_transform_controls_ = true;
  const auto clear_guard = qScopeGuard([this] { updating_transform_controls_ = false; });

  const bool has_state = state.has_value();
  const auto set_widget_enabled = [has_state](QWidget* widget) {
    if (widget != nullptr) {
      widget->setEnabled(has_state);
    }
  };
  for (auto* widget : {static_cast<QWidget*>(transform_reference_combo_), static_cast<QWidget*>(transform_x_spin_),
                       static_cast<QWidget*>(transform_y_spin_), static_cast<QWidget*>(transform_scale_x_spin_),
                       static_cast<QWidget*>(transform_scale_y_spin_), static_cast<QWidget*>(transform_link_scale_button_),
                       static_cast<QWidget*>(transform_rotation_spin_),
                       static_cast<QWidget*>(transform_interpolation_combo_)}) {
    set_widget_enabled(widget);
  }
  if (transform_apply_button_ != nullptr) {
    transform_apply_button_->setEnabled(has_state && state->active);
  }
  if (transform_cancel_button_ != nullptr) {
    transform_cancel_button_->setEnabled(has_state && state->active);
  }
  if (!state.has_value()) {
    return;
  }

  if (transform_reference_combo_ != nullptr) {
    QSignalBlocker blocker(transform_reference_combo_);
    const auto index = transform_reference_combo_->findData(static_cast<int>(state->reference_point));
    if (index >= 0) {
      transform_reference_combo_->setCurrentIndex(index);
    }
  }
  if (transform_x_spin_ != nullptr) {
    QSignalBlocker blocker(transform_x_spin_);
    transform_x_spin_->setValue(state->reference_position.x());
  }
  if (transform_y_spin_ != nullptr) {
    QSignalBlocker blocker(transform_y_spin_);
    transform_y_spin_->setValue(state->reference_position.y());
  }
  if (transform_scale_x_spin_ != nullptr) {
    QSignalBlocker blocker(transform_scale_x_spin_);
    transform_scale_x_spin_->setValue(state->scale_x_percent);
  }
  if (transform_scale_y_spin_ != nullptr) {
    QSignalBlocker blocker(transform_scale_y_spin_);
    transform_scale_y_spin_->setValue(state->scale_y_percent);
  }
  if (transform_rotation_spin_ != nullptr) {
    QSignalBlocker blocker(transform_rotation_spin_);
    transform_rotation_spin_->setValue(state->rotation_degrees);
  }
  if (transform_interpolation_combo_ != nullptr) {
    QSignalBlocker blocker(transform_interpolation_combo_);
    const auto index = transform_interpolation_combo_->findData(static_cast<int>(state->interpolation));
    if (index >= 0) {
      transform_interpolation_combo_->setCurrentIndex(index);
    }
  }
}

void MainWindow::register_option_action(QWidget* widget, std::initializer_list<CanvasTool> tools) {
  if (widget == nullptr) {
    return;
  }
  option_actions_.emplace_back(widget, std::vector<CanvasTool>(tools.begin(), tools.end()));
}

void MainWindow::refresh_options_bar() {
  const bool has_document = has_active_document();
  const bool edit_allowed = has_document && !preview_dialog_edit_locked();
  for (const auto& [widget, tools] : option_actions_) {
    if (widget == nullptr) {
      continue;
    }
    const auto visible = tools.empty() || std::find(tools.begin(), tools.end(), current_tool_) != tools.end();
    widget->setVisible(visible);
    widget->setEnabled(edit_allowed);
    // Buttons backed by a default action mirror that action's state, so keep the
    // action in sync too (otherwise it can override the widget flags we just set).
    if (auto* button = qobject_cast<QToolButton*>(widget);
        button != nullptr && button->defaultAction() != nullptr) {
      button->defaultAction()->setVisible(visible);
      button->defaultAction()->setEnabled(edit_allowed);
    }
  }

  const auto transform_state =
      canvas_ != nullptr ? canvas_->transform_controls_state() : std::optional<CanvasWidget::TransformControlsState>{};
  const bool show_transform_options =
      edit_allowed && transform_state.has_value() &&
      (transform_state->active || (canvas_ != nullptr && current_tool_ == CanvasTool::Move && canvas_->show_transform_controls()));
  for (auto* widget : transform_option_actions_) {
    if (widget != nullptr) {
      widget->setVisible(show_transform_options);
      widget->setEnabled(show_transform_options);
    }
  }
  if (options_flow_container_ != nullptr) {
    // Visibility changes alter how many controls there are, so recompute the
    // wrapped height and let the toolbar grow or shrink accordingly.
    options_flow_container_->layout()->invalidate();
    options_flow_container_->updateGeometry();
  }
  sync_transform_controls_from_canvas();

  if (move_auto_select_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(move_auto_select_check_);
    move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  }
  if (move_show_transform_controls_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(move_show_transform_controls_check_);
    move_show_transform_controls_check_->setChecked(canvas_->show_transform_controls());
  }
  if (clone_aligned_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(clone_aligned_check_);
    clone_aligned_check_->setChecked(canvas_->clone_aligned());
  }
  if (wand_contiguous_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(wand_contiguous_check_);
    wand_contiguous_check_->setChecked(canvas_->wand_contiguous());
  }
  if (wand_sample_all_layers_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(wand_sample_all_layers_check_);
    wand_sample_all_layers_check_->setChecked(canvas_->wand_sample_all_layers());
  }
  refresh_gradient_controls_from_canvas();
  // Show the active tool's stored combine mode. The temporary Shift/Alt override
  // is applied live from the canvas's key event filter (see
  // set_selection_mode_changed_callback), so it is not folded in here where a
  // stale global modifier state could mask an explicit choice.
  update_selection_mode_buttons(canvas_ != nullptr ? canvas_->selection_mode()
                                                   : CanvasWidget::SelectionMode::Replace);
  sync_text_alignment_buttons_from_editor();
}

void MainWindow::update_selection_mode_buttons(CanvasWidget::SelectionMode mode) {
  const auto set_checked = [](QAction* action, bool checked) {
    if (action == nullptr) {
      return;
    }
    QSignalBlocker blocker(action);
    action->setChecked(checked);
  };
  set_checked(selection_new_mode_action_, mode == CanvasWidget::SelectionMode::Replace);
  set_checked(selection_add_mode_action_, mode == CanvasWidget::SelectionMode::Add);
  set_checked(selection_subtract_mode_action_, mode == CanvasWidget::SelectionMode::Subtract);
  set_checked(selection_intersect_mode_action_, mode == CanvasWidget::SelectionMode::Intersect);
}

void MainWindow::apply_selection_modes_to_canvas(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return;
  }
  const std::array<CanvasTool, CanvasWidget::kSelectionToolCount> tools{
      CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagicWand};
  for (const auto tool : tools) {
    if (const auto index = CanvasWidget::selection_tool_index(tool); index >= 0) {
      canvas->set_selection_mode_for_tool(tool, selection_modes_[static_cast<std::size_t>(index)]);
    }
  }
}

MainWindow::PreviewDialogEditLock::PreviewDialogEditLock(MainWindow& window) noexcept : window_(&window) {
  window_->begin_preview_dialog_edit_lock();
}

MainWindow::PreviewDialogEditLock::PreviewDialogEditLock(PreviewDialogEditLock&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)) {}

MainWindow::PreviewDialogEditLock::~PreviewDialogEditLock() {
  release();
}

void MainWindow::PreviewDialogEditLock::release() noexcept {
  if (window_ == nullptr) {
    return;
  }
  window_->end_preview_dialog_edit_lock();
  window_ = nullptr;
}

MainWindow::PreviewDialogEditLock MainWindow::lock_preview_dialog_edits() {
  return PreviewDialogEditLock(*this);
}

void MainWindow::begin_preview_dialog_edit_lock() {
  ++preview_dialog_edit_lock_depth_;
  if (preview_dialog_edit_lock_depth_ != 1) {
    return;
  }
  preview_dialog_edit_lock_tab_index_ = document_tabs_ != nullptr ? document_tabs_->currentIndex() : -1;
  if (canvas_ != nullptr) {
    canvas_->set_edit_locked(true);
  }
  if (document_tabs_ != nullptr) {
    if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr) {
      tab_bar->setEnabled(false);
    }
  }
  update_document_action_state();
  update_undo_redo_actions();
}

void MainWindow::end_preview_dialog_edit_lock() {
  if (preview_dialog_edit_lock_depth_ <= 0) {
    preview_dialog_edit_lock_depth_ = 0;
    return;
  }
  --preview_dialog_edit_lock_depth_;
  if (preview_dialog_edit_lock_depth_ != 0) {
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->set_edit_locked(false);
  }
  preview_dialog_edit_lock_tab_index_ = -1;
  if (document_tabs_ != nullptr) {
    if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr) {
      tab_bar->setEnabled(true);
    }
  }
  update_document_action_state();
  update_undo_redo_actions();
  refresh_layer_controls();
}

bool MainWindow::preview_dialog_edit_locked() const noexcept {
  return preview_dialog_edit_lock_depth_ > 0;
}

bool MainWindow::document_action_enabled_during_preview_lock(const QAction* action) const {
  if (action == nullptr) {
    return false;
  }
  const auto object_name = action->objectName();
  if (object_name == QStringLiteral("viewZoomInAction") ||
      object_name == QStringLiteral("viewZoomOutAction") ||
      object_name == QStringLiteral("viewFitOnScreenAction") ||
      object_name == QStringLiteral("viewActualPixelsAction") ||
      object_name == QStringLiteral("viewToggleSelectionEdgesAction") ||
      object_name == QStringLiteral("viewToggleRulersAction") ||
      object_name == QStringLiteral("viewToggleGridAction") ||
      object_name == QStringLiteral("viewToggleGuidesAction") ||
      object_name == QStringLiteral("viewToggleSnapAction") ||
      object_name == QStringLiteral("viewSnapToMenu") ||
      object_name == QStringLiteral("viewSnapToGuidesAction") ||
      object_name == QStringLiteral("viewSnapToGridAction") ||
      object_name == QStringLiteral("viewSnapToDocumentAction") ||
      object_name == QStringLiteral("viewSnapToLayersAction") ||
      object_name == QStringLiteral("viewSnapToSelectionAction") ||
      object_name == QStringLiteral("windowForceRefreshAction")) {
    return true;
  }
  if (action->data().isValid()) {
    const auto tool = static_cast<CanvasTool>(action->data().toInt());
    return tool == CanvasTool::Pan || tool == CanvasTool::Zoom;
  }
  return false;
}

bool MainWindow::show_preview_dialog_edit_lock_message() {
  statusBar()->showMessage(tr("Finish the open dialog before editing the document"));
  return true;
}

void MainWindow::register_document_action(QAction* action) {
  if (action == nullptr) {
    return;
  }
  document_actions_.push_back(action);
}

void MainWindow::register_hotkey(QAction* action, QString id, QList<QKeySequence> default_shortcuts,
                                 QString category) {
  hotkey_registry_.register_command(action, std::move(id), std::move(default_shortcuts), std::move(category));
}

void MainWindow::register_hotkey(QAction* action, QString id, QKeySequence default_shortcut, QString category) {
  QList<QKeySequence> defaults;
  if (!default_shortcut.isEmpty()) {
    defaults << default_shortcut;
  }
  register_hotkey(action, std::move(id), std::move(defaults), std::move(category));
}

void MainWindow::register_document_widget(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  document_widgets_.push_back(widget);
}

void MainWindow::update_document_action_state() {
  const bool has_document = has_active_document();
  const bool locked = preview_dialog_edit_locked();
  for (auto* action : document_actions_) {
    if (action != nullptr) {
      action->setEnabled(has_document && (!locked || document_action_enabled_during_preview_lock(action)));
    }
  }
  for (auto* widget : document_widgets_) {
    if (widget != nullptr) {
      widget->setEnabled(has_document && !locked);
    }
  }
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setEnabled(has_document && !locked);
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setEnabled(has_document && !locked);
  }
  if (layer_list_ != nullptr) {
    layer_list_->setEnabled(has_document && !locked);
  }
  refresh_options_bar();
}

void MainWindow::sync_brush_controls_from_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  if (auto* brush_size = findChild<QSpinBox*>(QStringLiteral("brushSizeSpin")); brush_size != nullptr) {
    QSignalBlocker blocker(brush_size);
    brush_size->setValue(canvas_->brush_size());
  }
  if (auto* brush_size_slider = findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
      brush_size_slider != nullptr) {
    QSignalBlocker blocker(brush_size_slider);
    brush_size_slider->setValue(canvas_->brush_size());
  }
  if (auto* brush_opacity = findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin")); brush_opacity != nullptr) {
    QSignalBlocker blocker(brush_opacity);
    brush_opacity->setValue(canvas_->brush_opacity());
  }
  if (auto* brush_opacity_slider = findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
      brush_opacity_slider != nullptr) {
    QSignalBlocker blocker(brush_opacity_slider);
    brush_opacity_slider->setValue(canvas_->brush_opacity());
  }
  if (auto* brush_softness = findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin")); brush_softness != nullptr) {
    QSignalBlocker blocker(brush_softness);
    brush_softness->setValue(canvas_->brush_softness());
  }
  if (auto* brush_softness_slider = findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
      brush_softness_slider != nullptr) {
    QSignalBlocker blocker(brush_softness_slider);
    brush_softness_slider->setValue(canvas_->brush_softness());
  }
}

void MainWindow::load_recent_files() {
  auto settings = app_settings();
  recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
  recent_files_.erase(std::remove_if(recent_files_.begin(), recent_files_.end(), [](const QString& path) {
                        return path.trimmed().isEmpty() || !QFileInfo::exists(path);
                      }),
                      recent_files_.end());
  trim_recent_files(recent_files_);
}

void MainWindow::save_recent_files() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("recentFiles"), recent_files_);
}

void MainWindow::add_recent_file(QString path) {
  path = QFileInfo(path).absoluteFilePath();
  if (path.isEmpty()) {
    return;
  }
  recent_files_.removeAll(path);
  recent_files_.prepend(path);
  trim_recent_files(recent_files_);
  save_recent_files();
  rebuild_recent_files_menu();
}

void MainWindow::rebuild_recent_files_menu() {
  if (recent_files_menu_ == nullptr) {
    return;
  }
  recent_files_menu_->clear();
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());

  const auto add_recent_action = [this](QMenu* menu, const QString& path, int index) {
    const auto label = tr("&%1 %2").arg(index).arg(QDir::toNativeSeparators(path));
    auto* action = menu->addAction(label);
    action->setToolTip(path);
    action->setData(path);
    connect(action, &QAction::triggered, this, [this, path] { open_recent_document(path); });
  };

  const auto recent_count = static_cast<int>(recent_files_.size());
  const auto direct_count = std::min(recent_count, kRecentFilesMenuPageSize);
  for (int index = 0; index < direct_count; ++index) {
    add_recent_action(recent_files_menu_, recent_files_[index], index + 1);
  }

  if (recent_count > direct_count) {
    recent_files_menu_->addSeparator();
    for (int page_start = direct_count; page_start < recent_count; page_start += kRecentFilesMenuPageSize) {
      const auto page_end = std::min(page_start + kRecentFilesMenuPageSize, recent_count);
      auto* page_menu = recent_files_menu_->addMenu(tr("Recent Files %1-%2").arg(page_start + 1).arg(page_end));
      page_menu->setObjectName(QStringLiteral("fileOpenRecentRangeMenu%1").arg(page_start + 1));
      configure_recent_files_context_menu(page_menu);
      for (int index = page_start; index < page_end; ++index) {
        add_recent_action(page_menu, recent_files_[index], index + 1);
      }
    }
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

void MainWindow::load_recent_folders() {
  auto settings = app_settings();
  recent_folders_ = settings.value(QStringLiteral("recentFolders")).toStringList();
  recent_folders_.erase(std::remove_if(recent_folders_.begin(), recent_folders_.end(),
                                       [](const QString& dir) {
                                         return dir.trimmed().isEmpty() || !QFileInfo(dir).isDir();
                                       }),
                        recent_folders_.end());
  while (recent_folders_.size() > kMaxRecentFolders) {
    recent_folders_.removeLast();
  }
}

void MainWindow::save_recent_folders() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("recentFolders"), recent_folders_);
}

void MainWindow::add_recent_folder(QString dir) {
  dir = QFileInfo(dir).absoluteFilePath();
  if (dir.isEmpty()) {
    return;
  }
  recent_folders_.removeAll(dir);
  recent_folders_.prepend(dir);
  while (recent_folders_.size() > kMaxRecentFolders) {
    recent_folders_.removeLast();
  }
  save_recent_folders();
  rebuild_recent_folders_menu();
}

void MainWindow::rebuild_recent_folders_menu() {
  if (recent_folders_menu_ == nullptr) {
    return;
  }
  recent_folders_menu_->clear();
  recent_folders_menu_->setEnabled(!recent_folders_.isEmpty());

  for (int index = 0; index < static_cast<int>(recent_folders_.size()); ++index) {
    const auto dir = recent_folders_[index];
    const auto label = tr("&%1 %2").arg(index + 1).arg(QDir::toNativeSeparators(dir));
    auto* action = recent_folders_menu_->addAction(label);
    action->setToolTip(dir);
    action->setData(dir);
    connect(action, &QAction::triggered, this, [this, dir] {
      if (preview_dialog_edit_locked()) {
        show_preview_dialog_edit_lock_message();
        return;
      }
      const auto start_dir = QFileInfo(dir).isDir() ? dir : last_open_directory();
      const auto path = get_open_file_name(this, tr("Open"), start_dir, open_file_filter(), nullptr,
                                           QStringLiteral("openFileDialog"));
      if (!path.isEmpty()) {
        open_document_path(path);
      }
    });
  }

  if (!recent_folders_.isEmpty()) {
    recent_folders_menu_->addSeparator();
    auto* clear_action = recent_folders_menu_->addAction(tr("Clear Recent Folders"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentFoldersAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      recent_folders_.clear();
      save_recent_folders();
      rebuild_recent_folders_menu();
    });
  }
}

void MainWindow::configure_recent_files_context_menu(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  menu->setProperty(kRecentFilesMenuProperty, true);
  menu->setContextMenuPolicy(Qt::CustomContextMenu);
  menu->installEventFilter(this);
  connect(menu, &QMenu::customContextMenuRequested, this,
          [this, menu](const QPoint& position) { show_recent_file_context_menu(menu, position); });
}

void MainWindow::show_recent_file_context_menu(const QPoint& position) {
  show_recent_file_context_menu(recent_files_menu_, position);
}

void MainWindow::show_recent_file_context_menu(QMenu* menu, const QPoint& position) {
  if (menu == nullptr) {
    return;
  }

  const auto* action = menu->actionAt(position);
  if (action == nullptr || action->isSeparator()) {
    return;
  }

  const auto path = action->data().toString();
  if (path.isEmpty()) {
    return;
  }

  const bool is_folder = menu->property(kRecentFoldersMenuProperty).toBool();
  const auto close_menus = [this, menu] {
    if (menu != nullptr) {
      menu->close();
    }
    if (recent_files_menu_ != nullptr) {
      recent_files_menu_->close();
    }
    if (recent_folders_menu_ != nullptr) {
      recent_folders_menu_->close();
    }
  };

  QMenu context_menu(menu);
  context_menu.setObjectName(QStringLiteral("recentFileContextMenu"));

  auto* copy_path_action = context_menu.addAction(is_folder ? tr("Copy Folder Path") : tr("Copy File Path"));
  copy_path_action->setObjectName(is_folder ? QStringLiteral("recentFolderCopyPathAction")
                                            : QStringLiteral("recentFileCopyPathAction"));
  connect(copy_path_action, &QAction::triggered, this, [this, close_menus, is_folder, path] {
    QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    statusBar()->showMessage(is_folder ? tr("Folder path copied") : tr("File path copied"));
    close_menus();
  });

  auto* open_in_explorer_action = context_menu.addAction(tr("Open in File Explorer"));
  open_in_explorer_action->setObjectName(is_folder ? QStringLiteral("recentFolderOpenInExplorerAction")
                                                   : QStringLiteral("recentFileOpenInExplorerAction"));
  connect(open_in_explorer_action, &QAction::triggered, this, [this, close_menus, is_folder, path] {
    reveal_path_in_file_explorer(path, !is_folder);
    close_menus();
  });

  context_menu.exec(menu->mapToGlobal(position));
}

void MainWindow::reveal_path_in_file_explorer(const QString& path, bool is_file) {
  const QFileInfo info(path);
  if (is_file) {
    if (!info.exists()) {
      statusBar()->showMessage(tr("File is missing"));
      return;
    }
#ifdef Q_OS_WIN
    // Open the containing folder with the file pre-selected.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
    return;
  }
  if (!info.isDir()) {
    statusBar()->showMessage(tr("Folder is missing"));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
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
  bind_action_text(action, tool_action_source(tool));
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  action->setObjectName(tool_action_object_name(tool));
  register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
  group->addAction(action);
  register_document_action(action);
  return action;
}

void MainWindow::update_history(QString label) {
  if (history_list_ != nullptr) {
    history_list_->insertItem(0, label);
    history_list_->setCurrentRow(0);
  }
  refresh_document_info();
}

void MainWindow::update_file_path_actions() {
}

void MainWindow::update_undo_redo_actions() {
  if (preview_dialog_edit_locked()) {
    if (undo_action_ != nullptr) {
      undo_action_->setEnabled(false);
    }
    if (redo_action_ != nullptr) {
      redo_action_->setEnabled(false);
    }
    return;
  }
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
  show_about_splash(this);
}

}  // namespace patchy::ui
