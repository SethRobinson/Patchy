#include "ui/start_panel.hpp"

#include "ui/app_credits.hpp"
#include "ui/build_info.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/splash_artwork.hpp"

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
#include <initializer_list>

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

namespace patchy::ui {

namespace {

constexpr int kMaxRecentEntries = 8;
constexpr int kRecentRowHeight = 40;
constexpr int kRecentPathRole = Qt::UserRole + 1;

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
      painter->setBrush(selected ? theme().selection_soft_bg : theme().start_panel_row_hover_bg);
      painter->drawRoundedRect(row, 4.0, 4.0);
      painter->setRenderHint(QPainter::Antialiasing, false);
    }

    const QFileInfo info(index.data(kRecentPathRole).toString());
    const QRect text_area = row.adjusted(10, 3, -10, -3);
    const auto name_font = offset_font(option.font, 0, true);
    painter->setFont(name_font);
    painter->setPen(theme().text_primary);
    const QRect name_rect(text_area.left(), text_area.top(), text_area.width(), text_area.height() / 2);
    painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(info.fileName(), Qt::ElideMiddle, name_rect.width()));

    const auto path_font = offset_font(option.font, -1, false);
    painter->setFont(path_font);
    painter->setPen(theme().start_panel_muted_text);
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

  // About-style header: the logo card beside the title and tagline.
  auto* header_row = new QHBoxLayout();
  header_row->setSpacing(18);
  auto* artwork = new SplashArtwork(column);
  artwork->setFixedSize(110, 141);
  header_row->addStretch(1);
  header_row->addWidget(artwork);
  auto* header_text = new QVBoxLayout();
  header_text->setSpacing(4);
  auto* title = new QLabel(tr("Patchy Image Editor"), column);
  title->setObjectName(QStringLiteral("startPanelTitle"));
  auto* tagline = new QLabel(tr("Open source photo editing. Free forever, no subscriptions."), column);
  tagline->setObjectName(QStringLiteral("startPanelTagline"));
  tagline->setWordWrap(true);
  tagline->setMaximumWidth(240);
  header_text->addStretch(1);
  header_text->addWidget(title);
  header_text->addWidget(tagline);
  header_text->addStretch(1);
  header_row->addLayout(header_text);
  header_row->addStretch(1);
  column_layout->addLayout(header_row);
  column_layout->addSpacing(8);

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

#ifdef Q_OS_WASM
  // Web-only pitch for the native build. The privacy sentence is deliberate:
  // browser apps get assumed to upload, and this one never does.
  auto* wasm_note = new QLabel(column);
  wasm_note->setObjectName(QStringLiteral("startPanelWasmNote"));
  wasm_note->setTextFormat(Qt::RichText);
  wasm_note->setWordWrap(true);
  wasm_note->setAlignment(Qt::AlignHCenter);
  wasm_note->setTextInteractionFlags(Qt::TextBrowserInteraction);
  wasm_note->setOpenExternalLinks(true);
  const auto desktop_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                           "href=\"https://github.com/SethRobinson/Patchy#download\">%1</a>")
                                .arg(tr("desktop version"));
  set_themed_label_text(*wasm_note,
                        tr("Everything runs locally in your browser. Nothing you make is ever sent online.") +
                            QStringLiteral("<br/>") +
                            tr("For all your system fonts and better speed, get the %1.").arg(desktop_link));
  column_layout->addSpacing(2);
  column_layout->addWidget(wasm_note);
