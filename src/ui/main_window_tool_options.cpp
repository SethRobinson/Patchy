// MainWindow's tool and options-bar state, split out of main_window.cpp: the
// brush-tip/pattern/gradient/style preset-library accessors and brush-tip
// import/define flows, the selected-layer id queries and per-layer
// opacity/fill/blend/visibility/lock/clipping handlers, the color buttons and
// gradient-stop controls, tool activation and tool-settings load/save, the
// transform-session controls, register_option_action/refresh_options_bar, the
// selection-mode buttons, and sync_brush_controls_from_canvas, plus the
// anonymous-namespace gradient-stop dialog cluster only they use.
// current_text_color and sync_text_options_from_active_editor stay in
// main_window.cpp: they read the inline text editor's formats through the
// internal text helpers there.
// Pure function moves from main_window.cpp; behavior must stay identical.

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
#include "ui/shape_appearance_dialog.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
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
    QColor background, GradientLibrary* gradient_library) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("gradientStopsDialog"));
  dialog.resize(560, 430);

  auto* layout = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Edit Gradient Stops"));
  auto* preview = new GradientStopsEditorWidget(&dialog);
  preview->setObjectName(QStringLiteral("gradientStopsPreview"));
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
  auto* presets = new QPushButton(QObject::tr("Preset..."), selected_row);
  presets->setObjectName(QStringLiteral("gradientPresetButton"));
  presets->setEnabled(gradient_library != nullptr);
  selected_layout->addWidget(presets);
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
  const auto read_row_stops = [&] {
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
    return stops;
  };
  const auto read_stops = [&] {
    auto stops = read_row_stops();
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
    preview->set_stops(read_row_stops());
    preview->set_current_row(table->currentRow());
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
  const auto stops_from_definition = [&](GradientDefinition definition) {
    LayerStyleGradient gradient;
    static_cast<GradientDefinition&>(gradient) = std::move(definition);
    for (auto& stop : gradient.color_stops) {
      if (stop.kind == GradientColorStop::Kind::Foreground) {
        stop.color = RgbColor{static_cast<std::uint8_t>(foreground.red()),
                              static_cast<std::uint8_t>(foreground.green()),
                              static_cast<std::uint8_t>(foreground.blue())};
      } else if (stop.kind == GradientColorStop::Kind::Background) {
        stop.color = RgbColor{static_cast<std::uint8_t>(background.red()),
                              static_cast<std::uint8_t>(background.green()),
                              static_cast<std::uint8_t>(background.blue())};
      }
      stop.kind = GradientColorStop::Kind::User;
    }
    std::vector<GradientStop> sampled;
    const int sample_count = gradient.form == GradientDefinitionForm::Noise ? 65 : 33;
    sampled.reserve(sample_count);
    for (int index = 0; index < sample_count; ++index) {
      const auto position = static_cast<float>(index) / static_cast<float>(sample_count - 1);
      const auto color = gradient_color(gradient, position);
      sampled.push_back(GradientStop{
          position, EditColor{color.red, color.green, color.blue,
                              static_cast<std::uint8_t>(std::clamp(
                                  std::lround(gradient_stop_opacity(gradient, position) * 255.0F), 0L, 255L))}});
    }
    return sampled;
  };
  const auto set_row_color = [&](int row, QColor color) {
    if (row < 0 || row >= table->rowCount() || !color.isValid()) {
      return;
    }
    auto* item = table->item(row, 1);
    if (item == nullptr) {
      item = set_item(row, 1, QString());
    }
    item->setText(color.name(QColor::HexRgb).toUpper());
    item->setData(Qt::UserRole, color);
    using_default_stops = false;
    refresh_preview();
  };
  const auto set_row_location = [&](int row, int location) {
    if (row < 0 || row >= table->rowCount()) {
      return;
    }
    auto* item = table->item(row, 0);
    if (item == nullptr) {
      item = set_item(row, 0, QString());
    }
    item->setText(QString::number(std::clamp(location, 0, 100)));
    using_default_stops = false;
    refresh_preview();
  };
  const auto remove_row = [&](int row) {
    if (table->rowCount() <= 2 || row < 0 || row >= table->rowCount()) {
      return;
    }
    table->removeRow(row);
    table->setCurrentCell(std::min(row, table->rowCount() - 1), 0);
    using_default_stops = false;
    refresh_preview();
  };
  const auto choose_current_color = [&] {
    if (table->rowCount() <= 0) {
      return;
    }
    const auto row = std::clamp(table->currentRow(), 0, table->rowCount() - 1);
    const auto original_color = row_color(row);
    const bool was_using_default_stops = using_default_stops;
    const auto chosen = request_patchy_color(&dialog, original_color, QObject::tr("Choose Gradient Stop Color"),
                                             [&](QColor color) { set_row_color(row, color); });
    if (!chosen.has_value()) {
      set_row_color(row, original_color);
      using_default_stops = was_using_default_stops;
      refresh_preview();
      return;
    }
    set_row_color(row, *chosen);
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
    remove_row(std::clamp(table->currentRow(), 0, table->rowCount() - 1));
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
  QObject::connect(presets, &QPushButton::clicked, &dialog, [&] {
    if (gradient_library == nullptr) return;
    const auto selected = request_gradient_manager(&dialog, *gradient_library, {});
    if (const auto* entry = gradient_library->find_entry(selected); entry != nullptr) {
      load_stops(stops_from_definition(entry->definition));
      using_default_stops = false;
    }
  });
  preview->stop_selected = [&](int row) {
    if (row >= 0 && row < table->rowCount()) {
      table->setCurrentCell(row, 0);
    }
  };
  preview->choose_stop_color_requested = [&](int row) {
    if (row >= 0 && row < table->rowCount()) {
      table->setCurrentCell(row, 1);
      choose_current_color();
    }
  };
  preview->stop_location_changed = [&](int row, int location) { set_row_location(row, location); };
  preview->stop_color_picked = [&](int row, QColor color) { set_row_color(row, color); };
  preview->stop_add_requested = [&](GradientStop stop) {
    add_row(stop);
    const int row = table->rowCount() - 1;
    table->setCurrentCell(row, 0);
    using_default_stops = false;
    refresh_preview();
    return row;
  };
  preview->stop_delete_requested = [&](int row) { remove_row(row); };

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

}  // namespace

BrushTipLibrary& MainWindow::brush_tip_library() {
  if (brush_tip_library_ == nullptr) {
    brush_tip_library_ = new BrushTipLibrary({}, this);
    // Seed the built-in bitmap tips once. The version gate (not an emptiness check) means a
    // user who deletes some or all of them is respected — they never come back on their own;
    // the manager's "Restore Defaults" button brings them back on demand. On upgrade only tips
    // NEWER than the stored version are seeded, so a bump never resurrects deleted defaults.
    auto settings = app_settings();
    constexpr int kDefaultTipsVersion = 4;  // v4 (July 2026): texture, dual, color, wet-edge tips
    const auto stored_version =
        settings.value(QStringLiteral("brushes/defaultTipsVersion"), 0).toInt();
    if (stored_version < kDefaultTipsVersion) {
      brush_tip_library_->restore_default_tips(stored_version);
      if (stored_version < 2) {
        // The v2 dynamics migration. Never re-run once applied: it cannot tell "user reset
        // dynamics after v2" from "never migrated", so a later re-run would stomp the reset.
        brush_tip_library_->apply_default_tip_dynamics();
      }
      settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), kDefaultTipsVersion);
    }
  }
  return *brush_tip_library_;
}

