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
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
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

// The real bound is kMaxRecentFiles in main_window_files.cpp, which is what trims
// the persisted list; this only keeps a hand-edited settings file from building
// thousands of rows. kVisibleRecentRows is what the box actually shows: the rest
// are reached by scrolling, so the box never grows however long the list gets.
constexpr int kMaxRecentEntries = 200;
constexpr int kVisibleRecentRows = 8;
constexpr int kRecentRowHeight = 40;
constexpr int kRecentListChrome = 14;  // the list's own frame and padding
constexpr int kRecentPathRole = Qt::UserRole + 1;

// A list as tall as its rows, capped at kVisibleRecentRows; longer lists scroll.
// setFixedHeight cannot express that: the application style sheet re-polishes the
// widget and drops the minimum that call sets, which left QAbstractScrollArea's
// default 192 px hint deciding the height and clipping every row past the fourth.
// A real sizeHint survives the re-polish, and the Maximum policy lets a short
// window shrink the box instead of pushing the panel footer off screen.
class RecentFileList final : public QListWidget {
 public:
  explicit RecentFileList(QWidget* parent) : QListWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  }

  QSize sizeHint() const override {
    return {QListWidget::sizeHint().width(),
            std::clamp(count(), 1, kVisibleRecentRows) * kRecentRowHeight + kRecentListChrome};
  }

  QSize minimumSizeHint() const override {
    return {QListWidget::minimumSizeHint().width(), kRecentRowHeight + kRecentListChrome};
  }
};

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
    const auto path = index.data(kRecentPathRole).toString();
    if (path.isEmpty()) {
      // The no-match placeholder: one dimmed line, and no hover or selection
      // chrome, because there is nothing here to click.
      painter->setPen(theme().start_panel_muted_text);
      painter->drawText(row.adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString());
      painter->restore();
      return;
    }

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    if (selected || hovered) {
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->setPen(Qt::NoPen);
      painter->setBrush(selected ? theme().selection_soft_bg : theme().start_panel_row_hover_bg);
      painter->drawRoundedRect(row, 4.0, 4.0);
      painter->setRenderHint(QPainter::Antialiasing, false);
    }

    const QFileInfo info(path);
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

  // Section header: the label on the left, the name filter riding the empty space
  // on the right so it lines up with the list's edge.
  auto* recent_header = new QHBoxLayout();
  recent_header->setSpacing(10);
  recent_label_ = new QLabel(tr("Recent Files"), column);
  recent_label_->setObjectName(QStringLiteral("startPanelRecentLabel"));
  recent_header->addWidget(recent_label_);
  recent_header->addStretch(1);
  recent_filter_edit_ = new QLineEdit(column);
  recent_filter_edit_->setObjectName(QStringLiteral("startPanelRecentFilterEdit"));
  recent_filter_edit_->setClearButtonEnabled(true);
  recent_filter_edit_->setPlaceholderText(tr("Filter recent files..."));
  recent_filter_edit_->setFixedHeight(24);
  recent_filter_edit_->setMinimumWidth(140);
  recent_filter_edit_->setMaximumWidth(230);
  recent_header->addWidget(recent_filter_edit_, 1);
  column_layout->addLayout(recent_header);

  recent_list_ = new RecentFileList(column);
  recent_list_->setObjectName(QStringLiteral("startPanelRecentList"));
  recent_list_->setSelectionMode(QAbstractItemView::NoSelection);
  recent_list_->setFocusPolicy(Qt::NoFocus);
  recent_list_->setMouseTracking(true);
  // The delegate elides both text lines to the row width, so only the vertical bar
  // is ever wanted; per-pixel scrolling keeps the wheel smooth over a long list.
  recent_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  recent_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  recent_list_->setUniformItemSizes(true);  // constant row height; skips a sizeHint per row
  recent_list_->setItemDelegate(new RecentFileDelegate(recent_list_));
  recent_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  // On the viewport, not the list: the scroll bar keeps a normal arrow cursor.
  recent_list_->viewport()->setCursor(Qt::PointingHandCursor);
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
                            tr("Drop a font file or a zip of fonts here to use your own fonts.") +
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
      tr("Version %1 (built %2)").arg(QStringLiteral(PATCHY_VERSION), build_timestamp_text()), this);
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
  connect(recent_filter_edit_, &QLineEdit::textChanged, this, [this] { rebuild_recent_rows(); });
  // Enter opens the top match, the way the Open Recent menu's filter row does.
  connect(recent_filter_edit_, &QLineEdit::returnPressed, this, [this] { open_first_recent_match(); });
  connect(recent_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    // An empty path is the no-match placeholder row, which opens nothing.
    if (item != nullptr && !item->data(kRecentPathRole).toString().isEmpty()) {
      emit recent_file_requested(item->data(kRecentPathRole).toString());
    }
  });
  // customContextMenuRequested reports viewport coordinates for a scroll area, which
  // is what both itemAt and the viewport's mapToGlobal want.
  connect(recent_list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
    auto* item = recent_list_->itemAt(position);
    if (item == nullptr || item->data(kRecentPathRole).toString().isEmpty()) {
      return;
    }
    emit recent_file_context_menu_requested(item->data(kRecentPathRole).toString(),
                                            recent_list_->viewport()->mapToGlobal(position));
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
    QLineEdit#startPanelRecentFilterEdit {
      background: @list_surface_bg;
      border: 1px solid @list_surface_border;
      border-radius: 5px;
      color: @text_primary;
      font-size: 11px;
      padding: 0 8px;
    }
    QLineEdit#startPanelRecentFilterEdit:focus {
      border-color: @accent_bright;
    }
  )"));
  set_recent_files({});
}

