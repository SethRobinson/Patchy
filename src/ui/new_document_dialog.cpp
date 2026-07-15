// The New Document dialog: a two-pane layout with category chips over a clickable
// preset card grid on the left and the width/height/resolution/background details
// pane on the right. Pixels are the stored truth; the dimension spins convert
// through the chosen unit at the chosen resolution, like Image Size. Split out of
// main_window_document_dialogs.cpp when the dialog gained the card grid.

#include "ui/new_document_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace patchy::ui {

namespace {

// Resolution conventions follow Photoshop: screen/web/video presets (and the
// Clipboard) are 72 PPI; physical print presets are their paper size at 300 PPI.
constexpr double kScreenPpi = 72.0;
constexpr double kPrintPpi = 300.0;

constexpr int kPresetIdRole = kNewDocumentPresetIdRole;
constexpr int kPresetSizeRole = kNewDocumentPresetSizeRole;
constexpr int kPresetPpiRole = kNewDocumentPresetPpiRole;
constexpr int kPresetClipboardRole = Qt::UserRole + 4;
constexpr int kPresetThumbnailRole = Qt::UserRole + 5;

constexpr QSize kPresetCardCell(116, 100);

enum class PresetCategory { Screen, Print };

struct NewDocumentPreset {
  QString id;  // persisted as newDocument/lastPresetId in user settings - never rename
  QString name;
  QSize size;
  double ppi{kScreenPpi};
  bool clipboard{false};
};

std::vector<NewDocumentPreset> presets_for_category(PresetCategory category, QSize clipboard_size) {
  if (category == PresetCategory::Screen) {
    return {
        {QStringLiteral("clipboard"), QObject::tr("Clipboard"), clipboard_size, kScreenPpi, true},
        {QStringLiteral("screen-1024x768"), QObject::tr("Default"), QSize(1024, 768), kScreenPpi},
        {QStringLiteral("screen-720p"), QObject::tr("720p"), QSize(1280, 720), kScreenPpi},
        {QStringLiteral("screen-1080p"), QObject::tr("1080p"), QSize(1920, 1080), kScreenPpi},
        {QStringLiteral("screen-4k"), QObject::tr("4K"), QSize(3840, 2160), kScreenPpi},
        {QStringLiteral("screen-square-2048"), QObject::tr("Square", "new document preset"), QSize(2048, 2048),
         kScreenPpi},
        {QStringLiteral("social-square-1080"), QObject::tr("Social Post"), QSize(1080, 1080), kScreenPpi},
        {QStringLiteral("phone-story-1080x1920"), QObject::tr("Social Story"), QSize(1080, 1920), kScreenPpi},
        {QStringLiteral("photo-3x2-3000"), QObject::tr("Photo 3:2"), QSize(3000, 2000), kScreenPpi},
    };
  }
  return {
      {QStringLiteral("print-a5"), QObject::tr("A5"), QSize(1748, 2480), kPrintPpi},
      {QStringLiteral("print-a4"), QObject::tr("A4"), QSize(2480, 3508), kPrintPpi},
      {QStringLiteral("print-a3"), QObject::tr("A3"), QSize(3508, 4961), kPrintPpi},
      {QStringLiteral("print-us-letter"), QObject::tr("US Letter"), QSize(2550, 3300), kPrintPpi},
      {QStringLiteral("print-us-legal"), QObject::tr("US Legal"), QSize(2550, 4200), kPrintPpi},
      {QStringLiteral("print-5x7"), QObject::tr("5 x 7 in"), QSize(1500, 2100), kPrintPpi},
      {QStringLiteral("print-8x10"), QObject::tr("8 x 10 in"), QSize(2400, 3000), kPrintPpi},
  };
}

PresetCategory category_of_preset_id(const QString& id) {
  for (const auto& preset : presets_for_category(PresetCategory::Print, QSize())) {
    if (preset.id == id) {
      return PresetCategory::Print;
    }
  }
  return PresetCategory::Screen;
}

QFont derived_font(QFont font, int pixel_delta, bool bold) {
  font.setBold(bold);
  if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(8, font.pixelSize() + pixel_delta));
  } else {
    font.setPointSizeF(std::max(7.0, font.pointSizeF() + pixel_delta));
  }
  return font;
}

