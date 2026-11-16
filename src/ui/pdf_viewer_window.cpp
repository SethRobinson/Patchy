#include "ui/pdf_viewer_window.hpp"

#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/icon_theme.hpp"
#include "ui/pdf_book_print_dialog.hpp"
#include "ui/pdf_import.hpp"
#include "ui/pdf_links.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_text_search.hpp"
#include "ui/ui_text_size.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMouseEvent>
#include <QPdfDocument>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace patchy::ui {

namespace {

constexpr int kThumbnailBox = 96;
constexpr int kThumbnailPendingRole = Qt::UserRole + 1;
constexpr int kRememberedPositions = 50;
// How long the note that a search went around the end of the document stays up.
constexpr int kFindNoteMs = 3000;
// Back remembers this many places.
constexpr std::size_t kHistoryLimit = 100;
// The outline is given at least this much width when it is shown.
constexpr int kOutlineWidth = 250;
constexpr int kOutlinePageRole = Qt::UserRole + 1;
constexpr int kOutlineLocationRole = Qt::UserRole + 2;

// Permanent settings keys and tokens (docs/pdf-viewer.md).
QString settings_key(const char* leaf) {
  return QStringLiteral("pdfViewer/") + QLatin1String(leaf);
}

QString spread_token(PageSpreadMode mode) {
  switch (mode) {
    case PageSpreadMode::TwoPages:
      return QStringLiteral("two");
    case PageSpreadMode::TwoPagesCoverAlone:
      return QStringLiteral("book");
    case PageSpreadMode::Single:
      break;
  }
  return QStringLiteral("single");
}

PageSpreadMode spread_from_token(const QString& token) {
  if (token == QStringLiteral("two")) {
    return PageSpreadMode::TwoPages;
  }
  if (token == QStringLiteral("book")) {
    return PageSpreadMode::TwoPagesCoverAlone;
  }
  return PageSpreadMode::Single;
}

// A toolbar combo stays compact; its list is as wide as its longest entry (translations
// included), never elided, and short enough lists show every entry without scrolling.
void fit_combo_popup_to_items(QComboBox* combo) {
  const QFontMetrics metrics(combo->view()->font());
  int width = 0;
  for (int index = 0; index < combo->count(); ++index) {
    width = std::max(width, metrics.horizontalAdvance(combo->itemText(index)));
  }
  combo->view()->setTextElideMode(Qt::ElideNone);
  combo->view()->setMinimumWidth(width + 36);
  combo->setMaxVisibleItems(std::max(combo->maxVisibleItems(), combo->count()));
}

// Every key the platform gives a standard command, then `more`, each once.
// QKeySequence(StandardKey) alone is only the first of them: on Windows Close is Ctrl+F4,
// with Ctrl+W second.
QList<QKeySequence> standard_keys(QKeySequence::StandardKey standard, const QList<QKeySequence>& more = {}) {
  QList<QKeySequence> keys;
  for (const auto& key : QKeySequence::keyBindings(standard) + more) {
    if (!key.isEmpty() && !keys.contains(key)) {
      keys.append(key);
    }
  }
  return keys;
}

// The sequences a typed key can stand for: itself and, for a shifted symbol (Ctrl++ is
// Ctrl+Shift+= on many layouts), the key without the Shift, as Qt's own shortcut lookup does.
QList<QKeySequence> typed_key_candidates(const QKeyEvent& event) {
  const auto combination = event.keyCombination();
  const auto modifiers = combination.keyboardModifiers() & ~Qt::KeypadModifier;
  QList<QKeySequence> candidates{QKeySequence(QKeyCombination(modifiers, combination.key()))};
  if (modifiers.testFlag(Qt::ShiftModifier)) {
    candidates.append(QKeySequence(QKeyCombination(modifiers & ~Qt::ShiftModifier, combination.key())));
  }
  return candidates;
}

bool any_shortcut_matches(const QList<QKeySequence>& shortcuts, const QList<QKeySequence>& candidates) {
  for (const auto& shortcut : shortcuts) {
    for (const auto& candidate : candidates) {
      if (shortcut.matches(candidate) == QKeySequence::ExactMatch) {
        return true;
      }
    }
  }
  return false;
}

// Back or Forward: Alt and an arrow on every platform, with the platform's own keys for it
// that hold a modifier (Command+[ and Command+] on macOS). A bare key, the Backspace Windows
// lists for Back, stays what it is in a text field and on the pages.
QList<QKeySequence> history_keys(QKeySequence::StandardKey standard, Qt::Key arrow) {
  QList<QKeySequence> keys{QKeySequence(Qt::ALT | arrow)};
  for (const auto& key : QKeySequence::keyBindings(standard)) {
    if (!key.isEmpty() && key[0].keyboardModifiers() != Qt::NoModifier && !keys.contains(key)) {
      keys.append(key);
    }
  }
  return keys;
}

// A key that moves the caret by a word (Option+Left and Option+Right on macOS, which are
// also Back and Forward here).
bool moves_caret_by_word(const QKeyEvent& event) {
  return any_shortcut_matches(QKeySequence::keyBindings(QKeySequence::MoveToPreviousWord) +
                                  QKeySequence::keyBindings(QKeySequence::MoveToNextWord),
                              typed_key_candidates(event));
}

// A control that Enter and Esc belong to while the keyboard is in it, because they finish or
// drop what was typed there: the page field, the zoom box, any other text field.
bool types_its_own_enter(const QWidget* widget) {
  if (qobject_cast<const QAbstractSpinBox*>(widget) != nullptr || qobject_cast<const QLineEdit*>(widget) != nullptr) {
    return true;
  }
  const auto* combo = qobject_cast<const QComboBox*>(widget);
  return combo != nullptr && combo->isEditable();
}

// The key a tooltip names, the way this platform writes it.
QString key_name(const QKeySequence& key) {
  return key.toString(QKeySequence::NativeText);
}

// The platform's first key for a standard command, or `fallback` where it has none.
QKeySequence first_standard_key(QKeySequence::StandardKey standard, const QKeySequence& fallback) {
  const QKeySequence key(standard);
  return key.isEmpty() ? fallback : key;
}

// A zoom token: a percentage written with a dot (settings, combo data). 0 when it is not one.
double zoom_percent_from_token(const QString& token) {
  bool ok = false;
  const double percent = token.toDouble(&ok);
  return ok && percent > 0.0 ? percent : 0.0;
}

// Where each file was last read: "page<TAB>path" lines, newest first. Paths compare without
// case on Windows and macOS.
QString position_key(const QString& path) {
  const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return absolute.toCaseFolded();
#else
  return absolute;
#endif
}

int remembered_page(const QString& path) {
  const QString key = position_key(path);
  const QStringList entries = app_settings().value(settings_key("positions")).toStringList();
  for (const QString& entry : entries) {
    const qsizetype tab = entry.indexOf(QLatin1Char('\t'));
    if (tab > 0 && entry.mid(tab + 1) == key) {
      return entry.left(tab).toInt();
    }
  }
  return 0;
}

void remember_page(const QString& path, int page) {
  const QString key = position_key(path);
  auto settings = app_settings();
  QStringList entries = settings.value(settings_key("positions")).toStringList();
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&key](const QString& entry) {
                                 const qsizetype tab = entry.indexOf(QLatin1Char('\t'));
                                 return tab <= 0 || entry.mid(tab + 1) == key;
                               }),
                entries.end());
  entries.prepend(QString::number(page) + QLatin1Char('\t') + key);
  while (entries.size() > kRememberedPositions) {
    entries.removeLast();
  }
  settings.setValue(settings_key("positions"), entries);
}

}  // namespace