void StartPanel::set_recent_files(const QStringList& paths) {
  recent_paths_.clear();
  for (const auto& path : paths) {
    if (recent_paths_.size() >= kMaxRecentEntries) {
      break;
    }
    const QFileInfo info(path);
    if (!info.isFile()) {
      continue;  // Recent entries can outlive their files; dead rows would just error on click.
    }
    recent_paths_ << info.absoluteFilePath();
  }
  rebuild_recent_rows();
}

void StartPanel::rebuild_recent_rows() {
  recent_list_->clear();
  const auto tokens = recent_filter_edit_->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (const auto& path : recent_paths_) {
    const auto native_path = QDir::toNativeSeparators(path);
    // The Open Recent menu's matching rule: every token, anywhere in the whole
    // path, case-insensitive, so a folder name narrows as well as a file name.
    const bool matches = std::all_of(tokens.begin(), tokens.end(), [&native_path](const QString& token) {
      return native_path.contains(token, Qt::CaseInsensitive);
    });
    if (!matches) {
      continue;
    }
    auto* item = new QListWidgetItem(QFileInfo(path).fileName(), recent_list_);
    item->setData(kRecentPathRole, path);
    item->setToolTip(native_path);
  }

  const bool has_entries = !recent_paths_.isEmpty();
  if (has_entries && recent_list_->count() == 0) {
    // Keeps the box where it is while a filter hides every row, instead of
    // collapsing the section under the edit the user is typing into. The empty
    // path role is what marks this row as a placeholder everywhere else.
    auto* item = new QListWidgetItem(tr("No matching recent files"), recent_list_);
    item->setData(kRecentPathRole, QString());
  }
  recent_label_->setVisible(has_entries);
  recent_filter_edit_->setVisible(has_entries);
  recent_list_->setVisible(has_entries);
  recent_list_->updateGeometry();  // RecentFileList sizes itself from the new row count
}

void StartPanel::open_first_recent_match() {
  for (int index = 0; index < recent_list_->count(); ++index) {
    const auto path = recent_list_->item(index)->data(kRecentPathRole).toString();
    if (!path.isEmpty()) {
      emit recent_file_requested(path);
      return;
    }
  }
}

void StartPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  // The panel comes back with a clean filter, focused, so the next file can be
  // found by typing its name without clicking first.
  recent_filter_edit_->clear();
  if (!recent_paths_.isEmpty()) {
    recent_filter_edit_->setFocus(Qt::OtherFocusReason);
  }
}

void StartPanel::set_update_status(const QString& text) {
  if (update_status_label_ == nullptr) {
    return;
  }
  update_status_label_->setText(text);
  update_status_label_->setVisible(!text.isEmpty());
}

}  // namespace patchy::ui