// Paints one preset card: rounded background, an aspect-ratio miniature (the real
// clipboard thumbnail for the Clipboard card), the preset name, the pixel
// dimensions, and the preset resolution.
class PresetCardDelegate final : public QStyledItemDelegate {
 public:
  explicit PresetCardDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return kPresetCardCell; }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRect card = option.rect.adjusted(4, 4, -4, -4);
    const bool enabled = (index.flags() & Qt::ItemIsEnabled) != 0;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    QColor background(0x30, 0x30, 0x30);
    QColor border(0x45, 0x45, 0x45);
    if (!enabled) {
      background = QColor(0x2b, 0x2b, 0x2b);
      border = QColor(0x39, 0x39, 0x39);
    } else if (selected) {
      background = QColor(0x33, 0x41, 0x52);
      border = QColor(0x6f, 0x9b, 0xd1);
    } else if (hovered) {
      background = QColor(0x3a, 0x3a, 0x3a);
      border = QColor(0x5f, 0x5f, 0x5f);
    }
    QPainterPath card_path;
    card_path.addRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
    painter->fillPath(card_path, background);
    painter->setPen(QPen(border, selected ? 2.0 : 1.0));
    painter->drawPath(card_path);

    const QRect thumb_area(card.left() + 8, card.top() + 7, card.width() - 16, 32);
    const auto size = index.data(kPresetSizeRole).toSize();
    const auto thumbnail = index.data(kPresetThumbnailRole).value<QPixmap>();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (!thumbnail.isNull()) {
      const auto scaled =
          thumbnail.scaled(thumb_area.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      const QRect target(thumb_area.x() + (thumb_area.width() - scaled.width()) / 2,
                         thumb_area.y() + (thumb_area.height() - scaled.height()) / 2, scaled.width(),
                         scaled.height());
      painter->drawPixmap(target, scaled);
      painter->setPen(QPen(QColor(0x9a, 0x9a, 0x9a), 1.0));
      painter->drawRect(target.adjusted(0, 0, -1, -1));
    } else {
      QSizeF box = size.isValid() && size.width() > 0 && size.height() > 0 ? QSizeF(size) : QSizeF(3.0, 2.0);
      box.scale(QSizeF(thumb_area.size()), Qt::KeepAspectRatio);
      const QRectF outline(thumb_area.x() + (thumb_area.width() - box.width()) / 2.0,
                           thumb_area.y() + (thumb_area.height() - box.height()) / 2.0, box.width(),
                           box.height());
      painter->fillRect(outline, enabled ? QColor(0x47, 0x47, 0x47) : QColor(0x35, 0x35, 0x35));
      QPen outline_pen(enabled ? QColor(0xa2, 0xa2, 0xa2) : QColor(0x58, 0x58, 0x58), 1.0);
      if (!size.isValid() || size.width() <= 0) {
        outline_pen.setStyle(Qt::DashLine);  // Clipboard card with nothing on the clipboard.
      }
      painter->setPen(outline_pen);
      painter->drawRect(outline);
    }

    const auto name_font = derived_font(option.font, 0, true);
    painter->setFont(name_font);
    painter->setPen(enabled ? QColor(0xe8, 0xe8, 0xe8) : QColor(0x6f, 0x6f, 0x6f));
    const QRect name_rect(card.left() + 4, card.top() + 41, card.width() - 8, 16);
    painter->drawText(name_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                         Qt::ElideRight, name_rect.width()));

    const auto detail_font = derived_font(option.font, -1, false);
    painter->setFont(detail_font);
    painter->setPen(enabled ? QColor(0xa5, 0xa5, 0xa5) : QColor(0x5c, 0x5c, 0x5c));
    const QRect dims_rect(card.left() + 4, card.top() + 57, card.width() - 8, 14);
    const auto dims_text = size.isValid() && size.width() > 0
                               ? QObject::tr("%1 x %2 px").arg(size.width()).arg(size.height())
                               : QObject::tr("No image");
    painter->drawText(dims_rect, Qt::AlignHCenter | Qt::AlignVCenter, dims_text);

    painter->setPen(enabled ? QColor(0x83, 0x83, 0x83) : QColor(0x50, 0x50, 0x50));
    const QRect ppi_rect(card.left() + 4, card.top() + 71, card.width() - 8, 13);
    painter->drawText(ppi_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QObject::tr("%1 ppi").arg(QString::number(index.data(kPresetPpiRole).toDouble())));
    painter->restore();
  }
};

