#include "ui/pdf_viewer_window.hpp"

#include "ui/app_settings.hpp"
#include "ui/icon_theme.hpp"
#include "ui/pdf_book_print_dialog.hpp"
#include "ui/pdf_import.hpp"
#include "ui/pdf_page_view.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QPdfDocument>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

constexpr int kThumbnailBox = 96;
constexpr int kThumbnailPendingRole = Qt::UserRole + 1;
constexpr int kRememberedPositions = 50;

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
  view_->set_document(document_.get());
  reset_thumbnails();
  const int page = std::clamp(remembered_page(path_), 0, std::max(0, view_->page_count() - 1));
  if (page > 0) {
    view_->go_to_page(page);
  }
  sync_controls();
  on_current_page_changed(view_->current_page());
}

PdfViewerWindow::~PdfViewerWindow() {
  // The view's render worker reads the document: stop it before the document goes.
  if (view_ != nullptr) {
    view_->set_document(nullptr);
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

  sidebar_action_ = toolbar_->addAction(tr("Pages"));
  sidebar_action_->setObjectName(QStringLiteral("pdfViewerSidebarAction"));
  sidebar_action_->setCheckable(true);
  sidebar_action_->setToolTip(tr("Show or hide the sidebar"));
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
      view_->go_to_page(value - 1);
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
  thumbnails_ = new QListWidget(sidebar_);
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
  side->addWidget(thumbnails_, 1);
  connect(thumbnails_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    view_->go_to_page(thumbnails_->row(item));
    view_->setFocus();
  });
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
  });
  connect(view_, &PdfPageView::thumbnail_ready, this, [this](int page, const QImage& image) {
    if (auto* item = thumbnails_->item(page); item != nullptr) {
      item->setIcon(QIcon(QPixmap::fromImage(image)));
    }
  });
  connect(view_, &PdfPageView::full_screen_toggle_requested, this, [this] { set_full_screen(!full_screen_); });
  connect(view_, &PdfPageView::escape_pressed, this, [this] {
    if (full_screen_) {
      set_full_screen(false);
    }
  });
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
  const bool sidebar = settings.value(settings_key("sidebar"), true).toBool();
  sidebar_action_->setChecked(sidebar);
  sidebar_->setVisible(sidebar);
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
  const auto candidates = typed_key_candidates(event);
  for (auto* action : findChildren<QAction*>()) {
    if (action->isEnabled() && action->shortcutContext() == Qt::WindowShortcut &&
        any_shortcut_matches(action->shortcuts(), candidates)) {
      return action;
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
  // The main window's hotkeys are application-wide, so a key both bind (Ctrl+W, Ctrl+0,
  // Ctrl+P, F11 ...) would be ambiguous to Qt and run neither. Keys typed in this window
  // run this window's command: claim the shortcut override, then trigger on the press.
  if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr && widget->window() == this) {
      const auto& key = *static_cast<QKeyEvent*>(event);
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
  if (!sidebar_->isVisible()) {
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