PdfViewerWindow* PdfViewerWindow::open(QWidget* owner, const QString& path, EditHandler edit_handler) {
  auto document = std::make_unique<QPdfDocument>();
  QString password;
  if (!load_pdf_for_viewing(owner, *document, path, &password)) {
    return nullptr;
  }
  return new PdfViewerWindow(owner, path, std::move(document), std::move(password), std::move(edit_handler));
}

PdfViewerWindow::PdfViewerWindow(QWidget* owner, QString path, std::unique_ptr<QPdfDocument> document,
                                 QString password, EditHandler edit_handler)
    : QWidget(owner, Qt::Window),
      path_(std::move(path)),
      password_(std::move(password)),
      document_(std::move(document)),
      edit_handler_(std::move(edit_handler)) {
  setObjectName(QStringLiteral("pdfViewerWindow"));
  setAttribute(Qt::WA_DeleteOnClose);
  build_ui();
  update_title();
  restore_settings();
  // The outline first: reading it takes PDFium's lock, which the page renders hold later.
  fill_outline(read_pdf_outline(*document_));
  view_->set_document(document_.get());
  search_->set_document(document_.get());
  reset_thumbnails();
  const int page = std::clamp(remembered_page(path_), 0, std::max(0, view_->page_count() - 1));
  if (page > 0) {
    view_->go_to_page(page);
  }
  sync_controls();
  on_current_page_changed(view_->current_page());
}

PdfViewerWindow::~PdfViewerWindow() {
  // The view's render worker and the search's worker read the document: stop them before
  // the document goes.
  if (view_ != nullptr) {
    view_->set_document(nullptr);
  }
  if (search_ != nullptr) {
    search_->set_document(nullptr);
  }
}