#endif

  // Dim footer pinned to the bottom of the panel: version, credit, links, and
  // the startup update-check status.
  auto* footer = new QVBoxLayout();
  footer->setSpacing(3);
  const auto add_footer_row = [footer](std::initializer_list<QWidget*> widgets) {
    auto* row = new QHBoxLayout();
    row->setSpacing(14);
    row->addStretch(1);
    for (auto* widget : widgets) {
      row->addWidget(widget);
    }
    row->addStretch(1);
    footer->addLayout(row);
  };

  auto* version = new QLabel(
      tr("Version %1 (built %2)").arg(QStringLiteral(PATCHY_VERSION), build_date_text()), this);
  version->setObjectName(QStringLiteral("startPanelVersion"));
  version->setTextFormat(Qt::PlainText);
  auto* credit = new QLabel(tr("Created by Seth A. Robinson"), this);
  credit->setObjectName(QStringLiteral("startPanelCredit"));
  credit->setTextFormat(Qt::PlainText);
  add_footer_row({version, credit});

  auto* contributors = new QLabel(this);
  contributors->setObjectName(QStringLiteral("startPanelContributors"));
  contributors->setTextFormat(Qt::RichText);
  set_themed_label_text(
      *contributors,
      tr("Code contributions from %1").arg(code_contributors_link_html(QStringLiteral("@link_text"))));
  contributors->setTextInteractionFlags(Qt::TextBrowserInteraction);
  contributors->setOpenExternalLinks(true);
  add_footer_row({contributors});

  const auto make_home_label = [this](const QString& text) {
    auto* label = new QLabel(this);
    label->setObjectName(QStringLiteral("startPanelHome"));
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    set_themed_label_text(*label, text);
    return label;
  };
  const auto github_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                          "href=\"https://github.com/SethRobinson/Patchy\">SethRobinson/Patchy</a>");
  const auto seth_site_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                             "href=\"https://rtsoft.com\">rtsoft.com</a>");
  add_footer_row({make_home_label(tr("GitHub: %1").arg(github_link)),
                  make_home_label(tr("Seth's site: %1").arg(seth_site_link))});

  update_status_label_ = new QLabel(this);
  update_status_label_->setObjectName(QStringLiteral("startPanelUpdateStatus"));
  update_status_label_->setTextFormat(Qt::PlainText);
  update_status_label_->setVisible(false);
  add_footer_row({update_status_label_});

  outer->addLayout(footer);

  connect(new_button, &QPushButton::clicked, this, &StartPanel::new_document_requested);
  connect(open_button, &QPushButton::clicked, this, &StartPanel::open_requested);
  connect(recent_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (item != nullptr) {
      emit recent_file_requested(item->data(kRecentPathRole).toString());
    }
  });

  set_themed_style(*this, QStringLiteral(R"(
    QWidget#startPanel {
      background: @window_bg;
    }
    QWidget#startPanelColumn {
      background: transparent;
    }
    QLabel#startPanelTitle {
      background: transparent;
      color: @start_panel_title_text;
      font-size: 30px;
      font-weight: 700;
    }
    QLabel#startPanelTagline {
      background: transparent;
      color: @start_panel_tagline_text;
      font-size: 12px;
    }
    QLabel#startPanelVersion, QLabel#startPanelCredit, QLabel#startPanelContributors, QLabel#startPanelHome {
      background: transparent;
      color: @start_panel_muted_text;
      font-size: 11px;
    }
    QLabel#startPanelUpdateStatus {
      background: transparent;
      color: @start_panel_status_text;
      font-size: 11px;
    }
    QLabel#startPanelRecentLabel {
      background: transparent;
      color: @start_panel_section_text;
      font-size: 11px;
      font-weight: 700;
      padding-left: 2px;
    }
    QLabel#startPanelHint, QLabel#startPanelWasmNote {
      background: transparent;
      color: @start_panel_hint_text;
      font-size: 11px;
    }
    QWidget#startPanel QPushButton {
      background: @button_bg;
      border: 1px solid @field_border;
      border-radius: 14px;
      color: @text_bright;
      min-width: 130px;
      min-height: 28px;
      padding: 0 18px;
    }
    QWidget#startPanel QPushButton:hover {
      background: @neutral_button_hover_bg;
      border-color: @neutral_button_hover_border;
    }
    QWidget#startPanel QPushButton#startPanelNewButton {
      background: @primary_bg;
      border: 1px solid @primary_border;
      font-weight: 700;
    }
    QWidget#startPanel QPushButton#startPanelNewButton:hover {
      background: @primary_hover_bg;
    }
    QListWidget#startPanelRecentList {
      background: @list_surface_bg;
      border: 1px solid @list_surface_border;
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

void StartPanel::set_update_status(const QString& text) {
  if (update_status_label_ == nullptr) {
    return;
  }
  update_status_label_->setText(text);
  update_status_label_->setVisible(!text.isEmpty());
}

}  // namespace patchy::ui
