#include "ui/brush_tip_picker.hpp"

#include "ui/app_settings.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QScreen>
#include <QSizeGrip>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kThumbnailExtent = 48;

// The closed picker's face: the tip thumbnail, the name and a chevron, like the
// Artboard bar's preset picker.
constexpr int kFaceIconExtent = 16;
constexpr int kFaceInset = 3;
constexpr int kFaceTextGap = 4;
constexpr int kChevronSize = 10;
constexpr int kChevronMargin = 5;
constexpr int kChevronRoom = kChevronSize + kChevronMargin + 3;

const QString kPopupSizeSettingsKey = QStringLiteral("ui/brushTipPickerPopupSize");

// The picker popup is a frameless Qt::Popup, so resizing happens through an embedded
// QSizeGrip; the chosen size persists across openings (and launches) via app settings.
class ResizablePickerPopup : public QFrame {
public:
  using QFrame::QFrame;

protected:
  void closeEvent(QCloseEvent* event) override {
    auto settings = app_settings();
    settings.setValue(kPopupSizeSettingsKey, size());
    QFrame::closeEvent(event);
  }
};

[[nodiscard]] QString round_tip_display_name() {
  return QObject::tr("Round");
}

[[nodiscard]] QString square_tip_display_name() {
  return QObject::tr("Square");
}

[[nodiscard]] QString builtin_tip_display_name(const QString& id) {
  return id == builtin_square_brush_tip_id() ? square_tip_display_name() : round_tip_display_name();
}

[[nodiscard]] QPixmap builtin_tip_thumbnail(const QString& id, int extent);

[[nodiscard]] QPixmap round_tip_thumbnail(int extent) {
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  // Same light paper chip as brush_tip_thumbnail (brush_tip_library.cpp), so the black ink
  // stays visible on the dark UI.
  painter.setPen(QColor(0, 0, 0, 70));
  painter.setBrush(QColor(0xE9, 0xE9, 0xE9));
  painter.drawRoundedRect(QRectF(0.5, 0.5, extent - 1.0, extent - 1.0), 3.5, 3.5);
  const auto margin = extent / 5;
  QRadialGradient gradient(QPointF(extent / 2.0, extent / 2.0), (extent - 2.0 * margin) / 2.0);
  gradient.setColorAt(0.0, QColor(0, 0, 0, 255));
  gradient.setColorAt(0.75, QColor(0, 0, 0, 255));
  gradient.setColorAt(1.0, QColor(0, 0, 0, 0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(gradient);
  painter.drawEllipse(margin, margin, extent - 2 * margin, extent - 2 * margin);
  return pixmap;
}

// The procedural Square: a hard black square on the same paper chip.
[[nodiscard]] QPixmap square_tip_thumbnail(int extent) {
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QColor(0, 0, 0, 70));
  painter.setBrush(QColor(0xE9, 0xE9, 0xE9));
  painter.drawRoundedRect(QRectF(0.5, 0.5, extent - 1.0, extent - 1.0), 3.5, 3.5);
  const auto margin = extent / 4;
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0, 0, 0));
  painter.drawRect(margin, margin, extent - 2 * margin, extent - 2 * margin);
  return pixmap;
}

QPixmap builtin_tip_thumbnail(const QString& id, int extent) {
  return id == builtin_square_brush_tip_id() ? square_tip_thumbnail(extent) : round_tip_thumbnail(extent);
}

}  // namespace

BrushTipPicker::BrushTipPicker(BrushTipLibrary& library, QWidget* parent)
    : QToolButton(parent), library_(library), current_tip_id_(builtin_round_brush_tip_id()) {
  setObjectName(QStringLiteral("brushTipPicker"));
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  setPopupMode(QToolButton::InstantPopup);
  setIconSize(QSize(kFaceIconExtent, kFaceIconExtent));
  popup_clock_.start();
  update_button_face();
  connect(this, &QToolButton::clicked, this, &BrushTipPicker::show_popup);
  connect(&library_, &BrushTipLibrary::changed, this, &BrushTipPicker::refresh);
}

