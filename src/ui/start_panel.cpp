#include "ui/start_panel.hpp"

#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/desktop_only_feature.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_backdrop.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/splash_artwork.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/recent_file_list.hpp"
#include "ui/recent_thumbnails.hpp"
#include "ui/ui_text_size.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFont>
#include <QButtonGroup>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include "patchy_version.hpp"

namespace patchy::ui {

namespace {

// The real bound is kMaxRecentFiles in main_window_files.cpp, which is what trims
// the persisted list; this only keeps a hand-edited settings file from building
// thousands of rows. The Recent Files box asks for the height of ALL its entries and
// the panel's layout gives it what is free down to the bottom margin: a tall window shows
// more of them before the box scrolls, and a box with few entries is only as tall as
// they need (GitHub issue 750 follow-up; it was a fixed 8 rows, or 3 rows of cells).
constexpr int kMaxRecentEntries = 200;

constexpr int kArtworkSize = 136;
constexpr int kHeaderSpacing = 18;
// The cap of the compact block at the top: the header and the three buttons. Its real width
// is what its widest row asks for, which is the header: the logo, the gap and the widest of
// its text rows on one line. In English at the default text size that is the credit line
// (the credit with the website, a gap, the repository), then the tagline (313 px);
// ui_start_panel_tagline_takes_one_line_and_wraps_when_narrow prints the widths. A longer
// translation or a larger text size wraps the tagline and folds the credit line at this cap
// instead of widening the block further.
constexpr int kColumnMaxWidth = 560;
// Between the credit and the repository on the header's credit line, and between its two
// lines when it folds.
constexpr int kCreditLineGap = 14;
constexpr int kCreditLineFoldGap = 2;
// The Recent Files section is as wide as the panel, up to these caps (at the default text
// size). The grid uses most of a wide window: nine cells of about 210 px at the cap, so cells
// do not sprawl from edge to edge of an ultrawide screen. The list stops sooner: a row that
// long shows the whole of nearly any path, and longer ones are hard to read across.
// (The browser build keeps no recent files and leaves the section in the column.)
#ifndef Q_OS_WASM
constexpr int kRecentGridSectionMaxWidth = 1900;
constexpr int kRecentListSectionMaxWidth = 900;
// Space between the buttons and the section.
constexpr int kRecentSectionTopGap = 28;
#endif
// Space over the update status line at the bottom of the panel, while it shows.
constexpr int kUpdateStatusTopGap = 10;

// A word-wrapped label whose preferred size is its text on ONE line. QLabel's own hint for
// wrapped text is a narrower box of several lines, so a layout never offered the tagline the
// line it fits on (GitHub issue 750). With this hint the text takes one line whenever the
// column has the room and still wraps when a long translation, a large text size or a narrow
// window leaves less: the minimum stays QLabel's, the width of the longest word.
class OneLineFirstLabel final : public QLabel {
 public:
  using QLabel::QLabel;

  QSize sizeHint() const override {
    ensurePolished();
    const auto margins = contentsMargins();
    int advance = 0;
    if (textFormat() == Qt::RichText) {
      // What the text needs unwrapped, laid out the way the label lays it out (no margin).
      QTextDocument document;
      document.setDefaultFont(font());
      document.setDocumentMargin(0);
      document.setHtml(text());
      advance = static_cast<int>(std::ceil(document.idealWidth()));
    } else {
      advance = fontMetrics().horizontalAdvance(text());
    }
    // Two pixels of slack: the advance and the wrapped bounding box round differently.
    const int line = advance + margins.left() + margins.right() + 2 * margin() + 2;
    return {line, heightForWidth(line)};
  }
};

// The header's credit line: its parts side by side with a gap while the row has room for all
// of them, else one under the other at the same left edge, each wrapping inside the row if it
// is narrower still. It asks for the one line first, like the labels above. (FlowLayout, which
// the Options bar uses, gives every item its full width and so cut a part wider than the row.)
class CreditLineLayout final : public QLayout {
 public:
  using QLayout::QLayout;
  ~CreditLineLayout() override {
    for (auto* item : items_) {
      delete item;
    }
  }