void PdfViewerWindow::build_ui() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  toolbar_ = new QToolBar(this);
  toolbar_->setObjectName(QStringLiteral("pdfViewerToolbar"));
  toolbar_->setMovable(false);
  root->addWidget(toolbar_);

  view_ = new PdfPageView(this);
  search_ = new PdfTextSearch(this);
  view_->set_text_search(search_);
  build_find_bar();
  root->addWidget(find_bar_);

  sidebar_action_ = toolbar_->addAction(tr("Pages"));
  sidebar_action_->setObjectName(QStringLiteral("pdfViewerSidebarAction"));
  sidebar_action_->setCheckable(true);
  sidebar_action_->setToolTip(tr("Show or hide the sidebar"));
  toolbar_->addSeparator();

  // Back and Forward between the places jumps left (links, the outline, the page field).
  const auto back_keys = history_keys(QKeySequence::Back, Qt::Key_Left);
  const auto forward_keys = history_keys(QKeySequence::Forward, Qt::Key_Right);
  back_action_ = toolbar_->addAction(themed_svg_icon(QStringLiteral("arrow-left")), tr("Back"));
  back_action_->setObjectName(QStringLiteral("pdfViewerBackAction"));
  back_action_->setToolTip(
      //: PDF viewer toolbar. %1 is a key, as in "Back to where you were before the last jump (Alt+Left)".
      tr("Back to where you were before the last jump (%1)").arg(key_name(back_keys.constFirst())));
  connect(back_action_, &QAction::triggered, this, &PdfViewerWindow::go_back);
  forward_action_ = toolbar_->addAction(themed_svg_icon(QStringLiteral("arrow-right")), tr("Forward"));
  forward_action_->setObjectName(QStringLiteral("pdfViewerForwardAction"));
  forward_action_->setToolTip(
      //: PDF viewer toolbar. %1 is a key, as in "Forward again (Alt+Right)".
      tr("Forward again (%1)").arg(key_name(forward_keys.constFirst())));
  connect(forward_action_, &QAction::triggered, this, &PdfViewerWindow::go_forward);
  update_history_actions();
  toolbar_->addSeparator();

  auto* previous = toolbar_->addAction(themed_svg_icon(QStringLiteral("chevron-left")), tr("Previous Page"));
  previous->setObjectName(QStringLiteral("pdfViewerPreviousAction"));
  connect(previous, &QAction::triggered, view_, &PdfPageView::previous_row);
  page_spin_ = new QSpinBox(this);
  page_spin_->setObjectName(QStringLiteral("pdfViewerPageSpin"));
  page_spin_->setKeyboardTracking(false);
  page_spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  page_spin_->setAlignment(Qt::AlignRight);
  page_spin_->setToolTip(tr("Go to page"));
  toolbar_->addWidget(page_spin_);
  page_count_label_ = new QLabel(this);
  page_count_label_->setObjectName(QStringLiteral("pdfViewerPageCountLabel"));
  page_count_label_->setContentsMargins(4, 0, 4, 0);
  toolbar_->addWidget(page_count_label_);
  auto* next = toolbar_->addAction(themed_svg_icon(QStringLiteral("chevron-right")), tr("Next Page"));
  next->setObjectName(QStringLiteral("pdfViewerNextAction"));
  connect(next, &QAction::triggered, view_, &PdfPageView::next_row);
  connect(page_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    if (!syncing_) {
      jump([this, value] { view_->go_to_page(value - 1); });
      view_->setFocus();
    }
  });
  toolbar_->addSeparator();

  auto* zoom_out = toolbar_->addAction(themed_svg_icon(QStringLiteral("zoom-out")), tr("Zoom Out"));
  zoom_out->setObjectName(QStringLiteral("pdfViewerZoomOutAction"));
  connect(zoom_out, &QAction::triggered, view_, &PdfPageView::zoom_out);
  zoom_combo_ = new QComboBox(this);
  zoom_combo_->setObjectName(QStringLiteral("pdfViewerZoomCombo"));
  zoom_combo_->setEditable(true);
  zoom_combo_->setInsertPolicy(QComboBox::NoInsert);
  zoom_combo_->setMinimumContentsLength(9);
  zoom_combo_->setToolTip(tr("Zoom"));
  zoom_combo_->addItem(tr("Fit Page"), QStringLiteral("page"));
  zoom_combo_->addItem(tr("Fit Width"), QStringLiteral("width"));
  zoom_combo_->addItem(tr("Actual Size"), QStringLiteral("100"));
  for (const int percent : {25, 50, 75, 100, 125, 150, 200, 300, 400, 800}) {
    zoom_combo_->addItem(tr("%1%").arg(percent), QString::number(percent));
  }
  fit_combo_popup_to_items(zoom_combo_);
  toolbar_->addWidget(zoom_combo_);
  const auto apply_zoom_token = [this](const QString& token) {
    if (token == QStringLiteral("page")) {
      view_->set_fit_mode(PageFitMode::WholePage);
    } else if (token == QStringLiteral("width")) {
      view_->set_fit_mode(PageFitMode::Width);
    } else if (const double percent = zoom_percent_from_token(token); percent > 0.0) {
      view_->set_zoom_percent(percent);
    }
    apply_zoom_text();
    view_->setFocus();
  };
  connect(zoom_combo_, &QComboBox::activated, this, [this, apply_zoom_token](int index) {
    apply_zoom_token(zoom_combo_->itemData(index).toString());
  });
  connect(zoom_combo_->lineEdit(), &QLineEdit::editingFinished, this, [this, apply_zoom_token] {
    QString text = zoom_combo_->lineEdit()->text().trimmed();
    text.remove(QLatin1Char('%'));
    bool ok = false;
    const double percent = QLocale().toDouble(text, &ok);
    if (ok && percent > 0.0) {
      apply_zoom_token(QString::number(percent));
    } else {
      apply_zoom_text();
    }
  });
  auto* zoom_in = toolbar_->addAction(themed_svg_icon(QStringLiteral("zoom-in")), tr("Zoom In"));
  zoom_in->setObjectName(QStringLiteral("pdfViewerZoomInAction"));
  connect(zoom_in, &QAction::triggered, view_, &PdfPageView::zoom_in);
  toolbar_->addSeparator();

  layout_combo_ = new QComboBox(this);
  layout_combo_->setObjectName(QStringLiteral("pdfViewerLayoutCombo"));
  layout_combo_->setToolTip(tr("How pages sit side by side"));
  layout_combo_->addItem(tr("One Page"), spread_token(PageSpreadMode::Single));
  layout_combo_->addItem(tr("Two Pages"), spread_token(PageSpreadMode::TwoPages));
  layout_combo_->addItem(tr("Two Pages, Cover Alone"), spread_token(PageSpreadMode::TwoPagesCoverAlone));
  layout_combo_->setItemData(2, tr("A book or comic opened flat: the cover alone, then 2 and 3 side by side"),
                             Qt::ToolTipRole);
  fit_combo_popup_to_items(layout_combo_);
  toolbar_->addWidget(layout_combo_);
  connect(layout_combo_, &QComboBox::activated, this, [this](int index) {
    view_->set_spread_mode(spread_from_token(layout_combo_->itemData(index).toString()));
    view_->setFocus();
  });
  continuous_action_ = toolbar_->addAction(tr("Continuous"));
  continuous_action_->setObjectName(QStringLiteral("pdfViewerContinuousAction"));
  continuous_action_->setCheckable(true);
  continuous_action_->setToolTip(
      tr("Scroll through every page. Off shows one page or spread at a time, and the wheel turns the page."));
  connect(continuous_action_, &QAction::toggled, this, [this](bool on) {
    if (!syncing_) {
      view_->set_continuous(on);
    }
  });
  right_to_left_action_ = toolbar_->addAction(tr("Right to Left"));
  right_to_left_action_->setObjectName(QStringLiteral("pdfViewerRightToLeftAction"));
  right_to_left_action_->setCheckable(true);
  right_to_left_action_->setToolTip(
      tr("Put the earlier page of a spread on the right, for manga and right-to-left books"));
  connect(right_to_left_action_, &QAction::toggled, this, [this](bool on) {
    if (!syncing_) {
      view_->set_reading_direction(on ? PageReadingDirection::RightToLeft : PageReadingDirection::LeftToRight);
    }
  });
  auto* rotate = toolbar_->addAction(themed_svg_icon(QStringLiteral("rotate-clockwise")), tr("Rotate Clockwise"));
  rotate->setObjectName(QStringLiteral("pdfViewerRotateAction"));
  connect(rotate, &QAction::triggered, view_, &PdfPageView::rotate_clockwise);
  toolbar_->addSeparator();
  full_screen_action_ =
      toolbar_->addAction(themed_svg_icon(QStringLiteral("screen-mode-full")), tr("Full Screen"));
  full_screen_action_->setObjectName(QStringLiteral("pdfViewerFullScreenAction"));
  full_screen_action_->setCheckable(true);
  full_screen_action_->setShortcut(QKeySequence(Qt::Key_F11));
  full_screen_action_->setShortcutContext(Qt::WindowShortcut);
  full_screen_action_->setToolTip(tr("Full Screen (F11). Esc or a double-click returns."));
  connect(full_screen_action_, &QAction::triggered, this, [this](bool on) { set_full_screen(on); });
  toolbar_->addSeparator();
  auto* find = toolbar_->addAction(themed_svg_icon(QStringLiteral("search")), tr("Find"));
  find->setObjectName(QStringLiteral("pdfViewerFindAction"));
  find->setShortcuts(standard_keys(QKeySequence::Find, {QKeySequence(Qt::CTRL | Qt::Key_F)}));
  find->setShortcutContext(Qt::WindowShortcut);
  find->setToolTip(
      tr("Find text in this PDF (%1)").arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
  connect(find, &QAction::triggered, this, &PdfViewerWindow::open_find_bar);

  // Keys that belong to this window only (it is not the main window, so none of them is an
  // application hotkey): the zoom keys follow the canvas's Ctrl+0 / Ctrl+1 meanings.
  const auto add_key = [this](const QList<QKeySequence>& keys, const std::function<void()>& run) {
    auto* action = new QAction(this);
    action->setShortcuts(keys);
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this, run);
    addAction(action);
    return action;
  };
  // Every key the platform has for a standard command (standard_keys), not only its first.
  add_key(standard_keys(QKeySequence::ZoomIn, {QKeySequence(Qt::CTRL | Qt::Key_Equal)}), [this] { view_->zoom_in(); });
  add_key(standard_keys(QKeySequence::ZoomOut), [this] { view_->zoom_out(); });
  add_key({QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { view_->set_fit_mode(PageFitMode::WholePage); });
  add_key({QKeySequence(Qt::CTRL | Qt::Key_1)}, [this] { view_->set_zoom_percent(100.0); });
  add_key({QKeySequence(Qt::CTRL | Qt::Key_2)}, [this] { view_->set_fit_mode(PageFitMode::Width); });
  // Ctrl+W on every platform (Command+W on macOS), with the platform's own Close keys.
  close_action_ =
      add_key(standard_keys(QKeySequence::Close, {QKeySequence(Qt::CTRL | Qt::Key_W)}), [this] { close(); });
  add_key(standard_keys(QKeySequence::Print), [this] { print_as_book(); });
  // F3 and Ctrl+G step through the matches on every platform, beside the platform's own keys.
  add_key(standard_keys(QKeySequence::FindNext, {QKeySequence(Qt::Key_F3), QKeySequence(Qt::CTRL | Qt::Key_G)}),
          [this] { find_next(); });
  add_key(standard_keys(QKeySequence::FindPrevious, {QKeySequence(Qt::SHIFT | Qt::Key_F3),
                                                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)}),
          [this] { find_previous(); });
  // Always on, unlike the toolbar buttons: a Back with nowhere to go does nothing, and the
  // key still never reaches the pages (where the arrow would turn one) or the main window.
  add_key(back_keys, [this] { go_back(); });
  add_key(forward_keys, [this] { go_forward(); });
  // These keys win over the main window's hotkeys while this window has focus (eventFilter).
  qApp->installEventFilter(this);

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setObjectName(QStringLiteral("pdfViewerSplitter"));
  splitter_->setChildrenCollapsible(false);
  sidebar_ = new QWidget(splitter_);
  sidebar_->setObjectName(QStringLiteral("pdfViewerSidebar"));
  auto* side = new QVBoxLayout(sidebar_);
  side->setContentsMargins(8, 8, 8, 8);
  side->setSpacing(6);
  edit_button_ = new QPushButton(tr("Edit..."), sidebar_);
  edit_button_->setObjectName(QStringLiteral("pdfViewerEditButton"));
  edit_button_->setToolTip(tr("Open pages in Patchy to edit them, with the Import PDF options"));
  connect(edit_button_, &QPushButton::clicked, this, &PdfViewerWindow::edit_pages);
  side->addWidget(edit_button_);
  print_button_ = new QPushButton(tr("Print as Book..."), sidebar_);
  print_button_->setObjectName(QStringLiteral("pdfViewerPrintBookButton"));
  print_button_->setToolTip(tr("Print the pages so they fold, cut or stack into a book, with assembly steps"));
  connect(print_button_, &QPushButton::clicked, this, &PdfViewerWindow::print_as_book);
  side->addWidget(print_button_);
  // Pages and Outline: the tabs show only for a PDF that has an outline.
  sidebar_tabs_ = new QTabBar(sidebar_);
  sidebar_tabs_->setObjectName(QStringLiteral("pdfViewerSidebarTabs"));
  sidebar_tabs_->setDrawBase(false);
  sidebar_tabs_->setExpanding(true);
  sidebar_tabs_->addTab(tr("Pages"));
  sidebar_tabs_->addTab(tr("Outline"));
  sidebar_tabs_->hide();
  side->addWidget(sidebar_tabs_);
  sidebar_stack_ = new QStackedWidget(sidebar_);
  sidebar_stack_->setObjectName(QStringLiteral("pdfViewerSidebarStack"));
  thumbnails_ = new QListWidget(sidebar_stack_);
  thumbnails_->setObjectName(QStringLiteral("pdfViewerThumbnails"));
  thumbnails_->setViewMode(QListView::IconMode);
  thumbnails_->setFlow(QListView::TopToBottom);
  thumbnails_->setWrapping(false);
  thumbnails_->setMovement(QListView::Static);
  thumbnails_->setResizeMode(QListView::Adjust);
  thumbnails_->setUniformItemSizes(true);
  thumbnails_->setIconSize(QSize(kThumbnailBox, kThumbnailBox));
  thumbnails_->setGridSize(QSize(kThumbnailBox + 24, kThumbnailBox + 28));
  thumbnails_->setSelectionMode(QAbstractItemView::SingleSelection);
  thumbnails_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  thumbnails_->setMinimumWidth(kThumbnailBox + 40);
  sidebar_stack_->addWidget(thumbnails_);
  outline_ = new QTreeWidget(sidebar_stack_);
  outline_->setObjectName(QStringLiteral("pdfViewerOutline"));
  outline_->setColumnCount(1);
  outline_->setHeaderHidden(true);
  outline_->setUniformRowHeights(true);
  outline_->setIndentation(14);
  outline_->setTextElideMode(Qt::ElideRight);
  outline_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  outline_->setSelectionMode(QAbstractItemView::SingleSelection);
  sidebar_stack_->addWidget(outline_);
  side->addWidget(sidebar_stack_, 1);
  connect(sidebar_tabs_, &QTabBar::currentChanged, this, [this](int index) {
    if (!syncing_) {
      sidebar_view_ = index == 1 ? QStringLiteral("outline") : QStringLiteral("pages");
    }
    show_sidebar_view(index == 1);
  });
  connect(thumbnails_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    const int page = thumbnails_->row(item);
    jump([this, page] { view_->go_to_page(page); });
    view_->setFocus();
  });
  // A click follows an entry and hands the keyboard back to the pages; Enter on an entry
  // follows it and stays in the outline.
  const auto follow = [this](QTreeWidgetItem* item, bool to_pages) {
    const int page = item->data(0, kOutlinePageRole).toInt();
    if (page < 0) {
      return;
    }
    outline_chosen_ = item;
    jump_to({page, item->data(0, kOutlineLocationRole).toPointF()});
    {
      // The entry followed is the one marked, whichever page the jump leaves at the middle
      // of the window, until the reader moves on.
      const QSignalBlocker blocker(outline_);
      outline_->setCurrentItem(item);
    }
    if (to_pages) {
      view_->setFocus();
    }
  };
  connect(outline_, &QTreeWidget::itemClicked, this, [follow](QTreeWidgetItem* item) { follow(item, true); });
  connect(outline_, &QTreeWidget::itemActivated, this, [follow](QTreeWidgetItem* item) { follow(item, false); });
  connect(outline_, &QTreeWidget::itemExpanded, this, [this] { update_outline_current(); });
  connect(outline_, &QTreeWidget::itemCollapsed, this, [this] { update_outline_current(); });
  connect(thumbnails_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this] { request_visible_thumbnails(); });
  // Cells as wide as the list, so each page sits centred in it.
  thumbnails_->viewport()->installEventFilter(this);
  splitter_->addWidget(sidebar_);
  splitter_->addWidget(view_);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setSizes({kThumbnailBox + 72, 1000});
  root->addWidget(splitter_, 1);

  connect(sidebar_action_, &QAction::toggled, this, [this](bool on) {
    if (!full_screen_) {
      sidebar_->setVisible(on);
      QTimer::singleShot(0, this, [this] { request_visible_thumbnails(); });
    }
  });
  connect(view_, &PdfPageView::current_page_changed, this, &PdfViewerWindow::on_current_page_changed);
  connect(view_, &PdfPageView::zoom_changed, this, &PdfViewerWindow::on_zoom_changed);
  connect(view_, &PdfPageView::view_options_changed, this, [this] {
    sync_controls();
    if (thumbnails_->count() > 0) {
      reset_thumbnails();
    }
    update_outline_current();
  });
  connect(view_, &PdfPageView::jump_requested, this, &PdfViewerWindow::jump_to);
  connect(view_, &PdfPageView::url_open_requested, this, &PdfViewerWindow::open_link);
  connect(view_, &PdfPageView::url_copy_requested, this, &PdfViewerWindow::copy_link);
  connect(view_, &PdfPageView::thumbnail_ready, this, [this](int page, const QImage& image) {
    if (auto* item = thumbnails_->item(page); item != nullptr) {
      item->setIcon(QIcon(QPixmap::fromImage(image)));
    }
  });
  connect(view_, &PdfPageView::full_screen_toggle_requested, this, [this] { set_full_screen(!full_screen_); });
  // Esc closes the find bar first, then leaves full screen.
  connect(view_, &PdfPageView::escape_pressed, this, [this] {
    if (find_open_) {
      close_find_bar();
    } else if (full_screen_) {
      set_full_screen(false);
    }
  });
}

