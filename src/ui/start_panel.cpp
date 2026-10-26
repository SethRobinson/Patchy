#include "ui/start_panel.hpp"

#include "ui/build_info.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_backdrop.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/splash_artwork.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/recent_file_list.hpp"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <initializer_list>

#include "patchy_version.hpp"

namespace patchy::ui {

namespace {

// The real bound is kMaxRecentFiles in main_window_files.cpp, which is what trims
// the persisted list; this only keeps a hand-edited settings file from building
// thousands of rows. kVisibleRecentRows is what the box actually shows: the rest
// are reached by scrolling, so the box never grows however long the list gets.
constexpr int kMaxRecentEntries = 200;
constexpr int kVisibleRecentRows = 8;

}  // namespace

StartPanel::StartPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("startPanel"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 24, 24, 24);
  auto* content_layout = outer;
#ifdef Q_OS_WASM
  // Keep the card and browser actions reachable at 100% scale in short windows.
  // The footer stays outside the scrolling content, as on the desktop panel.
  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("startPanelWebScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* scroll_content = new QWidget(scroll);
  scroll_content->setObjectName(QStringLiteral("startPanelWebContent"));
  content_layout = new QVBoxLayout(scroll_content);
  content_layout->setContentsMargins(0, 0, 0, 0);
  scroll->setWidget(scroll_content);
  outer->addWidget(scroll, 1);
#endif

  auto* column = new QWidget(this);
  column->setObjectName(QStringLiteral("startPanelColumn"));
  column->setMaximumWidth(460);
  auto* column_layout = new QVBoxLayout(column);
  column_layout->setContentsMargins(0, 0, 0, 0);
  column_layout->setSpacing(14);

  content_layout->addStretch(3);
  auto* center_row = new QHBoxLayout();
  center_row->addStretch(1);
  center_row->addWidget(column);
  center_row->addStretch(1);
  content_layout->addLayout(center_row);
  content_layout->addStretch(4);

  // About-style header: the logo card beside the title and tagline.
  auto* header_row = new QHBoxLayout();
  header_row->setSpacing(18);
  auto* artwork = new SplashArtwork(column);
  artwork->setObjectName(QStringLiteral("startPanelArtwork"));
  artwork->setFixedSize(136, 136);
  header_row->addStretch(1);
  header_row->addWidget(artwork);
  auto* header_text = new QVBoxLayout();
  header_text->setSpacing(4);
  auto* title = new QLabel(tr("Patchy Image Editor"), column);
  bind_translated_text(title, QT_TR_NOOP("Patchy Image Editor"), "patchy::ui::StartPanel");
  title->setObjectName(QStringLiteral("startPanelTitle"));
  title->setWordWrap(true);
  auto* tagline = new QLabel(tr("Open source photo editing. Free forever, no subscriptions."), column);
  bind_translated_text(tagline, QT_TR_NOOP("Open source photo editing. Free forever, no subscriptions."), "patchy::ui::StartPanel");
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
  bind_translated_text(new_button, QT_TR_NOOP("New Document..."), "patchy::ui::StartPanel");
  new_button->setObjectName(QStringLiteral("startPanelNewButton"));
  new_button->setCursor(Qt::PointingHandCursor);
  auto* open_button = new QPushButton(tr("Open..."), column);
  bind_translated_text(open_button, QT_TR_NOOP("Open..."), "patchy::ui::StartPanel");
  open_button->setObjectName(QStringLiteral("startPanelOpenButton"));
  open_button->setCursor(Qt::PointingHandCursor);
  buttons_row->addStretch(1);
  buttons_row->addWidget(new_button);
  buttons_row->addWidget(open_button);
  buttons_row->addStretch(1);
  column_layout->addLayout(buttons_row);
  column_layout->addSpacing(6);

#ifdef Q_OS_WASM
  auto* desktop_card = new QWidget(column);
  desktop_card->setObjectName(QStringLiteral("startPanelDesktopCard"));
  desktop_card->setAttribute(Qt::WA_StyledBackground);
  auto* desktop_layout = new QVBoxLayout(desktop_card);
  desktop_layout->setContentsMargins(20, 20, 20, 20);
  desktop_layout->setSpacing(12);
  const auto add_desktop_label = [desktop_card, desktop_layout](const char* source, const char* name) {
    auto* label = new QLabel(tr(source), desktop_card);
    label->setObjectName(QString::fromLatin1(name));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    bind_translated_text(label, source, "patchy::ui::StartPanel");
    desktop_layout->addWidget(label);
    return label;
  };
  add_desktop_label(QT_TR_NOOP("More power on your desktop"), "startPanelDesktopTitle");
  add_desktop_label(QT_TR_NOOP("More features, nearly 400 G'MIC filters, faster editing, and full access to your "
                               "system fonts."),
                    "startPanelDesktopDescription");

  auto* download_button = new QPushButton(desktop_card);
  download_button->setObjectName(QStringLiteral("startPanelDesktopDownloadButton"));
  download_button->setCursor(Qt::PointingHandCursor);
  download_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  // A wrapping label keeps long translations inside the button at narrow widths.
  // The button retains normal keyboard activation and owns the accessible name.
  auto* download_layout = new QVBoxLayout(download_button);
  download_layout->setContentsMargins(16, 10, 16, 10);
  auto* download_label = new QLabel(tr("Download Patchy for Desktop"), download_button);
  download_label->setObjectName(QStringLiteral("startPanelDesktopDownloadLabel"));
  download_label->setTextFormat(Qt::PlainText);
  download_label->setWordWrap(true);
  download_label->setAlignment(Qt::AlignCenter);
  download_label->setAttribute(Qt::WA_TransparentForMouseEvents);
  bind_translated_text(download_label, QT_TR_NOOP("Download Patchy for Desktop"), "patchy::ui::StartPanel");
  download_layout->addWidget(download_label);
  retranslation_callbacks_.push_back([download_button, download_label] {
    download_button->setAccessibleName(download_label->text());
  });
  connect(download_button, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy#download")));
  });
  desktop_layout->addWidget(download_button);
  add_desktop_label(QT_TR_NOOP("Free · Windows, macOS & Linux"), "startPanelDesktopPlatforms");
  column_layout->addWidget(desktop_card);
#endif