  void addItem(QLayoutItem* item) override { items_.push_back(item); }
  int count() const override { return static_cast<int>(items_.size()); }
  QLayoutItem* itemAt(int index) const override {
    return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)] : nullptr;
  }
  QLayoutItem* takeAt(int index) override {
    if (index < 0 || index >= count()) {
      return nullptr;
    }
    auto* item = items_[static_cast<std::size_t>(index)];
    items_.erase(items_.begin() + index);
    return item;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    arrange(rect, true);
  }
  QSize sizeHint() const override {
    const int line = one_line_width();
    return {line, arrange(QRect(0, 0, line, 0), false)};
  }
  QSize minimumSize() const override {
    QSize size;
    for (const auto* item : items_) {
      size = size.expandedTo(item->minimumSize());
    }
    return size;
  }

 private:
  int one_line_width() const {
    int line = 0;
    for (const auto* item : items_) {
      line += item->sizeHint().width();
    }
    return line + kCreditLineGap * std::max(0, count() - 1);
  }

  // Places the parts in `rect` (when `apply`) and returns the height they take.
  int arrange(const QRect& rect, bool apply) const {
    const bool one_line = one_line_width() <= rect.width();
    int x = rect.x();
    int y = rect.y();
    int line_height = 0;
    for (auto* item : items_) {
      const int width = std::min(item->sizeHint().width(), std::max(rect.width(), 1));
      const int height = item->hasHeightForWidth() ? item->heightForWidth(width) : item->sizeHint().height();
      if (apply) {
        item->setGeometry(QRect(x, y, width, height));
      }
      if (one_line) {
        x += width + kCreditLineGap;
        line_height = std::max(line_height, height);
      } else {
        y += height + kCreditLineFoldGap;
      }
    }
    return one_line ? line_height : std::max(0, y - rect.y() - kCreditLineFoldGap);
  }

  std::vector<QLayoutItem*> items_;
};

}  // namespace

StartPanel::StartPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("startPanel"));
  // A click on the empty panel (or a label on it) takes focus out of the recent
  // filter, so Ctrl+V reaches Edit > Paste and opens a copied file path. Qt gives
  // click focus to the nearest ancestor that accepts it; Tab never stops here.
  setFocusPolicy(Qt::ClickFocus);

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
  column->setMaximumWidth(ui_text_length(kColumnMaxWidth));
  // No minimum width of its own: the panel's layout works out the block's height for the
  // width it really gives it. With the layout's own minimum (the widest row that cannot
  // shrink) a panel narrower than that measured the wrapped title at a width it never got and
  // cut its last line.
  column->setMinimumWidth(1);
  auto* column_layout = new QVBoxLayout(column);
  column_layout->setContentsMargins(0, 0, 0, 0);
  column_layout->setSpacing(14);

#ifndef Q_OS_WASM
  // The panel's rows sit flush: the one gap between the block and the section is spelled out
  // below, so the section's box can run down to the bottom margin with nothing under it.
  const int row_gap = content_layout->spacing();
  content_layout->setSpacing(0);
#endif
  content_layout->addStretch(3);
  auto* center_row = new QHBoxLayout();
  center_row->addStretch(1);
  center_row->addWidget(column);
  center_row->addStretch(1);
  content_layout->addLayout(center_row);
#ifndef Q_OS_WASM
  content_layout->addSpacing(std::max(row_gap, 0));