void BrushTipPicker::set_current_tip_id(const QString& id) {
  working_name_.clear(); working_preview_ = {};
  const auto effective = id.isEmpty() ? builtin_round_brush_tip_id() : id;
  if (current_tip_id_ == effective) {
    update_button_face();
    return;
  }
  current_tip_id_ = effective;
  update_button_face();
}

const QString& BrushTipPicker::current_tip_id() const noexcept {
  return current_tip_id_;
}

void BrushTipPicker::refresh() {
  if (!working_preview_.isNull()) { update_button_face(); return; }
  if (!is_builtin_brush_tip_id(current_tip_id_) && library_.find_entry(current_tip_id_) == nullptr) {
    current_tip_id_ = builtin_round_brush_tip_id();
    emit tip_selected(current_tip_id_);
  }
  update_button_face();
}

// The width comes from the size hints, not setFixedWidth: polishing applies the stylesheet's
// QToolButton minimum over a fixed width, and the button then followed its text (issue 364).
// It depends on the font only, never on the current tip.
QSize BrushTipPicker::sizeHint() const {
  const QFontMetrics metrics(font());
  // About twelve average characters of name: "Brick Road" fits whole (issue 364's example).
  const int width = std::clamp(kFaceInset + kFaceIconExtent + kFaceTextGap + kChevronRoom + metrics.averageCharWidth() * 12,
                               105, 150);
  return {width, QToolButton::sizeHint().height()};
}

QSize BrushTipPicker::minimumSizeHint() const {
  return sizeHint();
}

// Reads like the bar's other fields whatever the base style does with a tool button's label:
// the field surface (the brushTipPicker QSS rule), the thumbnail, the elided name and a chevron
// pinned at the right.
void BrushTipPicker::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  option.initFrom(this);
  option.subControls = QStyle::SC_ToolButton;
  option.toolButtonStyle = Qt::ToolButtonTextOnly;
  painter.drawComplexControl(QStyle::CC_ToolButton, option);
  const QRect icon_rect(kFaceInset, (height() - kFaceIconExtent) / 2, kFaceIconExtent, kFaceIconExtent);
  icon().paint(&painter, icon_rect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
  const auto& colors = theme();
  const QRect text_rect = rect().adjusted(icon_rect.right() + 1 + kFaceTextGap, 0, -kChevronRoom, 0);
  painter.setPen(isEnabled() ? colors.text_primary : colors.field_text_disabled);
  painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(text(), Qt::ElideRight, text_rect.width()));
  const QRect chevron(width() - kChevronSize - kChevronMargin, (height() - kChevronSize) / 2, kChevronSize, kChevronSize);
  themed_svg_icon(QStringLiteral("chevron-down"))
      .paint(&painter, chevron, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
}

void BrushTipPicker::update_button_face() {
  if (!working_preview_.isNull()) {
    setIcon(QIcon(working_preview_));
    setText(working_name_);
    setToolTip(tr("Brush tip: %1").arg(working_name_));
    return;
  }
  if (is_builtin_brush_tip_id(current_tip_id_)) {
    setIcon(QIcon(builtin_tip_thumbnail(current_tip_id_, kThumbnailExtent)));
    setText(builtin_tip_display_name(current_tip_id_));
    setToolTip(tr("Brush tip: %1").arg(builtin_tip_display_name(current_tip_id_)));
    return;
  }
  const auto* entry = library_.find_entry(current_tip_id_);
  if (entry == nullptr) {
    setIcon(QIcon(round_tip_thumbnail(kThumbnailExtent)));
    setText(round_tip_display_name());
    setToolTip(tr("Brush tip: %1").arg(round_tip_display_name()));
    return;
  }
  setIcon(QIcon(brush_tip_thumbnail_with_badge(*entry)));
  setText(entry->name);
  auto tooltip = tr("Brush tip: %1 (%2×%3)").arg(entry->name).arg(entry->size.width()).arg(entry->size.height());
  if (brush_tip_entry_has_dynamics(*entry)) {
    tooltip += tr(" • dynamics");
  }
  setToolTip(tooltip);
}