  // Section header: the label on the left, the name filter riding the empty space
  // on the right so it lines up with the list's edge.
  auto* recent_header = new QHBoxLayout();
  recent_header->setSpacing(10);
  recent_label_ = new QLabel(tr("Recent Files"), column);
  bind_translated_text(recent_label_, QT_TR_NOOP("Recent Files"), "patchy::ui::StartPanel");
  recent_label_->setObjectName(QStringLiteral("startPanelRecentLabel"));
  recent_header->addWidget(recent_label_);
  recent_header->addStretch(1);
  recent_filter_edit_ = new QLineEdit(column);
  recent_filter_edit_->setObjectName(QStringLiteral("startPanelRecentFilterEdit"));
  recent_filter_edit_->setClearButtonEnabled(true);
  recent_filter_edit_->setPlaceholderText(tr("Filter recent files..."));
  bind_translated_text(recent_filter_edit_, QT_TR_NOOP("Filter recent files..."), "patchy::ui::StartPanel");
  recent_filter_edit_->setFixedHeight(24);
  recent_filter_edit_->setMinimumWidth(140);
  recent_filter_edit_->setMaximumWidth(230);
  recent_header->addWidget(recent_filter_edit_, 1);
  column_layout->addLayout(recent_header);

  auto* recent_list = new RecentFileList(column, kVisibleRecentRows);
  recent_list_ = recent_list;
  recent_list_->setObjectName(QStringLiteral("startPanelRecentList"));
  retranslation_callbacks_.push_back([this, recent_list] {
    recent_list->set_remove_handler([this](const QString& path) { emit recent_file_remove_requested(path); },
                                    tr("Remove from Recent Files"));
  });
  recent_list_->setSelectionMode(QAbstractItemView::NoSelection);
  recent_list_->setFocusPolicy(Qt::NoFocus);
  column_layout->addWidget(recent_list_);

  auto* hint = new QLabel(tr("You can also drop image files anywhere in the window"), column);
  bind_translated_text(hint, QT_TR_NOOP("You can also drop image files anywhere in the window"), "patchy::ui::StartPanel");
  hint->setObjectName(QStringLiteral("startPanelHint"));
  hint->setAlignment(Qt::AlignHCenter);
#ifdef Q_OS_WASM
  hint->setWordWrap(true);
#endif
  column_layout->addSpacing(2);
  column_layout->addWidget(hint);

#ifdef Q_OS_WASM
  // Browser guidance. The privacy sentence is deliberate:
  // browser apps get assumed to upload, and this one never does.
  auto* wasm_note = new QLabel(column);
  wasm_note->setObjectName(QStringLiteral("startPanelWasmNote"));
  wasm_note->setTextFormat(Qt::RichText);
  wasm_note->setWordWrap(true);
  wasm_note->setAlignment(Qt::AlignHCenter);
  wasm_note->setTextInteractionFlags(Qt::TextBrowserInteraction);
  wasm_note->setOpenExternalLinks(true);
  retranslation_callbacks_.push_back([wasm_note] {
    set_themed_label_text(*wasm_note,
                          tr("Everything runs locally in your browser. Nothing you make is ever sent online.") +
                              QStringLiteral("<br/>") +
                              tr("Drop a font file or a zip of fonts here to use your own fonts."));
  });
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