void PdfViewerWindow::build_find_bar() {
  find_bar_ = new QWidget(this);
  find_bar_->setObjectName(QStringLiteral("pdfViewerFindBar"));
  auto* row = new QHBoxLayout(find_bar_);
  row->setContentsMargins(8, 4, 8, 4);
  row->setSpacing(6);
  find_edit_ = new QLineEdit(find_bar_);
  find_edit_->setObjectName(QStringLiteral("pdfViewerFindEdit"));
  find_edit_->setPlaceholderText(tr("Find in this PDF"));
  find_edit_->setClearButtonEnabled(true);
  find_edit_->setMinimumWidth(ui_text_length(180));
  find_edit_->setMaximumWidth(ui_text_length(340));
  row->addWidget(find_edit_, 1);
  const auto step_button = [this, row](const char* name, const QString& icon, const QString& tip) {
    auto* button = new QToolButton(find_bar_);
    button->setObjectName(QLatin1String(name));
    button->setIcon(themed_svg_icon(icon));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::TabFocus);
    row->addWidget(button);
    return button;
  };
  // The tips name the keys as this platform writes them: Enter and the platform's own Find
  // Next key (F3 on Windows, Command+G on macOS). Windows keyboards say Enter, not Return.
#if defined(Q_OS_MACOS)
  constexpr Qt::Key enter_key = Qt::Key_Return;