QString new_document_dialog_style() {
  return QStringLiteral(R"(
    QDialog#patchyNewDocumentDialog {
      background: #262626;
    }
    QDialog#patchyNewDocumentDialog QLabel {
      background: transparent;
      color: #e6e6e6;
    }
    QDialog#patchyNewDocumentDialog QLabel#newDocumentSummaryLabel {
      color: #a8a8a8;
    }
    QDialog#patchyNewDocumentDialog QListWidget#newDocumentPresetList {
      background: #222222;
      border: 1px solid #1b1b1b;
      border-radius: 5px;
      padding: 3px;
    }
    QToolButton[newDocumentChip="true"] {
      background: #303030;
      border: 1px solid #474747;
      border-radius: 12px;
      color: #d9d9d9;
      padding: 3px 14px;
      min-height: 17px;
    }
    QToolButton[newDocumentChip="true"]:hover {
      background: #3a3a3a;
      border-color: #5f5f5f;
    }
    QToolButton[newDocumentChip="true"]:checked {
      background: #33414f;
      border-color: #6f9bd1;
      color: #ffffff;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox,
    QDialog#patchyNewDocumentDialog QComboBox {
      background: #303030;
      border: 1px solid #4a4a4a;
      border-radius: 3px;
      color: #f0f0f0;
      min-height: 22px;
      padding: 0 6px;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox:focus,
    QDialog#patchyNewDocumentDialog QComboBox:focus {
      border-color: #6f9bd1;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox:disabled,
    QDialog#patchyNewDocumentDialog QComboBox:disabled {
      background: #2a2a2a;
      border-color: #383838;
      color: #6f6f6f;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton {
      background: #303030;
      border: 1px solid #4a4a4a;
      border-radius: 3px;
      min-height: 24px;
      max-height: 24px;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton:hover {
      border-color: #6f9bd1;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton:disabled {
      background: #2a2a2a;
      border-color: #383838;
    }
    QDialog#patchyNewDocumentDialog QPushButton {
      background: #3a3a3a;
      border: 1px solid #5a5a5a;
      border-radius: 13px;
      color: #f0f0f0;
      min-width: 84px;
      min-height: 26px;
      padding: 0 16px;
    }
    QDialog#patchyNewDocumentDialog QPushButton:hover {
      background: #454545;
      border-color: #7d7d7d;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentCreateButton {
      background: #354960;
      border: 1px solid #6f9bd1;
      font-weight: 700;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentCreateButton:hover {
      background: #3f5773;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch {
      background: #303030;
      border: 1px solid #4a4a4a;
      border-radius: 3px;
      min-width: 40px;
      max-width: 40px;
      min-height: 24px;
      max-height: 24px;
      padding: 0;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch:hover {
      border-color: #6f9bd1;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch:disabled {
      background: #2a2a2a;
      border-color: #383838;
    }
  )");
}

}  // namespace