  auto* version = new QLabel(this);
  retranslation_callbacks_.push_back([version] {
    version->setText(tr("Version %1 (built %2)").arg(QStringLiteral(PATCHY_VERSION), build_timestamp_text()));
  });
  version->setObjectName(QStringLiteral("startPanelVersion"));
  version->setTextFormat(Qt::PlainText);
  // The credit links to Seth's GitHub profile, in the themed link color like the rows below.
  auto* credit = new QLabel(this);
  credit->setObjectName(QStringLiteral("startPanelCredit"));
  credit->setTextFormat(Qt::RichText);
  credit->setTextInteractionFlags(Qt::TextBrowserInteraction);
  credit->setOpenExternalLinks(true);
  retranslation_callbacks_.push_back([credit] {
    set_themed_label_text(
        *credit, tr("Created by %1")
                     .arg(QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                         "href=\"https://github.com/SethRobinson\">Seth A. Robinson</a>")));
  });
  // The contributor credits live in the About dialog only: every name added
  // here would take a row from the recent-files list (Seth, October 2026). The
  // version gets its own row so the number stands alone; the author follows.
  add_footer_row({version});
  add_footer_row({credit});

  const auto make_home_label = [this](const char* source, const QString& link) {
    auto* label = new QLabel(this);
    label->setObjectName(QStringLiteral("startPanelHome"));
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    retranslation_callbacks_.push_back([label, source, link] {
      set_themed_label_text(*label, tr(source).arg(link));
    });
    return label;
  };
  const auto github_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                          "href=\"https://github.com/SethRobinson/Patchy\">SethRobinson/Patchy</a>");
  const auto seth_site_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                             "href=\"https://rtsoft.com\">rtsoft.com</a>");
  add_footer_row({make_home_label(QT_TR_NOOP("GitHub: %1"), github_link),
                  make_home_label(QT_TR_NOOP("Seth's site: %1"), seth_site_link)});

  update_status_label_ = new QLabel(this);
  update_status_label_->setObjectName(QStringLiteral("startPanelUpdateStatus"));
  update_status_label_->setTextFormat(Qt::PlainText);
  update_status_label_->setVisible(false);
  add_footer_row({update_status_label_});

  outer->addLayout(footer);
  for (const auto& callback : retranslation_callbacks_) callback();

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
    QWidget#startPanelColumn, QWidget#startPanelArtwork {
      background: transparent;
    }
    QScrollArea#startPanelWebScroll, QWidget#startPanelWebContent {
      background: @window_bg;
      border: none;
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
    QLabel#startPanelCredit, QLabel#startPanelHome {
      background: transparent;
      color: @start_panel_muted_text;
      font-size: 11px;
    }
    QLabel#startPanelVersion {
      /* The version is what a bug report needs first: larger and brighter than
         the rest of the footer (Seth, October 2026). */
      background: transparent;
      color: @start_panel_tagline_text;
      font-size: 14px;
      font-weight: 600;
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
    QWidget#startPanelDesktopCard {
      background: @list_surface_bg;
      border: 1px solid @list_surface_border;
      border-radius: 12px;
    }
    QWidget#startPanelDesktopCard QLabel {
      background: transparent;
      color: @text_primary;
      font-size: 14px;
    }
    QWidget#startPanelDesktopCard QLabel#startPanelDesktopTitle {
      color: @start_panel_title_text;
      font-size: 20px;
      font-weight: 700;
    }
    QWidget#startPanelDesktopCard QLabel#startPanelDesktopPlatforms {
      color: @start_panel_tagline_text;
      font-size: 12px;
    }
    QWidget#startPanel QPushButton#startPanelDesktopDownloadButton {
      background: @primary_bg;
      border: 2px solid @primary_border;
      border-radius: 10px;
      min-width: 0;
      min-height: 52px;
      padding: 0;
    }
    QWidget#startPanelDesktopCard QLabel#startPanelDesktopDownloadLabel {
      color: @text_bright;
      font-size: 16px;
      font-weight: 700;
    }
    QWidget#startPanel QPushButton#startPanelDesktopDownloadButton:hover,
    QWidget#startPanel QPushButton#startPanelDesktopDownloadButton:pressed {
      background: @primary_hover_bg;
    }
    QWidget#startPanel QPushButton#startPanelDesktopDownloadButton:focus {
      border-color: @accent_bright;
      border-style: dashed;
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
    // No stat here: MainWindow drops missing entries after its background
    // existence check, and a click on one that vanished since reports it.
    recent_paths_ << QFileInfo(path).absoluteFilePath();
  }
  rebuild_recent_rows();
}

void StartPanel::rebuild_recent_rows() {
  fill_recent_file_list(*recent_list_, recent_paths_, recent_filter_edit_->text(), tr("No matching recent files"));
  const bool has_entries = !recent_paths_.isEmpty();
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

void StartPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() != QEvent::LanguageChange) return;
  for (auto* child : findChildren<QObject*>()) apply_bound_translation(child);
  for (const auto& callback : retranslation_callbacks_) callback();
  rebuild_recent_rows();
}

void StartPanel::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  QStyleOption option;
  option.initFrom(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
  paint_theme_backdrop(painter, event != nullptr ? event->rect() : rect(), *this, theme().canvas_backdrop);
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