PatternLibrary& MainWindow::pattern_library() {
  if (pattern_library_ == nullptr) {
    pattern_library_ = new PatternLibrary({}, this);
    connect(pattern_library_, &PatternLibrary::changed, this, [this] {
      refresh_pattern_stamp_pattern_combo();
      apply_pattern_stamp_settings_to_canvas(canvas_);
      schedule_save_tool_settings();
    });
    // Seed code-generated defaults once. A user deletion stays deleted across
    // launches; the Pattern Manager's explicit restore command brings it back.
    auto settings = app_settings();
    const auto stored_version =
        settings.value(QStringLiteral("patterns/defaultPatternsVersion"), 0).toInt();
    if (stored_version < kDefaultPatternsVersion) {
      pattern_library_->restore_default_patterns(stored_version);
      if (pattern_library_->has_all_default_patterns_introduced_after(stored_version)) {
        settings.setValue(QStringLiteral("patterns/defaultPatternsVersion"),
                          kDefaultPatternsVersion);
      }
    }
  }
  return *pattern_library_;
}

GradientLibrary& MainWindow::gradient_library() {
  if (gradient_library_ == nullptr) {
    gradient_library_ = new GradientLibrary({}, this);
    auto settings = app_settings();
    const auto stored_version = settings.value(QStringLiteral("gradients/defaultGradientsVersion"), 0).toInt();
    if (stored_version < kDefaultGradientsVersion) {
      gradient_library_->restore_default_gradients(stored_version);
      if (gradient_library_->has_all_default_gradients_introduced_after(stored_version))
        settings.setValue(QStringLiteral("gradients/defaultGradientsVersion"), kDefaultGradientsVersion);
    }
  }
  return *gradient_library_;
}

StyleLibrary& MainWindow::style_library() {
  if (style_library_ == nullptr) {
    style_library_ = new StyleLibrary({}, this);
    // Seed code-generated defaults once. A user deletion stays deleted across
    // launches; the Style Manager's explicit restore command brings it back.
    auto settings = app_settings();
    const auto stored_version =
        settings.value(QStringLiteral("styles/defaultStylesVersion"), 0).toInt();
    if (stored_version < kDefaultStylesVersion) {
      style_library_->restore_default_styles(stored_version);
      if (style_library_->has_all_default_styles_introduced_after(stored_version)) {
        settings.setValue(QStringLiteral("styles/defaultStylesVersion"),
                          kDefaultStylesVersion);
      }
    }
  }
  return *style_library_;
}

void MainWindow::apply_brush_tip_to_canvas(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return;
  }
  if (active_brush_tip_id_.isEmpty() || active_brush_tip_id_ == builtin_round_brush_tip_id()) {
    canvas->set_brush_tip(nullptr, QString());
    // The Round brush carries session-only dynamics (reset every launch); while active they
    // stamp through a synthesized disc tip inside CanvasWidget.
    canvas->set_brush_dynamics(round_brush_dynamics_);
    canvas->set_brush_base_shape(round_brush_base_angle_degrees_,
                                 static_cast<int>(std::lround(round_brush_base_roundness_)));
    return;
  }
  auto tip = brush_tip_library().tip(active_brush_tip_id_);
  if (tip == nullptr) {
    canvas->set_brush_tip(nullptr, QString());
    canvas->set_brush_dynamics({});
    canvas->set_brush_base_shape(0.0, 100);
    return;
  }
  canvas->set_brush_tip(std::move(tip), active_brush_tip_id_);
  // Dynamics + static tip shape ride the tip: the library entry is the source of truth, so
  // popup edits propagate through the library's changed() -> re-apply path.
  if (const auto* entry = brush_tip_library().find_entry(active_brush_tip_id_); entry != nullptr) {
    canvas->set_brush_dynamics(entry->dynamics);
    canvas->set_brush_base_shape(entry->base_angle_degrees,
                                 static_cast<int>(std::lround(entry->base_roundness)));
  } else {
    canvas->set_brush_dynamics({});
    canvas->set_brush_base_shape(0.0, 100);
  }
}