#else
  constexpr Qt::Key enter_key = Qt::Key_Enter;
#endif
  find_previous_button_ = step_button(
      "pdfViewerFindPreviousButton", QStringLiteral("chevron-up"),
      //: Find in the PDF viewer. %1 and %2 are keys, as in "Previous match (Shift+Enter, Shift+F3)".
      tr("Previous match (%1, %2)")
          .arg(key_name(QKeySequence(Qt::SHIFT | enter_key)),
               key_name(first_standard_key(QKeySequence::FindPrevious, QKeySequence(Qt::SHIFT | Qt::Key_F3)))));
  find_next_button_ = step_button(
      "pdfViewerFindNextButton", QStringLiteral("chevron-down"),
      //: Find in the PDF viewer. %1 and %2 are keys, as in "Next match (Enter, F3)".
      tr("Next match (%1, %2)")
          .arg(key_name(QKeySequence(enter_key)),
               key_name(first_standard_key(QKeySequence::FindNext, QKeySequence(Qt::Key_F3)))));
  connect(find_previous_button_, &QToolButton::clicked, this, &PdfViewerWindow::find_previous);
  connect(find_next_button_, &QToolButton::clicked, this, &PdfViewerWindow::find_next);
  find_status_label_ = new QLabel(find_bar_);
  find_status_label_->setObjectName(QStringLiteral("pdfViewerFindStatusLabel"));
  find_status_label_->setContentsMargins(4, 0, 8, 0);
  row->addWidget(find_status_label_);
  find_match_case_check_ = new QCheckBox(tr("Match case"), find_bar_);
  find_match_case_check_->setObjectName(QStringLiteral("pdfViewerFindMatchCaseCheck"));
  find_match_case_check_->setToolTip(tr("Only find text with the same capital and small letters"));
  row->addWidget(find_match_case_check_);
  find_whole_words_check_ = new QCheckBox(tr("Whole words"), find_bar_);
  find_whole_words_check_->setObjectName(QStringLiteral("pdfViewerFindWholeWordsCheck"));
  find_whole_words_check_->setToolTip(tr("Only find the text where it is not part of a longer word"));
  row->addWidget(find_whole_words_check_);
  find_note_label_ = new QLabel(find_bar_);
  find_note_label_->setObjectName(QStringLiteral("pdfViewerFindNoteLabel"));
  find_note_label_->setContentsMargins(8, 0, 0, 0);
  row->addWidget(find_note_label_);
  row->addStretch(1);
  auto* close = new QToolButton(find_bar_);
  close->setObjectName(QStringLiteral("pdfViewerFindCloseButton"));
  close->setIcon(layer_filter_clear_icon());
  close->setAutoRaise(true);
  close->setToolTip(tr("Close the find bar (Esc)"));
  close->setFocusPolicy(Qt::TabFocus);
  connect(close, &QToolButton::clicked, this, &PdfViewerWindow::close_find_bar);
  row->addWidget(close);
  find_bar_->hide();

  find_note_timer_ = new QTimer(this);
  find_note_timer_->setSingleShot(true);
  find_note_timer_->setInterval(kFindNoteMs);
  connect(find_note_timer_, &QTimer::timeout, find_note_label_, &QLabel::clear);

  connect(find_edit_, &QLineEdit::textChanged, this, [this] { start_find(); });
  connect(find_match_case_check_, &QCheckBox::toggled, this, [this] { start_find(); });
  connect(find_whole_words_check_, &QCheckBox::toggled, this, [this] { start_find(); });
  connect(search_, &PdfTextSearch::results_changed, this, &PdfViewerWindow::update_find_status);
  connect(search_, &PdfTextSearch::wrapped, this, [this](bool forward) {
    show_find_note(forward ? tr("Reached the end, continued from the start")
                           : tr("Reached the start, continued from the end"));
  });
  update_find_status();
}

void PdfViewerWindow::open_find_bar() {
  find_open_ = true;
  find_bar_->show();
  find_edit_->setFocus(Qt::ShortcutFocusReason);
  find_edit_->selectAll();
  start_find();
}

void PdfViewerWindow::close_find_bar() {
  if (!find_open_) {
    return;
  }
  find_open_ = false;
  find_bar_->hide();
  find_note_timer_->stop();
  find_note_label_->clear();
  search_->clear();
  view_->setFocus();
}

void PdfViewerWindow::start_find() {
  if (syncing_ || !find_open_) {
    return;
  }
  const PdfFindOptions options{find_match_case_check_->isChecked(), find_whole_words_check_->isChecked()};
  search_->set_query(find_edit_->text(), options, view_->current_page());
}

void PdfViewerWindow::find_next() {
  if (!find_open_) {
    open_find_bar();
    return;
  }
  search_->find_next();
}

void PdfViewerWindow::find_previous() {
  if (!find_open_) {
    open_find_bar();
    return;
  }
  search_->find_previous();
}