#endif
  // The Recent Files section and the stretch under it follow once they are built (below).

  // About-style header: the logo card beside the title, the version, the tagline and one credit
  // line (GitHub issue 750 moved the credit and the links up from the footer, then onto one
  // line, then the version up under the title).
  auto* header_row = new QHBoxLayout();
  header_row->setSpacing(kHeaderSpacing);
  auto* artwork = new SplashArtwork(column);
  artwork->setObjectName(QStringLiteral("startPanelArtwork"));
  artwork->setFixedSize(kArtworkSize, kArtworkSize);
  header_row->addStretch(1);
  header_row->addWidget(artwork);
  auto* header_text = new QVBoxLayout();
  header_text->setSpacing(4);
  auto* title = new OneLineFirstLabel(tr("Patchy Image Editor"), column);
  bind_translated_text(title, QT_TR_NOOP("Patchy Image Editor"), "patchy::ui::StartPanel");
  title->setObjectName(QStringLiteral("startPanelTitle"));
  title->setWordWrap(true);
  auto* tagline = new OneLineFirstLabel(tr("Open source photo editing. Free forever, no subscriptions."), column);
  bind_translated_text(tagline, QT_TR_NOOP("Open source photo editing. Free forever, no subscriptions."), "patchy::ui::StartPanel");
  tagline->setObjectName(QStringLiteral("startPanelTagline"));
  tagline->setWordWrap(true);

  // The version and build date sit directly under the title (Seth, November 26, 2026; before
  // that they had a row of their own at the bottom of the panel). It is what a bug report needs
  // first, so it reads as part of the title block: one line while there is room, wrapping like
  // the tagline when there is less.
  auto* version = new OneLineFirstLabel(column);
  version->setObjectName(QStringLiteral("startPanelVersion"));
  version->setTextFormat(Qt::PlainText);
  version->setWordWrap(true);
  retranslation_callbacks_.push_back([version] {
    version->setText(tr("Version %1 (built %2)").arg(QStringLiteral(PATCHY_VERSION), build_timestamp_text()));
    version->updateGeometry();
  });

  // One line under the tagline (Seth, November 2026):
  //   Created by Seth A. Robinson (rtsoft.com)   GitHub: SethRobinson/Patchy
  // The name is plain text; the website and the repository are the two links, in the themed
  // link color. The existing "Created by %1" and "GitHub: %1" translations are reused: the
  // name with the website in parentheses goes into the credit's %1. The contributor credits
  // live in the About dialog only (Seth, October 2026).
  const auto make_credit_label = [this, column](const char* object_name, const char* source, const QString& argument) {
    auto* label = new OneLineFirstLabel(column);
    label->setObjectName(QString::fromLatin1(object_name));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);  // only when the row is narrower than this part alone
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    retranslation_callbacks_.push_back([label, source, argument] {
      set_themed_label_text(*label, tr(source).arg(argument));
      label->updateGeometry();
    });
    return label;
  };
  const auto website_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                           "href=\"https://rtsoft.com\">rtsoft.com</a>");
  const auto github_link = QStringLiteral("<a style=\"color:@link_text; text-decoration:none;\" "
                                          "href=\"https://github.com/SethRobinson/Patchy\">SethRobinson/Patchy</a>");
  // The two parts share the line and fold onto two, between them, when the header is narrower
  // than both (a small window, a larger text size, a long translation).
  auto* home_row = new QWidget(column);
  home_row->setObjectName(QStringLiteral("startPanelHomeRow"));
  auto* home_layout = new CreditLineLayout(home_row);
  home_layout->setContentsMargins(0, 0, 0, 0);
  home_layout->addWidget(make_credit_label("startPanelCredit", QT_TR_NOOP("Created by %1"),
                                           QStringLiteral("Seth A. Robinson (%1)").arg(website_link)));
  home_layout->addWidget(make_credit_label("startPanelHome", QT_TR_NOOP("GitHub: %1"), github_link));

  header_text->addStretch(1);
  header_text->addWidget(title);
  header_text->addWidget(version);
  header_text->addSpacing(2);
  header_text->addWidget(tagline);
  header_text->addSpacing(4);
  header_text->addWidget(home_row);
  header_text->addStretch(1);
  header_row->addLayout(header_text);
  header_row->addStretch(1);
  column_layout->addLayout(header_row);
  column_layout->addSpacing(8);

  auto* buttons_row = new QHBoxLayout();
  buttons_row->setSpacing(10);
  auto* new_button = new QPushButton(tr("New..."), column);
  bind_translated_text(new_button, QT_TR_NOOP("New..."), "patchy::ui::StartPanel");
  new_button->setObjectName(QStringLiteral("startPanelNewButton"));
  new_button->setCursor(Qt::PointingHandCursor);
  // Runs File > Open from Clipboard and follows its enabled state (GitHub issue 626);
  // MainWindow wires both through set_clipboard_available and open_clipboard_requested.
  // The browser build hides that command (its clipboard cache can be stale), so the
  // button is hidden there too.
  clipboard_button_ = new QPushButton(tr("Clipboard"), column);
  bind_translated_text(clipboard_button_, QT_TR_NOOP("Clipboard"), "patchy::ui::StartPanel");
  clipboard_button_->setToolTip(tr("New document from the image on the clipboard"));
  bind_translated_tooltip(clipboard_button_, QT_TR_NOOP("New document from the image on the clipboard"),
                          "patchy::ui::StartPanel");
  clipboard_button_->setObjectName(QStringLiteral("startPanelClipboardButton"));
  clipboard_button_->setCursor(Qt::PointingHandCursor);
  clipboard_button_->setEnabled(false);