std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent) {
  const auto clipboard_image = QApplication::clipboard()->image();
  const auto clipboard_size = clipboard_image.isNull() ? QSize() : clipboard_image.size();

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyNewDocumentDialog"));
  dialog.setWindowTitle(QObject::tr("New Document"));

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(14, 12, 14, 12);
  root->setSpacing(10);

  auto* body = new QHBoxLayout();
  body->setSpacing(18);
  root->addLayout(body, 1);

  // Left pane: category chips over the preset card grid.
  auto* left = new QVBoxLayout();
  left->setSpacing(8);
  body->addLayout(left, 1);

  auto* chips_row = new QHBoxLayout();
  chips_row->setSpacing(6);
  auto* chip_group = new QButtonGroup(&dialog);
  chip_group->setExclusive(true);
  const auto make_chip = [&dialog, chip_group, chips_row](const QString& label, const QString& object_name,
                                                          PresetCategory category) {
    auto* chip = new QToolButton(&dialog);
    chip->setObjectName(object_name);
    chip->setText(label);
    chip->setCheckable(true);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setProperty("newDocumentChip", true);
    chip_group->addButton(chip, static_cast<int>(category));
    chips_row->addWidget(chip);
    return chip;
  };
  auto* screen_chip =
      make_chip(QObject::tr("Screen"), QStringLiteral("newDocumentScreenChip"), PresetCategory::Screen);
  auto* print_chip =
      make_chip(QObject::tr("Print"), QStringLiteral("newDocumentPrintChip"), PresetCategory::Print);
  chips_row->addStretch(1);
  left->addLayout(chips_row);

  auto* preset_list = new QListWidget(&dialog);
  preset_list->setObjectName(QStringLiteral("newDocumentPresetList"));
  preset_list->setViewMode(QListView::IconMode);
  preset_list->setMovement(QListView::Static);
  preset_list->setResizeMode(QListView::Adjust);
  preset_list->setSelectionMode(QAbstractItemView::SingleSelection);
  preset_list->setUniformItemSizes(true);
  preset_list->setGridSize(kPresetCardCell);
  preset_list->setMouseTracking(true);
  preset_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  preset_list->setItemDelegate(new PresetCardDelegate(preset_list));
  preset_list->setMinimumSize(3 * kPresetCardCell.width() + 26, 3 * kPresetCardCell.height() + 10);
  left->addWidget(preset_list, 1);

  // Right pane: the details grid. Fixed field/unit column widths keep every row the
  // same size (the old form let the width row stretch wider than the others).
  auto* right = new QVBoxLayout();
  right->setSpacing(10);
  body->addLayout(right, 0);

  constexpr int kFieldWidth = 96;
  constexpr int kUnitWidth = 128;

  auto* grid = new QGridLayout();
  grid->setContentsMargins(0, 2, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(9);
  right->addLayout(grid);

  auto* width = new QDoubleSpinBox(&dialog);
  width->setObjectName(QStringLiteral("newDocumentWidthSpin"));
  configure_dialog_spinbox(width, kFieldWidth);
  width->setFixedWidth(kFieldWidth);
  auto* height = new QDoubleSpinBox(&dialog);
  height->setObjectName(QStringLiteral("newDocumentHeightSpin"));
  configure_dialog_spinbox(height, kFieldWidth);
  height->setFixedWidth(kFieldWidth);

  auto* unit = new QComboBox(&dialog);
  unit->setObjectName(QStringLiteral("newDocumentUnitCombo"));
  for (const auto dimension_unit : {MeasurementUnit::Pixels, MeasurementUnit::Inches,
                                    MeasurementUnit::Centimeters, MeasurementUnit::Millimeters}) {
    unit->addItem(measurement_unit_name(dimension_unit), static_cast<int>(dimension_unit));
  }
  unit->setFixedWidth(kUnitWidth);

  auto* swap_dimensions = new QToolButton(&dialog);
  swap_dimensions->setObjectName(QStringLiteral("newDocumentSwapDimensionsButton"));
  swap_dimensions->setIcon(QIcon(QStringLiteral(":/patchy/icons/rotate.svg")));
  swap_dimensions->setIconSize(QSize(18, 18));
  swap_dimensions->setToolTip(QObject::tr("Swap width and height"));
  swap_dimensions->setFixedWidth(28);

  auto* resolution = new QDoubleSpinBox(&dialog);
  resolution->setObjectName(QStringLiteral("newDocumentResolutionSpin"));
  resolution->setDecimals(2);
  resolution->setRange(0.01, 9999.0);
  configure_dialog_spinbox(resolution, kFieldWidth);
  resolution->setFixedWidth(kFieldWidth);
  auto* resolution_unit = new QComboBox(&dialog);
  resolution_unit->setObjectName(QStringLiteral("newDocumentResolutionUnitCombo"));
  resolution_unit->addItem(QObject::tr("Pixels/Inch"), 1.0);
  resolution_unit->addItem(QObject::tr("Pixels/Centimeter"), 2.54);
  resolution_unit->setFixedWidth(kUnitWidth);

  auto* background = new QComboBox(&dialog);
  background->setObjectName(QStringLiteral("newDocumentBackgroundCombo"));
  background->addItem(QObject::tr("Other..."), QColor(Qt::white));
  background->addItem(QObject::tr("White"), QColor(Qt::white));
  background->addItem(QObject::tr("Black"), QColor(Qt::black));
  background->addItem(QObject::tr("Transparent"), QColor(0, 0, 0, 0));
  auto* background_swatch = new QPushButton(&dialog);
  background_swatch->setObjectName(QStringLiteral("newDocumentBackgroundSwatch"));
  background_swatch->setAccessibleName(QObject::tr("Background color"));
  background_swatch->setToolTip(QObject::tr("Choose background color"));
  background_swatch->setCursor(Qt::PointingHandCursor);
  background_swatch->setFocusPolicy(Qt::StrongFocus);

  const auto add_row_label = [&dialog, grid](const QString& text, int row) {
    grid->addWidget(new QLabel(text, &dialog), row, 0, Qt::AlignRight | Qt::AlignVCenter);
  };
  add_row_label(QObject::tr("Width"), 0);
  grid->addWidget(width, 0, 1);
  grid->addWidget(unit, 0, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Height"), 1);
  grid->addWidget(height, 1, 1);
  grid->addWidget(swap_dimensions, 1, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Resolution"), 2);
  grid->addWidget(resolution, 2, 1);
  grid->addWidget(resolution_unit, 2, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Background"), 3);
  auto* background_row = new QHBoxLayout();
  background_row->setContentsMargins(0, 0, 0, 0);
  background_row->setSpacing(8);
  background_row->addWidget(background, 1);
  background_row->addWidget(background_swatch, 0);
  grid->addLayout(background_row, 3, 1, 1, 2);

  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("newDocumentSummaryLabel"));
  summary->setTextFormat(Qt::PlainText);
  summary->setMinimumHeight(52);
  summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  right->addSpacing(4);
  right->addWidget(summary);
  right->addStretch(1);

  auto* buttons_row = new QHBoxLayout();
  buttons_row->setSpacing(8);
  buttons_row->addStretch(1);
  auto* create = new QPushButton(QObject::tr("Create"), &dialog);
  create->setObjectName(QStringLiteral("newDocumentCreateButton"));
  create->setDefault(true);
  auto* cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel->setObjectName(QStringLiteral("newDocumentCancelButton"));
  buttons_row->addWidget(create);
  buttons_row->addWidget(cancel);
  right->addLayout(buttons_row);

  // Pixels are the stored truth; the width/height spins convert through the chosen
  // unit at the chosen resolution, like Image Size.
  struct NewDocumentState {
    int pixel_width{1024};
    int pixel_height{768};
    double ppi{kScreenPpi};
  };
  NewDocumentState state;
  QString current_preset_id = QStringLiteral("custom");
  QString current_preset_name = QObject::tr("Custom");
  QColor background_color(Qt::white);

  const auto current_unit = [unit] { return static_cast<MeasurementUnit>(unit->currentData().toInt()); };
  const auto refresh_dimension_spins = [&state, current_unit, width, height] {
    const auto dimension_unit = current_unit();
    for (auto* spin : {width, height}) {
      const QSignalBlocker blocker(spin);
      spin->setDecimals(measurement_unit_decimals(dimension_unit));
      spin->setRange(dimension_unit == MeasurementUnit::Pixels ? 1.0 : 0.001, 999999.0);
    }
    {
      const QSignalBlocker blocker(width);
      width->setValue(pixels_to_measurement_unit(state.pixel_width, dimension_unit, state.ppi, state.pixel_width));
    }
    {
      const QSignalBlocker blocker(height);
      height->setValue(
          pixels_to_measurement_unit(state.pixel_height, dimension_unit, state.ppi, state.pixel_height));
    }
  };
  const auto refresh_resolution_spin = [&state, resolution, resolution_unit] {
    const QSignalBlocker blocker(resolution);
    resolution->setValue(state.ppi / resolution_unit->currentData().toDouble());
  };

  const auto format_pixel_bytes = [](double bytes) {
    if (bytes >= 1024.0 * 1024.0) {
      return QObject::tr("%1M").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QObject::tr("%1K").arg(bytes / 1024.0, 0, 'f', 1);
  };
  const auto update_summary = [&state, &current_preset_name, summary, format_pixel_bytes] {
    // Bullet built from its code point: the sources build without /utf-8, so a raw
    // non-ASCII character in a literal would depend on the system codepage.
    const QString separator = QStringLiteral("  ") + QChar(0x2022) + QStringLiteral("  ");
    auto dimensions_line = QObject::tr("%1 x %2 px").arg(state.pixel_width).arg(state.pixel_height);
    const int divisor = std::gcd(std::max(1, state.pixel_width), std::max(1, state.pixel_height));
    const int ratio_width = state.pixel_width / divisor;
    const int ratio_height = state.pixel_height / divisor;
    if (std::max(ratio_width, ratio_height) <= 20) {
      dimensions_line += separator + QStringLiteral("%1:%2").arg(ratio_width).arg(ratio_height);
    }
    // A per-layer memory estimate at rgba8; deliberately simple (documents open with
    // two layers and formats vary), it exists to make huge sizes register as huge.
    dimensions_line += separator +
                       format_pixel_bytes(static_cast<double>(state.pixel_width) *
                                          static_cast<double>(state.pixel_height) * 4.0);
    const auto physical_line = QObject::tr("%1 x %2 in at %3 ppi")
                                   .arg(state.pixel_width / state.ppi, 0, 'f', 2)
                                   .arg(state.pixel_height / state.ppi, 0, 'f', 2)
                                   .arg(QString::number(state.ppi, 'g', 6));
    summary->setText(current_preset_name + QChar('\n') + dimensions_line + QChar('\n') + physical_line);
  };

  const auto set_fields_enabled = [width, height, unit, resolution, resolution_unit, background,
                                   background_swatch, swap_dimensions](bool enabled) {
    width->setEnabled(enabled);
    height->setEnabled(enabled);
    unit->setEnabled(enabled);
    resolution->setEnabled(enabled);
    resolution_unit->setEnabled(enabled);
    background->setEnabled(enabled);
    background_swatch->setEnabled(enabled);
    swap_dimensions->setEnabled(enabled);
  };

  const auto mark_custom = [&current_preset_id, &current_preset_name, preset_list, update_summary,
                            set_fields_enabled] {
    if (current_preset_id != QStringLiteral("custom")) {
      current_preset_id = QStringLiteral("custom");
      current_preset_name = QObject::tr("Custom");
      {
        const QSignalBlocker blocker(preset_list);
        preset_list->setCurrentItem(nullptr);
        preset_list->clearSelection();
      }
      set_fields_enabled(true);
    }
    update_summary();
  };

  const auto populate_list = [&](PresetCategory category) {
    const QSignalBlocker blocker(preset_list);
    preset_list->clear();
    for (const auto& preset : presets_for_category(category, clipboard_size)) {
      auto* item = new QListWidgetItem(preset.name, preset_list);
      item->setData(kPresetIdRole, preset.id);
      item->setData(kPresetSizeRole, preset.size);
      item->setData(kPresetPpiRole, preset.ppi);
      item->setData(kPresetClipboardRole, preset.clipboard);
      if (preset.clipboard) {
        if (clipboard_image.isNull()) {
          item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
          item->setToolTip(QObject::tr("Clipboard does not contain an image"));
        } else {
          item->setData(kPresetThumbnailRole,
                        QVariant::fromValue(QPixmap::fromImage(clipboard_image.scaled(
                            112, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
          item->setToolTip(QObject::tr("Create the document from the clipboard image"));
        }
      } else if (preset.size.isValid() && preset.size.width() > 0) {
        item->setToolTip(QObject::tr("%1: %2 x %3 px at %4 ppi")
                             .arg(preset.name)
                             .arg(preset.size.width())
                             .arg(preset.size.height())
                             .arg(QString::number(preset.ppi)));
      }
      if (preset.id == current_preset_id) {
        preset_list->setCurrentItem(item);
        item->setSelected(true);
      }
    }
  };

  const auto apply_preset_item = [&](QListWidgetItem* item) {
    if (item == nullptr) {
      return;
    }
    current_preset_id = item->data(kPresetIdRole).toString();
    current_preset_name = item->text();
    const auto size = item->data(kPresetSizeRole).toSize();
    if (size.isValid() && size.width() > 0) {
      state.pixel_width = std::clamp(size.width(), 1, 30000);
      state.pixel_height = std::clamp(size.height(), 1, 30000);
    }
    if (const auto preset_ppi = item->data(kPresetPpiRole).toDouble(); preset_ppi > 0.0) {
      state.ppi = preset_ppi;
    }
    refresh_dimension_spins();
    refresh_resolution_spin();
    set_fields_enabled(!item->data(kPresetClipboardRole).toBool());
    update_summary();
  };

  QObject::connect(preset_list, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem* item, QListWidgetItem*) { apply_preset_item(item); });
  QObject::connect(chip_group, &QButtonGroup::idToggled, &dialog, [&](int id, bool checked) {
    if (checked) {
      populate_list(static_cast<PresetCategory>(id));
    }
  });

  const auto handle_dimension_edit = [&state, current_unit, mark_custom](QDoubleSpinBox* spin,
                                                                         bool editing_width) {
    const auto pixels = std::clamp(
        static_cast<int>(std::lround(measurement_unit_to_pixels(spin->value(), current_unit(), state.ppi, 0.0))),
        1, 30000);
    (editing_width ? state.pixel_width : state.pixel_height) = pixels;
    mark_custom();
  };
  QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(width, true); });
  QObject::connect(height, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(height, false); });
  QObject::connect(unit, &QComboBox::currentIndexChanged, &dialog, [&](int) { refresh_dimension_spins(); });
  QObject::connect(resolution, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
    const auto new_ppi = std::clamp(value * resolution_unit->currentData().toDouble(), 0.01, 9999.0);
    if (measurement_unit_is_physical(current_unit())) {
      // Physical entry holds its size across a resolution change (Photoshop's
      // new-document behavior): the pixel dimensions re-derive.
      state.pixel_width =
          std::clamp(static_cast<int>(std::lround(state.pixel_width / state.ppi * new_ppi)), 1, 30000);
      state.pixel_height =
          std::clamp(static_cast<int>(std::lround(state.pixel_height / state.ppi * new_ppi)), 1, 30000);
      state.ppi = new_ppi;
      refresh_dimension_spins();
    } else {
      state.ppi = new_ppi;
    }
    mark_custom();
  });
  QObject::connect(resolution_unit, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { refresh_resolution_spin(); });
  QObject::connect(swap_dimensions, &QToolButton::clicked, &dialog, [&] {
    std::swap(state.pixel_width, state.pixel_height);
    mark_custom();
    refresh_dimension_spins();
  });

  const auto update_swatch = [background_swatch, &background_color] {
    QPixmap swatch(34, 18);
    QPainter painter(&swatch);
    constexpr int kTile = 6;  // checkerboard base so a transparent background reads as transparent
    for (int y = 0; y < swatch.height(); y += kTile) {
      for (int x = 0; x < swatch.width(); x += kTile) {
        const bool light = ((x / kTile) + (y / kTile)) % 2 == 0;
        painter.fillRect(QRect(x, y, kTile, kTile), light ? QColor(212, 212, 212) : QColor(150, 150, 150));
      }
    }
    painter.fillRect(swatch.rect(), background_color);
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    background_swatch->setIcon(QIcon(swatch));
    background_swatch->setIconSize(swatch.size());
  };
  const auto select_background_index_for = [background](const QColor& color) {
    const QSignalBlocker blocker(background);
    for (int index = 1; index < background->count(); ++index) {
      if (background->itemData(index).value<QColor>() == color) {
        background->setCurrentIndex(index);
        return;
      }
    }
    background->setItemData(0, color);
    background->setCurrentIndex(0);
  };
  const auto choose_background_color = [&dialog, &background_color, update_swatch,
                                        select_background_index_for] {
    const auto selected = QColorDialog::getColor(background_color, &dialog, QObject::tr("Background Color"),
                                                 QColorDialog::ShowAlphaChannel);
    if (selected.isValid()) {
      background_color = selected;
    }
    select_background_index_for(background_color);
    update_swatch();
  };
  // activated covers re-picking "Other..." while it is already current; the fixed
  // colors go through currentIndexChanged so programmatic changes (tests) match
  // user picks.
  QObject::connect(background, &QComboBox::activated, &dialog, [&](int index) {
    if (index == 0) {
      choose_background_color();
    }
  });
  QObject::connect(background, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    if (index > 0) {
      background_color = background->itemData(index).value<QColor>();
      update_swatch();
    }
  });
  QObject::connect(background_swatch, &QPushButton::clicked, &dialog, choose_background_color);

  QObject::connect(create, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

  // Restore the last accepted settings; a clipboard image preselects its card instead.
  {
    auto settings = app_settings();
    settings.beginGroup(QStringLiteral("newDocument"));
    const auto last_id = settings.value(QStringLiteral("lastPresetId")).toString();
    const auto last_width = settings.value(QStringLiteral("lastWidth")).toInt();
    const auto last_height = settings.value(QStringLiteral("lastHeight")).toInt();
    const auto last_ppi = settings.value(QStringLiteral("lastPpi")).toDouble();
    const auto last_background = settings.value(QStringLiteral("lastBackground")).value<QColor>();
    settings.endGroup();

    if (last_background.isValid()) {
      background_color = last_background;
    }
    if (!clipboard_image.isNull()) {
      current_preset_id = QStringLiteral("clipboard");
    } else if (last_id == QStringLiteral("custom") && last_width >= 1 && last_height >= 1) {
      state.pixel_width = std::clamp(last_width, 1, 30000);
      state.pixel_height = std::clamp(last_height, 1, 30000);
      state.ppi = std::clamp(last_ppi > 0.0 ? last_ppi : kScreenPpi, 0.01, 9999.0);
    } else if (!last_id.isEmpty() && last_id != QStringLiteral("clipboard")) {
      for (const auto category : {PresetCategory::Screen, PresetCategory::Print}) {
        for (const auto& preset : presets_for_category(category, clipboard_size)) {
          if (preset.id == last_id) {
            current_preset_id = last_id;
            break;
          }
        }
      }
    }
    if (current_preset_id == QStringLiteral("custom") && last_id != QStringLiteral("custom")) {
      // First run, or a remembered id this build no longer knows.
      current_preset_id = QStringLiteral("screen-1024x768");
    }
  }
  select_background_index_for(background_color);
  update_swatch();

  const auto initial_category = category_of_preset_id(current_preset_id);
  (initial_category == PresetCategory::Print ? print_chip : screen_chip)->setChecked(true);
  if (auto* item = preset_list->currentItem(); item != nullptr) {
    apply_preset_item(item);
  } else {
    refresh_dimension_spins();
    refresh_resolution_spin();
    set_fields_enabled(true);
    update_summary();
  }

  dialog.setStyleSheet(dialog.styleSheet() + new_document_dialog_style());
  dialog.resize(724, 470);
  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const bool clipboard_selected = current_preset_id == QStringLiteral("clipboard");
  if (!clipboard_selected) {
    auto settings = app_settings();
    settings.beginGroup(QStringLiteral("newDocument"));
    settings.setValue(QStringLiteral("lastPresetId"), current_preset_id);
    settings.setValue(QStringLiteral("lastWidth"), state.pixel_width);
    settings.setValue(QStringLiteral("lastHeight"), state.pixel_height);
    settings.setValue(QStringLiteral("lastPpi"), state.ppi);
    settings.setValue(QStringLiteral("lastBackground"), background_color);
    settings.endGroup();
  }
  return NewDocumentSettings{state.pixel_width,   state.pixel_height,
                             state.ppi,           background_color,
                             clipboard_selected,  clipboard_selected ? QApplication::clipboard()->image() : QImage()};
}

}  // namespace patchy::ui