void PdfViewerWindow::update_find_status() {
  QString text;
  const int count = search_->match_count();
  if (!search_->active()) {
    // Nothing typed: nothing to say.
  } else if (search_->current_match().valid()) {
    //: Find in the PDF viewer: which match is shown and how many there are, as in "3 of 17".
    text = tr("%1 of %2").arg(QLocale().toString(search_->current_index() + 1), QLocale().toString(count));
  } else if (search_->searching()) {
    text = tr("Searching...");
  } else if (search_->text_layer() == PdfTextSearch::TextLayer::Absent) {
    text = tr("This PDF has no searchable text");
  } else {
    text = tr("No matches");
  }
  find_status_label_->setText(text);
  find_previous_button_->setEnabled(count > 0);
  find_next_button_->setEnabled(count > 0);
}

void PdfViewerWindow::show_find_note(const QString& text) {
  find_note_label_->setText(text);
  find_note_timer_->start();
}

// --- Back and Forward, links (docs/pdf-viewer.md) ---

void PdfViewerWindow::jump(const std::function<void()>& go) {
  const PdfViewPosition before = view_->view_position();
  go();
  // A jump that went nowhere leaves nothing to go back to.
  if (view_->view_position() != before) {
    back_.push_back(before);
    if (back_.size() > kHistoryLimit) {
      back_.erase(back_.begin());
    }
    forward_.clear();
  }
  update_history_actions();
}

void PdfViewerWindow::jump_to(const PdfJumpTarget& target) {
  if (!target.valid()) {
    return;
  }
  jump([this, target] { view_->go_to_target(target); });
}

void PdfViewerWindow::go_back() {
  if (back_.empty()) {
    return;
  }
  const PdfViewPosition there = back_.back();
  back_.pop_back();
  forward_.push_back(view_->view_position());
  view_->restore_view_position(there);
  update_history_actions();
}

void PdfViewerWindow::go_forward() {
  if (forward_.empty()) {
    return;
  }
  const PdfViewPosition there = forward_.back();
  forward_.pop_back();
  back_.push_back(view_->view_position());
  view_->restore_view_position(there);
  update_history_actions();
}

void PdfViewerWindow::update_history_actions() {
  back_action_->setEnabled(!back_.empty());
  forward_action_->setEnabled(!forward_.empty());
}

void PdfViewerWindow::set_link_handlers(UrlOpener open, TextCopier copy) {
  open_url_ = std::move(open);
  copy_text_ = std::move(copy);
}

void PdfViewerWindow::open_link(const QUrl& url) {
  // Checked here as well as where the click is read: nothing but a web or mail address
  // ever reaches the system.
  if (!pdf_link_url_opens(url)) {
    return;
  }
  if (open_url_) {
    open_url_(url);
  } else {
    QDesktopServices::openUrl(url);
  }
}

void PdfViewerWindow::copy_link(const QUrl& url) {
  const QString address = pdf_link_address(url);
  if (copy_text_) {
    copy_text_(address);
  } else {
    QGuiApplication::clipboard()->setText(address);
  }
}

// --- Outline (docs/pdf-viewer.md) ---

void PdfViewerWindow::fill_outline(const std::vector<PdfOutlineEntry>& entries) {
  outline_->clear();
  outline_items_.clear();
  outline_chosen_ = nullptr;
  // Rows with a little air: the default packs titles line on line.
  const QSize row(0, outline_->fontMetrics().height() + 8);
  const std::function<void(QTreeWidgetItem*, const std::vector<PdfOutlineEntry>&)> add =
      [this, &add, row](QTreeWidgetItem* parent, const std::vector<PdfOutlineEntry>& children) {
        for (const auto& entry : children) {
          auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(outline_);
          item->setSizeHint(0, row);
          const QString page_text =
              //: The outline of the PDF viewer: the page an entry goes to, as its tooltip.
              entry.target.valid() ? tr("Page %1").arg(entry.target.page + 1) : QString();
          item->setText(0, !entry.title.isEmpty() ? entry.title
                                                  : !page_text.isEmpty() ? page_text : QStringLiteral("..."));
          item->setToolTip(0, page_text);
          item->setData(0, kOutlinePageRole, entry.target.valid() ? entry.target.page : -1);
          item->setData(0, kOutlineLocationRole, entry.target.location);
          outline_items_.push_back(item);
          add(item, entry.children);
        }
      };
  add(nullptr, entries);
  // A single entry at the top is the document itself: open it. The rest start closed.
  if (outline_->topLevelItemCount() == 1) {
    outline_->topLevelItem(0)->setExpanded(true);
  }
  const bool has_outline = !outline_items_.empty();
  sidebar_tabs_->setVisible(has_outline);
  const bool outline = has_outline && sidebar_view_ == QStringLiteral("outline");
  syncing_ = true;
  sidebar_tabs_->setCurrentIndex(outline ? 1 : 0);
  syncing_ = false;
  show_sidebar_view(outline);
}

void PdfViewerWindow::show_sidebar_view(bool outline) {
  sidebar_stack_->setCurrentWidget(outline ? static_cast<QWidget*>(outline_) : thumbnails_);
  if (!outline) {
    QTimer::singleShot(0, this, [this] { request_visible_thumbnails(); });
    return;
  }
  // Titles need more room than thumbnails: widen a narrow sidebar once it shows them.
  const QList<int> sizes = splitter_->sizes();
  if (sizes.size() == 2 && sizes[0] + sizes[1] <= 0) {
    // Not laid out yet (the window is opening on its outline): the share to start with.
    splitter_->setSizes({kOutlineWidth, 1000});
  } else if (sizes.size() == 2 && sizes[0] > 0 && sizes[0] < kOutlineWidth &&
             sizes[0] + sizes[1] > 2 * kOutlineWidth) {
    splitter_->setSizes({kOutlineWidth, sizes[0] + sizes[1] - kOutlineWidth});
  }
  update_outline_current();
}

void PdfViewerWindow::update_outline_current() {
  if (outline_items_.empty()) {
    return;
  }
  const auto pages = view_->current_pages();
  const int page = pages.empty() ? 0 : pages.back();
  QTreeWidgetItem* reading = nullptr;
  if (outline_chosen_ != nullptr &&
      std::find(pages.begin(), pages.end(), outline_chosen_->data(0, kOutlinePageRole).toInt()) != pages.end()) {
    // The entry the user followed stays marked while its page shows, though others start
    // there too.
    reading = outline_chosen_;
  } else {
    // The entry being read: the first of those with the last start page on or before the
    // last page showing.
    int best_page = -1;
    for (const auto* item : outline_items_) {
      const int start = item->data(0, kOutlinePageRole).toInt();
      if (start >= 0 && start <= page && start > best_page) {
        best_page = start;
      }
    }
    for (auto* item : outline_items_) {
      if (best_page >= 0 && item->data(0, kOutlinePageRole).toInt() == best_page) {
        reading = item;
        break;
      }
    }
  }
  // Marked on the row that shows it: the outermost closed entry above it. Nothing is opened
  // for the reader.
  QTreeWidgetItem* shown = reading;
  for (auto* above = reading != nullptr ? reading->parent() : nullptr; above != nullptr; above = above->parent()) {
    if (!above->isExpanded()) {
      shown = above;
    }
  }
  const QSignalBlocker blocker(outline_);
  outline_->setCurrentItem(shown);
  if (shown != nullptr) {
    outline_->scrollToItem(shown);
  }
}