#ifdef Q_OS_WASM
  clipboard_button_->setVisible(false);
#endif
  auto* open_button = new QPushButton(tr("Open..."), column);
  bind_translated_text(open_button, QT_TR_NOOP("Open..."), "patchy::ui::StartPanel");
  open_button->setObjectName(QStringLiteral("startPanelOpenButton"));
  open_button->setCursor(Qt::PointingHandCursor);
  buttons_row->addWidget(new_button);
  buttons_row->addWidget(clipboard_button_);
  buttons_row->addWidget(open_button);
  column_layout->addLayout(buttons_row);
  // Centered as a row of its own width (no stretches, whose spacing cost 20 px): the three
  // buttons fit the panel of an 800 px window, where the last one used to be cut.
  column_layout->setAlignment(buttons_row, Qt::AlignHCenter);
#ifdef Q_OS_WASM
  column_layout->addSpacing(6);
#endif

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
  connect(download_button, &QPushButton::clicked, this, [] { open_desktop_download_page(); });
  desktop_layout->addWidget(download_button);
  add_desktop_label(QT_TR_NOOP("Free · Windows, macOS & Linux"), "startPanelDesktopPlatforms");
  column_layout->addWidget(desktop_card);
#endif

  // The Recent Files section: its header row over the box. On the desktop it is its own row
  // of the panel, under the compact block, and grows with the window in both directions (see
  // the end of this constructor); the browser build, which keeps no recent files, leaves it in
  // the column where it always was.
  recent_section_ = new QWidget(this);
  recent_section_->setObjectName(QStringLiteral("startPanelRecentSection"));
  auto* section_layout = new QVBoxLayout(recent_section_);
  section_layout->setSpacing(14);
#ifdef Q_OS_WASM
  section_layout->setContentsMargins(0, 0, 0, 0);
#else
  section_layout->setContentsMargins(0, kRecentSectionTopGap, 0, 0);
  // Never taller than its entries need (Maximum: the size hint is the ceiling), as wide as
  // the row lets it be up to the cap update_recent_section_width sets.
  recent_section_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