void MainWindow::set_active_brush_tip(const QString& tip_id, bool announce,
                                      bool apply_tool_settings) {
  auto effective = tip_id.isEmpty() ? builtin_round_brush_tip_id() : tip_id;
  const auto* entry = brush_tip_library().find_entry(effective);
  if (effective != builtin_round_brush_tip_id() && entry == nullptr) {
    effective = builtin_round_brush_tip_id();
    entry = nullptr;
  }
  active_brush_tip_id_ = effective;
  apply_brush_tip_to_canvas(canvas_);
  if (apply_tool_settings && entry != nullptr &&
      (entry->tool_flow_percent.has_value() || entry->tool_airbrush.has_value())) {
    if (entry->tool_flow_percent.has_value()) {
      stored_paint_brush_settings_.flow = std::clamp(*entry->tool_flow_percent, 1, 100);
    }
    if (entry->tool_airbrush.has_value()) {
      stored_paint_brush_settings_.airbrush = *entry->tool_airbrush;
    }
    if (!eraser_brush_settings_active_ && canvas_ != nullptr) {
      canvas_->set_brush_flow(stored_paint_brush_settings_.flow);
      canvas_->set_brush_build_up(stored_paint_brush_settings_.airbrush);
      sync_brush_controls_from_canvas();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  }
  if (brush_tip_picker_ != nullptr) {
    brush_tip_picker_->set_current_tip_id(effective);
  }
  if (brush_dynamics_button_ != nullptr) {
    if (entry != nullptr) {
      brush_dynamics_button_->set_active_entry(entry);
    } else {
      brush_dynamics_button_->set_round_session(builtin_round_brush_tip_id(), round_brush_dynamics_,
                                                round_brush_base_angle_degrees_,
                                                round_brush_base_roundness_);
    }
  }
  schedule_save_tool_settings();
  if (announce) {
    statusBar()->showMessage(entry != nullptr ? tr("Brush tip: %1").arg(entry->name)
                                              : tr("Brush tip: Round"));
  }
}

void MainWindow::import_brush_tips_from_abr() {
  const auto path = QFileDialog::getOpenFileName(this, tr("Import Photoshop Brushes"), QString(),
                                                 tr("Photoshop Brushes (*.abr)"));
  if (path.isEmpty()) {
    return;
  }
  const auto before = brush_tip_library().entries().size();
  QString error;
  QStringList warnings;
  const auto first_id = brush_tip_library().import_abr(path, error, warnings);
  if (first_id.isEmpty()) {
    QMessageBox::warning(this, tr("Import Brushes"), error);
    return;
  }
  const auto imported = static_cast<int>(brush_tip_library().entries().size() - before);
  set_active_brush_tip(first_id, false);
  QMessageBox message(QMessageBox::Information, tr("Import Brushes"),
                      abr_import_summary(imported, warnings), QMessageBox::Ok, this);
  if (!warnings.isEmpty()) {
    message.setDetailedText(warnings.join(QStringLiteral("\n")));
  }
  message.exec();
}

void MainWindow::open_brush_tip_manager() {
  std::function<QImage()> capture;
  if (canvas_ != nullptr) {
    capture = [this] { return capture_brush_tip_define_source(); };
  }
  request_brush_tip_manager(this, brush_tip_library(), active_brush_tip_id_, capture,
                            [this](const QString& id) { set_active_brush_tip(id, true); });
}

QImage MainWindow::capture_brush_tip_define_source() const {
  if (canvas_ == nullptr) {
    return {};
  }
  const auto& doc = document();
  const auto canvas_rect = QRect(0, 0, doc.width(), doc.height());
  auto capture_rect = canvas_rect;
  const auto selected = canvas_->selected_document_rect();
  if (selected.has_value()) {
    capture_rect = selected->intersected(canvas_rect);
  }
  if (capture_rect.isEmpty() || capture_rect.width() > 4096 || capture_rect.height() > 4096) {
    return {};
  }

  const auto composited =
      qimage_from_document_rect(doc, capture_rect, true).convertToFormat(QImage::Format_ARGB32);
  if (composited.isNull()) {
    return {};
  }
  // Photoshop semantics: dark pixels paint, light pixels stay clear, transparency masks out.
  // A soft or non-rectangular selection additionally shapes the tip.
  QImage coverage(composited.size(), QImage::Format_Grayscale8);
  const auto use_selection_shape = selected.has_value();
  for (int y = 0; y < composited.height(); ++y) {
    const auto* src = reinterpret_cast<const QRgb*>(composited.constScanLine(y));
    auto* dst = coverage.scanLine(y);
    for (int x = 0; x < composited.width(); ++x) {
      const auto pixel = src[x];
      auto value = (255 - qGray(pixel)) * qAlpha(pixel) / 255;
      if (use_selection_shape) {
        const auto selection_alpha =
            canvas_->selection_alpha_at(QPoint(capture_rect.x() + x, capture_rect.y() + y));
        value = value * selection_alpha / 255;
      }
      dst[x] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }
  }
  return coverage;
}

void MainWindow::define_brush_tip_from_selection() {
  if (canvas_ == nullptr) {
    return;
  }
  const auto mask = capture_brush_tip_define_source();
  if (mask.isNull()) {
    show_status_error(tr("The selection is empty or too large to use as a brush tip (max 4096px)"));
    return;
  }
  bool accepted = false;
  const auto name = QInputDialog::getText(this, tr("Define Brush Tip"), tr("Name:"), QLineEdit::Normal,
                                          tr("Brush %1").arg(brush_tip_library().entries().size() + 1),
                                          &accepted);
  if (!accepted || name.trimmed().isEmpty()) {
    return;
  }
  const auto id = brush_tip_library().add_tip(name.trimmed(), mask, 0.25);
  if (id.isEmpty()) {
    show_status_error(tr("The selection is empty or too large to use as a brush tip (max 4096px)"));
    return;
  }
  set_active_brush_tip(id, false);
  statusBar()->showMessage(tr("Defined brush tip: %1").arg(name.trimmed()));
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
    show_status_error(tr("Layer pixels are locked."));
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
      // The Paths panel's transient layer-path row (and its Photoshop-style
      // auto-targeting) follows the active layer.
      refresh_paths_panel();
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

void MainWindow::set_active_layer_fill_opacity(int value) {
  if (updating_layer_controls_) return;
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_controls();
    return;
  }
  if (!pending_layer_fill_opacity_edit_active_) {
    if (!has_active_document()) return;
    auto ids = selected_or_active_layer_ids();
    auto& doc = document();
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&doc](LayerId id) {
                const auto* layer = doc.find_layer(id);
                return layer == nullptr || layer->kind() == LayerKind::Group;
              }), ids.end());
    if (ids.empty()) return;
    push_undo_snapshot(tr("Fill Opacity"));
    pending_layer_fill_opacity_ids_ = std::move(ids);
    pending_layer_fill_opacity_edit_active_ = true;
  }
  pending_layer_fill_opacity_value_ = std::clamp(value, 0, 100);
  if (layer_fill_opacity_apply_timer_ != nullptr) layer_fill_opacity_apply_timer_->start();
  else apply_pending_layer_fill_opacity();
  if (layer_fill_opacity_idle_timer_ != nullptr) layer_fill_opacity_idle_timer_->start();
}

void MainWindow::apply_pending_layer_fill_opacity() {
  if (!pending_layer_fill_opacity_value_.has_value()) return;
  const auto value = *pending_layer_fill_opacity_value_;
  pending_layer_fill_opacity_value_.reset();
  if (!has_active_document()) return;
  bool changed = false;
  for (const auto id : pending_layer_fill_opacity_ids_) {
    if (auto* layer = document().find_layer(id); layer != nullptr && layer->kind() != LayerKind::Group) {
      layer->set_fill_opacity(static_cast<float>(value) / 100.0F);
      changed = true;
    }
  }
  if (changed && canvas_ != nullptr) canvas_->document_changed_async_preview();
}

void MainWindow::finish_pending_layer_fill_opacity_edit() {
  if (layer_fill_opacity_apply_timer_ != nullptr) layer_fill_opacity_apply_timer_->stop();
  if (layer_fill_opacity_idle_timer_ != nullptr) layer_fill_opacity_idle_timer_->stop();
  apply_pending_layer_fill_opacity();
  reset_pending_layer_fill_opacity_edit();
}