void PdfViewerWindow::update_title() {
  setWindowTitle(tr("%1 - PDF Viewer").arg(QFileInfo(path_).fileName()));
}

void PdfViewerWindow::restore_settings() {
  auto settings = app_settings();
  syncing_ = true;
  view_->set_spread_mode(spread_from_token(settings.value(settings_key("pages")).toString()));
  view_->set_continuous(settings.value(settings_key("continuous"), true).toBool());
  view_->set_reading_direction(settings.value(settings_key("direction")).toString() == QStringLiteral("rtl")
                                   ? PageReadingDirection::RightToLeft
                                   : PageReadingDirection::LeftToRight);
  const QString zoom = settings.value(settings_key("zoom"), QStringLiteral("page")).toString();
  if (zoom == QStringLiteral("width")) {
    view_->set_fit_mode(PageFitMode::Width);
  } else if (const double percent = zoom_percent_from_token(zoom); percent > 0.0) {
    view_->set_zoom_percent(std::clamp(percent, 5.0, 1600.0));
  } else {
    view_->set_fit_mode(PageFitMode::WholePage);
  }
  sidebar_view_ = settings.value(settings_key("sidebarView")).toString() == QStringLiteral("outline")
                      ? QStringLiteral("outline")
                      : QStringLiteral("pages");
  const bool sidebar = settings.value(settings_key("sidebar"), true).toBool();
  sidebar_action_->setChecked(sidebar);
  sidebar_->setVisible(sidebar);
  find_match_case_check_->setChecked(settings.value(settings_key("findMatchCase"), false).toBool());
  find_whole_words_check_->setChecked(settings.value(settings_key("findWholeWords"), false).toBool());
  syncing_ = false;
  const QByteArray geometry = settings.value(settings_key("geometry")).toByteArray();
  if (geometry.isEmpty() || !restoreGeometry(geometry)) {
    // First time: cover the main window, so the viewer reads as Patchy's PDF mode.
    if (auto* owner = parentWidget(); owner != nullptr) {
      setGeometry(owner->window()->geometry());
    } else {
      resize(1100, 800);
    }
  }
}

void PdfViewerWindow::save_settings() const {
  auto settings = app_settings();
  settings.setValue(settings_key("pages"), spread_token(view_->spread_mode()));
  settings.setValue(settings_key("continuous"), view_->continuous());
  settings.setValue(settings_key("direction"), view_->reading_direction() == PageReadingDirection::RightToLeft
                                                   ? QStringLiteral("rtl")
                                                   : QStringLiteral("ltr"));
  switch (view_->fit_mode()) {
    case PageFitMode::WholePage:
      settings.setValue(settings_key("zoom"), QStringLiteral("page"));
      break;
    case PageFitMode::Width:
      settings.setValue(settings_key("zoom"), QStringLiteral("width"));
      break;
    case PageFitMode::None:
      settings.setValue(settings_key("zoom"), QString::number(view_->zoom_percent(), 'f', 1));
      break;
  }
  settings.setValue(settings_key("sidebar"), sidebar_action_->isChecked());
  settings.setValue(settings_key("sidebarView"), sidebar_view_);
  settings.setValue(settings_key("findMatchCase"), find_match_case_check_->isChecked());
  settings.setValue(settings_key("findWholeWords"), find_whole_words_check_->isChecked());
  if (!full_screen_) {
    settings.setValue(settings_key("geometry"), saveGeometry());
  }
}

void PdfViewerWindow::sync_controls() {
  syncing_ = true;
  const int count = view_->page_count();
  page_spin_->setRange(count > 0 ? 1 : 0, std::max(1, count));
  page_spin_->setEnabled(count > 0);
  page_count_label_->setText(tr("of %1").arg(count));
  layout_combo_->setCurrentIndex(std::max(0, layout_combo_->findData(spread_token(view_->spread_mode()))));
  continuous_action_->setChecked(view_->continuous());
  right_to_left_action_->setChecked(view_->reading_direction() == PageReadingDirection::RightToLeft);
  full_screen_action_->setChecked(full_screen_);
  syncing_ = false;
  apply_zoom_text();
}

void PdfViewerWindow::apply_zoom_text() {
  QString text;
  switch (view_->fit_mode()) {
    case PageFitMode::WholePage:
      text = tr("Fit Page");
      break;
    case PageFitMode::Width:
      text = tr("Fit Width");
      break;
    case PageFitMode::None:
      text = tr("%1%").arg(QLocale().toString(view_->zoom_percent(), 'f', 0));
      break;
  }
  const QSignalBlocker blocker(zoom_combo_);
  zoom_combo_->setCurrentIndex(-1);
  zoom_combo_->setEditText(text);
}

void PdfViewerWindow::on_current_page_changed(int page) {
  syncing_ = true;
  page_spin_->setValue(page + 1);
  syncing_ = false;
  if (auto* item = thumbnails_->item(page); item != nullptr) {
    const QSignalBlocker blocker(thumbnails_);
    thumbnails_->setCurrentItem(item);
    thumbnails_->scrollToItem(item);
  }
  request_visible_thumbnails();
  update_outline_current();
}

void PdfViewerWindow::on_zoom_changed(double percent) {
  Q_UNUSED(percent);
  apply_zoom_text();
}

void PdfViewerWindow::set_file_drop_handlers(FileDropHandler accept, FileDropHandler drop) {
  accept_file_drag_ = std::move(accept);
  drop_files_ = std::move(drop);
  setAcceptDrops(accept_file_drag_ && drop_files_);
}

void PdfViewerWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (!accept_file_drag_ || !accept_file_drag_(event)) {
    QWidget::dragEnterEvent(event);
  }
}

void PdfViewerWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (!accept_file_drag_ || !accept_file_drag_(event)) {
    QWidget::dragMoveEvent(event);
  }
}

void PdfViewerWindow::dropEvent(QDropEvent* event) {
  if (!drop_files_ || !drop_files_(event)) {
    QWidget::dropEvent(event);
  }
}

void PdfViewerWindow::set_close_key_source(QAction* action) {
  close_key_source_ = action;
}

QAction* PdfViewerWindow::window_key_action(const QKeyEvent& event) const {
  // The key as typed first, then without its Shift: Shift+F3 is Find Previous, never F3.
  const auto actions = findChildren<QAction*>();
  for (const auto& candidate : typed_key_candidates(event)) {
    for (auto* action : actions) {
      if (action->isEnabled() && action->shortcutContext() == Qt::WindowShortcut &&
          any_shortcut_matches(action->shortcuts(), {candidate})) {
        return action;
      }
    }
  }
  return nullptr;
}

bool PdfViewerWindow::is_close_source_key(const QKeyEvent& event) const {
  return close_key_source_ != nullptr &&
         any_shortcut_matches(close_key_source_->shortcuts(), typed_key_candidates(event));
}