#endif

  // Section header: the label on the left, then the name filter riding the empty space
  // and the List and Grid view buttons, which end at the list's right edge.
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
  recent_filter_edit_->setFixedHeight(ui_text_length(24));
  recent_filter_edit_->installEventFilter(this);
  recent_filter_edit_->setMinimumWidth(ui_text_length(140));
  recent_filter_edit_->setMaximumWidth(ui_text_length(230));
  recent_header->addWidget(recent_filter_edit_, 1);
  // The view pair every list-or-grid picker in Patchy has (the Shapes picker, Layer Style >
  // Styles): two text buttons, the current view's checked.
  const auto make_view_button = [column](const char* name, const char* text, const char* tooltip) {
    auto* button = new QToolButton(column);
    button->setObjectName(QString::fromLatin1(name));
    button->setText(tr(text));
    bind_translated_text(button, text, "patchy::ui::StartPanel");
    button->setToolTip(tr(tooltip));
    bind_translated_tooltip(button, tooltip, "patchy::ui::StartPanel");
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(ui_text_length(24));
    return button;
  };
  recent_list_view_button_ =
      make_view_button("startPanelRecentListViewButton", QT_TR_NOOP("List"),
                       QT_TR_NOOP("Show the recent files as a list with their folders"));
  recent_grid_view_button_ =
      make_view_button("startPanelRecentGridViewButton", QT_TR_NOOP("Grid"),
                       QT_TR_NOOP("Show the recent files as a grid of thumbnails"));
  auto* view_buttons = new QButtonGroup(this);
  view_buttons->setExclusive(true);
  view_buttons->addButton(recent_list_view_button_);
  view_buttons->addButton(recent_grid_view_button_);
  auto* view_row = new QHBoxLayout();
  view_row->setSpacing(2);
  view_row->addWidget(recent_list_view_button_);
  view_row->addWidget(recent_grid_view_button_);
  recent_header->addLayout(view_row);
  section_layout->addLayout(recent_header);

  recent_thumbnails_ = new RecentThumbnailCache(this);
  recent_list_ = new RecentFileList(recent_section_, kMaxRecentEntries);
  recent_list_->setObjectName(QStringLiteral("startPanelRecentList"));
  retranslation_callbacks_.push_back([this] {
    recent_list_->set_remove_handler([this](const QString& path) { emit recent_file_remove_requested(path); },
                                     tr("Remove from Recent Files"));
  });
  // A picture arrived from the worker: its own cell repaints, nothing else.
  connect(recent_thumbnails_, &RecentThumbnailCache::thumbnail_changed, this,
          [this](const QString& path) {
            // The cache names a picture by its cleaned path; a row whose stored text differs
            // (a path with ".." in it) is not found by name and repaints with the viewport.
            if (!recent_list_->update_path(path)) {
              recent_list_->viewport()->update();
            }
          });
  connect(recent_list_view_button_, &QToolButton::clicked, this, [this] { set_recent_grid_view(false); });
  connect(recent_grid_view_button_, &QToolButton::clicked, this, [this] { set_recent_grid_view(true); });
#ifndef Q_OS_WASM
  recent_grid_view_ = stored_start_panel_recent_grid_view();
#endif
  recent_list_->setSelectionMode(QAbstractItemView::NoSelection);
  recent_list_->setFocusPolicy(Qt::NoFocus);
  section_layout->addWidget(recent_list_);
#ifdef Q_OS_WASM
  column_layout->addWidget(recent_section_);
#endif

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
#else
  // How the panel uses its height (GitHub issue 750 follow-up): the compact block, then the
  // section, between a stretch of 3 above and one of 4 below. The section's box asks for the
  // height of all its entries and may shrink to one row. While everything fits, the two
  // stretches share what is left and the block sits about in the middle, as it always did.
  // Once the entries need more than the panel has, the stretches are empty: the header is at
  // the top margin and the box takes all the height down to the bottom margin (or to the
  // update status line while that shows), scrolling the rest.
  // Nothing jumps between the two: the box is the only item that gives way.
  // Across, the section takes the row's width up to its cap and is centered in what is left.
  auto* recent_row = new QHBoxLayout();
  recent_row->addStretch(1);
  recent_row->addWidget(recent_section_, 1000);
  recent_row->addStretch(1);
  content_layout->addLayout(recent_row);
#endif

  // The bottom of the panel holds one thing: the startup update-check status, hidden until the
  // check has something to say. The version sits under the title and the credit and the links
  // in the header (GitHub issue 750; Seth, November 26, 2026). The status stays down here
  // because it appears a moment after the window shows: in the header it would push the
  // tagline, the buttons and the grid down then. Here it only takes a line from the bottom.
  update_status_label_ = new QLabel(this);
  update_status_label_->setObjectName(QStringLiteral("startPanelUpdateStatus"));
  update_status_label_->setTextFormat(Qt::PlainText);
  update_status_label_->setVisible(false);
  auto* status_row = new QHBoxLayout();
  status_row->addStretch(1);
  status_row->addWidget(update_status_label_);
  status_row->addStretch(1);