void MainWindow::reset_pending_layer_fill_opacity_edit() {
  pending_layer_fill_opacity_ids_.clear();
  pending_layer_fill_opacity_value_.reset();
  pending_layer_fill_opacity_edit_active_ = false;
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
  // The lock buttons don't take focus, so an open inline text edit would not auto-commit and
  // the snapshot below would embed its provisional layer; settle the edit first like every
  // other document-mutating action.
  finish_active_text_editor();
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

void MainWindow::refresh_layer_clipping_action_state() {
  if (layer_clipping_mask_action_ == nullptr) {
    return;
  }
  bool enabled = false;
  bool clipped = false;
  if (canvas_ != nullptr && has_active_document()) {
    const auto& doc = std::as_const(document());
    if (const auto active = doc.active_layer_id(); active.has_value()) {
      if (const auto location = find_layer_location(doc.layers(), *active);
          location.has_value() && location->siblings != nullptr) {
        const auto& layer = (*location->siblings)[location->index];
        clipped = layer.clipped();
        if (layer.kind() != LayerKind::Group) {
          enabled = clipped || effective_clip_base(*location->siblings, location->index) != nullptr;
        }
      }
    }
  }
  layer_clipping_mask_action_->setText(clipped ? tr("Release Clipping Mask") : tr("Create Clipping Mask"));
  layer_clipping_mask_action_->setEnabled(enabled);
}

void MainWindow::toggle_active_layer_clipping() {
  if (updating_layer_controls_) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto location = find_layer_location(std::as_const(doc).layers(), *active);
  if (!location.has_value() || location->siblings == nullptr) {
    return;
  }
  const auto& layer = (*location->siblings)[location->index];
  const bool clipped = layer.clipped();
  if (layer.kind() == LayerKind::Group) {
    return;
  }
  if (!clipped && effective_clip_base(*location->siblings, location->index) == nullptr) {
    show_status_error(tr("Create Clipping Mask needs a pixel layer below"));
    return;
  }

  push_undo_snapshot(clipped ? tr("Release clipping mask") : tr("Create clipping mask"));
  auto* mutable_layer = doc.find_layer(*active);
  if (mutable_layer == nullptr) {
    return;
  }
  mutable_layer->set_clipped(!clipped);
  QRect affected = to_qrect(layer_render_bounds(*mutable_layer));
  // The base's rendering changes too (the isolated group re-forms around it);
  // bump its render revision so the undo diff repaints its footprint.
  if (const auto base_location = find_layer_location(std::as_const(doc).layers(), *active);
      base_location.has_value()) {
    if (const auto* base = effective_clip_base(*base_location->siblings, base_location->index); base != nullptr) {
      if (auto* mutable_base = doc.find_layer(base->id()); mutable_base != nullptr) {
        mutable_base->mark_render_changed();
        affected = affected.united(to_qrect(layer_render_bounds(*mutable_base)));
      }
    }
  }
  if (canvas_ != nullptr) {
    canvas_->document_changed(affected);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(clipped ? tr("Clipping mask released") : tr("Clipping mask created"));
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
  // See set_active_layer_lock_flag: settle an open inline text edit before snapshotting.
  finish_active_text_editor();
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
                                                    canvas_->secondary_color(), &gradient_library());
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

void MainWindow::load_tool_settings() {
  if (canvas_ == nullptr) {
    return;
  }
  auto settings = app_settings();
  // Standard Brush tip, opacity, flow, Airbrush, and softness are deliberately not
  // restored: every launch starts from the Round startup preset (round tip,
  // 100% opacity, 100% flow, Airbrush off, 0% soft) so a leftover bitmap tip or
  // a barely-visible paint rate cannot leave the brush in a confusing state.
  // The eraser resets the same way; only its size is kept across restarts.
  if (const auto* preset = find_brush_preset(default_startup_brush_preset_id()); preset != nullptr) {
    stored_paint_brush_settings_ =
        BrushToolSettings{preset->size, preset->opacity, preset->flow, preset->softness,
                          preset->build_up};
  } else {
    stored_paint_brush_settings_ = BrushToolSettings{};
  }
  stored_eraser_brush_settings_ = stored_paint_brush_settings_;
  stored_eraser_brush_settings_.size =
      settings.value(QStringLiteral("tools/eraserSize"), stored_paint_brush_settings_.size).toInt();
  set_active_brush_tip(builtin_round_brush_tip_id(), false);
  apply_active_brush_settings_to_canvas();
  current_mixer_wet_ =
      std::clamp(settings.value(QStringLiteral("tools/mixerWet"), current_mixer_wet_).toInt(), 0, 100);
  current_mixer_load_ =
      std::clamp(settings.value(QStringLiteral("tools/mixerLoad"), current_mixer_load_).toInt(), 1, 100);
  current_mixer_mix_ =
      std::clamp(settings.value(QStringLiteral("tools/mixerMix"), current_mixer_mix_).toInt(), 0, 100);
  current_mixer_flow_ =
      std::clamp(settings.value(QStringLiteral("tools/mixerFlow"), current_mixer_flow_).toInt(), 1, 100);
  canvas_->set_mixer_wet(current_mixer_wet_);
  canvas_->set_mixer_load(current_mixer_load_);
  canvas_->set_mixer_mix(current_mixer_mix_);
  canvas_->set_mixer_flow(current_mixer_flow_);
  canvas_->set_wand_tolerance(settings.value(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance()).toInt());
  canvas_->set_wand_contiguous(settings.value(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous()).toBool());
  canvas_->set_wand_sample_all_layers(
      settings.value(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers()).toBool());
  canvas_->set_quick_select_size(
      settings.value(QStringLiteral("tools/quickSelectSize"), canvas_->quick_select_size()).toInt());
  canvas_->set_quick_select_sample_all_layers(
      settings.value(QStringLiteral("tools/quickSelectSampleAllLayers"), canvas_->quick_select_sample_all_layers())
          .toBool());
  canvas_->set_quick_select_enhance_edge(
      settings.value(QStringLiteral("tools/quickSelectEnhanceEdge"), canvas_->quick_select_enhance_edge()).toBool());
  canvas_->set_magnetic_lasso_width(
      settings.value(QStringLiteral("tools/magneticLassoWidth"), canvas_->magnetic_lasso_width()).toInt());
  canvas_->set_magnetic_lasso_edge_contrast(
      settings.value(QStringLiteral("tools/magneticLassoEdgeContrast"), canvas_->magnetic_lasso_edge_contrast())
          .toInt());
  canvas_->set_magnetic_lasso_frequency(
      settings.value(QStringLiteral("tools/magneticLassoFrequency"), canvas_->magnetic_lasso_frequency()).toInt());
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
  current_pattern_stamp_pattern_id_ =
      settings.value(QStringLiteral("tools/patternStampPatternId")).toString();
  const auto& patterns = pattern_library().entries();
  if (pattern_library().find_entry_by_pattern_id(current_pattern_stamp_pattern_id_) == nullptr) {
    current_pattern_stamp_pattern_id_ = patterns.empty() ? QString() : patterns.front().id;
  }
  current_pattern_stamp_aligned_ =
      settings.value(QStringLiteral("tools/patternStampAligned"), true).toBool();
  apply_pattern_stamp_settings_to_canvas(canvas_);
  current_healing_diffusion_ =
      std::clamp(settings.value(QStringLiteral("tools/healingDiffusion"), current_healing_diffusion_).toInt(), 1, 7);
  canvas_->set_healing_diffusion(current_healing_diffusion_);
  current_local_adjustment_strength_ =
      std::clamp(settings.value(QStringLiteral("tools/localAdjustmentStrength"),
                                current_local_adjustment_strength_).toInt(),
                 1, 100);
  const auto local_tone_range =
      settings.value(QStringLiteral("tools/localToneRange"), QStringLiteral("midtones")).toString();
  current_local_tone_range_ = local_tone_range == QStringLiteral("shadows")
                                  ? CanvasWidget::LocalToneRange::Shadows
                              : local_tone_range == QStringLiteral("highlights")
                                  ? CanvasWidget::LocalToneRange::Highlights
                                  : CanvasWidget::LocalToneRange::Midtones;
  current_local_protect_tones_ =
      settings.value(QStringLiteral("tools/localProtectTones"), current_local_protect_tones_).toBool();
  const auto sponge_mode =
      settings.value(QStringLiteral("tools/spongeMode"), QStringLiteral("desaturate")).toString();
  current_sponge_mode_ = sponge_mode == QStringLiteral("saturate")
                             ? CanvasWidget::SpongeMode::Saturate
                             : CanvasWidget::SpongeMode::Desaturate;
  current_sponge_vibrance_ =
      settings.value(QStringLiteral("tools/spongeVibrance"), current_sponge_vibrance_).toBool();
  canvas_->set_local_adjustment_strength(current_local_adjustment_strength_);
  canvas_->set_local_tone_range(current_local_tone_range_);
  canvas_->set_local_protect_tones(current_local_protect_tones_);
  canvas_->set_sponge_mode(current_sponge_mode_);
  canvas_->set_sponge_vibrance(current_sponge_vibrance_);
  canvas_->set_shape_corner_radius(
      settings.value(QStringLiteral("tools/shapeCornerRadius"), canvas_->shape_corner_radius()).toInt());
  // The spin resync below is signal-blocked, so mirror the loaded value by hand.
  current_shape_corner_radius_ = canvas_->shape_corner_radius();
  if (auto* shape_corner_radius = findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
      shape_corner_radius != nullptr) {
    QSignalBlocker blocker(shape_corner_radius);
    shape_corner_radius->setValue(canvas_->shape_corner_radius());
  }
  const auto vector_mode_name =
      settings.value(QStringLiteral("tools/vectorToolMode"), QStringLiteral("shape")).toString();
  current_vector_tool_mode_ = vector_mode_name == QStringLiteral("path")     ? VectorToolMode::Path
                              : vector_mode_name == QStringLiteral("pixels") ? VectorToolMode::Pixels
                                                                             : VectorToolMode::Shape;
  canvas_->set_vector_tool_mode(current_vector_tool_mode_);
  if (vector_mode_combo_ != nullptr) {
    QSignalBlocker blocker(vector_mode_combo_);
    vector_mode_combo_->setCurrentIndex(current_vector_tool_mode_ == VectorToolMode::Path     ? 1
                                        : current_vector_tool_mode_ == VectorToolMode::Pixels ? 2
                                                                                              : 0);
  }
  // Options-bar paint mirrors: solid colors keep the historical keys; the
  // paint kind and pattern/gradient references are their own append-only keys.
  // Gradient placement and pattern placement params deliberately reset to
  // defaults each launch (the appearance dialog owns per-layer tuning).
  const auto load_vector_paint = [this, &settings](patchy::VectorFill& paint,
                                                   QString& gradient_id, const char* color_key,
                                                   const char* kind_key, const char* pattern_key,
                                                   const char* gradient_key) {
    const QColor default_color(paint.color.red, paint.color.green, paint.color.blue);
    if (const auto color = QColor(
            settings.value(QLatin1String(color_key), default_color.name()).toString());
        color.isValid()) {
      paint.color = RgbColor{static_cast<std::uint8_t>(color.red()),
                             static_cast<std::uint8_t>(color.green()),
                             static_cast<std::uint8_t>(color.blue())};
    }
    const auto kind_name = settings.value(QLatin1String(kind_key), QStringLiteral("solid")).toString();
    paint.kind = kind_name == QStringLiteral("none")       ? patchy::VectorFillKind::None
                 : kind_name == QStringLiteral("gradient") ? patchy::VectorFillKind::Gradient
                 : kind_name == QStringLiteral("pattern")  ? patchy::VectorFillKind::Pattern
                                                           : patchy::VectorFillKind::Solid;
    if (paint.kind == patchy::VectorFillKind::Pattern) {
      const auto pattern_id = settings.value(QLatin1String(pattern_key)).toString();
      if (const auto* entry = pattern_library().find_entry_by_pattern_id(pattern_id);
          entry != nullptr) {
        paint.pattern_id = pattern_id.toStdString();
        paint.pattern_name = entry->name.toStdString();
      } else {
        paint.kind = patchy::VectorFillKind::Solid;  // unresolvable reference
      }
    }
    if (paint.kind == patchy::VectorFillKind::Gradient) {
      gradient_id = settings.value(QLatin1String(gradient_key)).toString();
      const auto* entry = gradient_library().find_entry(gradient_id);
      if (entry == nullptr && !gradient_library().entries().empty()) {
        gradient_id = gradient_library().entries().front().storage_id;
        entry = gradient_library().find_entry(gradient_id);
      }
      const auto foreground = canvas_ != nullptr ? canvas_->primary_color() : QColor(Qt::black);
      const auto background = canvas_ != nullptr ? canvas_->secondary_color() : QColor(Qt::white);
      const auto fg = RgbColor{static_cast<std::uint8_t>(foreground.red()),
                               static_cast<std::uint8_t>(foreground.green()),
                               static_cast<std::uint8_t>(foreground.blue())};
      const auto bg = RgbColor{static_cast<std::uint8_t>(background.red()),
                               static_cast<std::uint8_t>(background.green()),
                               static_cast<std::uint8_t>(background.blue())};
      if (entry != nullptr) {
        static_cast<GradientDefinition&>(paint.gradient) =
            resolve_gradient_definition(entry->definition, fg, bg);
      } else {
        paint.gradient.color_stops = {GradientColorStop{0.0F, fg, 0.5F},
                                      GradientColorStop{1.0F, bg, 0.5F}};
        paint.gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F},
                                      GradientAlphaStop{1.0F, 1.0F, 0.5F}};
      }
      paint.gradient.type = LayerStyleGradientType::Linear;
      paint.gradient.angle_degrees = 90.0F;
      paint.gradient.scale = 1.0F;
      paint.gradient.reverse = false;
    }
  };
  load_vector_paint(current_vector_fill_, current_vector_fill_gradient_id_,
                    "tools/vectorFillColor", "tools/vectorFillKind", "tools/vectorFillPatternId",
                    "tools/vectorFillGradientId");
  load_vector_paint(current_vector_stroke_paint_, current_vector_stroke_gradient_id_,
                    "tools/vectorStrokeColor", "tools/vectorStrokePaintKind",
                    "tools/vectorStrokePatternId", "tools/vectorStrokeGradientId");
  current_vector_stroke_enabled_ =
      settings.value(QStringLiteral("tools/vectorStrokeEnabled"), current_vector_stroke_enabled_).toBool();
  current_vector_stroke_width_ = std::clamp(
      settings.value(QStringLiteral("tools/vectorStrokeWidth"), current_vector_stroke_width_).toDouble(),
      0.1, 1000.0);
  current_vector_line_weight_ = std::clamp(
      settings.value(QStringLiteral("tools/vectorLineWeight"), current_vector_line_weight_).toInt(), 1,
      1000);
  if (auto* stroke_check = findChild<QCheckBox*>(QStringLiteral("vectorStrokeCheck"));
      stroke_check != nullptr) {
    QSignalBlocker blocker(stroke_check);
    stroke_check->setChecked(current_vector_stroke_enabled_);
  }
  if (auto* stroke_width = findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
      stroke_width != nullptr) {
    QSignalBlocker blocker(stroke_width);
    stroke_width->setValue(current_vector_stroke_width_);
  }
  if (auto* line_weight = findChild<QSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
      line_weight != nullptr) {
    QSignalBlocker blocker(line_weight);
    line_weight->setValue(current_vector_line_weight_);
  }
  current_line_arrow_start_ =
      settings.value(QStringLiteral("tools/lineArrowStart"), current_line_arrow_start_).toBool();
  current_line_arrow_end_ =
      settings.value(QStringLiteral("tools/lineArrowEnd"), current_line_arrow_end_).toBool();
  if (auto* arrow_start = findChild<QCheckBox*>(QStringLiteral("lineArrowStartCheck"));
      arrow_start != nullptr) {
    QSignalBlocker blocker(arrow_start);
    arrow_start->setChecked(current_line_arrow_start_);
  }
  if (auto* arrow_end = findChild<QCheckBox*>(QStringLiteral("lineArrowEndCheck"));
      arrow_end != nullptr) {
    QSignalBlocker blocker(arrow_end);
    arrow_end->setChecked(current_line_arrow_end_);
  }
  if (auto* sides = findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin")); sides != nullptr) {
    QSignalBlocker blocker(sides);
    sides->setValue(
        std::clamp(settings.value(QStringLiteral("tools/polygonSides"), sides->value()).toInt(), 3, 100));
    canvas_->set_polygon_sides(sides->value());
  }
  if (auto* inset = findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin")); inset != nullptr) {
    QSignalBlocker blocker(inset);
    inset->setValue(std::clamp(
        settings.value(QStringLiteral("tools/polygonStarInset"), inset->value()).toInt(), 0, 99));
    canvas_->set_polygon_star_inset(inset->value());
  }
  if (custom_shape_combo_ != nullptr) {
    const auto stored_shape = settings.value(QStringLiteral("tools/customShapeId")).toString();
    if (!stored_shape.isEmpty()) {
      if (const auto index = custom_shape_combo_->findData(stored_shape); index >= 0) {
        QSignalBlocker blocker(custom_shape_combo_);
        custom_shape_combo_->setCurrentIndex(index);
      }
    }
    apply_custom_shape_selection();
  }
  update_vector_swatch_icons();
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

void MainWindow::schedule_save_tool_settings() {
  if (tool_settings_save_timer_ != nullptr) {
    tool_settings_save_timer_->start();
  } else {
    save_tool_settings();
  }
}

void MainWindow::save_tool_settings() const {
  if (canvas_ == nullptr) {
    return;
  }
  // An explicit save captures the current state, so drop any debounced one
  // still pending; otherwise it would fire later and rewrite the same values.
  if (tool_settings_save_timer_ != nullptr) {
    tool_settings_save_timer_->stop();
  }
  auto settings = app_settings();
  // Standard Brush tip/opacity/flow/Airbrush/softness (and the paint brush size) reset to
  // the Round startup preset on every launch (see load_tool_settings()), so the
  // eraser size is the only brush value worth persisting.
  const auto eraser_size =
      eraser_brush_settings_active_ ? canvas_->brush_size() : stored_eraser_brush_settings_.size;
  settings.setValue(QStringLiteral("tools/eraserSize"), eraser_size);
  settings.setValue(QStringLiteral("tools/mixerWet"), current_mixer_wet_);
  settings.setValue(QStringLiteral("tools/mixerLoad"), current_mixer_load_);
  settings.setValue(QStringLiteral("tools/mixerMix"), current_mixer_mix_);
  settings.setValue(QStringLiteral("tools/mixerFlow"), current_mixer_flow_);
  settings.setValue(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance());
  settings.setValue(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous());
  settings.setValue(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers());
  settings.setValue(QStringLiteral("tools/quickSelectSize"), canvas_->quick_select_size());
  settings.setValue(QStringLiteral("tools/quickSelectSampleAllLayers"), canvas_->quick_select_sample_all_layers());
  settings.setValue(QStringLiteral("tools/quickSelectEnhanceEdge"), canvas_->quick_select_enhance_edge());
  settings.setValue(QStringLiteral("tools/magneticLassoWidth"), canvas_->magnetic_lasso_width());
  settings.setValue(QStringLiteral("tools/magneticLassoEdgeContrast"), canvas_->magnetic_lasso_edge_contrast());
  settings.setValue(QStringLiteral("tools/magneticLassoFrequency"), canvas_->magnetic_lasso_frequency());
  settings.setValue(QStringLiteral("tools/showTransformControls"), canvas_->show_transform_controls());
  settings.setValue(QStringLiteral("tools/transformInterpolation"), static_cast<int>(canvas_->transform_interpolation()));
  settings.setValue(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned());
  settings.setValue(QStringLiteral("tools/patternStampPatternId"), current_pattern_stamp_pattern_id_);
  settings.setValue(QStringLiteral("tools/patternStampAligned"), current_pattern_stamp_aligned_);
  settings.setValue(QStringLiteral("tools/healingDiffusion"), current_healing_diffusion_);
  settings.setValue(QStringLiteral("tools/localAdjustmentStrength"), current_local_adjustment_strength_);
  QString local_tone_range = QStringLiteral("midtones");
  switch (current_local_tone_range_) {
    case CanvasWidget::LocalToneRange::Shadows:
      local_tone_range = QStringLiteral("shadows");
      break;
    case CanvasWidget::LocalToneRange::Midtones:
      break;
    case CanvasWidget::LocalToneRange::Highlights:
      local_tone_range = QStringLiteral("highlights");
      break;
  }
  settings.setValue(QStringLiteral("tools/localToneRange"), local_tone_range);
  settings.setValue(QStringLiteral("tools/localProtectTones"), current_local_protect_tones_);
  settings.setValue(QStringLiteral("tools/spongeMode"),
                    current_sponge_mode_ == CanvasWidget::SpongeMode::Saturate
                        ? QStringLiteral("saturate")
                        : QStringLiteral("desaturate"));
  settings.setValue(QStringLiteral("tools/spongeVibrance"), current_sponge_vibrance_);
  settings.setValue(QStringLiteral("tools/shapeCornerRadius"), canvas_->shape_corner_radius());
  settings.setValue(QStringLiteral("tools/vectorToolMode"),
                    current_vector_tool_mode_ == VectorToolMode::Path     ? QStringLiteral("path")
                    : current_vector_tool_mode_ == VectorToolMode::Pixels ? QStringLiteral("pixels")
                                                                          : QStringLiteral("shape"));
  const auto save_vector_paint = [&settings](const patchy::VectorFill& paint,
                                             const QString& gradient_id, const char* color_key,
                                             const char* kind_key, const char* pattern_key,
                                             const char* gradient_key) {
    settings.setValue(QLatin1String(color_key),
                      QColor(paint.color.red, paint.color.green, paint.color.blue)
                          .name(QColor::HexRgb));
    settings.setValue(QLatin1String(kind_key),
                      paint.kind == patchy::VectorFillKind::None       ? QStringLiteral("none")
                      : paint.kind == patchy::VectorFillKind::Gradient ? QStringLiteral("gradient")
                      : paint.kind == patchy::VectorFillKind::Pattern  ? QStringLiteral("pattern")
                                                                       : QStringLiteral("solid"));
    settings.setValue(QLatin1String(pattern_key), QString::fromStdString(paint.pattern_id));
    settings.setValue(QLatin1String(gradient_key), gradient_id);
  };
  save_vector_paint(current_vector_fill_, current_vector_fill_gradient_id_,
                    "tools/vectorFillColor", "tools/vectorFillKind", "tools/vectorFillPatternId",
                    "tools/vectorFillGradientId");
  save_vector_paint(current_vector_stroke_paint_, current_vector_stroke_gradient_id_,
                    "tools/vectorStrokeColor", "tools/vectorStrokePaintKind",
                    "tools/vectorStrokePatternId", "tools/vectorStrokeGradientId");
  settings.setValue(QStringLiteral("tools/vectorStrokeEnabled"), current_vector_stroke_enabled_);
  settings.setValue(QStringLiteral("tools/vectorStrokeWidth"), current_vector_stroke_width_);
  settings.setValue(QStringLiteral("tools/vectorLineWeight"), current_vector_line_weight_);
  settings.setValue(QStringLiteral("tools/lineArrowStart"), current_line_arrow_start_);
  settings.setValue(QStringLiteral("tools/lineArrowEnd"), current_line_arrow_end_);
  if (auto* sides = findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin")); sides != nullptr) {
    settings.setValue(QStringLiteral("tools/polygonSides"), sides->value());
  }
  if (auto* inset = findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin")); inset != nullptr) {
    settings.setValue(QStringLiteral("tools/polygonStarInset"), inset->value());
  }
  if (custom_shape_combo_ != nullptr) {
    settings.setValue(QStringLiteral("tools/customShapeId"),
                      custom_shape_combo_->currentData().toString());
  }
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
      BrushToolSettings{canvas_->brush_size(), canvas_->brush_opacity(), canvas_->brush_flow(),
                        canvas_->brush_softness(), canvas_->brush_build_up()};
}

void MainWindow::apply_active_brush_settings_to_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  const auto values = active_stored_brush_settings();
  canvas_->set_brush_size(values.size);
  canvas_->set_brush_opacity(values.opacity);
  canvas_->set_brush_flow(values.flow);
  canvas_->set_brush_softness(values.softness);
  canvas_->set_brush_build_up(values.airbrush);
  // Brush tips are application-wide like the rest of the brush settings; an incoming canvas
  // (new tab or tab switch) may hold a stale or empty tip.
  apply_brush_tip_to_canvas(canvas_);
}