void BrushTipPicker::set_working_preview(const QString& name, const QPixmap& image) {
  working_name_ = name; working_preview_ = image; update_button_face();
}

void BrushTipPicker::rebuild_popup_list(QListWidget* list, const QString& folder_filter) const {
  list->clear();
  QListWidgetItem* round_item = nullptr;
  if (folder_filter.isEmpty()) {
    // The two procedural brushes lead the list: Round, then Square right under it.
    for (const auto& id : {builtin_round_brush_tip_id(), builtin_square_brush_tip_id()}) {
      auto* item = new QListWidgetItem(QIcon(builtin_tip_thumbnail(id, kThumbnailExtent)), builtin_tip_display_name(id));
      item->setData(Qt::UserRole, id);
      item->setToolTip(builtin_tip_display_name(id));
      list->addItem(item);
      if (id == builtin_round_brush_tip_id()) {
        round_item = item;
      }
      if (id == current_tip_id_) {
        list->setCurrentItem(item);
      }
    }
  }
  for (const auto& entry : library_.entries()) {
    if (!folder_filter.isEmpty() && entry.folder != folder_filter) {
      continue;
    }
    auto* item = new QListWidgetItem(QIcon(brush_tip_thumbnail_with_badge(entry)), entry.name);
    item->setData(Qt::UserRole, entry.id);
    auto tooltip = entry.folder.isEmpty()
                       ? tr("%1 (%2×%3)").arg(entry.name).arg(entry.size.width()).arg(entry.size.height())
                       : tr("%1 - %2 (%3×%4)")
                             .arg(entry.folder, entry.name)
                             .arg(entry.size.width())
                             .arg(entry.size.height());
    if (brush_tip_entry_has_dynamics(entry)) {
      tooltip += tr(" • dynamics");
    }
    item->setToolTip(tooltip);
    list->addItem(item);
    if (entry.id == current_tip_id_) {
      list->setCurrentItem(item);
    }
  }
  if (list->currentItem() == nullptr && round_item != nullptr) {
    list->setCurrentItem(round_item);
  }
}