bool PdfViewerWindow::eventFilter(QObject* watched, QEvent* event) {
  if (thumbnails_ != nullptr && watched == thumbnails_->viewport() && event->type() == QEvent::Resize) {
    size_thumbnail_cells();
    request_visible_thumbnails();
  }
  // The mouse's Back and Forward buttons, anywhere in this window.
  if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr && widget->window() == this) {
      const auto button = static_cast<QMouseEvent*>(event)->button();
      if (button == Qt::BackButton || button == Qt::ForwardButton) {
        if (button == Qt::BackButton) {
          go_back();
        } else {
          go_forward();
        }
        event->accept();
        return true;
      }
    }
  }
  // The main window's hotkeys are application-wide, so a key both bind (Ctrl+W, Ctrl+0,
  // Ctrl+P, F11 ...) would be ambiguous to Qt and run neither. Keys typed in this window
  // run this window's command: claim the shortcut override, then trigger on the press.
  if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr && widget->window() == this) {
      const auto& key = *static_cast<QKeyEvent*>(event);
      // While the find bar is open, Enter and Shift+Enter step through the matches and Esc
      // closes the bar wherever the keyboard is in this window (the field, a page, an
      // option, the sidebar), before any hotkey of the main window can take them. Only the
      // control that holds the keyboard counts (a key it passed on is not typed again for
      // its parents), and a control that types its own Enter keeps it.
      if (find_open_ && widget == (focusWidget() != nullptr ? focusWidget() : this) &&
          (widget == find_edit_ || !types_its_own_enter(widget))) {
        const bool enter = key.key() == Qt::Key_Return || key.key() == Qt::Key_Enter;
        const bool plain = (key.modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier)) == Qt::NoModifier;
        if (plain && (enter || key.key() == Qt::Key_Escape)) {
          event->accept();
          if (event->type() == QEvent::KeyPress) {
            if (!enter) {
              close_find_bar();
            } else if (key.modifiers().testFlag(Qt::ShiftModifier)) {
              find_previous();
            } else {
              find_next();
            }
          }
          return true;
        }
      }
      // A key that moves the caret by a word in a text field stays with the field, though
      // it is Back or Forward everywhere else (Option+Left and Option+Right on macOS).
      if (types_its_own_enter(widget) && moves_caret_by_word(key)) {
        return QWidget::eventFilter(watched, event);
      }
      auto* action = window_key_action(key);
      // The key the user gave the main window's Close closes this window too, instead of the
      // document behind it. Only once the key reaches the window itself: a control under
      // the focus that uses the key (a letter typed in the page field) took it before that.
      if (action == nullptr && watched == this && is_close_source_key(key)) {
        action = close_action_;
      }
      if (action != nullptr) {
        event->accept();
        if (event->type() == QEvent::KeyPress) {
          action->trigger();
        }
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PdfViewerWindow::size_thumbnail_cells() {
  const QSize cell(std::max(kThumbnailBox + 8, thumbnails_->viewport()->width()), kThumbnailBox + 28);
  if (thumbnails_->gridSize() == cell) {
    return;
  }
  thumbnails_->setGridSize(cell);
  for (int row = 0; row < thumbnails_->count(); ++row) {
    thumbnails_->item(row)->setSizeHint(cell);
  }
}

void PdfViewerWindow::reset_thumbnails() {
  const int current = view_->current_page();
  thumbnails_->clear();
  const int count = view_->page_count();
  for (int page = 0; page < count; ++page) {
    const QSizeF points = document_->pagePointSize(page);
    QSizeF turned = points;
    if (view_->quarter_turns() % 2 == 1) {
      turned.transpose();
    }
    QSize size(kThumbnailBox, kThumbnailBox);
    if (turned.width() > 0.0 && turned.height() > 0.0) {
      const double scale = std::min(kThumbnailBox / turned.width(), kThumbnailBox / turned.height());
      size = QSize(std::max(1, static_cast<int>(std::lround(turned.width() * scale))),
                   std::max(1, static_cast<int>(std::lround(turned.height() * scale))));
    }
    QPixmap blank(size);
    blank.fill(Qt::transparent);
    auto* item = new QListWidgetItem(QIcon(blank), QString::number(page + 1), thumbnails_);
    item->setData(kThumbnailPendingRole, true);
    item->setTextAlignment(Qt::AlignHCenter);
  }
  thumbnails_->setGridSize(QSize());
  size_thumbnail_cells();
  if (auto* item = thumbnails_->item(current); item != nullptr) {
    thumbnails_->setCurrentItem(item);
  }
  // After the list has its geometry, so "visible" means something.
  QTimer::singleShot(0, this, [this] { request_visible_thumbnails(); });
}

void PdfViewerWindow::request_visible_thumbnails() {
  if (!sidebar_->isVisible() || sidebar_stack_->currentWidget() != thumbnails_) {
    return;
  }
  const QRect viewport = thumbnails_->viewport()->rect();
  for (int row = 0; row < thumbnails_->count(); ++row) {
    auto* item = thumbnails_->item(row);
    if (!item->data(kThumbnailPendingRole).toBool()) {
      continue;
    }
    if (thumbnails_->visualItemRect(item).intersects(viewport)) {
      item->setData(kThumbnailPendingRole, false);
      view_->request_thumbnail(row, QSize(kThumbnailBox, kThumbnailBox));
    }
  }
}

void PdfViewerWindow::edit_pages() {
  if (!edit_handler_) {
    return;
  }
  const std::vector<int> pages = view_->current_pages();
  if (edit_handler_(*this, pages, password_)) {
    close();
  }
}

void PdfViewerWindow::print_as_book() {
  if (full_screen_) {
    set_full_screen(false);
  }
  PdfBookPrintStart start;
  start.right_to_left = view_->reading_direction() == PageReadingDirection::RightToLeft;
  start.cover_alone = view_->spread_mode() == PageSpreadMode::TwoPagesCoverAlone;
  run_pdf_book_print_dialog(this, *document_, path_, start);
}

void PdfViewerWindow::set_full_screen(bool full_screen) {
  if (full_screen == full_screen_) {
    full_screen_action_->setChecked(full_screen_);
    return;
  }
  full_screen_ = full_screen;
  if (full_screen_) {
    was_maximized_ = isMaximized();
    toolbar_->hide();
    sidebar_->hide();
    showFullScreen();
  } else {
    toolbar_->show();
    sidebar_->setVisible(sidebar_action_->isChecked());
    if (was_maximized_) {
      showMaximized();
    } else {
      showNormal();
    }
  }
  full_screen_action_->setChecked(full_screen_);
  view_->setFocus();
}

void PdfViewerWindow::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && full_screen_ && !isFullScreen()) {
    // Left full screen some other way (the window manager): bring the chrome back.
    full_screen_ = false;
    toolbar_->show();
    sidebar_->setVisible(sidebar_action_->isChecked());
    full_screen_action_->setChecked(false);
  }
}

void PdfViewerWindow::closeEvent(QCloseEvent* event) {
  save_settings();
  remember_page(path_, view_->current_page());
  QWidget::closeEvent(event);
}

}  // namespace patchy::ui
