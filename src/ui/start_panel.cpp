#include "ui/start_panel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kMaxRecentEntries = 8;
constexpr int kRecentRowHeight = 40;
constexpr int kRecentPathRole = Qt::UserRole + 1;

QFont derived_font(QFont font, int pixel_delta, bool bold) {
  font.setBold(bold);
  if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(8, font.pixelSize() + pixel_delta));
  } else {
    font.setPointSizeF(std::max(7.0, font.pointSizeF() + pixel_delta));
  }
  return font;
}

// Two-line recent row: file name over its dimmed directory.
class RecentFileDelegate final : public QStyledItemDelegate {
 public:
  explicit RecentFileDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return QSize(option.rect.width(), kRecentRowHeight);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const QRect row = option.rect.adjusted(2, 2, -2, -2);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    if (selected || hovered) {
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->setPen(Qt::NoPen);
      painter->setBrush(selected ? QColor(0x33, 0x41, 0x4f) : QColor(0x32, 0x32, 0x32));
      painter->drawRoundedRect(row, 4.0, 4.0);
      painter->setRenderHint(QPainter::Antialiasing, false);
    }

    const QFileInfo info(index.data(kRecentPathRole).toString());
    const QRect text_area = row.adjusted(10, 3, -10, -3);
    const auto name_font = derived_font(option.font, 0, true);
    painter->setFont(name_font);
    painter->setPen(QColor(0xe6, 0xe6, 0xe6));
    const QRect name_rect(text_area.left(), text_area.top(), text_area.width(), text_area.height() / 2);
    painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(info.fileName(), Qt::ElideMiddle, name_rect.width()));

    const auto path_font = derived_font(option.font, -1, false);
    painter->setFont(path_font);
    painter->setPen(QColor(0x8b, 0x8b, 0x8b));
    const QRect path_rect(text_area.left(), text_area.top() + text_area.height() / 2, text_area.width(),
                          text_area.height() - text_area.height() / 2);
    painter->drawText(path_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(path_font).elidedText(QDir::toNativeSeparators(info.absolutePath()),
                                                         Qt::ElideMiddle, path_rect.width()));
    painter->restore();
  }
};

}  // namespace

StartPanel::StartPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("startPanel"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 24, 24, 24);

  auto* column = new QWidget(this);
  column->setObjectName(QStringLiteral("startPanelColumn"));
  column->setMaximumWidth(460);
  auto* column_layout = new QVBoxLayout(column);
  column_layout->setContentsMargins(0, 0, 0, 0);
  column_layout->setSpacing(14);

  outer->addStretch(3);
  auto* center_row = new QHBoxLayout();
  center_row->addStretch(1);
  center_row->addWidget(column);
  center_row->addStretch(1);
  outer->addLayout(center_row);
  outer->addStretch(4);

  auto* title = new QLabel(QStringLiteral("Patchy"), column);
  title->setObjectName(QStringLiteral("startPanelTitle"));
  title->setAlignment(Qt::AlignHCenter);
  column_layout->addWidget(title);

  auto* buttons_row = new QHBoxLayout();
  buttons_row->setSpacing(10);
  auto* new_button = new QPushButton(tr("New Document..."), column);
  new_button->setObjectName(QStringLiteral("startPanelNewButton"));
  new_button->setCursor(Qt::PointingHandCursor);
  auto* open_button = new QPushButton(tr("Open..."), column);
  open_button->setObjectName(QStringLiteral("startPanelOpenButton"));
  open_button->setCursor(Qt::PointingHandCursor);
  buttons_row->addStretch(1);
  buttons_row->addWidget(new_button);
  buttons_row->addWidget(open_button);
  buttons_row->addStretch(1);
  column_layout->addLayout(buttons_row);
  column_layout->addSpacing(6);

  recent_label_ = new QLabel(tr("Recent Files"), column);
  recent_label_->setObjectName(QStringLiteral("startPanelRecentLabel"));
  column_layout->addWidget(recent_label_);

  recent_list_ = new QListWidget(column);
  recent_list_->setObjectName(QStringLiteral("startPanelRecentList"));
  recent_list_->setSelectionMode(QAbstractItemView::NoSelection);
  recent_list_->setFocusPolicy(Qt::NoFocus);
  recent_list_->setMouseTracking(true);
  recent_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list_->setItemDelegate(new RecentFileDelegate(recent_list_));
  recent_list_->setCursor(Qt::PointingHandCursor);
  column_layout->addWidget(recent_list_);

  auto* hint = new QLabel(tr("You can also drop image files anywhere in the window"), column);
  hint->setObjectName(QStringLiteral("startPanelHint"));
  hint->setAlignment(Qt::AlignHCenter);
  column_layout->addSpacing(2);
  column_layout->addWidget(hint);

  connect(new_button, &QPushButton::clicked, this, &StartPanel::new_document_requested);
  connect(open_button, &QPushButton::clicked, this, &StartPanel::open_requested);
  connect(recent_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (item != nullptr) {
      emit recent_file_requested(item->data(kRecentPathRole).toString());
    }
  });

  setStyleSheet(QStringLiteral(R"(
    QWidget#startPanel {
      background: #262626;
    }
    QWidget#startPanelColumn {
      background: transparent;
    }
    QLabel#startPanelTitle {
      background: transparent;
      color: #e9e9e9;
      font-size: 30px;
      font-weight: 700;
      padding-bottom: 4px;
    }
    QLabel#startPanelRecentLabel {
      background: transparent;
      color: #9a9a9a;
      font-size: 11px;
      font-weight: 700;
      padding-left: 2px;
    }
    QLabel#startPanelHint {
      background: transparent;
      color: #7a7a7a;
      font-size: 11px;
    }
    QWidget#startPanel QPushButton {
      background: #3a3a3a;
      border: 1px solid #5a5a5a;
      border-radius: 14px;
      color: #f0f0f0;
      min-width: 130px;
      min-height: 28px;
      padding: 0 18px;
    }
    QWidget#startPanel QPushButton:hover {
      background: #454545;
      border-color: #7d7d7d;
    }
    QWidget#startPanel QPushButton#startPanelNewButton {
      background: #354960;
      border: 1px solid #6f9bd1;
      font-weight: 700;
    }
    QWidget#startPanel QPushButton#startPanelNewButton:hover {
      background: #3f5773;
    }
    QListWidget#startPanelRecentList {
      background: #222222;
      border: 1px solid #1b1b1b;
      border-radius: 5px;
      padding: 3px;
    }
  )"));
  set_recent_files({});
}

void StartPanel::set_recent_files(const QStringList& paths) {
  recent_list_->clear();
  for (const auto& path : paths) {
    if (recent_list_->count() >= kMaxRecentEntries) {
      break;
    }
    const QFileInfo info(path);
    if (!info.isFile()) {
      continue;  // Recent entries can outlive their files; dead rows would just error on click.
    }
    auto* item = new QListWidgetItem(info.fileName(), recent_list_);
    item->setData(kRecentPathRole, info.absoluteFilePath());
    item->setToolTip(QDir::toNativeSeparators(info.absoluteFilePath()));
  }
  const bool has_entries = recent_list_->count() > 0;
  recent_label_->setVisible(has_entries);
  recent_list_->setVisible(has_entries);
  // Rows + the list's own frame and padding; the scrollbars are off, so anything
  // tighter clips the last row's directory line.
  recent_list_->setFixedHeight(std::max(1, recent_list_->count()) * kRecentRowHeight + 14);
}

}  // namespace patchy::ui