#ifdef Q_OS_WASM
  content_layout->addStretch(4);
  // Outside the scrolling content, so it stays in view.
  update_status_area_ = new QVBoxLayout();
  update_status_area_->setContentsMargins(0, 0, 0, 0);
  update_status_area_->addLayout(status_row);
  outer->addLayout(update_status_area_);
#else
  // The space under the section, with the status line at its end. While the line is hidden
  // this is the plain stretch it replaces and has no height of its own, so a long list's box
  // reaches the bottom margin. When the line shows, its height comes out of this space alone
  // (or out of the box, once the list fills the panel): the header does not move.
  update_status_area_ = new QVBoxLayout();
  update_status_area_->setContentsMargins(0, 0, 0, 0);
  update_status_area_->setSpacing(0);
  update_status_area_->addStretch(1);
  update_status_area_->addLayout(status_row);
  content_layout->addLayout(update_status_area_, 4);
#endif
  for (const auto& callback : retranslation_callbacks_) callback();

  connect(new_button, &QPushButton::clicked, this, &StartPanel::new_document_requested);
  connect(clipboard_button_, &QPushButton::clicked, this, &StartPanel::open_clipboard_requested);
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
    QWidget#startPanelColumn, QWidget#startPanelArtwork, QWidget#startPanelHomeRow,
    QWidget#startPanelRecentSection {
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
      /* Directly under the title (Seth, November 26, 2026). It is what a bug report
         needs first: larger and heavier than the tagline under it, in the same color. */
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
    QLabel#startPanelWasmNote {
      background: transparent;
      color: @start_panel_hint_text;
      font-size: 11px;
    }
    QWidget#startPanel QPushButton {
      background: @button_bg;
      border: 1px solid @field_border;
      border-radius: 14px;
      color: @text_bright;
      min-width: 76px;
      min-height: 28px;
      padding: 0 12px;
    }
    QWidget#startPanel QPushButton:hover {
      background: @neutral_button_hover_bg;
      border-color: @neutral_button_hover_border;
    }
    QWidget#startPanel QPushButton:disabled {
      background: @field_bg_disabled;
      border-color: @field_border_disabled;
      color: @field_text_disabled;
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
    QToolButton#startPanelRecentListViewButton, QToolButton#startPanelRecentGridViewButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 5px;
      color: @start_panel_section_text;
      font-size: 11px;
      padding: 0 7px;
    }
    QToolButton#startPanelRecentListViewButton:hover, QToolButton#startPanelRecentGridViewButton:hover {
      background: @start_panel_row_hover_bg;
      color: @text_primary;
    }
    QToolButton#startPanelRecentListViewButton:checked, QToolButton#startPanelRecentGridViewButton:checked {
      background: @list_surface_bg;
      border-color: @list_surface_border;
      color: @text_primary;
    }
  )"));
  apply_recent_view();
  rebuild_recent_rows();  // no entries yet: hides the Recent section
}

void StartPanel::set_recent_grid_view(bool grid) {
#ifdef Q_OS_WASM
  grid = false;  // no recent files in the browser build, so no grid of them
#endif
  if (grid == recent_grid_view_) {
    apply_recent_view();  // a click on the checked button of the pair leaves it checked
    return;
  }
  recent_grid_view_ = grid;
  set_stored_start_panel_recent_grid_view(grid);
  if (!grid) {
    // The list shows no pictures, so none stay on disk either: the cached thumbnails are
    // deleted (on the worker) when the list is chosen. The grid is the default, so the cache
    // is no longer something a user asked for by pressing Grid; choosing List gives it up.
    recent_thumbnails_->clear();
  }
  apply_recent_view();
  emit recent_grid_view_changed(grid);
}

void StartPanel::apply_recent_view() {
  recent_list_view_button_->setChecked(!recent_grid_view_);
  recent_grid_view_button_->setChecked(recent_grid_view_);
  recent_list_->set_grid_view(recent_grid_view_, recent_thumbnails_, kMaxRecentEntries);
  update_recent_section_width();
  recent_list_->updateGeometry();
}