void MainWindow::apply_pattern_stamp_settings_to_canvas(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_pattern_stamp_aligned(current_pattern_stamp_aligned_);
  canvas->set_pattern_stamp_pattern(pattern_library().resource(current_pattern_stamp_pattern_id_));
}

void MainWindow::refresh_pattern_stamp_pattern_combo() {
  if (pattern_stamp_pattern_combo_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(pattern_stamp_pattern_combo_);
  pattern_stamp_pattern_combo_->clear();
  for (const auto& entry : pattern_library().entries()) {
    pattern_stamp_pattern_combo_->addItem(QIcon(entry.thumbnail),
                                          pattern_library_entry_display_name(entry), entry.id);
    const auto index = pattern_stamp_pattern_combo_->count() - 1;
    auto detail = QStringLiteral("%1 x %2").arg(entry.size.width()).arg(entry.size.height());
    if (!entry.folder.isEmpty()) {
      detail = entry.folder + QStringLiteral(" - ") + detail;
    }
    pattern_stamp_pattern_combo_->setItemData(index, detail, Qt::ToolTipRole);
  }
  auto index = pattern_stamp_pattern_combo_->findData(current_pattern_stamp_pattern_id_);
  if (index < 0 && pattern_stamp_pattern_combo_->count() > 0) {
    index = 0;
    current_pattern_stamp_pattern_id_ = pattern_stamp_pattern_combo_->itemData(0).toString();
  }
  pattern_stamp_pattern_combo_->setCurrentIndex(index);
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
  const bool warp_active = canvas_ != nullptr && canvas_->warp_transform_active();
  const bool session_active = warp_active || (has_state && state->active);
  for (auto* button : {transform_warp_mode_button_, transform_apply_button_, transform_cancel_button_}) {
    if (button != nullptr) {
      button->setEnabled(session_active);
    }
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
  const auto transform_state =
      canvas_ != nullptr ? canvas_->transform_controls_state() : std::optional<CanvasWidget::TransformControlsState>{};
  const bool free_transform_session = edit_allowed && transform_state.has_value() && transform_state->active;
  const bool warp_session = edit_allowed && canvas_ != nullptr && canvas_->warp_transform_active();
  const bool transform_session_active = free_transform_session || warp_session;
  for (const auto& [widget, tools] : option_actions_) {
    if (widget == nullptr) {
      continue;
    }
    // A transform/warp session owns the options bar (Photoshop behavior): the
    // tool's own controls are unusable while one runs (the canvas consumes every
    // click), so they hide instead of stacking next to the session controls and
    // wrapping the bar onto a second row (which shifted the canvas down).
    const auto tool_matches = tools.empty() || std::find(tools.begin(), tools.end(), current_tool_) != tools.end();
    const auto visible = tool_matches && !transform_session_active;
    widget->setVisible(visible);
    auto enabled = edit_allowed;
    if (widget == brush_dynamics_button_ && brush_dynamics_button_ != nullptr) {
      // Enabled once a model is loaded (bitmap tip or the Round session); only the brief
      // pre-initialization state has neither.
      enabled = enabled && brush_dynamics_button_->has_active_tip();
    }
    widget->setEnabled(enabled);
    // Buttons backed by a default action mirror that action's state, so keep the
    // action in sync too (otherwise it can override the widget flags we just set).
    if (auto* button = qobject_cast<QToolButton*>(widget);
        button != nullptr && button->defaultAction() != nullptr) {
      button->defaultAction()->setVisible(visible);
      button->defaultAction()->setEnabled(edit_allowed);
    }
  }

  // The numeric transform controls show only while a session is actually active
  // (Photoshop): the Move tool's passive box swaps them in the instant a handle
  // drag or Ctrl+T starts the session.
  const bool show_transform_options = free_transform_session;
  for (auto* widget : transform_option_actions_) {
    if (widget != nullptr) {
      widget->setVisible(show_transform_options);
      widget->setEnabled(show_transform_options);
    }
  }
  const bool show_warp_options = warp_session;
  for (auto* widget : warp_option_actions_) {
    if (widget != nullptr) {
      widget->setVisible(show_warp_options);
      widget->setEnabled(show_warp_options);
    }
  }
  for (auto* widget : transform_session_actions_) {
    if (widget != nullptr) {
      widget->setVisible(transform_session_active);
      widget->setEnabled(transform_session_active);
    }
  }
  if (transform_warp_mode_button_ != nullptr) {
    // setChecked never emits clicked, so no blocker is needed; this also restores
    // the visual state after a refused switch (text layer, undecodable source).
    transform_warp_mode_button_->setChecked(warp_session);
  }
  // The text session's apply/cancel pair rides next to the text controls while an
  // inline editor is open.  The finished-property check matters: commit teardown
  // has a window between marking the editor finished and reparenting it away in
  // which re-entrant refreshes (layer-list updates) still find the child.
  auto* inline_text_editor =
      canvas_ != nullptr ? canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) : nullptr;
  const bool text_session_active = !transform_session_active && inline_text_editor != nullptr &&
                                   !inline_text_editor->property(kTextEditorFinishedProperty).toBool();
  for (auto* button : {text_apply_button_, text_cancel_button_}) {
    if (button != nullptr) {
      button->setVisible(text_session_active);
      button->setEnabled(text_session_active);
    }
  }
  // The non-modal Character dialog grays out (and shows its click-in-text hint) whenever
  // no live editor session exists; every session boundary funnels through this refresh.
  sync_text_character_dialog_from_editor();
  if (show_warp_options && warp_style_combo_ != nullptr && warp_bend_spin_ != nullptr) {
    // Mirror the canvas state (a handle drag flips the style back to Custom).
    QSignalBlocker combo_blocker(warp_style_combo_);
    QSignalBlocker spin_blocker(warp_bend_spin_);
    const auto index = warp_style_combo_->findData(canvas_->warp_style_preset());
    if (index >= 0) {
      warp_style_combo_->setCurrentIndex(index);
    }
    if (canvas_->warp_style_preset() != QStringLiteral("warpCustom")) {
      warp_bend_spin_->setValue(canvas_->warp_style_preset_value());
    }
  }
  refresh_vector_tool_options_visibility();
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
  if (pattern_stamp_pattern_combo_ != nullptr) {
    const QSignalBlocker blocker(pattern_stamp_pattern_combo_);
    pattern_stamp_pattern_combo_->setCurrentIndex(
        pattern_stamp_pattern_combo_->findData(current_pattern_stamp_pattern_id_));
  }
  if (pattern_stamp_aligned_check_ != nullptr) {
    const QSignalBlocker blocker(pattern_stamp_aligned_check_);
    pattern_stamp_aligned_check_->setChecked(current_pattern_stamp_aligned_);
  }
  if (auto* healing_diffusion = findChild<QSpinBox*>(QStringLiteral("healingDiffusionSpin"));
      healing_diffusion != nullptr) {
    QSignalBlocker blocker(healing_diffusion);
    healing_diffusion->setValue(current_healing_diffusion_);
  }
  if (local_adjustment_strength_spin_ != nullptr) {
    QSignalBlocker blocker(local_adjustment_strength_spin_);
    local_adjustment_strength_spin_->setValue(current_local_adjustment_strength_);
  }
  if (local_tone_range_combo_ != nullptr) {
    const auto index = local_tone_range_combo_->findData(static_cast<int>(current_local_tone_range_));
    QSignalBlocker blocker(local_tone_range_combo_);
    local_tone_range_combo_->setCurrentIndex(std::max(0, index));
  }
  if (local_protect_tones_check_ != nullptr) {
    QSignalBlocker blocker(local_protect_tones_check_);
    local_protect_tones_check_->setChecked(current_local_protect_tones_);
  }
  if (sponge_mode_combo_ != nullptr) {
    const auto index = sponge_mode_combo_->findData(static_cast<int>(current_sponge_mode_));
    QSignalBlocker blocker(sponge_mode_combo_);
    sponge_mode_combo_->setCurrentIndex(std::max(0, index));
  }
  if (sponge_vibrance_check_ != nullptr) {
    QSignalBlocker blocker(sponge_vibrance_check_);
    sponge_vibrance_check_->setChecked(current_sponge_vibrance_);
  }
  if (wand_contiguous_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(wand_contiguous_check_);
    wand_contiguous_check_->setChecked(canvas_->wand_contiguous());
  }
  if (wand_sample_all_layers_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(wand_sample_all_layers_check_);
    wand_sample_all_layers_check_->setChecked(canvas_->wand_sample_all_layers());
  }
  if (quick_select_sample_all_layers_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(quick_select_sample_all_layers_check_);
    quick_select_sample_all_layers_check_->setChecked(canvas_->quick_select_sample_all_layers());
  }
  if (quick_select_enhance_edge_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(quick_select_enhance_edge_check_);
    quick_select_enhance_edge_check_->setChecked(canvas_->quick_select_enhance_edge());
  }
  if (canvas_ != nullptr) {
    if (auto* spin = findChild<QSpinBox*>(QStringLiteral("quickSelectSizeSpin")); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(canvas_->quick_select_size());
    }
    if (auto* slider = findChild<QSlider*>(QStringLiteral("quickSelectSizeSlider")); slider != nullptr) {
      QSignalBlocker blocker(slider);
      slider->setValue(canvas_->quick_select_size());
    }
    if (auto* spin = findChild<QSpinBox*>(QStringLiteral("magneticLassoWidthSpin")); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(canvas_->magnetic_lasso_width());
    }
    if (auto* spin = findChild<QSpinBox*>(QStringLiteral("magneticLassoContrastSpin")); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(canvas_->magnetic_lasso_edge_contrast());
    }
    if (auto* spin = findChild<QSpinBox*>(QStringLiteral("magneticLassoFrequencySpin")); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(canvas_->magnetic_lasso_frequency());
    }
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
      CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
      CanvasTool::MagicWand, CanvasTool::QuickSelect};
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
  if (auto* brush_flow = findChild<QSpinBox*>(QStringLiteral("brushFlowSpin")); brush_flow != nullptr) {
    QSignalBlocker blocker(brush_flow);
    brush_flow->setValue(canvas_->brush_flow());
  }
  if (auto* brush_airbrush = findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
      brush_airbrush != nullptr) {
    QSignalBlocker blocker(brush_airbrush);
    brush_airbrush->setChecked(canvas_->brush_build_up());
  }
  const auto sync_mixer_spin = [this](const char* object_name, int value) {
    if (auto* spin = findChild<QSpinBox*>(QString::fromLatin1(object_name)); spin != nullptr) {
      QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
  };
  sync_mixer_spin("mixerWetSpin", current_mixer_wet_);
  sync_mixer_spin("mixerLoadSpin", current_mixer_load_);
  sync_mixer_spin("mixerMixSpin", current_mixer_mix_);
  sync_mixer_spin("mixerFlowSpin", current_mixer_flow_);
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

}  // namespace patchy::ui