void BrushTipPicker::show_popup() {
  // Clicking the button while its popup is open must DISMISS it, not reopen it. The press that
  // closes the Qt::Popup is replayed onto the button, so by the time clicked() fires the popup
  // is either still closing (pointer alive) or was just destroyed (timestamp) — both mean this
  // click was the dismissal.
  if (popup_ != nullptr) {
    popup_->close();
    return;
  }
  if (popup_dismissed_ms_ >= 0 && popup_clock_.elapsed() - popup_dismissed_ms_ < 300) {
    popup_dismissed_ms_ = -1;
    return;
  }
  auto* popup = new ResizablePickerPopup(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("brushTipPickerPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] {
    // Arm the swallow window only when the popup died under a click on the button itself;
    // closing it by choosing a tip must not eat a quick legitimate reopen.
    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
      popup_dismissed_ms_ = popup_clock_.elapsed();
    }
  });

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  // Folder filter: fast navigation when many sets are installed. Empty data = show everything.
  auto* folder_combo = new QComboBox(popup);
  folder_combo->setObjectName(QStringLiteral("brushTipPickerFolderCombo"));
  folder_combo->addItem(tr("All Brushes"), QString());
  const auto folders = library_.folders();
  for (const auto& folder : folders) {
    folder_combo->addItem(folder, folder);
  }
  if (!popup_folder_filter_.isEmpty()) {
    const auto filter_index = folder_combo->findData(popup_folder_filter_);
    folder_combo->setCurrentIndex(std::max(0, filter_index));
  }
  folder_combo->setVisible(!folders.isEmpty());
  layout->addWidget(folder_combo);

  auto* list = new QListWidget(popup);
  list->setObjectName(QStringLiteral("brushTipPickerList"));
  list->setViewMode(QListView::IconMode);
  list->setIconSize(QSize(kThumbnailExtent, kThumbnailExtent));
  list->setGridSize(QSize(kThumbnailExtent + 22, kThumbnailExtent + 30));
  list->setResizeMode(QListView::Adjust);
  list->setMovement(QListView::Static);
  list->setWordWrap(true);
  list->setTextElideMode(Qt::ElideRight);
  list->setUniformItemSizes(true);
  // Resizable (was a fixed 5×4 grid): the list reflows with the popup, whose size persists.
  list->setMinimumSize(3 * (kThumbnailExtent + 22) + 28, 2 * (kThumbnailExtent + 30) + 8);
  rebuild_popup_list(list, folder_combo->currentData().toString());
  layout->addWidget(list, 1);

  connect(folder_combo, &QComboBox::currentIndexChanged, popup, [this, folder_combo, list](int) {
    popup_folder_filter_ = folder_combo->currentData().toString();
    rebuild_popup_list(list, popup_folder_filter_);
  });

  auto* buttons = new QHBoxLayout();
  auto* import_button = new QPushButton(tr("Import .abr…"), popup);
  import_button->setObjectName(QStringLiteral("brushTipImportButton"));
  import_button->setToolTip(tr("Import brushes from a Photoshop .abr file"));
  auto* define_button = new QPushButton(tr("New from Selection…"), popup);
  define_button->setObjectName(QStringLiteral("brushTipDefineButton"));
  define_button->setToolTip(
      tr("Create a brush tip from the current selection (or the whole image): dark pixels paint, "
         "light pixels stay clear"));
  auto* manage_button = new QPushButton(tr("Manage…"), popup);
  manage_button->setObjectName(QStringLiteral("brushTipManageButton"));
  buttons->addWidget(import_button);
  buttons->addWidget(define_button);
  buttons->addStretch(1);
  buttons->addWidget(manage_button);
  // A frameless popup has no native resize border; the grip is the resize handle.
  buttons->addWidget(new VisibleSizeGrip(popup), 0, Qt::AlignBottom);
  layout->addLayout(buttons);

  connect(list, &QListWidget::itemClicked, popup, [this, popup](QListWidgetItem* item) {
    if (item == nullptr) {
      return;
    }
    const auto id = item->data(Qt::UserRole).toString();
    popup->close();
    set_current_tip_id(id);
    emit tip_selected(current_tip_id_);
  });
  connect(import_button, &QPushButton::clicked, popup, [this, popup] {
    popup->close();
    emit import_requested();
  });
  connect(define_button, &QPushButton::clicked, popup, [this, popup] {
    popup->close();
    emit define_requested();
  });
  connect(manage_button, &QPushButton::clicked, popup, [this, popup] {
    popup->close();
    emit manage_requested();
  });

  popup->adjustSize();
  {
    // Restore the user's popup size; first-time default matches the classic 5×4 grid.
    const auto saved = app_settings().value(kPopupSizeSettingsKey).toSize();
    if (saved.isValid()) {
      popup->resize(saved.expandedTo(popup->minimumSizeHint()));
    } else {
      const QSize default_list_size(5 * (kThumbnailExtent + 22) + 28, 4 * (kThumbnailExtent + 30) + 8);
      const QSize minimum_list_size(3 * (kThumbnailExtent + 22) + 28, 2 * (kThumbnailExtent + 30) + 8);
      popup->resize(popup->size() + (default_list_size - minimum_list_size));
    }
  }
  position_popup_below(*this, *popup);
  popup->show();
}

}  // namespace patchy::ui
