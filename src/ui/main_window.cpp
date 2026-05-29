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
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
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
#include <QInputDialog>
#include <QItemSelection>
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
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPointer>
#include <QProgressDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
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

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

namespace patchy::ui {

namespace {

constexpr auto kTranslationContextProperty = "patchy.translationContext";
constexpr auto kTranslationTextProperty = "patchy.translationText";
constexpr auto kTranslationToolTipProperty = "patchy.translationToolTip";
constexpr auto kTranslationStatusTipProperty = "patchy.translationStatusTip";
constexpr auto kMainWindowTranslationContext = "patchy::ui::MainWindow";
constexpr int kLayerRowBaseIndent = 8;
constexpr int kLayerFolderDisclosureWidth = 18;
constexpr int kLayerFolderDisclosureBorderWidth = 2;
constexpr int kLayerFolderDisclosureHeight = 20;
constexpr int kLayerRowHorizontalSpacing = 10;
constexpr int kLayerChildIndent = kLayerFolderDisclosureWidth + kLayerFolderDisclosureBorderWidth +
                                  kLayerRowHorizontalSpacing;

QString default_startup_brush_preset_id() {
  return QStringLiteral("ink");
}

void apply_brush_preset(CanvasWidget& canvas, const BrushPreset& preset) {
  canvas.set_brush_build_up(preset.build_up);
  canvas.set_brush_size(preset.size);
  canvas.set_brush_opacity(preset.opacity);
  canvas.set_brush_softness(preset.softness);
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

bool window_resize_hit_targets_scroll_bar(QWidget* window, QPoint global_position) {
  if (window == nullptr) {
    return false;
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
                                    int expanded_maximum_height = QWIDGETSIZE_MAX) {
  constexpr int kRightDockMinimumWidth = 280;
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
  toggle->setChecked(true);
  toggle->setText(QStringLiteral("v"));
  toggle->setFixedSize(18, 18);
  toggle->setToolTip(QObject::tr("Collapse panel"));
  layout->addWidget(toggle);

  auto* label = new QLabel(dock->windowTitle(), title);
  label->setObjectName(object_prefix + QStringLiteral("DockTitleLabel"));
  if (dock->property(kTranslationTextProperty).isValid()) {
    label->setProperty(kTranslationContextProperty, dock->property(kTranslationContextProperty));
    label->setProperty(kTranslationTextProperty, dock->property(kTranslationTextProperty));
    apply_bound_translation(label);
  }
  layout->addWidget(label, 1);

  QObject::connect(toggle, &QToolButton::toggled, dock,
                   [dock, content, toggle, expanded_minimum_height, expanded_maximum_height](bool expanded) {
    content->setVisible(expanded);
    toggle->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
    toggle->setToolTip(expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
    const auto collapsed_height = dock->titleBarWidget()->sizeHint().height() + 8;
    if (expanded_minimum_height > 0) {
      dock->setMinimumHeight(expanded ? expanded_minimum_height : collapsed_height);
    }
    dock->setMaximumHeight(expanded ? expanded_maximum_height : collapsed_height);
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
  if (!style.outer_glows.empty()) {
    effects << QObject::tr("Outer Glow");
  }
  if (!style.color_overlays.empty()) {
    effects << QObject::tr("Color Overlay");
  }
  if (!style.gradient_fills.empty()) {
    effects << QObject::tr("Gradient Fill");
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
      return QObject::tr("Levels: black %1, white %2, gamma %3%")
          .arg(settings->levels.black_input)
          .arg(settings->levels.white_input)
          .arg(settings->levels.gamma_percent);
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

QString text_layer_summary(const Layer& layer) {
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
  const auto size = value(kLayerMetadataTextSize, QObject::tr("?"));
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
  return QObject::tr("%1\nFont: %2, %3 px%4\nColor: %5\nFlow: %6\n%7")
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
    pixmap.fill(QColor(35, 38, 44));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath shadow;
    shadow.moveTo(5.0, 11.0);
    shadow.lineTo(11.6, 11.0);
    shadow.lineTo(13.9, 8.0);
    shadow.lineTo(23.8, 8.0);
    shadow.quadTo(25.0, 8.0, 25.0, 9.2);
    shadow.lineTo(25.0, 24.0);
    shadow.lineTo(5.0, 24.0);
    shadow.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 70));
    painter.drawPath(shadow.translated(1.0, 1.0));

    QPainterPath back_outline;
    back_outline.moveTo(4.5, 22.8);
    back_outline.lineTo(4.5, 10.0);
    back_outline.quadTo(4.5, 8.8, 5.7, 8.8);
    back_outline.lineTo(11.3, 8.8);
    back_outline.lineTo(13.6, 6.0);
    back_outline.lineTo(22.8, 6.0);
    back_outline.quadTo(24.2, 6.0, 24.2, 7.4);
    back_outline.lineTo(24.2, 22.8);
    back_outline.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(245, 205, 105));
    painter.drawPath(back_outline);

    QPainterPath back_inner;
    back_inner.moveTo(6.2, 21.2);
    back_inner.lineTo(6.2, 10.8);
    back_inner.lineTo(12.0, 10.8);
    back_inner.lineTo(14.3, 7.9);
    back_inner.lineTo(22.3, 7.9);
    back_inner.lineTo(22.3, 21.2);
    back_inner.closeSubpath();
    painter.setBrush(QColor(58, 51, 32));
    painter.drawPath(back_inner);

    QPainterPath front_outline;
    front_outline.moveTo(4.2, 13.8);
    front_outline.lineTo(24.8, 13.8);
    front_outline.lineTo(22.7, 22.9);
    front_outline.quadTo(22.4, 24.0, 21.2, 24.0);
    front_outline.lineTo(5.6, 24.0);
    front_outline.quadTo(4.6, 24.0, 4.4, 23.0);
    front_outline.closeSubpath();
    painter.setBrush(QColor(245, 205, 105));
    painter.drawPath(front_outline);

    QPainterPath front;
    front.moveTo(6.0, 15.6);
    front.lineTo(22.6, 15.6);
    front.lineTo(21.0, 22.0);
    front.lineTo(6.2, 22.0);
    front.closeSubpath();
    QLinearGradient front_gradient(QPointF(4.0, 13.8), QPointF(24.0, 24.0));
    front_gradient.setColorAt(0.0, QColor(89, 67, 28));
    front_gradient.setColorAt(1.0, QColor(52, 43, 27));
    painter.setBrush(front_gradient);
    painter.drawPath(front);

    painter.fillRect(QRectF(6.1, 14.8, 17.0, 1.0), QColor(255, 235, 145));
    painter.fillRect(QRectF(6.0, 13.0, 17.6, 1.0), QColor(130, 88, 25));
    painter.setPen(QPen(QColor(150, 158, 168), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(0, 0, kSize - 1, kSize - 1));
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
                               std::function<void(LayerId)> toggle_group_expanded = {},
                               std::function<void(LayerId, bool)> set_mask_linked = {},
                               bool content_target_active = false, bool mask_target_active = false) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("layerRowWidget"));
  row->setAttribute(Qt::WA_StyledBackground, true);
  auto* list_parent = dynamic_cast<LayerListWidget*>(parent);
  if (list_parent != nullptr) {
    row->installEventFilter(list_parent);
  }
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(kLayerRowBaseIndent + std::max(0, depth) * kLayerChildIndent, 5, 8, 5);
  layout->setSpacing(kLayerRowHorizontalSpacing);

  if (layer.kind() == LayerKind::Group) {
    auto* disclosure = new QToolButton(row);
    disclosure->setObjectName(QStringLiteral("layerFolderDisclosureButton"));
    disclosure->setCheckable(true);
    disclosure->setChecked(group_expanded);
    disclosure->setText(group_expanded ? QStringLiteral("v") : QStringLiteral(">"));
    disclosure->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
  }
  auto* thumbnail = new QLabel(row);
  thumbnail->setObjectName(QStringLiteral("layerContentThumbnail"));
  thumbnail->setFixedSize(30, 30);
  thumbnail->setPixmap(layer_content_thumbnail(layer));
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
  QString dimensions;
  if (layer_is_text(layer)) {
    dimensions = QObject::tr("text layer");
  } else if (layer.kind() == LayerKind::Pixel) {
    dimensions = QObject::tr("%1 x %2").arg(layer.bounds().width).arg(layer.bounds().height);
  } else if (layer.kind() == LayerKind::Adjustment) {
    dimensions = adjustment_layer_detail(layer);
  } else {
    dimensions = QObject::tr("folder, %1 layers").arg(layer_descendant_count(layer));
  }
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
  bool boxed{false};
  int box_width{0};
  int box_height{0};
};

constexpr auto kTextEditorFinishedProperty = "patchy.textEditorFinished";
constexpr auto kTextEditorPreviewPaintProperty = "patchy.previewPaintsText";
constexpr auto kTextFlowPoint = "point";
constexpr auto kTextFlowBox = "box";
constexpr int kMinimumTextBoxDocumentSize = 16;

QString text_flow_metadata_value(bool boxed) {
  return boxed ? QString::fromLatin1(kTextFlowBox) : QString::fromLatin1(kTextFlowPoint);
}

bool text_flow_is_box(const QString& value) {
  return value.compare(QString::fromLatin1(kTextFlowBox), Qt::CaseInsensitive) == 0;
}

bool layer_requires_text_editor_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || layer.blend_mode() != BlendMode::Normal ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty());
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

class InlineTextEdit final : public QTextEdit {
public:
  explicit InlineTextEdit(QWidget* parent = nullptr) : QTextEdit(parent) {}

protected:
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

    QPainter painter(viewport());
    painter.setClipRect(event->rect());
    paint_selection_highlight(painter);
    if (hasFocus()) {
      auto caret = cursorRect();
      caret.setWidth(std::max(1, cursorWidth()));
      painter.fillRect(caret.intersected(viewport()->rect()), palette().color(QPalette::Text));
    }
  }

private:
  void paint_selection_highlight(QPainter& painter) const {
    const auto selection = textCursor();
    if (!selection.hasSelection()) {
      return;
    }

    QColor highlight = palette().color(QPalette::Highlight);
    highlight.setAlpha(120);
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
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_pixel_size = format_font.pixelSize();
      const auto before_point_size = format_font.pointSizeF();
      scale_font_size(format_font, scale);
      if (format_font.pixelSize() == before_pixel_size &&
          std::abs(format_font.pointSizeF() - before_point_size) < 0.0001) {
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

std::unique_ptr<QTextDocument> document_from_editor_in_document_units(const QTextEdit& editor, double zoom) {
  auto document = std::make_unique<QTextDocument>();
  document->setDocumentMargin(0);
  document->setDefaultFont(editor.document()->defaultFont());
  document->setDefaultTextOption(editor.document()->defaultTextOption());
  document->setHtml(editor.document()->toHtml());
  scale_document_font_sizes(*document, zoom > 0.0 ? 1.0 / zoom : 1.0);
  return document;
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
  const auto fallback_family = fallback.family.isEmpty() ? QApplication::font().family() : fallback.family;
  const auto fallback_size = std::max(1, fallback.size);
  const auto fallback_color_name = (fallback_color.isValid() ? fallback_color : QColor(Qt::black)).name(QColor::HexRgb);

  const auto append_run = [&lines, &fallback_family, fallback_size, &fallback_color_name](int start, int length,
                                                                                          const QTextCharFormat& format) {
    if (length <= 0) {
      return;
    }
    auto format_font = format.font();
    auto family = format_font.family();
    if (family.isEmpty()) {
      family = fallback_family;
    }
    int size = format_font.pixelSize();
    if (size <= 0) {
      size = format_font.pointSizeF() > 0.0 ? static_cast<int>(std::round(format_font.pointSizeF())) : fallback_size;
    }
    QColor color = format.foreground().color();
    if (!color.isValid()) {
      color = QColor(fallback_color_name);
    }
    const auto encoded_family = QString::fromLatin1(family.toUtf8().toPercentEncoding());
    lines << QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                 .arg(start)
                 .arg(length)
                 .arg(std::max(1, size))
                 .arg(format_font.weight() >= QFont::Bold ? 1 : 0)
                 .arg(format_font.italic() ? 1 : 0)
                 .arg(color.name(QColor::HexRgb))
                 .arg(encoded_family);
  };

  bool found_fragment = false;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      found_fragment = true;
      append_run(fragment.position(), fragment.length(), fragment.charFormat());
    }
  }

  if (!found_fragment) {
    QTextCharFormat format;
    QFont font(fallback_family);
    font.setPixelSize(fallback_size);
    font.setBold(fallback.bold);
    font.setItalic(fallback.italic);
    format.setFont(font);
    format.setForeground(QBrush(QColor(fallback_color_name)));
    append_run(0, document.toPlainText().size(), format);
  }
  return lines.join(QLatin1Char('\n'));
}

QString paragraph_runs_from_document(const QTextDocument& document) {
  QStringList lines;
  lines << QStringLiteral("v1");
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto length = std::max(0, std::min(block.length(), std::max(0, plain_length - start)));
    lines << QStringLiteral("%1\t%2\t%3")
                 .arg(start)
                 .arg(length)
                 .arg(paragraph_alignment_name(block.blockFormat().alignment()));
  }
  return lines.join(QLatin1Char('\n'));
}

void apply_paragraph_runs_to_document(QTextDocument& document, const QString& paragraph_runs) {
  const auto lines = paragraph_runs.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1")) {
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
    cursor.mergeBlockFormat(format);
  }
}

void apply_plain_text_format(QTextDocument& document, const QFont& font, QColor color) {
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(font);
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);
}

QTextCharFormat text_editor_typing_format(const QFont& font, QColor color) {
  QTextCharFormat format;
  format.setFont(font);
  format.setForeground(QBrush(color.isValid() ? color : QColor(Qt::black)));
  return format;
}

void preserve_empty_text_editor_typing_format(QTextEdit& editor, const QFont& font, QColor color) {
  if (!editor.toPlainText().isEmpty()) {
    return;
  }
  editor.document()->setDefaultFont(font);
  editor.setCurrentCharFormat(text_editor_typing_format(font, color));
}

PixelBuffer render_text_pixels(const TextToolSettings& settings, QColor color, std::int32_t max_width,
                               const QString& paragraph_runs = QString()) {
  QFont font(settings.family);
  font.setPixelSize(std::max(1, settings.size));
  font.setBold(settings.bold);
  font.setItalic(settings.italic);

  const auto text_width = settings.boxed ? std::max(kMinimumTextBoxDocumentSize, settings.box_width)
                                         : std::max(64, max_width);
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(font);
  QTextOption option;
  option.setWrapMode(settings.boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
  document.setDefaultTextOption(option);
  if (!settings.html.trimmed().isEmpty()) {
    document.setHtml(settings.html);
    document.setDocumentMargin(0);
    document.setDefaultTextOption(option);
  } else {
    document.setPlainText(settings.text);
    apply_plain_text_format(document, font, color);
  }
  document.setTextWidth(text_width);
  if (!paragraph_runs.trimmed().isEmpty()) {
    apply_paragraph_runs_to_document(document, paragraph_runs);
  }

  const auto size = document.size();
  const auto image_width = settings.boxed ? text_width : std::max(1, static_cast<int>(std::ceil(size.width())) + 2);
  const auto image_height = settings.boxed ? std::max(kMinimumTextBoxDocumentSize, settings.box_height)
                                           : std::max(1, static_cast<int>(std::ceil(size.height())) + 2);
  QImage image(image_width, image_height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  document.drawContents(&painter, QRectF(0, 0, image.width(), image.height()));
  painter.end();
  return pixels_from_image_rgba(image);
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

std::optional<QRectF> psd_text_frame_rect(const Layer& layer) {
  const auto& metadata = layer.metadata();
  const auto transform_found = metadata.find(kLayerMetadataPsdTextTransform);
  const auto box_found = metadata.find(kLayerMetadataPsdTextBoxBounds);
  if (transform_found == metadata.end() || box_found == metadata.end()) {
    return std::nullopt;
  }
  const auto transform = parse_space_separated_doubles(transform_found->second);
  const auto box = parse_space_separated_doubles(box_found->second);
  if (transform.size() < 6U || box.size() < 4U) {
    return std::nullopt;
  }

  const auto map_point = [&transform](double x, double y) {
    return QPointF(transform[0] * x + transform[2] * y + transform[4],
                   transform[1] * x + transform[3] * y + transform[5]);
  };
  const std::array<QPointF, 4> points = {
      map_point(box[0], box[1]),
      map_point(box[2], box[1]),
      map_point(box[2], box[3]),
      map_point(box[0], box[3]),
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

std::optional<PixelBuffer> render_text_layer_pixels_from_metadata(const Layer& layer) {
  const auto& metadata = layer.metadata();
  const auto text = [&metadata] {
    const auto found = metadata.find(kLayerMetadataText);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second).trimmed();
  }();
  if (text.isEmpty()) {
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
  auto family = value(kLayerMetadataTextFont).value_or(QApplication::font().family());
  if (family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    family = QApplication::font().family();
  }
  const auto size = value(kLayerMetadataTextSize).has_value()
                        ? std::max(1, std::atoi(value(kLayerMetadataTextSize)->toUtf8().constData()))
                        : 36;
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
                            boxed,
                            box_width,
                            box_height};
  return render_text_pixels(settings, color.isValid() ? color : QColor(Qt::black),
                            layer.bounds().width > 0 ? layer.bounds().width : 320, paragraph_runs);
}

void clear_layer_text_metadata(Layer& layer) {
  static constexpr std::array<const char*, 19> kTextMetadataKeys = {
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
      kLayerMetadataTextRasterStatus,
      kLayerMetadataPsdTextTransform,
      kLayerMetadataPsdTextBounds,
      kLayerMetadataPsdTextBoundingBox,
      kLayerMetadataPsdTextBoxBounds,
      kLayerMetadataPsdTextTailBounds,
      kLayerMetadataPsdTextIndex,
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
  layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  clear_layer_psd_text_source(layer);
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
  LocalizationManager::instance().load_saved_language();
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
  setCentralWidget(document_tabs_);
  connect(document_tabs_, &QTabWidget::currentChanged, this, [this](int index) { activate_document_tab(index); });
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_document_tab(index); });
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
          [this] { clear_internal_clipboard_on_external_change(); });
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
  load_bundled_legacy_plugins();
  create_docks();
  refresh_layer_list();
  refresh_layer_controls();
  update_document_action_state();
  update_file_path_actions();
  update_undo_redo_actions();
  qApp->installEventFilter(this);

  setWindowTitle(QStringLiteral("Patchy"));
  setWindowIcon(patchy_app_icon());
  resize(1280, 860);
  setStyleSheet(photoshop_style());
  ensure_native_resizable_frame();
  statusBar()->showMessage(tr("Ready"));
}

bool MainWindow::handle_layer_action_button_drag_event(QObject* watched, QEvent* event) {
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
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

  if (handle_layer_action_button_drag_event(watched, event)) {
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

  if (watched == canvas_ && canvas_ != nullptr) {
    auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool()) {
      switch (event->type()) {
        case QEvent::MouseButtonPress: {
          auto* mouse_event = static_cast<QMouseEvent*>(event);
          if (mouse_event->button() == Qt::LeftButton) {
            if (auto* handle = text_editor_resize_handle_at(mouse_event->pos()); handle != nullptr) {
              return handle_text_editor_resize_event(handle, editor, event);
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

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
  if (message != nullptr && result != nullptr && !isMaximized() && !isFullScreen()) {
    auto* native_message = static_cast<MSG*>(message);
    if (native_message->message == WM_NCCALCSIZE && native_message->wParam != FALSE) {
      *result = 0;
      return true;
    }
    if (native_message->message == WM_NCHITTEST) {
      RECT window_rect;
      if (GetWindowRect(reinterpret_cast<HWND>(winId()), &window_rect) != 0) {
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
  QMainWindow::changeEvent(event);
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

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  ensure_native_resizable_frame();
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
  native_resizable_frame_applied_ = true;
#else
  native_resizable_frame_applied_ = true;
#endif
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
  const auto actions = findChildren<QAction*>();
  for (auto* action : actions) {
    refresh_action_tooltip(action);
  }
  rebuild_recent_files_menu();
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
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  auto* export_flat_action = file_menu->addAction(tr("Export &Flat Image..."));
  auto* page_setup_action = file_menu->addAction(tr("Page Set&up..."));
  auto* print_action = file_menu->addAction(tr("&Print..."));
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
  preferences_action->setObjectName(QStringLiteral("filePreferencesAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  page_setup_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  print_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
  preferences_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  apply_action_shortcut(new_action, QKeySequence(Qt::CTRL | Qt::Key_N));
  apply_action_shortcut(open_action, QKeySequence(Qt::CTRL | Qt::Key_O));
  apply_action_shortcut(save_action, QKeySequence(Qt::CTRL | Qt::Key_S));
  apply_action_shortcut(save_as_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  apply_action_shortcut(print_action, QKeySequence(Qt::CTRL | Qt::Key_P));
  apply_action_shortcut(quit_action, QKeySequence(Qt::CTRL | Qt::Key_Q));

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(page_setup_action, &QAction::triggered, this, [this] { page_setup(); });
  connect(print_action, &QAction::triggered, this, [this] { print_document(); });
  connect(preferences_action, &QAction::triggered, this, [this] { show_preferences(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);
  for (auto* action : {save_action, save_as_action, export_flat_action, page_setup_action, print_action}) {
    register_document_action(action);
  }

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
  paste_action->setIcon(simple_icon(QStringLiteral("paste")));
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
  auto* add_mask_action = layer_menu->addAction(tr("Add Layer &Mask from Selection"));
  delete_layer_mask_action_ = layer_menu->addAction(tr("&Delete Layer Mask"));
  link_layer_mask_action_ = layer_menu->addAction(tr("Link Layer &Mask"));
  disable_layer_mask_action_ = layer_menu->addAction(tr("&Disable Layer Mask"));
  invert_layer_mask_action_ = layer_menu->addAction(tr("&Invert Layer Mask"));
  apply_layer_mask_action_ = layer_menu->addAction(tr("&Apply Layer Mask"));
  layer_menu->addSeparator();
  auto* edit_adjustment_action = layer_menu->addAction(tr("&Edit Adjustment..."));
  layer_blending_options_action_ = layer_menu->addAction(tr("&Blending Options..."));
  layer_copy_style_action_ = new QAction(tr("Copy Layer Style"), this);
  layer_paste_style_action_ = new QAction(tr("Paste Layer Style"), this);
  layer_delete_style_action_ = new QAction(tr("Delete Layer Style"), this);
  layer_rasterize_action_ = new QAction(tr("Rasterize"), this);
  layer_rasterize_layer_style_action_ = new QAction(tr("Rasterize (including layer style)"), this);
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
  delete_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)));
  link_layer_mask_action_->setIcon(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)));
  disable_layer_mask_action_->setIcon(simple_icon(QStringLiteral("off"), QColor(255, 190, 120)));
  invert_layer_mask_action_->setIcon(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)));
  apply_layer_mask_action_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  link_layer_mask_action_->setCheckable(true);
  disable_layer_mask_action_->setCheckable(true);
  edit_adjustment_action->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_copy_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_paste_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_delete_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(255, 150, 150)));
  layer_rasterize_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  layer_rasterize_layer_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
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
  for (auto* action : {add_layer_action, add_folder_action, new_adjustment_layer_menu->menuAction(),
                       layer_via_copy_action, layer_via_cut_action, add_mask_action, duplicate_layer_action,
                       merge_visible_action, merge_selected_action, rename_layer_action, delete_layer_action,
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
    if (!shortcut.isEmpty()) {
      apply_action_shortcut(action, shortcut);
    }
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
    return action;
  };
  add_adjustment_action(tr("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("patchy.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  apply_action_shortcut(levels_action, QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  register_document_action(levels_action);
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  apply_action_shortcut(curves_action, QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  register_document_action(curves_action);
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  apply_action_shortcut(hue_saturation_action, QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  register_document_action(hue_saturation_action);
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  apply_action_shortcut(color_balance_action, QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  register_document_action(color_balance_action);
  add_adjustment_action(tr("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("patchy.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(tr("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("patchy.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  adjustments_menu->addSeparator();
  add_adjustment_action(tr("&Brightness..."), QStringLiteral("imageAdjustBrightnessAction"),
                        QStringLiteral("patchy.filters.brightness_plus"));
  add_adjustment_action(tr("&Contrast..."), QStringLiteral("imageAdjustContrastAction"),
                        QStringLiteral("patchy.filters.contrast_plus"));
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
  for (auto* action : {adjustments_menu->menuAction(), image_size_action, canvas_size_action, crop_action,
                       rotate_cw_action, rotate_ccw_action}) {
    register_document_action(action);
  }

  for (const auto& filter : filters_.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    if (is_adjustment_only_filter(identifier)) {
      continue;
    }
    const auto display_name = filter_display_name(filter);
    auto* action = filter_menu->addAction(display_name);
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
  zoom_in->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  zoom_out->setShortcut(QKeySequence::ZoomOut);
  zoom_in->setShortcutContext(Qt::ApplicationShortcut);
  zoom_out->setShortcutContext(Qt::ApplicationShortcut);
  refresh_action_tooltip(zoom_in);
  refresh_action_tooltip(zoom_out);
  apply_action_shortcut(fit_on_screen, QKeySequence(Qt::CTRL | Qt::Key_0));
  apply_action_shortcut(zoom_reset, QKeySequence(Qt::CTRL | Qt::Key_1));
  apply_action_shortcut(selection_edges_action, QKeySequence(Qt::CTRL | Qt::Key_H));
  apply_action_shortcut(view_rulers_action_, QKeySequence(Qt::CTRL | Qt::Key_R));
  apply_action_shortcut(view_grid_action_, QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  apply_action_shortcut(view_guides_action_, QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  apply_action_shortcut(view_snap_action_, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  apply_action_shortcut(view_lock_guides_action_, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
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
    apply_action_shortcut(action, shortcut);
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
    apply_action_shortcut(action, shortcut);
    tool_group->addAction(action);
    shape_menu->addAction(action);
    addAction(action);
    register_document_action(action);
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
    if (canvas_ == nullptr) {
      return;
    }
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
    refresh_document_info();
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
          [this](const QFont&) { apply_text_options_to_active_editor(); });
  connect(text_size_spin_, &QSpinBox::valueChanged, this,
          [this](int) {
    apply_text_options_to_active_editor();
    refresh_document_info();
  });
  connect(text_bold_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });
  connect(text_italic_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });
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
      {recent_files_menu_->menuAction(), "Open &Recent"},
      {save_action, "&Save"},
      {save_as_action, "Save &As..."},
      {export_flat_action, "Export &Flat Image..."},
      {page_setup_action, "Page Set&up..."},
      {print_action, "&Print..."},
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
      {add_mask_action, "Add Layer &Mask from Selection"},
      {delete_layer_mask_action_, "&Delete Layer Mask"},
      {link_layer_mask_action_, "Link Layer &Mask"},
      {disable_layer_mask_action_, "&Disable Layer Mask"},
      {invert_layer_mask_action_, "&Invert Layer Mask"},
      {apply_layer_mask_action_, "&Apply Layer Mask"},
      {edit_adjustment_action, "&Edit Adjustment..."},
      {layer_blending_options_action_, "&Blending Options..."},
      {layer_copy_style_action_, "Copy Layer Style"},
      {layer_paste_style_action_, "Paste Layer Style"},
      {layer_delete_style_action_, "Delete Layer Style"},
      {layer_rasterize_action_, "Rasterize"},
      {layer_rasterize_layer_style_action_, "Rasterize (including layer style)"},
      {duplicate_layer_action, "&Duplicate Layer"},
      {merge_visible_action, "Merge &Visible to New Layer"},
      {merge_selected_action, "&Merge Selected to New Layer"},
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
  layer_list->set_thumbnail_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    auto* layer = document().find_layer(id);
    if (layer == nullptr) {
      return;
    }
    document().set_active_layer(id);
    canvas_->set_layer_edit_target(target == LayerCtrlClickTarget::MaskThumbnail
                                       ? CanvasWidget::LayerEditTarget::Mask
                                       : CanvasWidget::LayerEditTarget::Content);
    update_layer_target_styles(layer_list_, document().active_layer_id(), canvas_->layer_edit_target());
    restyle_layer_rows(layer_list_);
    refresh_layer_controls();
    statusBar()->showMessage(target == LayerCtrlClickTarget::MaskThumbnail ? tr("Editing layer mask")
                                                                           : tr("Editing layer pixels"));
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(250);
  layer_list_->setMinimumHeight(120);
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
  register_document_widget(opacity_slider_);
  register_document_widget(opacity_spin_);
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

  lock_transparency_check_ = new QCheckBox(tr("Lock transparent pixels"), layers_panel);
  lock_transparency_check_->setObjectName(QStringLiteral("layerLockTransparencyCheck"));
  lock_transparency_check_->setToolTip(tr("Preserve existing transparency while painting, filling, or erasing"));
  layers_layout->addWidget(lock_transparency_check_);
  connect(lock_transparency_check_, &QCheckBox::toggled, this,
          [this](bool checked) { set_active_layer_lock_transparency(checked); });
  register_document_widget(lock_transparency_check_);

  layers_dock->setWidget(layers_panel);
  install_collapsible_dock_title(layers_dock, layers_panel, QStringLiteral("layers"), 300);
  addDockWidget(Qt::RightDockWidgetArea, layers_dock);

  auto* history_dock = new QDockWidget(tr("History"), this);
  history_dock->setObjectName(QStringLiteral("historyDock"));
  bind_widget_text(history_dock, "History");
  history_list_ = new QListWidget(history_dock);
  history_list_->setObjectName(QStringLiteral("historyList"));
  history_dock->setWidget(history_list_);
  install_collapsible_dock_title(history_dock, history_list_, QStringLiteral("history"));
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
  install_collapsible_dock_title(properties_dock, properties_scroll, QStringLiteral("properties"), 0, 230);
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
  apply_canvas_aid_settings(canvas);
  canvas->set_before_edit_callback([this](QString label) { push_undo_snapshot(std::move(label)); });
  canvas->set_color_picked_callback([this, canvas](QColor color) {
    canvas->set_primary_color(color);
    refresh_color_buttons();
    statusBar()->showMessage(tr("Picked color"));
  });
  canvas->set_text_requested_callback([this](QPoint point, QRect requested_text_box) {
    add_text_at(point, requested_text_box);
  });
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
  canvas->set_view_changed_callback([this, canvas] { handle_canvas_view_changed(canvas); });
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
  } else if (const auto* preset = find_brush_preset(default_startup_brush_preset_id()); preset != nullptr) {
    apply_brush_preset(*session->canvas, *preset);
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
  apply_canvas_aid_settings(session->canvas);

  auto* canvas = session->canvas;
  const auto tab_title = session->title;
  sessions_.push_back(std::move(session));
  const auto tab_index = document_tabs_->addTab(canvas, tab_title);
  document_tabs_->setCurrentIndex(tab_index);
  canvas_ = canvas;
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
  auto* canvas = index >= 0 ? dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)) : nullptr;
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
    return;
  }
  canvas_ = canvas;
  canvas_->set_tool(current_tool_);
  canvas_->set_selection_mode(current_selection_mode_);
  canvas_->set_marquee_style(current_marquee_style_);
  canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  canvas_->set_selection_feather_radius(current_selection_feather_radius_);
  canvas_->set_selection_antialias(current_selection_antialias_);
  apply_canvas_aid_settings(canvas_);
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_file_path_actions();
  update_undo_redo_actions();
  update_document_action_state();
}

void MainWindow::close_document_tab(int index) {
  if (index < 0 || index >= document_tabs_->count()) {
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
  update_document_action_state();
  if (sessions_.empty() && statusBar() != nullptr) {
    statusBar()->showMessage(tr("No document"));
  }
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
  const auto path = get_open_file_name(this, tr("Open"), default_file_dialog_directory(), open_file_filter(), nullptr,
                                       QStringLiteral("openFileDialog"));
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
                     tr("Patchy %1 is available. You are using version %2.")
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
  auto* application_group = new QGroupBox(tr("Application"), &dialog);
  application_group->setObjectName(QStringLiteral("preferencesApplicationGroup"));
  auto* application_form = new QFormLayout(application_group);
  application_form->setContentsMargins(10, 10, 10, 10);
  application_form->setSpacing(8);

  auto* language_combo = new QComboBox(application_group);
  language_combo->setObjectName(QStringLiteral("preferencesLanguageCombo"));
  language_combo->addItem(tr("English"), QStringLiteral("en"));
  language_combo->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
  const auto current_language = LocalizationManager::instance().current_language();
  const auto current_index = language_combo->findData(current_language);
  language_combo->setCurrentIndex(current_index >= 0 ? current_index : 0);
  application_form->addRow(tr("Language:"), language_combo);
  auto* update_check = new QCheckBox(tr("Check for updates on startup"), application_group);
  update_check->setObjectName(QStringLiteral("preferencesCheckForUpdatesCheck"));
  update_check->setChecked(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
  application_form->addRow(update_check);
  content->addWidget(application_group);

  connect(language_combo, &QComboBox::currentIndexChanged, &dialog, [this, language_combo] {
    const auto code = language_combo->currentData().toString();
    if (!code.isEmpty() && LocalizationManager::instance().set_language(code)) {
      refresh_language_actions();
    }
  });

  auto* view_group = new QGroupBox(tr("Grids, Rulers, Guides, and Snapping"), &dialog);
  view_group->setObjectName(QStringLiteral("preferencesCanvasAidsGroup"));
  auto* view_form = new QFormLayout(view_group);
  view_form->setContentsMargins(10, 10, 10, 10);
  view_form->setSpacing(8);

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
    button->setText(color.name(QColor::HexRgb).toUpper());
    button->setStyleSheet(color_button_style(color));
  };
  refresh_color_choice_button(grid_color_button, selected_grid_color);
  refresh_color_choice_button(guide_color_button, selected_guide_color);
  connect(grid_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_grid_color, &dialog, tr("Grid Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_grid_color = color;
      refresh_color_choice_button(grid_color_button, selected_grid_color);
    }
  });
  connect(guide_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_guide_color, &dialog, tr("Guide Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_guide_color = color;
      refresh_color_choice_button(guide_color_button, selected_guide_color);
    }
  });

  auto* snap_guides_check = new QCheckBox(tr("Guides"), view_group);
  snap_guides_check->setObjectName(QStringLiteral("preferencesSnapGuidesCheck"));
  snap_guides_check->setChecked(view_snap_to_guides_);
  auto* snap_grid_check = new QCheckBox(tr("Grid"), view_group);
  snap_grid_check->setObjectName(QStringLiteral("preferencesSnapGridCheck"));
  snap_grid_check->setChecked(view_snap_to_grid_);
  auto* snap_document_check = new QCheckBox(tr("Document bounds and center"), view_group);
  snap_document_check->setObjectName(QStringLiteral("preferencesSnapDocumentCheck"));
  snap_document_check->setChecked(view_snap_to_document_);
  auto* snap_layers_check = new QCheckBox(tr("Layer bounds and centers"), view_group);
  snap_layers_check->setObjectName(QStringLiteral("preferencesSnapLayersCheck"));
  snap_layers_check->setChecked(view_snap_to_layers_);
  auto* snap_selection_check = new QCheckBox(tr("Selection bounds and center"), view_group);
  snap_selection_check->setObjectName(QStringLiteral("preferencesSnapSelectionCheck"));
  snap_selection_check->setChecked(view_snap_to_selection_);

  auto* visibility_row = new QWidget(view_group);
  auto* visibility_layout = new QHBoxLayout(visibility_row);
  visibility_layout->setContentsMargins(0, 0, 0, 0);
  visibility_layout->addWidget(default_rulers_check);
  visibility_layout->addWidget(default_grid_check);
  visibility_layout->addWidget(default_guides_check);
  visibility_layout->addWidget(lock_guides_check);
  auto* snap_targets_row = new QWidget(view_group);
  auto* snap_targets_layout = new QHBoxLayout(snap_targets_row);
  snap_targets_layout->setContentsMargins(0, 0, 0, 0);
  snap_targets_layout->addWidget(snap_guides_check);
  snap_targets_layout->addWidget(snap_grid_check);
  snap_targets_layout->addWidget(snap_document_check);
  snap_targets_layout->addWidget(snap_layers_check);
  snap_targets_layout->addWidget(snap_selection_check);

  view_form->addRow(tr("Ruler units:"), ruler_units_combo);
  view_form->addRow(tr("Default visibility:"), visibility_row);
  view_form->addRow(tr("Grid spacing:"), grid_spacing_spin);
  view_form->addRow(tr("Grid subdivisions:"), grid_subdivisions_spin);
  view_form->addRow(tr("Grid style:"), grid_style_combo);
  view_form->addRow(tr("Grid color:"), grid_color_button);
  view_form->addRow(tr("Guide color:"), guide_color_button);
  view_form->addRow(tr("Snap:"), snap_check);
  view_form->addRow(tr("Snap targets:"), snap_targets_row);
  content->addWidget(view_group);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  buttons->setObjectName(QStringLiteral("preferencesButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);

  if (exec_dialog(dialog) == QDialog::Accepted) {
    const auto new_grid_spacing_32 =
        std::clamp(static_cast<int>(std::lround(grid_spacing_spin->value() * 32.0)), 1, 320000);
    settings.setValue(QStringLiteral("updates/checkOnStartup"), update_check->isChecked());
    settings.setValue(QStringLiteral("view/rulerUnits"), ruler_units_combo->currentData().toString());
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
    }
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
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_cut.push_back(layer.id());
  }
  if (layers_to_cut.empty()) {
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

void MainWindow::add_text_at(QPoint document_point, QRect requested_text_box) {
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }

  const QPoint requested_document_point = document_point;
  std::optional<LayerId> editing_layer;
  std::optional<bool> editing_layer_was_visible;
  std::optional<LayerId> restore_active_layer = document().active_layer_id();
  bool editing_layer_needs_preview = false;
  QString initial_text;
  QString initial_html;
  QString initial_paragraph_runs;
  QString family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : font().family();
  int document_text_size = text_size_spin_ != nullptr ? text_size_spin_->value() : 48;
  bool text_bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  bool text_italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
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
      editing_layer = *active;
      editing_layer_was_visible = layer->visible();
      initial_text = QString::fromStdString(layer->metadata().at(kLayerMetadataText));
      if (const auto found = layer->metadata().find(kLayerMetadataTextHtml); found != layer->metadata().end()) {
        initial_html = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextParagraphRuns); found != layer->metadata().end()) {
        initial_paragraph_runs = QString::fromStdString(found->second);
      }
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
      boxed_text = text_flow_is_box(
          layer->metadata().contains(kLayerMetadataTextFlow)
              ? QString::fromStdString(layer->metadata().at(kLayerMetadataTextFlow))
              : QString::fromLatin1(kTextFlowPoint));
      const auto psd_frame = psd_text_frame_rect(*layer);
      const bool using_psd_frame = psd_frame.has_value() && boxed_text;
      if (using_psd_frame) {
        document_point = QPoint(static_cast<int>(std::floor(psd_frame->left())),
                                static_cast<int>(std::floor(psd_frame->top())));
        document_editor_width =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->width())));
        document_editor_height =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->height())));
      } else {
        document_point = QPoint(layer->bounds().x, layer->bounds().y);
      }
      if (boxed_text && !using_psd_frame) {
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
        document_editor_width = std::max(160, layer->bounds().width);
        document_editor_height = std::max(64, layer->bounds().height + document_text_size);
      }
      layer->set_visible(false);
      editing_layer_needs_preview = *editing_layer_was_visible && layer_requires_text_editor_preview(*layer);
      canvas_->document_changed(to_qrect(layer_render_bounds(*layer)));
    }
  }

  auto* editor = new InlineTextEdit(canvas_);
  editor->setObjectName(QStringLiteral("inlineTextEditor"));
  editor->setAcceptRichText(true);
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
  editor->setLineWrapMode(boxed_text ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
  editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setProperty("patchy.documentTextSize", document_text_size);
  editor->setProperty("patchy.documentTextWidth", document_editor_width);
  editor->setProperty("patchy.documentTextHeight", document_editor_height);
  editor->setProperty("patchy.documentTextFlow", text_flow_metadata_value(boxed_text));
  editor->setProperty("patchy.editorZoom", canvas_->zoom());
  editor->setProperty("patchy.documentTextColor", text_color);
  editor->setProperty("patchy.documentTextX", document_point.x());
  editor->setProperty("patchy.documentTextY", document_point.y());
  editor->setProperty(kTextEditorPreviewPaintProperty, editing_layer_needs_preview);
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
  if (!initial_html.trimmed().isEmpty()) {
    QFont document_font(family);
    document_font.setPixelSize(std::max(1, document_text_size));
    document_font.setBold(text_bold);
    document_font.setItalic(text_italic);
    editor->setHtml(document_html_for_editor(initial_html, document_font, canvas_->zoom()));
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs);
    }
  } else {
    editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    const auto format = text_editor_typing_format(editor_font, text_color);
    cursor.mergeCharFormat(format);
    editor->setCurrentCharFormat(format);
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs);
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
  connect(editor, &QTextEdit::textChanged, editor, resize_editor);
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
    sync_text_alignment_buttons_from_editor();
  });
  editor->show();
  editor->installEventFilter(this);
  editor->viewport()->installEventFilter(this);
  resize_editor();
  if (editing_layer.has_value()) {
    const auto click_widget_point = canvas_->widget_position_for_document_point(requested_document_point);
    const auto click_viewport_point = editor->viewport()->mapFrom(canvas_, click_widget_point);
    editor->setTextCursor(editor->cursorForPosition(click_viewport_point));
  }
  update_text_editor_handles(editor);
  if (editor->property(kTextEditorPreviewPaintProperty).toBool()) {
    update_text_editor_preview(editor);
  }
  schedule_text_editor_preview(editor);
  editor->setFocus(Qt::OtherFocusReason);
  sync_text_alignment_buttons_from_editor();
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
      return color_dialog_ != nullptr && color_dialog_->property("patchy.colorTarget").toString() == QStringLiteral("text") &&
             (widget == color_dialog_ || color_dialog_->isAncestorOf(widget));
    };
    if (text_color_dialog_has_focus_change(old) || text_color_dialog_has_focus_change(now)) {
      return;
    }
    if (canvas_ != nullptr && now == canvas_ &&
        text_editor_resize_handle_at(canvas_->mapFromGlobal(QCursor::pos())) != nullptr) {
      return;
    }
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
      editor->property("patchy.editingLayerWasVisible").isValid()
          ? editor->property("patchy.editingLayerWasVisible").toBool()
          : true;
  remove_text_editor_preview(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed(to_qrect(layer_render_bounds(*layer)));
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
  const auto text = editor->toPlainText().trimmed();
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
      canvas_->document_changed(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  };
  remove_text_editor_preview(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();
  if (text.isEmpty()) {
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
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            normalized_rich_text_html(*document_text),
                            editor->font().family(),
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            boxed_text,
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  auto pixels = render_text_pixels(settings, text_color, text_width, paragraph_runs);
  if (pixels.empty()) {
    restore_hidden_text_layer();
    return;
  }

  auto committed_bounds = Rect{document_point.x(), document_point.y(), pixels.width(), pixels.height()};

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
      layer->set_bounds(committed_bounds);
      layer->set_visible(restore_existing_visibility);
      store_patchy_text_metadata(*layer, settings, text_color, rich_text_runs, paragraph_runs, text_width,
                                 boxed_text ? text_height : layer->pixels().height());
    }
  } else {
    Layer text_layer(document().allocate_layer_id(), tr("Text: %1").arg(name).toStdString(), std::move(pixels));
    text_layer.set_bounds(
        Rect{document_point.x(), document_point.y(), text_layer.pixels().width(), text_layer.pixels().height()});
    store_patchy_text_metadata(text_layer, settings, text_color, rich_text_runs, paragraph_runs, text_width,
                               boxed_text ? text_height : text_layer.pixels().height());
    document().add_layer(std::move(text_layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  refresh_text_color_button();
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
  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  std::optional<LayerId> layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
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
    const auto display_name = filter_display_name(*filter);
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

    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("filterProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
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
      return !progress.wasCanceled();
    }};

    PixelBuffer final_pixels;
    try {
      final_pixels = build_filter_preview_pixels(original_pixels, selection, bounds, identifier, filters_,
                                                 FilterPreviewSettings{true, *settings}, foreground, background,
                                                 &filter_progress);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(*active);
      if (layer != nullptr) {
        layer->set_pixels(original_pixels);
        canvas_->document_changed(to_qrect(bounds));
      }
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }
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

  push_undo_snapshot(tr("New layer"));
  auto layer_pixels =
      make_solid_pixels(doc.width(), doc.height(), QColor(0, 0, 0, 0), PixelFormat::rgba8());
  auto& layer = doc.add_pixel_layer(name.toStdString(), std::move(layer_pixels));
  layer.set_opacity(1.0F);
  layer.set_blend_mode(BlendMode::Normal);
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

void MainWindow::set_active_layer_mask_disabled(bool disabled) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    statusBar()->showMessage(tr("Active layer has no mask"));
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

  push_undo_snapshot(tr("Invert layer mask"));
  auto& mask = *layer->mask();
  mask.default_color = static_cast<std::uint8_t>(255 - mask.default_color);
  if (!mask.pixels.empty()) {
    for (auto& value : mask.pixels.data()) {
      value = static_cast<std::uint8_t>(255 - value);
    }
  }
  const auto dirty = mask.bounds.empty() ? layer->bounds() : mask.bounds;
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
    canvas_->document_changed(to_qrect(bounds));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Applied layer mask"));
}

void MainWindow::duplicate_active_layer() {
  duplicate_layers(selected_or_active_layer_ids());
}

void MainWindow::duplicate_layers(std::vector<LayerId> ids) {
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
  if (has_active_document()) {
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
    if (layer == nullptr || !layer_can_rasterize(*layer)) {
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
    if (layer == nullptr || !layer_can_rasterize_layer_style(*layer)) {
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
    return layer != nullptr && layer_can_rasterize(*layer);
  });
  const auto has_rasterizable_layer_style = std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && layer_can_rasterize_layer_style(*layer);
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
  auto* merge_selected_action =
      menu.addAction(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)), tr("Merge Selected to New Layer"));
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
  merge_selected_action->setEnabled(has_layer);
  visibility_action->setEnabled(has_layer);
  lock_action->setEnabled(has_layer);
  select_opaque_action->setEnabled(active_layer != nullptr && canvas_ != nullptr);
  add_mask_action->setEnabled(active_layer != nullptr && active_layer->kind() == LayerKind::Pixel &&
                              canvas_ != nullptr && canvas_->has_selection());
  delete_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  link_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  disable_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  invert_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value());
  apply_mask_action->setEnabled(active_layer != nullptr && active_layer->mask().has_value() &&
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
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) {
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
    affected = unite_rect(affected, patchy::fill_rect(doc, id, target, options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::clear_active_layer() {
  if (canvas_ != nullptr && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) {
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
    affected = unite_rect(affected, patchy::clear_rect(doc, id, layer->bounds(), options));
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
    canvas_->expand_selection(*pixels);
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
    canvas_->contract_selection(*pixels);
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

  auto& doc = document();
  push_undo_snapshot(tr("Flip vertical"));
  Rect affected;
  for (const auto id : ids) {
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
      },
                                      active.has_value() && *active == it->id() &&
                                          edit_target == CanvasWidget::LayerEditTarget::Content,
                                      active.has_value() && *active == it->id() &&
                                          edit_target == CanvasWidget::LayerEditTarget::Mask));
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
  if (layer_list_ == nullptr || !has_active_document()) {
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
        (layer->kind() == LayerKind::Pixel || layer_is_text(*layer))) {
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
  if (lock_transparency_check_ != nullptr) {
    lock_transparency_check_->setChecked(layer_locks_transparent_pixels(*layer));
  }
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(true);
  }
  refresh_layer_style_action_states();
  if (layer_rasterize_action_ != nullptr) {
    layer_rasterize_action_->setEnabled(layer_can_rasterize(*layer));
  }
  if (layer_rasterize_layer_style_action_ != nullptr) {
    layer_rasterize_layer_style_action_->setEnabled(layer_can_rasterize_layer_style(*layer));
  }
  if (delete_layer_mask_action_ != nullptr) {
    delete_layer_mask_action_->setEnabled(layer->mask().has_value());
  }
  if (link_layer_mask_action_ != nullptr) {
    link_layer_mask_action_->setEnabled(layer->mask().has_value());
    link_layer_mask_action_->setChecked(layer_mask_linked(*layer));
  }
  if (disable_layer_mask_action_ != nullptr) {
    disable_layer_mask_action_->setEnabled(layer->mask().has_value());
    disable_layer_mask_action_->setChecked(layer->mask().has_value() && layer->mask()->disabled);
  }
  if (invert_layer_mask_action_ != nullptr) {
    invert_layer_mask_action_->setEnabled(layer->mask().has_value());
  }
  if (apply_layer_mask_action_ != nullptr) {
    apply_layer_mask_action_->setEnabled(layer->mask().has_value() && layer->kind() == LayerKind::Pixel);
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
    set_property_label_text(active_layer_info_label_,
                            tr("Layer: %1 | %2 | Mode: %3 | Opacity: %4% | %5%6")
                                .arg(QString::fromStdString(layer->name()))
                                .arg(layer_is_text(*layer) ? layer_kind_name(LayerKind::Text)
                                                           : layer_kind_name(layer->kind()))
                                .arg(blend_mode_name(layer->blend_mode()))
                                .arg(static_cast<int>(std::round(layer->opacity() * 100.0F)))
                                .arg(layer->visible() ? tr("Visible") : tr("Hidden"))
                                .arg(layer_locks_transparent_pixels(*layer) ? tr(" | Transparent pixels locked")
                                                                            : QString()));
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
      set_property_label_text(active_layer_text_label_, tr("Text: %1").arg(text_layer_summary(*layer)));
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
      lines << tr("Text size: %1 px").arg(text_size_spin_->value());
    }
    set_property_label_text(active_tool_info_label_, lines.join(QStringLiteral(" | ")));
  }
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

void MainWindow::choose_text_color() {
  if (canvas_ == nullptr) {
    return;
  }
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
      apply_text_options_to_active_editor();
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
      const auto color = editor->property("patchy.documentTextColor").value<QColor>();
      if (color.isValid()) {
        return color;
      }
    }
    return canvas_->primary_color();
  }
  return QColor(Qt::black);
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
  canvas_->set_wand_contiguous(settings.value(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous()).toBool());
  canvas_->set_wand_sample_all_layers(
      settings.value(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers()).toBool());
  canvas_->set_clone_aligned(settings.value(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned()).toBool());
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
}

void MainWindow::save_tool_settings() const {
  if (canvas_ == nullptr) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("tools/brushSize"), canvas_->brush_size());
  settings.setValue(QStringLiteral("tools/brushOpacity"), canvas_->brush_opacity());
  settings.setValue(QStringLiteral("tools/brushSoftness"), canvas_->brush_softness());
  settings.setValue(QStringLiteral("tools/brushBuildUp"), canvas_->brush_build_up());
  settings.setValue(QStringLiteral("tools/wandTolerance"), canvas_->wand_tolerance());
  settings.setValue(QStringLiteral("tools/wandContiguous"), canvas_->wand_contiguous());
  settings.setValue(QStringLiteral("tools/wandSampleAllLayers"), canvas_->wand_sample_all_layers());
  settings.setValue(QStringLiteral("tools/cloneAligned"), canvas_->clone_aligned());
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
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  preserve_empty_text_editor_typing_format(*editor, editor_font, text_color);
  auto document_editor_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  auto document_editor_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());

  QTextOption option;
  option.setWrapMode(boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
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
    width = allow_point_auto_expand ? std::max(minimum_width, content_width) : minimum_width;
    document_editor_width = std::max(document_editor_width,
                                     static_cast<int>(std::ceil(static_cast<double>(width) / zoom)));
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
  update_text_editor_handles(editor);
  editor->updateGeometry();
  reset_text_editor_scroll(editor);
  editor->update();
}

void MainWindow::update_text_editor_handles(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr) {
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
  const auto handle_size = 18;
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
          "background: rgb(245, 248, 252); border: 1px solid rgb(35, 38, 44); border-radius: 2px;"));
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
    const auto hit_rect = handle->geometry().adjusted(-4, -4, 4, 4);
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
      editor->setProperty("patchy.documentTextFlow", QString::fromLatin1(kTextFlowBox));
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

void MainWindow::schedule_text_editor_preview(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (!editor->property(kTextEditorPreviewPaintProperty).toBool()) {
    remove_text_editor_preview(editor);
  }
  if (editor->property("patchy.textPreviewPending").toBool()) {
    return;
  }
  editor->setProperty("patchy.textPreviewPending", true);
  QTimer::singleShot(33, editor, [this, editor = QPointer<QTextEdit>(editor)] {
    if (editor == nullptr) {
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
  auto& doc = document();
  std::optional<LayerId> editing_layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    editing_layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  auto* source = editing_layer_id.has_value() ? doc.find_layer(*editing_layer_id) : nullptr;
  const auto source_was_visible = !editor->property("patchy.editingLayerWasVisible").isValid() ||
                                  editor->property("patchy.editingLayerWasVisible").toBool();
  const auto needs_preview = source != nullptr && source_was_visible && layer_requires_text_editor_preview(*source);
  editor->setProperty(kTextEditorPreviewPaintProperty, needs_preview);
  editor->viewport()->update();
  if (!needs_preview) {
    remove_text_editor_preview(editor);
    return;
  }

  const auto text = editor->toPlainText().trimmed();
  if (text.isEmpty()) {
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
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            normalized_rich_text_html(*document_text),
                            editor->font().family(),
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            boxed_text,
                            text_width,
                            text_height};
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  auto pixels = render_text_pixels(settings, text_color, text_width, paragraph_runs);
  if (pixels.empty()) {
    remove_text_editor_preview(editor);
    return;
  }

  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  const Rect preview_bounds{document_point.x(), document_point.y(), pixels.width(), pixels.height()};
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
      canvas_->document_changed(dirty);
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
  canvas_->document_changed(dirty);
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
    canvas_->document_changed(dirty);
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

void MainWindow::apply_text_options_to_active_editor() {
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
  const auto document_text_size =
      text_size_spin_ != nullptr ? text_size_spin_->value()
                                 : std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : editor->font().family();

  QFont editor_font(family);
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))));
  editor_font.setBold(text_bold_button_ != nullptr && text_bold_button_->isChecked());
  editor_font.setItalic(text_italic_button_ != nullptr && text_italic_button_->isChecked());

  editor->setProperty("patchy.documentTextSize", document_text_size);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));

  auto cursor = editor->textCursor();
  const auto format = text_editor_typing_format(editor_font, text_color);
  if (editor->toPlainText().isEmpty()) {
    editor->setCurrentCharFormat(format);
  } else if (cursor.hasSelection()) {
    cursor.mergeCharFormat(format);
    editor->setTextCursor(cursor);
  } else {
    editor->mergeCurrentCharFormat(format);
  }

  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

bool MainWindow::is_text_option_widget(QWidget* widget) const {
  if (widget == nullptr) {
    return false;
  }
  if (widget->property("patchy.textResizeHandle").toBool()) {
    return true;
  }
  const auto owns = [widget](const QWidget* candidate) {
    return candidate != nullptr && (widget == candidate || candidate->isAncestorOf(widget));
  };
  return owns(text_font_combo_) || owns(text_size_spin_) || owns(text_bold_button_) || owns(text_italic_button_) ||
         owns(text_color_button_) || owns(text_align_left_button_) || owns(text_align_center_button_) ||
         owns(text_align_right_button_);
}

void MainWindow::register_option_action(QAction* action, std::initializer_list<CanvasTool> tools) {
  if (action == nullptr) {
    return;
  }
  option_actions_.emplace_back(action, std::vector<CanvasTool>(tools.begin(), tools.end()));
}

void MainWindow::refresh_options_bar() {
  const bool has_document = has_active_document();
  for (const auto& [action, tools] : option_actions_) {
    if (action == nullptr) {
      continue;
    }
    const auto visible = tools.empty() || std::find(tools.begin(), tools.end(), current_tool_) != tools.end();
    action->setVisible(visible);
    action->setEnabled(has_document);
  }

  if (move_auto_select_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(move_auto_select_check_);
    move_auto_select_check_->setChecked(canvas_->auto_select_layer());
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
  sync_text_alignment_buttons_from_editor();
}

void MainWindow::register_document_action(QAction* action) {
  if (action == nullptr) {
    return;
  }
  document_actions_.push_back(action);
}

void MainWindow::register_document_widget(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  document_widgets_.push_back(widget);
}

void MainWindow::update_document_action_state() {
  const bool has_document = has_active_document();
  for (auto* action : document_actions_) {
    if (action != nullptr) {
      action->setEnabled(has_document);
    }
  }
  for (auto* widget : document_widgets_) {
    if (widget != nullptr) {
      widget->setEnabled(has_document);
    }
  }
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setEnabled(has_document);
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setEnabled(has_document);
  }
  if (layer_list_ != nullptr) {
    layer_list_->setEnabled(has_document);
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
  while (recent_files_.size() > 10) {
    recent_files_.removeLast();
  }
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
    const auto label = tr("&%1 %2").arg(index++).arg(QDir::toNativeSeparators(path));
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
  bind_action_text(action, tool_action_source(tool));
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  apply_action_shortcut(action, shortcut);
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