void StartPanel::update_recent_section_width() {
#ifndef Q_OS_WASM
  int cap = ui_text_length(kRecentListSectionMaxWidth);
  if (recent_grid_view_) {
    // A few entries get a box of their own size, not a wide frame around three cells: the
    // width at which they fill one row at the target cell width, but never narrower than the
    // compact block above. From the unfiltered count, so the filter never resizes the box.
    recent_list_->ensurePolished();
    const int entries = std::max(1, static_cast<int>(recent_paths_.size()));
    cap = std::clamp(recent_list_->width_for_grid_columns(entries), ui_text_length(kColumnMaxWidth),
                     kRecentGridSectionMaxWidth);
  }
  if (recent_section_->maximumWidth() != cap) {
    recent_section_->setMaximumWidth(cap);
  }
#endif
}

void StartPanel::set_recent_files(const QStringList& paths) {
  QStringList shown;
  for (const auto& path : paths) {
    if (shown.size() >= kMaxRecentEntries) {
      break;
    }
    // No stat here: MainWindow drops missing entries after its background
    // existence check, and a click on one that vanished since reports it.
    shown << QFileInfo(path).absoluteFilePath();
  }
  if (shown == recent_paths_) {
    return;
  }
  recent_paths_ = std::move(shown);
  // Loaded pictures of entries that left the list are dropped; their cache files are
  // MainWindow's to delete, which knows a removed entry from one that is only missing.
  recent_thumbnails_->keep_loaded_only(recent_paths_);
  rebuild_recent_rows();
}

void StartPanel::show_newest_recent_file() {
  recent_filter_edit_->clear();  // rebuilds the rows when a filter was typed
  // The rows are refilled inside one layout pass, so a list as long as before would
  // keep its scroll position and leave the newest file out of view.
  recent_list_->scrollToTop();
}

void StartPanel::rebuild_recent_rows() {
  // The box keeps the height of the whole list while a filter narrows it: typing must not
  // resize the box or move the header (the Open Recent menus pin theirs the same way).
  recent_list_->set_fixed_rows(static_cast<int>(recent_paths_.size()));
  fill_recent_file_list(*recent_list_, recent_paths_, recent_filter_edit_->text(), tr("No matching recent files"));
  update_recent_section_width();
  const bool has_entries = !recent_paths_.isEmpty();
  recent_section_->setVisible(has_entries);
  recent_label_->setVisible(has_entries);
  recent_filter_edit_->setVisible(has_entries);
#ifdef Q_OS_WASM
  const bool view_buttons = false;
#else
  const bool view_buttons = has_entries;
#endif
  recent_list_view_button_->setVisible(view_buttons);
  recent_grid_view_button_->setVisible(view_buttons);
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

bool StartPanel::eventFilter(QObject* watched, QEvent* event) {
  // Escape leaves the filter the way a background click does. The key still
  // travels on, so MainWindow's Escape (leaving full screen) keeps working.
  if (watched == recent_filter_edit_ && event->type() == QEvent::KeyPress) {
    const auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Escape && key_event->modifiers() == Qt::NoModifier &&
        recent_filter_edit_->hasFocus()) {
      setFocus(Qt::OtherFocusReason);
    }
  }
  return QWidget::eventFilter(watched, event);
}

void StartPanel::set_clipboard_available(bool available) {
#ifdef Q_OS_WASM
  available = false;
#endif
  clipboard_button_->setEnabled(available);
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
  // found by typing its name without clicking first, and at the top of the list,
  // where the file just closed is.
  show_newest_recent_file();
  // A file may have changed while a document was open: each picture gets one more look at
  // its file's size and time (on the worker) the next time its cell is painted.
  recent_thumbnails_->refresh_all();
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
  if (update_status_area_ != nullptr) {
    // The gap over the line exists only with the line: hidden, the area has no height at all.
    update_status_area_->setContentsMargins(0, text.isEmpty() ? 0 : kUpdateStatusTopGap, 0, 0);
  }
}

}  // namespace patchy::ui
