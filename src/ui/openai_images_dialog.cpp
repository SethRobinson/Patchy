#include "ui/openai_images_dialog.hpp"

#include "ui/action_icons.hpp"
#include "core/layer.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QPushButton>
#include <QScreen>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QHelpEvent>
#include <QToolTip>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

// Persisted settings keys (never renamed).
constexpr char kModelKey[] = "openAiImages/model";
constexpr char kSizeKey[] = "openAiImages/size";
constexpr char kQualityKey[] = "openAiImages/quality";
constexpr char kBackgroundKey[] = "openAiImages/background";
constexpr char kCountKey[] = "openAiImages/count";
constexpr char kTargetKey[] = "openAiImages/target";
constexpr char kAnimateColorsKey[] = "openAiImages/animateColors";
constexpr char kEdgeBlendKey[] = "openAiImages/edgeBlend";
constexpr int kEdgeBlendDefault = 80;  // percent (Seth, 2026-11-06)
constexpr char kExpandBlendKey[] = "openAiImages/expandBlend";
constexpr int kExpandBlendDefault = 25;  // percent: the probe's best seam (docs/openai-images-tabs.md#expand)

// Size tokens stored in kSizeKey.
constexpr char kSizeCanvas[] = "canvas";
constexpr char kSizeAuto[] = "auto";

constexpr int kDialogFloorHeight = 480;  // the shortest the dialog gets (Seth, 2026-11-06)
constexpr int kReferenceIconSize = 48;
constexpr int kReferenceListHeight = kReferenceIconSize + 40;
constexpr int kReferenceLabelRole = Qt::UserRole + 1;  // the reference's name, without its number

// The GPT Image 2.6 models the dialog offers (the only family it supports).
constexpr const char* kModelIds[] = {"gpt-image-2.6-sunburst", "gpt-image-2.6-flare"};

// Local image files among copied file URLs (a file copied in the file manager).
QStringList clipboard_image_files(const QMimeData* mime) {
  QStringList paths;
  if (mime == nullptr || !mime->hasUrls()) {
    return paths;
  }
  const auto formats = QImageReader::supportedImageFormats();
  for (const auto& url : mime->urls()) {
    const auto path = url.toLocalFile();
    if (!path.isEmpty() && formats.contains(QFileInfo(path).suffix().toLower().toLatin1())) {
      paths.push_back(path);
    }
  }
  return paths;
}

bool mime_offers_reference(const QMimeData* mime) {
  return mime != nullptr && (mime->hasImage() || !clipboard_image_files(mime).isEmpty());
}

// The prompt fields are five lines tall; longer text scrolls (Seth, 2026-11-06: half the
// height they had when they stretched, then shorter again so the dialog stays below the
// main window's minimum height).
constexpr int kPromptLines = 5;
constexpr int kDescribeLines = 8;  // Describe's read-only box: no reference area on its tab
void size_prompt_edit(QPlainTextEdit& edit, int lines = kPromptLines) {
  const int frame = 2 * edit.frameWidth();
  const int margins = static_cast<int>(std::ceil(2.0 * edit.document()->documentMargin()));
  edit.setFixedHeight(edit.fontMetrics().lineSpacing() * lines + frame + margins);
  edit.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

// The reference list: image files or image data dropped from outside (a file manager, a
// browser) are added like Paste adds them; while empty it says it takes drops.
class ReferenceList final : public QListWidget {
 public:
  ReferenceList(QWidget* parent, std::function<void(const QMimeData*)> dropped)
      : QListWidget(parent), dropped_(std::move(dropped)) {
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
  }

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override { accept_if_reference(event); }
  void dragMoveEvent(QDragMoveEvent* event) override { accept_if_reference(event); }
  void dropEvent(QDropEvent* event) override {
    if (!mime_offers_reference(event->mimeData())) {
      event->ignore();
      return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    dropped_(event->mimeData());
  }
  [[nodiscard]] Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
  // One row of thumbnails with their numbers. The theme's list rule sets a minimum height when
  // it polishes the widget, which would let the layout squeeze the row: the fixed height goes
  // back on after each polish or style change.
  bool event(QEvent* event) override {
    const bool handled = QListWidget::event(event);
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange) {
      setFixedHeight(kReferenceListHeight);
    }
    return handled;
  }
  void paintEvent(QPaintEvent* event) override {
    QListWidget::paintEvent(event);
    if (count() > 0) {
      return;
    }
    QPainter painter(viewport());
    painter.setPen(theme().text_secondary);
    painter.drawText(viewport()->rect().adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap,
                     QCoreApplication::translate("patchy::ui::OpenAiImagesDialog",
                                                 "Drop image files here, or use Add or Paste"));
  }

 private:
  static void accept_if_reference(QDragMoveEvent* event) {
    if (mime_offers_reference(event->mimeData())) {
      event->setDropAction(Qt::CopyAction);
      event->accept();
    } else {
      event->ignore();
    }
  }
  std::function<void(const QMimeData*)> dropped_;
};

// A small X in each reference thumbnail's top-right corner removes it.
class ReferenceDelegate final : public QStyledItemDelegate {
 public:
  ReferenceDelegate(QObject* parent, std::function<void(int)> remove)
      : QStyledItemDelegate(parent), remove_(std::move(remove)) {}

  static QRect close_rect(const QRect& item) { return QRect(item.right() - 17, item.top() + 2, 16, 16); }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyledItemDelegate::paint(painter, option, index);
    const auto& palette = theme();
    const auto rect = close_rect(option.rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette.title_bar_bg);
    painter->drawEllipse(rect);
    QPen pen(palette.text_bright, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    const auto inner = QRectF(rect).adjusted(5, 5, -5, -5);
    painter->drawLine(inner.topLeft(), inner.bottomRight());
    painter->drawLine(inner.topRight(), inner.bottomLeft());
    painter->restore();
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseButtonDblClick) {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() == Qt::LeftButton && close_rect(option.rect).contains(mouse->position().toPoint())) {
        if (event->type() == QEvent::MouseButtonRelease) {
          remove_(index.row());
        }
        return true;  // the X never selects the item
      }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
  }

  bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                 const QModelIndex& index) override {
    if (event->type() == QEvent::ToolTip && close_rect(option.rect).contains(event->pos())) {
      QToolTip::showText(event->globalPos(),
                         QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Remove this reference image"),
                         view);
      return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
  }

 private:
  std::function<void(int)> remove_;
};

// The operation tabs: as tall as the CURRENT page wants, not the tallest one. QTabWidget's
// own size hint counts every page, and at a text size other than 100% dialogs grow to their
// layout's height for width after each layout change (dialog_utils.cpp, GitHub issue 495), so
// every tab grew to the tallest page's height and fought the per-tab fit (stale duplicated
// controls on screen, Seth, 2026-11-06).
class CurrentPageTabWidget final : public QTabWidget {
 public:
  using QTabWidget::QTabWidget;
  [[nodiscard]] QSize sizeHint() const override {
    auto size = QTabWidget::sizeHint();
    if (const auto* page = currentWidget(); page != nullptr) {
      size.setHeight(tabBar()->sizeHint().height() + page->sizeHint().height());
    }
    return size;
  }
  [[nodiscard]] bool hasHeightForWidth() const override { return false; }
};

bool clipboard_offers_reference() {
  const auto* clipboard = QApplication::clipboard();
  return clipboard != nullptr && mime_offers_reference(clipboard->mimeData());
}

// The prompt field pastes text as usual; a clipboard holding an image and no text goes to
// the reference images instead of doing nothing.
class PromptEdit final : public QPlainTextEdit {
 public:
  PromptEdit(QWidget* parent, std::function<void()> paste_images)
      : QPlainTextEdit(parent), paste_images_(std::move(paste_images)) {}

 protected:
  bool canInsertFromMimeData(const QMimeData* source) const override {
    return QPlainTextEdit::canInsertFromMimeData(source) || (!source->hasText() && mime_offers_reference(source));
  }
  void insertFromMimeData(const QMimeData* source) override {
    if (!source->hasText() && mime_offers_reference(source)) {
      paste_images_();
      return;
    }
    QPlainTextEdit::insertFromMimeData(source);
  }

 private:
  std::function<void()> paste_images_;
};

void select_data(QComboBox& combo, const QString& value) {
  const auto index = combo.findData(value);
  if (index >= 0) {
    combo.setCurrentIndex(index);
  }
}

}  // namespace

OpenAiExpandFrameView::OpenAiExpandFrameView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("openAiImagesExpandView"));
  setMouseTracking(true);
  setMinimumHeight(180);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void OpenAiExpandFrameView::set_image(const QImage& thumbnail, QSize canvas) {
  thumbnail_ = thumbnail;
  canvas_ = canvas;
  update();
}

void OpenAiExpandFrameView::set_margins(QMargins margins) {
  if (margins_ == margins) {
    return;
  }
  margins_ = margins;
  update();
}

double OpenAiExpandFrameView::layout_scale() const {
  const double total_width = canvas_.width() + margins_.left() + margins_.right();
  const double total_height = canvas_.height() + margins_.top() + margins_.bottom();
  constexpr double kPadding = 14.0;
  if (total_width <= 0 || total_height <= 0) {
    return 1.0;
  }
  return std::max(1e-6, std::min((width() - 2 * kPadding) / total_width, (height() - 2 * kPadding) / total_height));
}

QRectF OpenAiExpandFrameView::frame_rect() const {
  const double scale = layout_scale();
  const QSizeF size((canvas_.width() + margins_.left() + margins_.right()) * scale,
                    (canvas_.height() + margins_.top() + margins_.bottom()) * scale);
  return QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
}

void OpenAiExpandFrameView::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto& palette = theme();
  painter.fillRect(rect(), palette.field_bg_large);
  if (canvas_.isEmpty()) {
    return;
  }
  const double scale = layout_scale();
  const auto frame = frame_rect();
  // The new area, tinted with the accent; the image on top of it.
  auto tint = palette.accent;
  tint.setAlpha(60);
  painter.fillRect(frame, tint);
  const QRectF image(frame.left() + margins_.left() * scale, frame.top() + margins_.top() * scale,
                     canvas_.width() * scale, canvas_.height() * scale);
  if (!thumbnail_.isNull()) {
    painter.drawImage(image, thumbnail_);
  }
  painter.setPen(QPen(palette.text_secondary, 1.0, Qt::DashLine));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(image);
  painter.setPen(QPen(palette.accent, 1.5));
  painter.drawRect(frame);
  // Handles on the corners and the middle of each edge.
  painter.setBrush(palette.accent);
  painter.setPen(Qt::NoPen);
  constexpr double kHandle = 7.0;
  for (const auto x : {frame.left(), frame.center().x(), frame.right()}) {
    for (const auto y : {frame.top(), frame.center().y(), frame.bottom()}) {
      if (x == frame.center().x() && y == frame.center().y()) {
        continue;
      }
      painter.drawRect(QRectF(x - kHandle / 2.0, y - kHandle / 2.0, kHandle, kHandle));
    }
  }
}

int OpenAiExpandFrameView::edges_at(QPointF position) const {
  if (canvas_.isEmpty()) {
    return None;
  }
  constexpr double kGrab = 7.0;
  const auto frame = frame_rect();
  if (!frame.adjusted(-kGrab, -kGrab, kGrab, kGrab).contains(position)) {
    return None;
  }
  int edges = None;
  if (std::abs(position.x() - frame.left()) <= kGrab) {
    edges |= Left;
  } else if (std::abs(position.x() - frame.right()) <= kGrab) {
    edges |= Right;
  }
  if (std::abs(position.y() - frame.top()) <= kGrab) {
    edges |= Top;
  } else if (std::abs(position.y() - frame.bottom()) <= kGrab) {
    edges |= Bottom;
  }
  return edges;
}

void OpenAiExpandFrameView::update_cursor(int edges) {
  const bool horizontal = (edges & (Left | Right)) != 0;
  const bool vertical = (edges & (Top | Bottom)) != 0;
  if (horizontal && vertical) {
    const bool main_diagonal = (edges == (Left | Top)) || (edges == (Right | Bottom));
    setCursor(main_diagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
  } else if (horizontal) {
    setCursor(Qt::SizeHorCursor);
  } else if (vertical) {
    setCursor(Qt::SizeVerCursor);
  } else {
    unsetCursor();
  }
}

void OpenAiExpandFrameView::mousePressEvent(QMouseEvent* event) {
  drag_edges_ = event->button() == Qt::LeftButton ? edges_at(event->position()) : None;
  if (drag_edges_ == None) {
    QWidget::mousePressEvent(event);
    return;
  }
  drag_start_ = event->position();
  drag_start_margins_ = margins_;
  // The scale is held for the whole drag, so the frame tracks the pointer even as it grows.
  drag_scale_ = layout_scale();
  event->accept();
}

void OpenAiExpandFrameView::mouseMoveEvent(QMouseEvent* event) {
  if (drag_edges_ == None) {
    update_cursor(edges_at(event->position()));
    return;
  }
  const auto delta = (event->position() - drag_start_) / drag_scale_;
  const auto clamp_side = [](double value) { return std::clamp(static_cast<int>(std::lround(value)), 0, 10000); };
  auto margins = drag_start_margins_;
  if ((drag_edges_ & Left) != 0) {
    margins.setLeft(clamp_side(drag_start_margins_.left() - delta.x()));
  }
  if ((drag_edges_ & Right) != 0) {
    margins.setRight(clamp_side(drag_start_margins_.right() + delta.x()));
  }
  if ((drag_edges_ & Top) != 0) {
    margins.setTop(clamp_side(drag_start_margins_.top() - delta.y()));
  }
  if ((drag_edges_ & Bottom) != 0) {
    margins.setBottom(clamp_side(drag_start_margins_.bottom() + delta.y()));
  }
  if (margins != margins_) {
    margins_ = margins;
    update();
    if (on_changed_) {
      on_changed_(margins_);
    }
  }
  event->accept();
}

void OpenAiExpandFrameView::mouseReleaseEvent(QMouseEvent* event) {
  if (drag_edges_ != None && event->button() == Qt::LeftButton) {
    drag_edges_ = None;
    update();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void OpenAiExpandFrameView::leaveEvent(QEvent* event) {
  if (drag_edges_ == None) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

OpenAiImagesDialog::OpenAiImagesDialog(MainWindow* window, QWidget* parent)
    : QDialog(parent), window_(window), client_(std::make_unique<openai::Client>(this)),
      key_store_(openai::make_key_store()) {
  setObjectName(QStringLiteral("openAiImagesDialog"));
  setWindowTitle(tr("OpenAI Image Tools via API Key"));
  build_ui();
  load_settings();
}

OpenAiImagesDialog::~OpenAiImagesDialog() { client_->cancel(); }

void OpenAiImagesDialog::build_ui() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  auto* content = install_dark_dialog_chrome(*this, root, tr("OpenAI Image Tools via API Key"));

  // Config (the last tab): the user's own key, billed by OpenAI, and the image model and
  // quality every image tab uses (Seth, 2026-11-06).
  auto* config_page = new QWidget(this);
  config_page->setObjectName(QStringLiteral("openAiImagesConfigPage"));
  auto* config_layout = new QVBoxLayout(config_page);
  config_layout->setContentsMargins(0, 8, 0, 0);
  auto* key_row = new QHBoxLayout();
  auto* key_label = new QLabel(tr("API key:"), this);
  key_edit_ = new QLineEdit(this);
  key_edit_->setObjectName(QStringLiteral("openAiImagesKeyEdit"));
  key_edit_->setEchoMode(QLineEdit::Password);
  key_edit_->setPlaceholderText(QStringLiteral("sk-..."));  // the key format, not prose
  key_label->setBuddy(key_edit_);
  check_button_ = new QPushButton(tr("Check Key"), this);
  check_button_->setObjectName(QStringLiteral("openAiImagesCheckKeyButton"));
  connect(check_button_, &QPushButton::clicked, this, &OpenAiImagesDialog::check_key);
  key_row->addWidget(key_label);
  key_row->addWidget(key_edit_, 1);
  key_row->addWidget(check_button_);
  config_layout->addLayout(key_row);

  auto* remember_row = new QHBoxLayout();
  remember_row->setSpacing(4);
  remember_check_ = new QCheckBox(tr("Remember the key on this computer"), this);
  remember_check_->setObjectName(QStringLiteral("openAiImagesRememberCheck"));
  remember_check_->setEnabled(key_store_->available());
  // The info mark (Clone Source's) explains where this platform keeps the key.
  auto* storage_info = new QToolButton(this);
  storage_info->setObjectName(QStringLiteral("openAiImagesKeyStorageInfoButton"));
  storage_info->setIcon(info_mark_icon());
  storage_info->setIconSize(QSize(16, 16));
  storage_info->setFixedSize(18, 18);
  storage_info->setAutoRaise(true);
  storage_info->setToolTip(tr("Where the key is saved"));
  storage_info->setAccessibleName(tr("Where the key is saved"));
  connect(storage_info, &QToolButton::clicked, this, [this] {
    show_information_message(this, tr("Where the Key Is Saved"), key_storage_note(),
                             QStringLiteral("openAiImagesKeyStorageMessage"));
  });
  connect(remember_check_, &QCheckBox::toggled, this, [this](bool remember) {
    if (remember && key_store_->unencrypted_file()) {
      // No system keyring here: say plainly where the key goes and that it is not encrypted.
      show_status(tr("The key will be saved unencrypted in %1. Only your user account can open the file, but "
                     "any program running as you can read it.")
                      .arg(QDir::toNativeSeparators(openai::key_file_path())),
                  true);
    }
    if (!remember && !stored_key_.isEmpty()) {
      (void)key_store_->clear();
      stored_key_.clear();
      show_status(tr("The key is no longer remembered on this computer."));
    }
  });
  auto* key_link = new QLabel(this);
  key_link->setObjectName(QStringLiteral("openAiImagesKeyLink"));
  key_link->setTextFormat(Qt::RichText);
  key_link->setTextInteractionFlags(Qt::TextBrowserInteraction);
  key_link->setOpenExternalLinks(true);
  set_themed_label_text(*key_link,
                        QStringLiteral("<a style=\"color:@accent\" href=\"https://platform.openai.com/api-keys\">%1</a>")
                            .arg(tr("Create an API key").toHtmlEscaped()));
  remember_row->addWidget(remember_check_);
  remember_row->addWidget(storage_info, 0, Qt::AlignVCenter);
  remember_row->addStretch(1);
  remember_row->addWidget(key_link);
  config_layout->addLayout(remember_row);

  // One tab per operation, so each is easy to find. Create is built; the others are planned
  // and wait on their design re-checks (docs/legal-constraints.md, "Prompt-driven image
  // generation"), so they show disabled.
  mode_tabs_ = new CurrentPageTabWidget(this);
  mode_tabs_->setObjectName(QStringLiteral("openAiImagesModeTabs"));
  mode_tabs_->setDocumentMode(true);
  // No base line beside the tabs (the panel tab group's reason, main_window_theme.cpp), and the
  // planned tabs read as unavailable.
  mode_tabs_->tabBar()->setDrawBase(false);
  // The current tab is filled with the accent, as the checked Variations button is.
  set_themed_style(*mode_tabs_->tabBar(), QStringLiteral(R"(
    QTabBar::tab:disabled { color: @text_disabled; }
    QTabBar::tab:selected { background: @accent; color: @text_on_accent; }
  )"));

  auto* create_page = new QWidget(mode_tabs_);
  create_page->setObjectName(QStringLiteral("openAiImagesCreatePage"));
  auto* create_layout = new QVBoxLayout(create_page);
  create_layout->setContentsMargins(0, 8, 0, 0);
  prompt_edit_ = new PromptEdit(create_page, [this] { (void)paste_references_from_clipboard(); });
  prompt_edit_->setObjectName(QStringLiteral("openAiImagesPromptEdit"));
  prompt_edit_->setPlaceholderText(
      tr("Describe the image to create. With reference images, refer to them by their numbers, for example: the cat "
         "from image 1 wearing the hat from image 2"));
  prompt_edit_->setAccessibleName(tr("Prompt"));
  prompt_edit_->setTabChangesFocus(true);
  // The global sheet themes QTextEdit but not QPlainTextEdit (ai_setup_dialog.cpp).
  set_themed_style(*prompt_edit_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  size_prompt_edit(*prompt_edit_);
  create_layout->addWidget(prompt_edit_);

  // Reference images: one list below the tabs, shown on Create, Edit and Animate.
  references_box_ = new QWidget(this);
  references_box_->setObjectName(QStringLiteral("openAiImagesReferencesBox"));
  auto* references_layout = new QVBoxLayout(references_box_);
  references_layout->setContentsMargins(0, 6, 0, 0);
  auto* references_label =
      new QLabel(tr("References (up to %1; the prompt names them by number):").arg(openai::kMaxReferenceImages),
                 references_box_);
  auto* reference_buttons = new QHBoxLayout();
  reference_buttons->addWidget(references_label);
  reference_buttons->addStretch(1);
  references_layout->addLayout(reference_buttons);
  references_list_ = new ReferenceList(references_box_, [this](const QMimeData* mime) {
    if (!add_references_from_mime(mime)) {
      show_status(tr("Patchy could not read an image from what was dropped."), true);
    }
  });
  references_list_->setObjectName(QStringLiteral("openAiImagesReferenceList"));
  references_list_->setViewMode(QListView::IconMode);
  references_list_->setFlow(QListView::LeftToRight);
  references_list_->setWrapping(false);
  references_list_->setMovement(QListView::Static);
  references_list_->setIconSize(QSize(kReferenceIconSize, kReferenceIconSize));
  references_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  // Drags arrive at the viewport, and Qt delivers them only to a widget that accepts drops;
  // setMovement(Static) above turned that off, so it is set after it.
  references_list_->viewport()->setAcceptDrops(true);
  references_list_->setFixedHeight(kReferenceListHeight);
  references_list_->setItemDelegate(new ReferenceDelegate(references_list_, [this](int row) {
    // Queued: the click is still being delivered to the item it removes.
    QTimer::singleShot(0, this, [this, row] { remove_reference(row); });
  }));
  references_label->setBuddy(references_list_);
  connect(references_list_, &QListWidget::itemSelectionChanged, this, &OpenAiImagesDialog::update_reference_buttons);
  references_layout->addWidget(references_list_);

  // Add: one menu button for the three sources, so the buttons share the label's row.
  auto* add_button = new QToolButton(references_box_);
  add_button->setObjectName(QStringLiteral("openAiImagesAddReferenceButton"));
  add_button->setText(tr("Add"));
  add_button->setPopupMode(QToolButton::InstantPopup);
  add_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto* add_menu = new QMenu(add_button);
  add_layer_button_ = add_menu->addAction(tr("Add Layer"));
  add_layer_button_->setObjectName(QStringLiteral("openAiImagesAddLayerAction"));
  add_layer_button_->setToolTip(tr("Add the active layer as a reference image"));
  connect(add_layer_button_, &QAction::triggered, this, &OpenAiImagesDialog::add_reference_from_layer);
  add_visible_button_ = add_menu->addAction(tr("Add Visible"));
  add_visible_button_->setObjectName(QStringLiteral("openAiImagesAddVisibleAction"));
  add_visible_button_->setToolTip(tr("Add the visible image, all layers merged, as a reference image"));
  connect(add_visible_button_, &QAction::triggered, this, &OpenAiImagesDialog::add_reference_from_visible);
  add_file_button_ = add_menu->addAction(tr("Add File..."));
  add_file_button_->setObjectName(QStringLiteral("openAiImagesAddFileAction"));
  connect(add_file_button_, &QAction::triggered, this, &OpenAiImagesDialog::add_reference_from_file);
  add_button->setMenu(add_menu);
  paste_reference_button_ = new QPushButton(tr("Paste"), references_box_);
  paste_reference_button_->setObjectName(QStringLiteral("openAiImagesPasteReferenceButton"));
  paste_reference_button_->setToolTip(tr("Add the image on the clipboard as a reference image"));
  connect(paste_reference_button_, &QPushButton::clicked, this, &OpenAiImagesDialog::paste_or_report);
  if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    connect(clipboard, &QClipboard::dataChanged, this, &OpenAiImagesDialog::update_reference_buttons);
  }
  // The platform's Paste keys (Ctrl+V, Cmd+V) anywhere in the dialog but a text field add the
  // clipboard's image (eventFilter); the key and prompt fields keep their own text paste.
  qApp->installEventFilter(this);
  remove_reference_button_ = new QPushButton(tr("Remove"), references_box_);
  remove_reference_button_->setObjectName(QStringLiteral("openAiImagesRemoveReferenceButton"));
  connect(remove_reference_button_, &QPushButton::clicked, this, [this] {
    for (auto* item : references_list_->selectedItems()) {
      delete item;
    }
    number_references();
    update_reference_buttons();
  });
  reference_buttons->addWidget(add_button);
  reference_buttons->addWidget(paste_reference_button_);
  reference_buttons->addWidget(remove_reference_button_);

  auto* create_options = new QFormLayout();
  size_combo_ = new QComboBox(create_page);
  size_combo_->setObjectName(QStringLiteral("openAiImagesSizeCombo"));
  size_combo_->addItem(tr("Match the canvas"), QString::fromLatin1(kSizeCanvas));
  size_combo_->addItem(tr("Square (1024 x 1024)"), QStringLiteral("1024x1024"));
  size_combo_->addItem(tr("Landscape (1536 x 1024)"), QStringLiteral("1536x1024"));
  size_combo_->addItem(tr("Portrait (1024 x 1536)"), QStringLiteral("1024x1536"));
  size_combo_->addItem(tr("Chosen by the model"), QString::fromLatin1(kSizeAuto));
  size_combo_->setToolTip(tr("Match the canvas requests the canvas's shape and scales the result to fit it"));
  // Size and Background share a row, to keep the dialog short.
  auto* create_row = new QWidget(create_page);
  auto* create_row_layout = new QHBoxLayout(create_row);
  create_row_layout->setContentsMargins(0, 0, 0, 0);
  create_row_layout->addWidget(size_combo_, 3);
  create_options->addRow(tr("Size:"), create_row);
  background_combo_ = new QComboBox(create_page);
  background_combo_->setObjectName(QStringLiteral("openAiImagesBackgroundCombo"));
  background_combo_->addItem(tr("Transparent"), QStringLiteral("transparent"));
  background_combo_->addItem(tr("Opaque"), QStringLiteral("opaque"));
  background_combo_->addItem(tr("Chosen by the model"), QStringLiteral("auto"));
  auto* background_label = new QLabel(tr("Background:"), create_row);
  background_label->setBuddy(background_combo_);
  create_row_layout->addSpacing(8);
  create_row_layout->addWidget(background_label);
  create_row_layout->addWidget(background_combo_, 2);
  create_layout->addLayout(create_options);
  create_layout->addStretch(1);
  mode_tabs_->addTab(create_page, tr("Create"));

  // Edit and Remove work on the selection: the visible image around it goes to OpenAI with a
  // mask, and the result comes back as a new layer masked to the selection
  // (docs/openai-images.md#edit-and-remove).
  const auto add_hint = [](QWidget* page, QVBoxLayout* layout, const QString& text, const char* object_name) {
    auto* hint = new QLabel(text, page);
    hint->setObjectName(QString::fromLatin1(object_name));
    hint->setWordWrap(true);
    layout->addWidget(hint);
  };
  auto* edit_page = new QWidget(mode_tabs_);
  edit_page->setObjectName(QStringLiteral("openAiImagesEditPage"));
  auto* edit_layout = new QVBoxLayout(edit_page);
  edit_layout->setContentsMargins(0, 8, 0, 0);
  add_hint(edit_page, edit_layout,
           tr("Select the area to change (Marquee, Lasso or Object Selection), or nothing to change the whole "
              "picture. The result goes on a new layer, masked to the selection; Edge blend sets how softly it meets "
              "the rest. The picture is sent as image 1, so reference images start at image 2."),
           "openAiImagesEditHint");
  edit_prompt_edit_ = new QPlainTextEdit(edit_page);
  edit_prompt_edit_->setObjectName(QStringLiteral("openAiImagesEditPromptEdit"));
  edit_prompt_edit_->setPlaceholderText(
      tr("Describe what the selection should become, or leave it empty to continue the surroundings"));
  edit_prompt_edit_->setAccessibleName(tr("Prompt"));
  edit_prompt_edit_->setTabChangesFocus(true);
  set_themed_style(*edit_prompt_edit_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  size_prompt_edit(*edit_prompt_edit_);
  edit_layout->addWidget(edit_prompt_edit_);
  edit_layout->addStretch(1);
  mode_tabs_->addTab(edit_page, tr("Edit"));

  auto* remove_page = new QWidget(mode_tabs_);
  remove_page->setObjectName(QStringLiteral("openAiImagesRemovePage"));
  auto* remove_layout = new QVBoxLayout(remove_page);
  remove_layout->setContentsMargins(0, 8, 0, 0);
  add_hint(remove_page, remove_layout,
           tr("Select what to remove, with a little room around it. The area is filled from its surroundings "
              "on a new layer, masked to the selection, so the original stays underneath."),
           "openAiImagesRemoveHint");
  remove_layout->addStretch(1);
  mode_tabs_->addTab(remove_page, tr("Remove"));

  // Expand grows the canvas by the pixels typed for each side (never a list of shapes, never a
  // crop) and fills the new area on a new layer masked to it.
  auto* expand_page = new QWidget(mode_tabs_);
  expand_page->setObjectName(QStringLiteral("openAiImagesExpandPage"));
  auto* expand_layout = new QVBoxLayout(expand_page);
  expand_layout->setContentsMargins(0, 8, 0, 0);
  add_hint(expand_page, expand_layout,
           tr("Add room around the image: Patchy grows the canvas by the pixels below and fills the new area on a "
              "new layer above the original, which stays untouched underneath. Blend sets how far that layer reaches "
              "into the original."),
           "openAiImagesExpandHint");
  expand_view_ = new OpenAiExpandFrameView(expand_page);
  expand_view_->setToolTip(tr("Drag an edge or a corner outward to add room on that side"));
  expand_view_->set_on_margins_changed([this](QMargins margins) {
    const QSignalBlocker block_left(expand_left_);
    const QSignalBlocker block_top(expand_top_);
    const QSignalBlocker block_right(expand_right_);
    const QSignalBlocker block_bottom(expand_bottom_);
    expand_left_->setValue(margins.left());
    expand_top_->setValue(margins.top());
    expand_right_->setValue(margins.right());
    expand_bottom_->setValue(margins.bottom());
    update_expand_readout();
  });
  expand_layout->addWidget(expand_view_, 2);
  auto* sides = new QGridLayout();
  const auto make_side = [&](const QString& label, const char* object_name, int row, int column) {
    auto* caption = new QLabel(label, expand_page);
    auto* spin = new QSpinBox(expand_page);
    spin->setObjectName(QString::fromLatin1(object_name));
    spin->setRange(0, 10000);
    spin->setSuffix(tr(" px"));
    caption->setBuddy(spin);
    sides->addWidget(caption, row, column * 2);
    sides->addWidget(spin, row, column * 2 + 1);
    connect(spin, &QSpinBox::valueChanged, this, &OpenAiImagesDialog::update_expand_readout);
    return spin;
  };
  expand_left_ = make_side(tr("Left:"), "openAiImagesExpandLeft", 0, 0);
  expand_right_ = make_side(tr("Right:"), "openAiImagesExpandRight", 0, 1);
  expand_top_ = make_side(tr("Top:"), "openAiImagesExpandTop", 1, 0);
  expand_bottom_ = make_side(tr("Bottom:"), "openAiImagesExpandBottom", 1, 1);
  sides->setColumnStretch(4, 1);
  expand_layout->addLayout(sides);
  expand_readout_ = new QLabel(expand_page);
  expand_readout_->setObjectName(QStringLiteral("openAiImagesExpandReadout"));
  // Blend, on the readout's row: how much of the original next to the new area the model's
  // version replaces before it fades into the original. It applies to the current result.
  auto* readout_row = new QHBoxLayout();
  readout_row->addWidget(expand_readout_);
  readout_row->addStretch(1);
  auto* blend_label = new QLabel(tr("Blend:"), expand_page);
  expand_blend_slider_ = new QSlider(Qt::Horizontal, expand_page);
  expand_blend_slider_->setObjectName(QStringLiteral("openAiImagesExpandBlendSlider"));
  expand_blend_slider_->setRange(0, 100);
  expand_blend_slider_->setValue(kExpandBlendDefault);
  expand_blend_slider_->setMinimumWidth(160);
  expand_blend_slider_->setToolTip(
      tr("How much of the original next to the new area takes OpenAI's version before fading into the original: "
         "0% keeps the original up to its edge, 100% uses OpenAI's whole picture"));
  blend_label->setBuddy(expand_blend_slider_);
  expand_blend_label_ = new QLabel(expand_page);
  expand_blend_label_->setMinimumWidth(40);
  expand_blend_label_->setText(tr("%1%").arg(expand_blend_slider_->value()));
  readout_row->addWidget(blend_label);
  readout_row->addWidget(expand_blend_slider_);
  readout_row->addWidget(expand_blend_label_);
  expand_layout->addLayout(readout_row);
  expand_blend_timer_ = new QTimer(this);
  expand_blend_timer_->setSingleShot(true);
  expand_blend_timer_->setInterval(40);
  connect(expand_blend_timer_, &QTimer::timeout, this, &OpenAiImagesDialog::apply_expand_blend);
  connect(expand_blend_slider_, &QSlider::valueChanged, this, [this](int value) {
    expand_blend_label_->setText(tr("%1%").arg(value));
    expand_blend_timer_->start();  // coalesce a drag's steps
  });
  expand_prompt_edit_ = new QPlainTextEdit(expand_page);
  expand_prompt_edit_->setObjectName(QStringLiteral("openAiImagesExpandPromptEdit"));
  expand_prompt_edit_->setPlaceholderText(tr("Optional: describe what the new area shows"));
  expand_prompt_edit_->setAccessibleName(tr("Prompt"));
  expand_prompt_edit_->setTabChangesFocus(true);
  set_themed_style(*expand_prompt_edit_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  expand_prompt_edit_->setMaximumHeight(70);
  expand_layout->addWidget(expand_prompt_edit_);
  mode_tabs_->addTab(expand_page, tr("Expand"));

  // Animate: an animated GIF from the prompt, or from the prompt and one image. Each GIF
  // opens as a new document with a layer per frame (docs/openai-images-tabs.md#animate).
  auto* animate_page = new QWidget(mode_tabs_);
  animate_page->setObjectName(QStringLiteral("openAiImagesAnimatePage"));
  auto* animate_layout = new QVBoxLayout(animate_page);
  animate_layout->setContentsMargins(0, 8, 0, 0);
  add_hint(animate_page, animate_layout,
           tr("Make a short animation. It opens as a new document with one layer per frame, frame 1 on top, ready "
              "for File > Export > Layers as Animated GIF."),
           "openAiImagesAnimateHint");
  animate_prompt_edit_ = new QPlainTextEdit(animate_page);
  animate_prompt_edit_->setObjectName(QStringLiteral("openAiImagesAnimatePromptEdit"));
  animate_prompt_edit_->setPlaceholderText(tr("Describe the motion, for example: the flag waves in the wind"));
  animate_prompt_edit_->setAccessibleName(tr("Prompt"));
  animate_prompt_edit_->setTabChangesFocus(true);
  set_themed_style(*animate_prompt_edit_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  size_prompt_edit(*animate_prompt_edit_);
  animate_layout->addWidget(animate_prompt_edit_);
  // Two columns of options, to keep the dialog short.
  auto* animate_options = new QGridLayout();
  animate_options->setHorizontalSpacing(8);
  animate_options->setColumnStretch(1, 1);
  animate_options->setColumnStretch(3, 1);
  const auto add_option = [&animate_options, animate_page](int row, int column, const QString& text, QWidget* field,
                                                           int span = 1) {
    auto* label = new QLabel(text, animate_page);
    label->setBuddy(field);
    animate_options->addWidget(label, row, column);
    animate_options->addWidget(field, row, column + 1, 1, span);
  };
  animate_source_combo_ = new QComboBox(animate_page);
  animate_source_combo_->setObjectName(QStringLiteral("openAiImagesAnimateSourceCombo"));
  animate_source_combo_->addItem(tr("The prompt only"), QStringLiteral("prompt"));
  animate_source_combo_->addItem(tr("The active layer"), QStringLiteral("layer"));
  animate_source_combo_->addItem(tr("The visible image"), QStringLiteral("visible"));
  add_option(0, 0, tr("Start from:"), animate_source_combo_);
  // Colors: OpenAI's GIF mode (the default, Seth 2026-11-07) shares one 256-color palette
  // across all frames; Full color asks for the frames on one PNG sheet and slices it here.
  animate_colors_combo_ = new QComboBox(animate_page);
  animate_colors_combo_->setObjectName(QStringLiteral("openAiImagesAnimateColorsCombo"));
  animate_colors_combo_->addItem(tr("GIF (256 colors)"), QStringLiteral("gif"));
  animate_colors_combo_->addItem(tr("Full color (one sheet)"), QStringLiteral("sheet"));
  animate_colors_combo_->setToolTip(
      tr("GIF uses OpenAI's animation mode, with 256 colors shared by all frames. Full color asks for the frames "
         "as one full-color picture and cuts it into layers, with soft transparency; the frame count rounds up to "
         "fill its grid."));
  add_option(0, 2, tr("Colors:"), animate_colors_combo_);
  // A starting image goes first, so the references' numbers move up by one.
  connect(animate_source_combo_, &QComboBox::currentIndexChanged, this, [this] { number_references(); });
  animate_frames_spin_ = new QSpinBox(animate_page);
  animate_frames_spin_->setObjectName(QStringLiteral("openAiImagesAnimateFramesSpin"));
  animate_frames_spin_->setRange(openai::kMinGifFrames, openai::kMaxGifFrames);
  animate_frames_spin_->setValue(9);
  add_option(1, 0, tr("Frames:"), animate_frames_spin_);
  animate_delay_spin_ = new QSpinBox(animate_page);
  animate_delay_spin_->setObjectName(QStringLiteral("openAiImagesAnimateDelaySpin"));
  animate_delay_spin_->setRange(openai::kMinGifFrameMs, openai::kMaxGifFrameMs);
  animate_delay_spin_->setSingleStep(10);
  animate_delay_spin_->setValue(200);
  animate_delay_spin_->setSuffix(tr(" ms"));
  add_option(1, 2, tr("Frame delay:"), animate_delay_spin_);
  animate_size_combo_ = new QComboBox(animate_page);
  animate_size_combo_->setObjectName(QStringLiteral("openAiImagesAnimateSizeCombo"));
  animate_size_combo_->addItem(tr("512 x 512"), 512);
  animate_size_combo_->addItem(tr("720 x 720"), 720);
  animate_size_combo_->addItem(tr("960 x 960"), 960);
  animate_size_combo_->setToolTip(
      tr("OpenAI draws every frame on one sheet, so more than 9 frames allow at most 720 x 720"));
  add_option(2, 0, tr("Frame size:"), animate_size_combo_);
  // Sizes too large for the frame count are greyed out (openai::max_gif_frame_edge).
  const auto limit_sizes = [this](int frames) {
    auto* items = qobject_cast<QStandardItemModel*>(animate_size_combo_->model());
    if (items == nullptr) {
      return;
    }
    const int largest = openai::max_gif_frame_edge(frames);
    int fallback = 0;
    for (int row = 0; row < animate_size_combo_->count(); ++row) {
      const bool fits = animate_size_combo_->itemData(row).toInt() <= largest;
      items->item(row)->setEnabled(fits);
      if (fits) {
        fallback = row;
      }
    }
    if (animate_size_combo_->itemData(animate_size_combo_->currentIndex()).toInt() > largest) {
      animate_size_combo_->setCurrentIndex(fallback);
    }
  };
  connect(animate_frames_spin_, &QSpinBox::valueChanged, this, limit_sizes);
  limit_sizes(animate_frames_spin_->value());
  animate_background_combo_ = new QComboBox(animate_page);
  animate_background_combo_->setObjectName(QStringLiteral("openAiImagesAnimateBackgroundCombo"));
  animate_background_combo_->addItem(tr("Transparent"), QStringLiteral("transparent"));
  animate_background_combo_->addItem(tr("Opaque"), QStringLiteral("opaque"));
  animate_background_combo_->setToolTip(tr("GIF transparency is on or off per pixel, so soft edges come out hard"));
  add_option(2, 2, tr("Background:"), animate_background_combo_);
  animate_layout->addLayout(animate_options);
  animate_layout->addStretch(1);
  mode_tabs_->addTab(animate_page, tr("Animate"));

  // Describe: the image goes to a text model and its description fills the box here. Text
  // only: nothing turns it into a prompt or adds it to the document
  // (docs/legal-constraints.md, "Prompt-driven image generation").
  auto* describe_page = new QWidget(mode_tabs_);
  describe_page->setObjectName(QStringLiteral("openAiImagesDescribePage"));
  auto* describe_layout = new QVBoxLayout(describe_page);
  describe_layout->setContentsMargins(0, 8, 0, 0);
  add_hint(describe_page, describe_layout,
           tr("The image below goes to OpenAI's %1 model, which writes a detailed description of it here. Nothing "
              "is added to the document.")
               .arg(QString::fromLatin1(openai::kDescribeModel)),
           "openAiImagesDescribeHint");
  auto* describe_options = new QFormLayout();
  describe_source_combo_ = new QComboBox(describe_page);
  describe_source_combo_->setObjectName(QStringLiteral("openAiImagesDescribeSourceCombo"));
  describe_source_combo_->addItem(tr("The visible image"), QStringLiteral("visible"));
  describe_source_combo_->addItem(tr("The active layer"), QStringLiteral("layer"));
  describe_options->addRow(tr("Image:"), describe_source_combo_);
  describe_layout->addLayout(describe_options);
  describe_preview_ = new QLabel(describe_page);
  describe_preview_->setObjectName(QStringLiteral("openAiImagesDescribePreview"));
  describe_preview_->setAlignment(Qt::AlignCenter);
  describe_preview_->setFixedHeight(130);
  describe_layout->addWidget(describe_preview_);
  describe_text_edit_ = new QPlainTextEdit(describe_page);
  describe_text_edit_->setObjectName(QStringLiteral("openAiImagesDescribeText"));
  describe_text_edit_->setReadOnly(true);
  // Read-only selects by mouse only; the keyboard too, so Ctrl+A then Ctrl+C copies it all.
  describe_text_edit_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  describe_text_edit_->setPlaceholderText(tr("The description appears here"));
  describe_text_edit_->setAccessibleName(tr("Description"));
  describe_text_edit_->setTabChangesFocus(true);
  set_themed_style(*describe_text_edit_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  size_prompt_edit(*describe_text_edit_, kDescribeLines);
  describe_layout->addWidget(describe_text_edit_);
  describe_layout->addStretch(1);
  connect(describe_source_combo_, &QComboBox::currentIndexChanged, this, [this] { refresh_describe_preview(); });
  mode_tabs_->addTab(describe_page, tr("Describe"));
  content->addWidget(mode_tabs_, 1);
  content->addWidget(references_box_);

  // Shared by every operation.
  auto* shared_options = new QFormLayout();
  model_combo_ = new QComboBox(this);
  model_combo_->setObjectName(QStringLiteral("openAiImagesModelCombo"));
  model_combo_->addItem(tr("%1 (higher quality)").arg(QString::fromLatin1(kModelIds[0])),
                        QString::fromLatin1(kModelIds[0]));
  model_combo_->addItem(tr("%1 (faster)").arg(QString::fromLatin1(kModelIds[1])), QString::fromLatin1(kModelIds[1]));
  model_combo_->setToolTip(tr("Check Key greys out a model your key cannot use"));
  // Model and Quality share a row, to keep the dialog short.
  auto* model_row = new QWidget(this);
  auto* model_row_layout = new QHBoxLayout(model_row);
  model_row_layout->setContentsMargins(0, 0, 0, 0);
  model_row_layout->addWidget(model_combo_, 3);
  quality_combo_ = new QComboBox(this);
  quality_combo_->setObjectName(QStringLiteral("openAiImagesQualityCombo"));
  quality_combo_->addItem(tr("Automatic"), QStringLiteral("auto"));
  quality_combo_->addItem(tr("Low"), QStringLiteral("low"));
  quality_combo_->addItem(tr("Medium"), QStringLiteral("medium"));
  quality_combo_->addItem(tr("High"), QStringLiteral("high"));
  auto* quality_label = new QLabel(tr("Quality:"), model_row);
  quality_label->setBuddy(quality_combo_);
  model_row_layout->addSpacing(8);
  model_row_layout->addWidget(quality_label);
  model_row_layout->addWidget(quality_combo_, 2);
  auto* config_options = new QFormLayout();
  config_options->addRow(tr("Model:"), model_row);
  config_layout->addLayout(config_options);
  add_hint(config_page, config_layout,
           tr("The model and quality apply to every image tab. Describe always uses %1.")
               .arg(QString::fromLatin1(openai::kDescribeModel)),
           "openAiImagesConfigHint");
  config_layout->addStretch(1);
  mode_tabs_->addTab(config_page, tr("Config"));
  // Variations: one button per count, each image its own layer.
  auto* count_row = new QHBoxLayout();
  count_row->setSpacing(2);
  count_group_ = new QButtonGroup(this);
  count_group_->setExclusive(true);
  for (int count = 1; count <= openai::kMaxImagesPerRequest; ++count) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("openAiImagesCount%1").arg(count));
    button->setText(QString::number(count));
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(24);
    button->setToolTip(tr("Each image becomes its own layer, and each one is billed"));
    count_group_->addButton(button, count);
    count_row->addWidget(button);
  }
  count_group_->button(1)->setChecked(true);
  shared_options->addRow(tr("Variations:"), count_row);
  // Results: one thumbnail per variation that came back; clicking one shows it in the result
  // layer. The Variations buttons above keep choosing how many the next request asks for.
  results_row_ = new QWidget(this);
  results_row_->setObjectName(QStringLiteral("openAiImagesResultsRow"));
  auto* results_layout = new QHBoxLayout(results_row_);
  results_layout->setContentsMargins(0, 0, 0, 0);
  results_layout->setSpacing(4);
  results_group_ = new QButtonGroup(this);
  results_group_->setExclusive(true);
  for (int index = 0; index < openai::kMaxImagesPerRequest; ++index) {
    auto* button = new QToolButton(results_row_);
    button->setObjectName(QStringLiteral("openAiImagesResult%1").arg(index + 1));
    button->setCheckable(true);
    button->setIconSize(QSize(44, 44));
    button->setFixedSize(52, 52);
    button->setToolTip(tr("Show variation %1").arg(index + 1));
    results_group_->addButton(button, index);
    results_layout->addWidget(button);
  }
  results_layout->addStretch(1);
  connect(results_group_, &QButtonGroup::idClicked, this, [this](int index) { (void)show_variation(index); });
  results_label_ = new QLabel(tr("Results:"), this);
  shared_options->addRow(results_label_, results_row_);
  // Edit and Remove: a new layer masked to the selection, or straight into the active layer.
  target_combo_ = new QComboBox(this);
  target_combo_->setObjectName(QStringLiteral("openAiImagesTargetCombo"));
  target_combo_->addItem(tr("New layer"), QStringLiteral("new"));
  target_combo_->addItem(tr("Active layer"), QStringLiteral("active"));
  target_combo_->setToolTip(tr("Active layer writes the result into the active pixel layer, inside the selection"));
  target_label_ = new QLabel(tr("Target:"), this);
  // Target and Edge blend share a row (Edit and Remove only), to keep the dialog short.
  auto* masked_row = new QWidget(this);
  auto* masked_row_layout = new QHBoxLayout(masked_row);
  masked_row_layout->setContentsMargins(0, 0, 0, 0);
  masked_row_layout->addWidget(target_combo_, 2);
  // Edge blend: how far past the selection the result shows and how softly it meets the rest;
  // it applies to the current result as it moves.
  edge_row_ = new QWidget(this);
  auto* edge_layout = new QHBoxLayout(edge_row_);
  edge_layout->setContentsMargins(0, 0, 0, 0);
  edge_slider_ = new QSlider(Qt::Horizontal, edge_row_);
  edge_slider_->setObjectName(QStringLiteral("openAiImagesEdgeBlendSlider"));
  edge_slider_->setRange(0, 100);
  edge_slider_->setValue(kEdgeBlendDefault);
  edge_slider_->setToolTip(tr("0% keeps the result inside the selection with a hard edge; 100% lets it reach past "
                              "the selection with a soft edge"));
  edge_value_label_ = new QLabel(edge_row_);
  edge_value_label_->setMinimumWidth(40);
  edge_layout->addWidget(edge_slider_, 1);
  edge_layout->addWidget(edge_value_label_);
  edge_timer_ = new QTimer(this);
  edge_timer_->setSingleShot(true);
  edge_timer_->setInterval(40);
  connect(edge_timer_, &QTimer::timeout, this, &OpenAiImagesDialog::apply_edge_blend);
  connect(edge_slider_, &QSlider::valueChanged, this, [this](int value) {
    edge_value_label_->setText(tr("%1%").arg(value));
    edge_timer_->start();  // coalesce a drag's steps
  });
  edge_value_label_->setText(tr("%1%").arg(edge_slider_->value()));
  auto* edge_label = new QLabel(tr("Edge blend:"), masked_row);
  edge_label->setBuddy(edge_slider_);
  masked_row_layout->addSpacing(8);
  masked_row_layout->addWidget(edge_label);
  masked_row_layout->addWidget(edge_row_, 3);
  shared_options->addRow(target_label_, masked_row);
  content->addLayout(shared_options);
  shared_options->setRowVisible(masked_row, false);
  connect(mode_tabs_, &QTabWidget::currentChanged, this, [this, shared_options, count_row, masked_row] {
    const bool masked = mode_tabs_->currentIndex() == EditTab || mode_tabs_->currentIndex() == RemoveTab;
    shared_options->setRowVisible(masked_row, masked);
    // Variations apply to the image tabs; Describe uses a text model, Config sends nothing.
    const bool images = mode_tabs_->currentIndex() != DescribeTab && mode_tabs_->currentIndex() != ConfigTab;
    shared_options->setRowVisible(count_row, images);
    const int tab = mode_tabs_->currentIndex();
    fit_tabs_to_current_page();
    references_box_->setVisible(tab == CreateTab || tab == EditTab || tab == AnimateTab);
    number_references();
    // After the shown and hidden widgets settle, so the minimum is the new tab's.
    QTimer::singleShot(0, this, &OpenAiImagesDialog::fit_height_to_current_tab);
    update_action_button();
  });
  // Both option groups share one label column, so their fields start at the same x.
  int label_width = 0;
  std::vector<QWidget*> option_labels;
  for (auto* form : {create_options, shared_options}) {
    for (int row = 0; row < form->rowCount(); ++row) {
      if (auto* item = form->itemAt(row, QFormLayout::LabelRole); item != nullptr && item->widget() != nullptr) {
        option_labels.push_back(item->widget());
        label_width = std::max(label_width, item->widget()->sizeHint().width());
      }
    }
  }
  for (auto* label : option_labels) {
    label->setMinimumWidth(label_width);
  }

  auto* notice = new QLabel(tr("Your prompt and any reference images are sent to OpenAI, and generations use your API "
                                "key, so be careful as they cost real money."),
                             this);
  notice->setObjectName(QStringLiteral("openAiImagesNoticeLabel"));
  notice->setWordWrap(true);
  content->addWidget(notice);

  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("openAiImagesStatusLabel"));
  status_label_->setWordWrap(true);
  status_label_->setTextFormat(Qt::RichText);
  status_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  status_label_->setOpenExternalLinks(true);
  set_themed_style(*status_label_, QStringLiteral(R"(
    QLabel[error="true"] { color: @status_error_text; }
  )"));
  content->addWidget(status_label_);

  // The action button fills the row and is named after the operation.
  auto* buttons = new QHBoxLayout();
  stop_button_ = new QPushButton(tr("Stop"), this);
  stop_button_->setObjectName(QStringLiteral("openAiImagesStopButton"));
  stop_button_->setEnabled(false);
  connect(stop_button_, &QPushButton::clicked, this, &OpenAiImagesDialog::cancel_request);
  generate_button_ = new QPushButton(tr("Create"), this);
  generate_button_->setObjectName(QStringLiteral("openAiImagesGenerateButton"));
  generate_button_->setDefault(true);
  generate_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  generate_button_->setMinimumHeight(30);
  connect(generate_button_, &QPushButton::clicked, this, &OpenAiImagesDialog::generate);
  auto* close_button = new QPushButton(tr("Close"), this);
  close_button->setObjectName(QStringLiteral("openAiImagesCloseButton"));
  connect(close_button, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(stop_button_);
  buttons->addWidget(generate_button_, 1);
  buttons->addWidget(close_button);
  content->addLayout(buttons);

  spinner_timer_ = new QTimer(this);
  spinner_timer_->setInterval(70);
  connect(spinner_timer_, &QTimer::timeout, this, &OpenAiImagesDialog::advance_spinner);
  busy_timer_ = new QTimer(this);
  busy_timer_->setInterval(1000);
  connect(busy_timer_, &QTimer::timeout, this, [this] {
    show_status(tr("%1 (%2 s)").arg(busy_message_).arg(elapsed_.elapsed() / 1000));
  });
  update_reference_buttons();
  clear_results();
  // The dialog is as tall as the current tab needs, which keeps it shorter than the main
  // window's smallest height (about 690 px), so it never covers more than the window it
  // belongs to (Seth, 2026-11-06). QTabWidget's preferred size counts every page, so the
  // height comes from the minimum size, where hidden pages ask for nothing.
  fit_tabs_to_current_page();
  resize(760, std::max(minimumSizeHint().height(), kDialogFloorHeight));
}

void OpenAiImagesDialog::load_settings() {
  auto settings = app_settings();
  // A model the dialog no longer offers (an older family) falls back to the default.
  select_data(*model_combo_, openai::default_model());
  select_data(*model_combo_, settings.value(QString::fromLatin1(kModelKey), openai::default_model()).toString());
  select_data(*size_combo_, settings.value(QString::fromLatin1(kSizeKey), QString::fromLatin1(kSizeCanvas)).toString());
  select_data(*quality_combo_, settings.value(QString::fromLatin1(kQualityKey), QStringLiteral("auto")).toString());
  select_data(*background_combo_,
              settings.value(QString::fromLatin1(kBackgroundKey), QStringLiteral("transparent")).toString());
  count_group_->button(std::clamp(settings.value(QString::fromLatin1(kCountKey), 1).toInt(), 1,
                                  openai::kMaxImagesPerRequest))
      ->setChecked(true);

  select_data(*target_combo_, settings.value(QString::fromLatin1(kTargetKey), QStringLiteral("new")).toString());
  select_data(*animate_colors_combo_, settings.value(QString::fromLatin1(kAnimateColorsKey), QStringLiteral("gif")).toString());
  {
    const QSignalBlocker block_edge(edge_slider_);
    edge_slider_->setValue(std::clamp(settings.value(QString::fromLatin1(kEdgeBlendKey), kEdgeBlendDefault).toInt(), 0, 100));
    edge_value_label_->setText(tr("%1%").arg(edge_slider_->value()));
    const QSignalBlocker block_expand(expand_blend_slider_);
    expand_blend_slider_->setValue(
        std::clamp(settings.value(QString::fromLatin1(kExpandBlendKey), kExpandBlendDefault).toInt(), 0, 100));
    expand_blend_label_->setText(tr("%1%").arg(expand_blend_slider_->value()));
  }

  stored_key_ = key_store_->available() ? key_store_->load() : QString();
  const QSignalBlocker block(remember_check_);
  if (!stored_key_.isEmpty()) {
    key_edit_->setText(stored_key_);
    remember_check_->setChecked(true);
  } else if (const auto environment_key = openai::environment_api_key(); !environment_key.isEmpty()) {
    key_edit_->setText(environment_key);
    show_status(tr("Using the key from the OPENAI_API_KEY environment variable."));
  }
}

void OpenAiImagesDialog::save_settings() const {
  auto settings = app_settings();
  settings.setValue(QString::fromLatin1(kModelKey), model_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kSizeKey), size_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kQualityKey), quality_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kBackgroundKey), background_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kCountKey), image_count());
  settings.setValue(QString::fromLatin1(kTargetKey), target_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kAnimateColorsKey), animate_colors_combo_->currentData().toString());
  settings.setValue(QString::fromLatin1(kEdgeBlendKey), edge_slider_->value());
}

QString OpenAiImagesDialog::take_api_key() {
  const auto key = key_edit_->text().trimmed();
  if (key.isEmpty()) {
    mode_tabs_->setCurrentIndex(ConfigTab);  // the key lives on the Config tab
    show_status(tr("Enter your OpenAI API key first."), true);
    key_edit_->setFocus();
    return {};
  }
  if (remember_check_->isChecked() && key_store_->available() && key != stored_key_) {
    if (key_store_->save(key)) {
      stored_key_ = key;
    } else {
      show_status(tr("The key could not be saved on this computer; it is used for this session only."), true);
    }
  }
  client_->set_api_key(key);
  return key;
}

void OpenAiImagesDialog::check_key() {
  if (client_->busy() || take_api_key().isEmpty()) {
    return;
  }
  set_busy(true, tr("Checking the key..."), tr("Checking..."));
  client_->list_models(
      [this](QStringList models) {
        set_busy(false);
        select_available_models(models);
        const bool any = std::any_of(std::begin(kModelIds), std::end(kModelIds),
                                     [&](const char* id) { return models.contains(QString::fromLatin1(id)); });
        if (!any) {
          show_status(tr("The key works, but it cannot use the GPT Image 2.6 models."), true);
          return;
        }
        show_status(tr("The key works."));
      },
      [this](const QString& error) {
        set_busy(false);
        show_error(error, client_->last_error_help());
      });
}

openai::GenerateRequest OpenAiImagesDialog::build_request(bool* fit_to_canvas) const {
  openai::GenerateRequest request;
  request.model = model_combo_->currentData().toString();
  request.prompt = prompt_edit_->toPlainText().trimmed();
  request.quality = quality_combo_->currentData().toString();
  request.background = background_combo_->currentData().toString();
  request.count = image_count();
  const auto size_token = size_combo_->currentData().toString();
  *fit_to_canvas = size_token == QLatin1String(kSizeCanvas);
  if (*fit_to_canvas) {
    request.size = openai::fit_request_size(window_ != nullptr ? window_->openai_canvas_size() : QSize());
  } else if (size_token != QLatin1String(kSizeAuto)) {
    const auto parts = size_token.split(QLatin1Char('x'));
    if (parts.size() == 2) {
      request.size = QSize(parts[0].toInt(), parts[1].toInt());
    }
  }
  for (int row = 0; row < references_list_->count(); ++row) {
    request.references.push_back(references_list_->item(row)->data(Qt::UserRole).value<QImage>());
  }
  return request;
}

void OpenAiImagesDialog::update_action_button() {
  if (client_ != nullptr && client_->busy()) {
    return;  // the button shows the request's progress until it ends
  }
  // Config has nothing to send: no action button there.
  generate_button_->setVisible(mode_tabs_->currentIndex() != ConfigTab);
  switch (mode_tabs_->currentIndex()) {
    case EditTab:
      generate_button_->setText(tr("Edit"));
      break;
    case RemoveTab:
      generate_button_->setText(tr("Remove"));
      break;
    case ExpandTab:
      generate_button_->setText(tr("Expand"));
      // A fresh thumbnail each time the tab opens: the document may have changed.
      if (window_ != nullptr && expand_view_ != nullptr) {
        QString ignored;
        const auto visible = window_->openai_reference_from_visible_image(&ignored);
        expand_view_->set_image(visible.isNull() ? QImage()
                                                 : visible.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                                window_->openai_canvas_size());
      }
      update_expand_readout();
      break;
    case AnimateTab:
      generate_button_->setText(tr("Animate"));
      break;
    case DescribeTab:
      generate_button_->setText(tr("Describe Image"));
      refresh_describe_preview();  // the document may have changed since
      break;
    default:
      generate_button_->setText(tr("Create"));
      break;
  }
}

void OpenAiImagesDialog::update_expand_readout() {
  if (expand_readout_ == nullptr) {
    return;
  }
  const auto canvas = window_ != nullptr ? window_->openai_canvas_size() : QSize();
  if (expand_view_ != nullptr) {
    expand_view_->set_margins(
        QMargins(expand_left_->value(), expand_top_->value(), expand_right_->value(), expand_bottom_->value()));
  }
  if (canvas.isEmpty()) {
    expand_readout_->setText(tr("Open a document first."));
    if (expand_view_ != nullptr) {
      expand_view_->set_image({}, {});
    }
    return;
  }
  expand_readout_->setText(tr("New canvas: %1 x %2 px")
                               .arg(canvas.width() + expand_left_->value() + expand_right_->value())
                               .arg(canvas.height() + expand_top_->value() + expand_bottom_->value()));
}

void OpenAiImagesDialog::generate_expand() {
  const QMargins margins(expand_left_->value(), expand_top_->value(), expand_right_->value(), expand_bottom_->value());
  if (margins.isNull()) {
    show_status(tr("Add pixels to at least one side."), true);
    return;
  }
  QString error;
  std::int64_t session_id = -1;
  QSize canvas;
  QImage visible;
  if (window_ == nullptr || !window_->openai_expand_source(&session_id, &canvas, &visible, &error)) {
    show_status(error.isEmpty() ? tr("Open a document first.") : error, true);
    return;
  }
  constexpr int kMaxCanvasEdge = 30000;
  if (canvas.width() + margins.left() + margins.right() > kMaxCanvasEdge ||
      canvas.height() + margins.top() + margins.bottom() > kMaxCanvasEdge) {
    show_status(tr("The new canvas would be larger than %1 px on a side.").arg(kMaxCanvasEdge), true);
    return;
  }
  if (take_api_key().isEmpty()) {
    return;
  }
  save_settings();
  const auto frame = openai::expand_frame(visible, margins);
  const auto sent = openai::masked_edit_images(frame.context, frame.coverage);
  openai::GenerateRequest request;
  request.model = model_combo_->currentData().toString();
  request.quality = quality_combo_->currentData().toString();
  request.count = image_count();
  request.background = QStringLiteral("opaque");
  request.size = sent.image.size();
  request.complexity = QString::fromLatin1(openai::kMaskedEditComplexity);
  // The frame alone, its new area transparent, and no mask (docs/openai-images-tabs.md#expand).
  request.edit_image = sent.image;
  const auto typed = expand_prompt_edit_->toPlainText().trimmed();
  request.prompt = typed.isEmpty() ? QString::fromLatin1(openai::kExpandPromptWhenEmpty)
                                   : typed + QStringLiteral("\n\n") + QString::fromLatin1(openai::kExpandKeepInstructions);
  const auto name = tr("OpenAI Expand");
  set_busy(true, tr("Expanding..."), tr("Expanding..."));
  client_->generate(
      request,
      [this, session_id, canvas, margins, sent, name, visible](openai::GenerateResult result) {
        set_busy(false);
        if (window_ == nullptr || result.images.empty()) {
          return;
        }
        ResultSet set;
        for (const auto& image : result.images) {
          set.images.push_back(openai::result_for_region(image, sent));
        }
        // Blend is read when the pixels are prepared: at arrival, a variation swap, or a move
        // of the slider (apply_expand_blend).
        set.prepare = [this, visible, margins](const QImage& image) {
          return openai::expand_layer_image(image, visible, margins, expand_blend_amount());
        };
        set.expand_adjustable = true;
        LayerId layer_id = 0;
        const auto added = window_->add_openai_expanded_layers(session_id, {set.prepare(set.images.front())}, canvas,
                                                               margins, name, &layer_id);
        if (added < 0) {
          show_status(tr("The images arrived, but that document is closed or its canvas changed meanwhile."), true);
          return;
        }
        if (added == 0) {
          show_status(tr("The images arrived, but could not be added to a document."), true);
          return;
        }
        set.session_id = session_id;
        set.layer_id = layer_id;
        // The expansion is applied: clear the side fields, so the frame shows the grown canvas
        // alone instead of the same margins again as a next, unfilled area (Seth, 2026-11-06).
        for (auto* spin : {expand_left_, expand_top_, expand_right_, expand_bottom_}) {
          spin->setValue(0);
        }
        update_action_button();  // a fresh thumbnail of the grown canvas
        present_results(std::move(set));
        report_added(added, result.total_tokens);
      },
      [this](const QString& message) {
        set_busy(false);
        show_error(message, client_->last_error_help());
      });
}

QImage OpenAiImagesDialog::describe_source_image(QString* error) const {
  if (window_ == nullptr) {
    return {};
  }
  QString label;
  return describe_source_combo_->currentData().toString() == QLatin1String("layer")
             ? window_->openai_reference_from_active_layer(&label, error)
             : window_->openai_reference_from_visible_image(error);
}

void OpenAiImagesDialog::refresh_describe_preview() {
  if (describe_preview_ == nullptr) {
    return;
  }
  QString error;
  const auto image = describe_source_image(&error);
  if (image.isNull()) {
    describe_preview_->setPixmap(QPixmap());
    describe_preview_->setText(error.isEmpty() ? tr("Open a document first.") : error);
    return;
  }
  describe_preview_->setText(QString());
  // Before the tab is laid out the label has no width yet: fit the dialog's content width.
  const int width = describe_preview_->width() > 50 ? describe_preview_->width() : 560;
  describe_preview_->setPixmap(QPixmap::fromImage(
      image.scaled(QSize(width, describe_preview_->height()), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void OpenAiImagesDialog::generate_description() {
  QString error;
  const auto image = describe_source_image(&error);
  if (image.isNull()) {
    show_status(error.isEmpty() ? tr("Open a document first.") : error, true);
    return;
  }
  refresh_describe_preview();
  if (take_api_key().isEmpty()) {
    return;
  }
  save_settings();
  set_busy(true, tr("Describing the image..."), tr("Describing..."));
  client_->describe(
      image,
      [this](openai::DescribeResult result) {
        set_busy(false);
        describe_text_edit_->setPlainText(result.text);
        auto message = tr("Described the image.");
        if (result.total_tokens > 0) {
          message += QLatin1Char(' ') +
                     tr("%1 tokens used.").arg(QLocale().toString(static_cast<qlonglong>(result.total_tokens)));
        }
        show_status(message);
      },
      [this](const QString& message) {
        set_busy(false);
        show_error(message, client_->last_error_help());
      });
}

void OpenAiImagesDialog::generate_animation() {
  const auto prompt = animate_prompt_edit_->toPlainText().trimmed();
  if (prompt.isEmpty()) {
    show_status(tr("Type a prompt first."), true);
    animate_prompt_edit_->setFocus();
    return;
  }
  openai::GenerateRequest request;
  const auto source = animate_source_combo_->currentData().toString();
  if (source != QLatin1String("prompt")) {
    QString error;
    QString label;
    const auto image = window_ == nullptr ? QImage()
                       : source == QLatin1String("layer")
                           ? window_->openai_reference_from_active_layer(&label, &error)
                           : window_->openai_reference_from_visible_image(&error);
    if (image.isNull()) {
      show_status(error.isEmpty() ? tr("Open a document first.") : error, true);
      return;
    }
    // The starting image goes first in a GIF edit, with no mask.
    request.references.push_back(openai::prepare_reference_image(image));
  }
  const bool from_image = !request.references.empty();
  const auto extra = reference_images(openai::kMaxReferenceImages - static_cast<int>(request.references.size()));
  request.references.insert(request.references.end(), extra.begin(), extra.end());
  if (take_api_key().isEmpty()) {
    return;
  }
  save_settings();
  request.model = model_combo_->currentData().toString();
  request.quality = quality_combo_->currentData().toString();
  request.count = image_count();
  request.background = animate_background_combo_->currentData().toString();
  const int edge = animate_size_combo_->currentData().toInt();
  const bool sheet = animate_colors_combo_->currentData().toString() == QLatin1String("sheet");
  const auto layout = openai::sheet_layout(animate_frames_spin_->value());
  const int delay_ms = animate_delay_spin_->value();
  if (sheet) {
    // Full color: an ordinary PNG request for the frames on one sheet, cut into frames on
    // arrival (docs/openai-images-tabs.md#animate).
    request.prompt = openai::sheet_animation_prompt(prompt, layout, from_image, static_cast<int>(extra.size()));
    request.size = openai::sheet_request_size(layout, edge);
  } else {
    request.prompt = openai::animation_prompt(prompt, animate_frames_spin_->value(), from_image,
                                             static_cast<int>(extra.size()));
    request.size = QSize(edge, edge);
    request.animated_gif = true;
    request.frame_count = animate_frames_spin_->value();
    request.frame_duration_ms = delay_ms;
  }
  set_busy(true, tr("Building the animation..."), tr("Building animation..."));
  client_->generate(
      request,
      [this, sheet, layout, delay_ms, edge](openai::GenerateResult result) {
        set_busy(false);
        if (window_ == nullptr) {
          return;
        }
        int added = 0;
        if (sheet) {
          std::vector<std::vector<openai::GifFrame>> animations;
          for (const auto& image : result.images) {
            animations.push_back(openai::sheet_frames(image, layout, delay_ms, QSize(edge, edge)));
          }
          added = window_->add_openai_animation_frame_documents(std::move(animations));
        } else {
          added = window_->add_openai_animation_documents(result.encoded);
        }
        if (added <= 0) {
          show_status(tr("OpenAI's reply held no animation."), true);
          return;
        }
        auto message = tr("Opened %n animation(s).", nullptr, added);
        if (sheet && layout.frames() != animate_frames_spin_->value()) {
          message += QLatin1Char(' ') + tr("Full color drew %1 frames, to fill a %2 x %3 sheet.")
                                            .arg(layout.frames())
                                            .arg(layout.columns)
                                            .arg(layout.rows);
        }
        if (result.total_tokens > 0) {
          message += QLatin1Char(' ') +
                     tr("%1 tokens used.").arg(QLocale().toString(static_cast<qlonglong>(result.total_tokens)));
        }
        show_status(message);
      },
      [this](const QString& message) {
        set_busy(false);
        show_error(message, client_->last_error_help());
      });
}

void OpenAiImagesDialog::generate_masked(bool remove) {
  QString error;
  std::int64_t session_id = -1;
  QRect region;
  QImage context;
  QImage coverage;
  if (window_ == nullptr ||
      !window_->openai_masked_source(&session_id, &region, &context, &coverage, &error, /*whole_when_none=*/!remove)) {
    show_status(error.isEmpty() ? tr("Open a document first.") : error, true);
    return;
  }
  // The whole picture (no selection, or all of it selected): no outline to draw, so the
  // picture and the prompt alone, and the result covers it all with no mask or edge blend.
  const bool whole = !remove && region.size() == window_->openai_canvas_size() && openai::coverage_is_full(coverage);
  const auto typed_prompt = edit_prompt_edit_->toPlainText().trimmed();
  if (whole && typed_prompt.isEmpty()) {
    show_status(tr("Describe the change first; with nothing selected, the whole picture is edited."), true);
    edit_prompt_edit_->setFocus();
    return;
  }
  // Seams (docs/openai-images.md#seams): the model may change a band past the selection, the
  // result shows as much of it as Edge blend asks for, and its tone is matched to the original
  // on a ring just outside. The result layer keeps only the part Edge blend can show: three
  // bands past the selection (`keep`, in region coordinates).
  const int band = openai::seam_band(openai::coverage_bounds(coverage).size());
  const auto request_cover = openai::dilate_coverage(coverage, 3 * band);
  const auto ring = openai::tone_ring(coverage, band);
  const auto keep = openai::coverage_bounds(request_cover);
  const auto layer_region = keep.translated(region.topLeft());
  // Into the active layer: its pixels over the kept area are captured now, so every variation
  // composes over the same original.
  const bool into_active = target_combo_->currentData().toString() == QLatin1String("active");
  std::int64_t active_session = -1;
  LayerId active_layer = 0;
  std::uint64_t active_revision = 0;
  QImage original;
  if (into_active && !window_->openai_active_layer_region(layer_region, &active_session, &active_layer,
                                                          &active_revision, &original, &error)) {
    show_status(error, true);
    return;
  }
  if (take_api_key().isEmpty()) {
    return;
  }
  save_settings();
  // Edit marks the selection with a red outline (markup) and says what to change; Remove sends
  // a mask, since the whole area is to be redrawn (docs/openai-images.md#edit-and-remove).
  const auto sent = remove  ? openai::masked_edit_images(context, request_cover)
                    : whole ? openai::masked_edit_images(context, coverage)
                            : openai::masked_edit_images(context, coverage, openai::markup_edge_pad(context.size()));
  openai::GenerateRequest request;
  request.model = model_combo_->currentData().toString();
  request.quality = quality_combo_->currentData().toString();
  request.count = image_count();
  // Opaque: a selection-masked request never asks for a transparent background
  // (docs/legal-constraints.md, "Prompt-driven image generation").
  request.background = QStringLiteral("opaque");
  request.size = sent.image.size();
  request.complexity = QString::fromLatin1(openai::kMaskedEditComplexity);
  request.edit_image = sent.image;
  if (remove) {
    request.edit_mask = sent.mask;
  } else {
    if (!whole) {
      request.edit_markup = openai::markup_overlay(coverage, sent);
    }
    // References follow the picture (image 1) as image 2 onward (Seth's decision, 2026-11-06).
    request.references = reference_images(openai::kMaxReferenceImages - 1);
  }
  const auto typed = edit_prompt_edit_->toPlainText().trimmed();
  request.prompt = remove ? QString::fromLatin1(openai::kRemovePrompt)
                   : whole ? typed + QStringLiteral("\n\n") + QString::fromLatin1(openai::kEditWholeInstructions)
                   : typed.isEmpty()
                       ? QString::fromLatin1(openai::kEditPromptWhenEmpty)
                       : typed + QStringLiteral("\n\n") + QString::fromLatin1(openai::kEditOutlineInstructions);
  if (!request.references.empty()) {
    request.prompt += QStringLiteral("\n\n") + QString::fromLatin1(openai::kEditReferencesNote);
  }
  auto words = typed.simplified();
  if (words.size() > 48) {
    words = words.left(48).trimmed() + QStringLiteral("...");
  }
  const auto name = remove ? tr("OpenAI Remove") : (words.isEmpty() ? tr("OpenAI Edit") : tr("OpenAI Edit: %1").arg(words));
  set_busy(true, remove ? tr("Removing...") : tr("Editing..."), remove ? tr("Removing...") : tr("Editing..."));
  client_->generate(
      request,
      [this, session_id, context, coverage, ring, band, keep, layer_region, sent, name, into_active, whole,
       active_session, active_layer, active_revision, original](openai::GenerateResult result) {
        set_busy(false);
        if (window_ == nullptr || result.images.empty()) {
          return;
        }
        ResultSet set;
        for (const auto& image : result.images) {
          set.images.push_back(openai::match_tone(openai::result_for_region(image, sent), context, ring).copy(keep));
        }
        const auto selection = coverage.copy(keep);
        set.selection = selection;
        set.band = band;
        set.edge_adjustable = !whole;  // the whole picture has no edge to blend
        if (into_active) {
          // Composed at the Edge blend amount current when it is written (also by the slider).
          set.prepare = [this, original, selection, band, whole](const QImage& image) {
            if (whole) {
              return openai::blend_with_weight(original, image, selection);
            }
            const double amount = edge_amount();
            const auto weight = openai::feather_coverage(openai::edge_coverage(selection, band, amount),
                                                         openai::edge_feather(band, amount));
            return openai::blend_with_weight(original, image, weight);
          };
          std::uint64_t revision = 0;
          if (!window_->openai_write_into_layer(active_session, active_layer, active_revision, layer_region,
                                                set.prepare(set.images.front()), true, &revision)) {
            show_status(tr("The images arrived, but the active layer changed or closed meanwhile."), true);
            return;
          }
          set.session_id = active_session;
          set.layer_id = active_layer;
          set.revision = revision;
          set.into_active_layer = true;
          set.region = layer_region;
          present_results(std::move(set));
          report_added(0, result.total_tokens);
          return;
        }
        // A layer over the region whose mask is the selection grown and feathered as Edge blend
        // says; the slider rewrites the mask, the Results swap the pixels.
        set.prepare = [](const QImage& image) { return image; };
        set.region = layer_region;
        LayerId layer_id = 0;
        // The whole picture: a plain layer, no mask (a feathered one would fade the canvas edges).
        const double amount = edge_amount();
        const auto added = window_->add_openai_masked_layers(
            session_id, {set.images.front()}, layer_region,
            whole ? QImage() : openai::edge_coverage(selection, band, amount), name,
            whole ? 0.0 : openai::edge_feather(band, amount), &layer_id);
        if (added <= 0) {
          show_status(tr("The images arrived, but the document they were made for is closed."), true);
          return;
        }
        set.session_id = session_id;
        set.layer_id = layer_id;
        present_results(std::move(set));
        report_added(added, result.total_tokens);
      },
      [this](const QString& message) {
        set_busy(false);
        show_error(message, client_->last_error_help());
      });
}

void OpenAiImagesDialog::generate() {
  if (client_->busy()) {
    return;
  }
  // A new request ends the previous result's variations: what is shown stays.
  clear_results();
  // Feedback at the click, painted now: capturing the document and preparing the images take a
  // moment before the request goes out (Seth, 2026-11-07).
  generate_button_->setText(busy_button_text());
  spinner_angle_ = 0;
  advance_spinner();
  show_status(tr("Preparing the request..."));
  generate_button_->repaint();
  status_label_->repaint();
  start_request();
  if (!client_->busy()) {
    set_busy(false);  // stopped before sending (no selection, no key): the button as it was
  }
}

QString OpenAiImagesDialog::busy_button_text() const {
  switch (mode_tabs_->currentIndex()) {
    case EditTab:
      return tr("Editing...");
    case RemoveTab:
      return tr("Removing...");
    case ExpandTab:
      return tr("Expanding...");
    case AnimateTab:
      return tr("Building animation...");
    case DescribeTab:
      return tr("Describing...");
    default:
      return tr("Creating...");
  }
}

void OpenAiImagesDialog::start_request() {
  if (mode_tabs_->currentIndex() == ExpandTab) {
    generate_expand();
    return;
  }
  if (mode_tabs_->currentIndex() == AnimateTab) {
    generate_animation();
    return;
  }
  if (mode_tabs_->currentIndex() == DescribeTab) {
    generate_description();
    return;
  }
  if (mode_tabs_->currentIndex() == EditTab || mode_tabs_->currentIndex() == RemoveTab) {
    generate_masked(mode_tabs_->currentIndex() == RemoveTab);
    return;
  }
  if (prompt_edit_->toPlainText().trimmed().isEmpty()) {
    show_status(tr("Type a prompt first."), true);
    prompt_edit_->setFocus();
    return;
  }
  if (take_api_key().isEmpty()) {
    return;
  }
  save_settings();
  bool fit_to_canvas = false;
  const auto request = build_request(&fit_to_canvas);
  target_session_id_ = window_ != nullptr ? window_->openai_target_session_id() : -1;
  set_busy(true, tr("Generating %n image(s)...", nullptr, request.count), tr("Creating..."));
  client_->generate(
      request,
      [this, prompt = request.prompt, fit_to_canvas](openai::GenerateResult result) {
        set_busy(false);
        if (window_ == nullptr || result.images.empty()) {
          return;
        }
        LayerId layer_id = 0;
        const auto added =
            window_->add_openai_generated_layers(target_session_id_, {result.images.front()}, prompt, fit_to_canvas, &layer_id);
        if (added <= 0) {
          show_status(tr("The images arrived, but could not be added to a document."), true);
          return;
        }
        // The layer's document is the active one now (a new one when the target had closed).
        ResultSet set;
        set.session_id = window_->openai_target_session_id();
        set.layer_id = layer_id;
        set.images = result.images;
        const auto canvas = window_->openai_canvas_size();
        set.prepare = [canvas, fit_to_canvas](const QImage& image) {
          QPoint origin;
          return openai::placed_image(image, canvas, fit_to_canvas, &origin);
        };
        present_results(std::move(set));
        report_added(added, result.total_tokens);
      },
      [this](const QString& error) {
        set_busy(false);
        show_error(error, client_->last_error_help());
      });
}

void OpenAiImagesDialog::cancel_request() {
  if (!client_->busy()) {
    return;
  }
  client_->cancel();
  set_busy(false);
  show_status(tr("Stopped. OpenAI may still bill a request it had already started."));
}

void OpenAiImagesDialog::set_busy(bool busy, const QString& message, const QString& button_text) {
  generate_button_->setProperty("busy", busy);
  check_button_->setEnabled(!busy);
  stop_button_->setEnabled(busy);
  if (busy) {
    busy_message_ = message;
    elapsed_.start();
    show_status(message);
    busy_timer_->start();
    // The action button says what is happening and spins until the result arrives; Stop or
    // Close cancels. A click on it meanwhile does nothing (generate() returns while busy).
    generate_button_->setText(button_text.isEmpty() ? message : button_text);
    spinner_angle_ = 0;
    spinner_timer_->start();
    advance_spinner();
  } else {
    busy_timer_->stop();
    spinner_timer_->stop();
    generate_button_->setIcon(QIcon());
    update_action_button();
  }
}

void OpenAiImagesDialog::advance_spinner() {
  // A rotating arc in the button's text role, drawn per frame at the screen's scale.
  const qreal scale = devicePixelRatioF();
  constexpr int kSize = 16;
  QPixmap pixmap(static_cast<int>(kSize * scale), static_cast<int>(kSize * scale));
  pixmap.setDevicePixelRatio(scale);
  pixmap.fill(Qt::transparent);
  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto& palette = theme();
    auto track = palette.text_primary;
    track.setAlpha(60);
    painter.setPen(QPen(track, 2.0));
    painter.drawEllipse(QRectF(2, 2, kSize - 4, kSize - 4));
    QPen arc(palette.text_primary, 2.0);
    arc.setCapStyle(Qt::RoundCap);
    painter.setPen(arc);
    painter.drawArc(QRectF(2, 2, kSize - 4, kSize - 4), -spinner_angle_ * 16, 100 * 16);
  }
  generate_button_->setIcon(QIcon(pixmap));
  generate_button_->setIconSize(QSize(kSize, kSize));
  spinner_angle_ = (spinner_angle_ + 30) % 360;
}

void OpenAiImagesDialog::show_error(const QString& message, const openai::ErrorHelp& help) {
  show_status(message, true);
  if (!help.url.isEmpty()) {
    // A link to the OpenAI page that fixes it, after the message.
    set_themed_label_text(*status_label_, message.toHtmlEscaped() +
                                              QStringLiteral(" <a style=\"color:@accent\" href=\"%1\">%2</a>")
                                                  .arg(help.url.toHtmlEscaped(), help.text.toHtmlEscaped()));
  }
}

void OpenAiImagesDialog::show_status(const QString& message, bool error) {
  set_themed_label_text(*status_label_, message.toHtmlEscaped());
  if (status_label_->property("error").toBool() != error) {
    status_label_->setProperty("error", error);
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
  }
}

int OpenAiImagesDialog::reference_count() const { return references_list_->count(); }

double OpenAiImagesDialog::edge_amount() const { return edge_slider_->value() / 100.0; }

void OpenAiImagesDialog::apply_edge_blend() {
  if (!results_.has_value() || !results_->edge_adjustable || window_ == nullptr) {
    return;
  }
  std::uint64_t revision = 0;
  const double amount = edge_amount();
  const bool written =
      results_->into_active_layer
          ? window_->openai_write_into_layer(results_->session_id, results_->layer_id, results_->revision,
                                             results_->region,
                                             results_->prepare(results_->images[static_cast<std::size_t>(results_->shown)]),
                                             false, &revision)
          : window_->openai_set_layer_mask(results_->session_id, results_->layer_id, results_->revision,
                                           results_->region,
                                           openai::edge_coverage(results_->selection, results_->band, amount),
                                           openai::edge_feather(results_->band, amount), &revision);
  if (!written) {
    clear_results();
    show_status(tr("The result layer changed since, so its other variations were cleared."), true);
    return;
  }
  results_->revision = revision;
  QSettings settings = app_settings();
  settings.setValue(QString::fromLatin1(kEdgeBlendKey), edge_slider_->value());
}

double OpenAiImagesDialog::expand_blend_amount() const { return expand_blend_slider_->value() / 100.0; }

void OpenAiImagesDialog::apply_expand_blend() {
  QSettings settings = app_settings();
  settings.setValue(QString::fromLatin1(kExpandBlendKey), expand_blend_slider_->value());
  if (!results_.has_value() || !results_->expand_adjustable || window_ == nullptr) {
    return;
  }
  std::uint64_t revision = 0;
  if (!window_->openai_replace_layer_pixels(
          results_->session_id, results_->layer_id, results_->revision,
          results_->prepare(results_->images[static_cast<std::size_t>(results_->shown)]), &revision)) {
    clear_results();
    show_status(tr("The result layer changed since, so its other variations were cleared."), true);
    return;
  }
  results_->revision = revision;
}

int OpenAiImagesDialog::result_count() const { return results_.has_value() ? static_cast<int>(results_->images.size()) : 0; }

int OpenAiImagesDialog::shown_variation() const { return results_.has_value() ? results_->shown : -1; }

void OpenAiImagesDialog::report_added(int added, std::int64_t tokens) {
  auto message = added > 0 ? tr("Added %n layer(s).", nullptr, added) : tr("Wrote the result into the active layer.");
  if (result_count() > 1) {
    message += QLatin1Char(' ') + tr("Click a result to see another variation.");
  }
  if (tokens > 0) {
    message += QLatin1Char(' ') + tr("%1 tokens used.").arg(QLocale().toString(static_cast<qlonglong>(tokens)));
  }
  show_status(message);
}

void OpenAiImagesDialog::present_results(ResultSet results) {
  if (window_ != nullptr && !results.into_active_layer) {
    results.revision = window_->openai_layer_revision(results.session_id, results.layer_id).value_or(0);
  }
  results.shown = 0;
  results_ = std::move(results);
  const int count = static_cast<int>(results_->images.size());
  for (int index = 0; index < openai::kMaxImagesPerRequest; ++index) {
    auto* button = results_group_->button(index);
    button->setVisible(index < count);
    if (index < count) {
      button->setIcon(QIcon(QPixmap::fromImage(results_->images[static_cast<std::size_t>(index)].scaled(
          44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    }
  }
  results_group_->button(0)->setChecked(true);
  // A single result needs no picker.
  results_row_->setVisible(count > 1);
  results_label_->setVisible(count > 1);
}

void OpenAiImagesDialog::clear_results() {
  results_.reset();
  if (results_row_ != nullptr) {
    results_row_->setVisible(false);
    results_label_->setVisible(false);
  }
}

bool OpenAiImagesDialog::show_variation(int index) {
  if (!results_.has_value() || window_ == nullptr || index < 0 ||
      index >= static_cast<int>(results_->images.size())) {
    return false;
  }
  if (index == results_->shown) {
    return true;
  }
  const auto pixels = results_->prepare(results_->images[static_cast<std::size_t>(index)]);
  std::uint64_t revision = 0;
  const bool written =
      results_->into_active_layer
          ? window_->openai_write_into_layer(results_->session_id, results_->layer_id, results_->revision,
                                             results_->region, pixels, false, &revision)
          : window_->openai_replace_layer_pixels(results_->session_id, results_->layer_id, results_->revision, pixels,
                                                 &revision);
  if (!written) {
    // The layer was edited, undone or closed: the variations no longer apply to it.
    clear_results();
    show_status(tr("The result layer changed since, so its other variations were cleared."), true);
    return false;
  }
  results_->revision = revision;
  results_->shown = index;
  results_group_->button(index)->setChecked(true);
  show_status(tr("Showing variation %1.").arg(index + 1));
  return true;
}

int OpenAiImagesDialog::image_count() const { return std::max(1, count_group_->checkedId()); }

bool OpenAiImagesDialog::busy() const { return client_->busy(); }

void OpenAiImagesDialog::add_reference_for_testing(const QImage& image, const QString& label) {
  add_reference(image, label);
}

void OpenAiImagesDialog::add_reference(const QImage& image, const QString& label) {
  if (image.isNull()) {
    return;
  }
  if (references_list_->count() >= openai::kMaxReferenceImages) {
    show_status(tr("OpenAI takes at most %1 reference images.").arg(openai::kMaxReferenceImages), true);
    return;
  }
  const auto prepared = openai::prepare_reference_image(image);
  auto* item = new QListWidgetItem(
      QIcon(QPixmap::fromImage(prepared.scaled(kReferenceIconSize, kReferenceIconSize, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation))),
      label, references_list_);
  item->setData(Qt::UserRole, QVariant::fromValue(prepared));
  item->setData(kReferenceLabelRole, label);
  number_references();
  update_reference_buttons();
}

void OpenAiImagesDialog::fit_tabs_to_current_page() {
  // A tab widget is as tall as its tallest page; hidden pages are made to ask for no height,
  // so each tab needs only its own (the dialog stays below the main window's minimum height).
  for (int index = 0; index < mode_tabs_->count(); ++index) {
    mode_tabs_->widget(index)->setSizePolicy(
        QSizePolicy::Preferred, index == mode_tabs_->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
  }
  mode_tabs_->updateGeometry();
}

void OpenAiImagesDialog::fit_height_to_current_tab() {
  // Each tab gets the height it needs: no empty band where a shorter tab would leave slack.
  if (layout() != nullptr) {
    layout()->activate();
  }
  // A floor keeps the short tabs (Remove, Config) from making the dialog jump in height. The
  // layout's height for the width counts too: dialogs at a text size other than 100% grow to
  // it after each layout change, and the two must agree or they fight.
  int needed = std::max(minimumSizeHint().height(), kDialogFloorHeight);
  if (layout() != nullptr && layout()->hasHeightForWidth()) {
    needed = std::max(needed, layout()->totalHeightForWidth(width()));
  }
  const int available = screen() != nullptr ? screen()->availableGeometry().height() - 40 : needed;
  resize(width(), std::max(minimumSizeHint().height(), std::min(needed, available)));
}

int OpenAiImagesDialog::first_reference_number() const {
  if (mode_tabs_ == nullptr) {
    return 1;
  }
  if (mode_tabs_->currentIndex() == EditTab) {
    return 2;
  }
  if (mode_tabs_->currentIndex() == AnimateTab && animate_source_combo_ != nullptr &&
      animate_source_combo_->currentData().toString() != QLatin1String("prompt")) {
    return 2;
  }
  return 1;
}

void OpenAiImagesDialog::number_references() {
  if (references_list_ == nullptr) {
    return;
  }
  // The prompt refers to references by these numbers: the request sends them in this order,
  // after the picture being edited or the starting image where there is one.
  const int first = first_reference_number();
  for (int row = 0; row < references_list_->count(); ++row) {
    auto* item = references_list_->item(row);
    const auto label = item->data(kReferenceLabelRole).toString();
    const auto image = item->data(Qt::UserRole).value<QImage>();
    item->setText(tr("%1. %2").arg(row + first).arg(label));
    item->setToolTip(
        tr("Image %1: %2 (%3 x %4 px)").arg(row + first).arg(label).arg(image.width()).arg(image.height()));
  }
}

std::vector<QImage> OpenAiImagesDialog::reference_images(int max) const {
  std::vector<QImage> images;
  for (int row = 0; row < references_list_->count() && static_cast<int>(images.size()) < max; ++row) {
    images.push_back(references_list_->item(row)->data(Qt::UserRole).value<QImage>());
  }
  return images;
}

void OpenAiImagesDialog::remove_reference(int row) {
  if (row < 0 || row >= references_list_->count()) {
    return;
  }
  delete references_list_->takeItem(row);
  number_references();
  update_reference_buttons();
}

void OpenAiImagesDialog::add_reference_from_layer() {
  if (window_ == nullptr) {
    return;
  }
  QString label;
  QString error;
  const auto image = window_->openai_reference_from_active_layer(&label, &error);
  if (image.isNull()) {
    show_status(error, true);
    return;
  }
  add_reference(image, label);
}

void OpenAiImagesDialog::add_reference_from_visible() {
  if (window_ == nullptr) {
    return;
  }
  QString error;
  const auto image = window_->openai_reference_from_visible_image(&error);
  if (image.isNull()) {
    show_status(error, true);
    return;
  }
  add_reference(image, tr("Visible"));
}

void OpenAiImagesDialog::add_reference_from_file() {
  const auto paths = get_open_file_names(
      this, tr("Add Reference Images"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
      tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff)"), nullptr,
      QStringLiteral("openAiImagesReferenceFileDialog"));
  for (const auto& path : paths) {
    QString error;
    const auto image = openai::load_reference_image_file(path, &error);
    if (image.isNull()) {
      show_status(error, true);
      continue;
    }
    add_reference(image, QFileInfo(path).completeBaseName());
  }
}

void OpenAiImagesDialog::paste_or_report() {
  if (!paste_references_from_clipboard()) {
    show_status(tr("The clipboard holds no image."), true);
  }
}

bool OpenAiImagesDialog::eventFilter(QObject* watched, QEvent* event) {
  if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && isActiveWindow()) {
    auto* widget = qobject_cast<QWidget*>(watched);
    auto* key_event = static_cast<QKeyEvent*>(event);
    // Patchy's hotkeys are application-wide: without this, Ctrl+A and Ctrl+C in a text field
    // here (the read-only Describe box above all) select and copy the canvas instead. Claiming
    // the override sends the key on to the field, which handles it as text.
    if (event->type() == QEvent::ShortcutOverride && widget != nullptr && widget->window() == this &&
        (qobject_cast<QLineEdit*>(widget) != nullptr || qobject_cast<QPlainTextEdit*>(widget) != nullptr)) {
      for (const auto standard : {QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut, QKeySequence::Paste,
                                  QKeySequence::Undo, QKeySequence::Redo}) {
        if (key_event->matches(standard)) {
          key_event->accept();
          return true;
        }
      }
    }
    if (widget != nullptr && widget->window() == this && key_event->matches(QKeySequence::Paste) &&
        qobject_cast<QLineEdit*>(widget) == nullptr && qobject_cast<QPlainTextEdit*>(widget) == nullptr) {
      // Claim the key from the hotkeys first, then paste on the press itself.
      key_event->accept();
      if (event->type() == QEvent::KeyPress) {
        paste_or_report();
      }
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

bool OpenAiImagesDialog::paste_references_from_clipboard() {
  const auto* clipboard = QApplication::clipboard();
  return add_references_from_mime(clipboard != nullptr ? clipboard->mimeData() : nullptr);
}

bool OpenAiImagesDialog::add_references_from_mime(const QMimeData* mime) {
  if (mime == nullptr) {
    return false;
  }
  if (const auto files = clipboard_image_files(mime); !files.isEmpty()) {
    for (const auto& path : files) {
      QString error;
      const auto image = openai::load_reference_image_file(path, &error);
      if (image.isNull()) {
        show_status(error, true);
        continue;
      }
      add_reference(image, QFileInfo(path).completeBaseName());
    }
    return true;
  }
  if (!mime->hasImage()) {
    return false;
  }
  const auto image = qvariant_cast<QImage>(mime->imageData());
  if (image.isNull()) {
    return false;
  }
  add_reference(image, tr("Clipboard"));
  return true;
}

QString OpenAiImagesDialog::key_storage_note() {
  const auto common = tr("The key is never written to Patchy's settings file, and Patchy sends it only to OpenAI.");
#if defined(Q_OS_WIN)
  return tr("Patchy saves the key in Windows Credential Manager, under Windows Credentials, as the generic "
            "credential \"com.rtsoft.patchy.openai\". Only your Windows account can read it. Untick "
            "\"Remember the key on this computer\" to delete it.") +
         QStringLiteral("\n\n") + common;
#elif defined(Q_OS_MACOS)
  return tr("Patchy saves the key in your login keychain as the password item \"com.rtsoft.patchy.openai\" "
            "(account \"api-key\"), which the Keychain Access app shows. Only your macOS account can read it. "
            "Untick \"Remember the key on this computer\" to delete it.") +
         QStringLiteral("\n\n") + common;
#else
  return tr("Patchy saves the key in the file %1. The file is not encrypted: only your user account can open it, "
            "but any program running as you can read it. Untick \"Remember the key on this computer\" to delete "
            "the file. You can instead set the OPENAI_API_KEY environment variable, which the dialog reads when "
            "it opens.")
             .arg(QDir::toNativeSeparators(openai::key_file_path())) +
         QStringLiteral("\n\n") + common;
#endif
}

void OpenAiImagesDialog::select_available_models(const QStringList& available) {
  auto* items = qobject_cast<QStandardItemModel*>(model_combo_->model());
  if (items == nullptr) {
    return;
  }
  for (int row = 0; row < model_combo_->count(); ++row) {
    if (auto* item = items->item(row); item != nullptr) {
      item->setEnabled(available.contains(model_combo_->itemData(row).toString()));
    }
  }
  const auto* current = items->item(model_combo_->currentIndex());
  if (current != nullptr && !current->isEnabled()) {
    for (int row = 0; row < model_combo_->count(); ++row) {
      if (items->item(row)->isEnabled()) {
        model_combo_->setCurrentIndex(row);
        break;
      }
    }
  }
}

void OpenAiImagesDialog::update_reference_buttons() {
  // Documents open and close while this non-modal dialog stays up, so the layer and visible
  // buttons stay enabled and report a missing document when used.
  const bool room = references_list_->count() < openai::kMaxReferenceImages;
  add_layer_button_->setEnabled(window_ != nullptr && room);
  add_visible_button_->setEnabled(window_ != nullptr && room);
  add_file_button_->setEnabled(room);
  paste_reference_button_->setEnabled(room && clipboard_offers_reference());
  remove_reference_button_->setEnabled(!references_list_->selectedItems().isEmpty());
}

void OpenAiImagesDialog::done(int result) {
  // Closing stops a request that is still running; nothing arrives into a closed dialog. The
  // shown variation stays; the others are dropped.
  client_->cancel();
  clear_results();
  set_busy(false);
  QDialog::done(result);
}

}  // namespace patchy::ui
