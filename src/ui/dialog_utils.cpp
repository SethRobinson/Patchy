#include "ui/dialog_utils.hpp"

#include "ui/app_settings.hpp"

#include "ui/action_icons.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr auto kDialogPositionMemoryInstalledProperty = "patchy.dialogPositionMemoryInstalled";
constexpr auto kDialogPositionMemoryIdProperty = "patchy.dialogPositionMemoryId";

QIcon dialog_close_icon() {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawLine(QPointF(10.0, 10.0), QPointF(22.0, 22.0));
  painter.drawLine(QPointF(22.0, 10.0), QPointF(10.0, 22.0));
  return QIcon(pixmap);
}

QIcon compact_symbol_icon(const QString& symbol) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(238, 242, 246), 4.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawLine(QPointF(8.0, 16.0), QPointF(24.0, 16.0));
  if (symbol == QStringLiteral("+")) {
    painter.drawLine(QPointF(16.0, 8.0), QPointF(16.0, 24.0));
  }
  return QIcon(pixmap);
}

QString dialog_chrome_style() {
  return QStringLiteral(R"(
    QDialog {
      background: #262626;
      color: #e6e6e6;
      border: 1px solid #1f1f1f;
    }
    QWidget#dialogChromeTitleBar {
      background: #4f4f4f;
      border-bottom: 1px solid #343434;
      min-height: 34px;
      max-height: 34px;
    }
    QLabel#dialogChromePatchyBadge {
      background: transparent;
      border: 0;
    }
    QLabel#dialogChromeTitleLabel {
      background: transparent;
      color: #f0f0f0;
      font-weight: 600;
    }
    QWidget#dialogChromeContent {
      background: #262626;
    }
    QToolButton#dialogChromeCloseButton {
      background: transparent;
      border: 0;
      border-radius: 0;
      padding: 0;
      min-width: 46px;
      max-width: 46px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton#dialogChromeCloseButton:hover {
      background: #c42b1c;
      border: 0;
    }
    QToolButton#dialogChromeCloseButton:pressed {
      background: #9f2117;
    }
    QPushButton[compactSymbolButton="true"] {
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
  )");
}

class DialogChromeDragFilter final : public QObject {
public:
  explicit DialogChromeDragFilter(QDialog& dialog, QObject* parent) : QObject(parent), dialog_(dialog) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    Q_UNUSED(watched);
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
          drag_position_ = mouse_event->globalPosition().toPoint() - dialog_.frameGeometry().topLeft();
          dragging_ = true;
          if (auto* handle = dialog_.windowHandle(); handle != nullptr && handle->startSystemMove()) {
            dragging_ = false;
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (dragging_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
          if (!dialog_.isMaximized() && !dialog_.isFullScreen()) {
            dialog_.move(mouse_event->globalPosition().toPoint() - drag_position_);
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease:
        dragging_ = false;
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QDialog& dialog_;
  bool dragging_{false};
  QPoint drag_position_;
};

QString dialog_position_group(const QDialog& dialog) {
  auto id = dialog.property(kDialogPositionMemoryIdProperty).toString();
  if (id.isEmpty()) {
    id = dialog.objectName();
  }
  if (id.isEmpty()) {
    return {};
  }
  return QStringLiteral("dialogPositions/%1").arg(id);
}

QString dialog_position_key(const QDialog& dialog) {
  const auto group = dialog_position_group(dialog);
  return group.isEmpty() ? QString() : group + QStringLiteral("/pos");
}

QString dialog_position_moved_key(const QDialog& dialog) {
  const auto group = dialog_position_group(dialog);
  return group.isEmpty() ? QString() : group + QStringLiteral("/moved");
}

QSize dialog_placement_size(const QDialog& dialog) {
  auto size = dialog.testAttribute(Qt::WA_Resized) ? dialog.size() : dialog.sizeHint();
  if (!size.isValid() || size.isEmpty()) {
    size = dialog.size();
  }
  if (!size.isValid() || size.isEmpty()) {
    size = QSize(320, 200);
  }
  return size;
}

QRect dialog_owner_geometry(const QDialog& dialog) {
  if (auto* parent = dialog.parentWidget(); parent != nullptr) {
    if (auto* owner = parent->window(); owner != nullptr && owner != &dialog && owner->frameGeometry().isValid()) {
      return owner->frameGeometry();
    }
    if (parent->frameGeometry().isValid()) {
      return parent->frameGeometry();
    }
  }

  if (auto* active = QApplication::activeWindow();
      active != nullptr && active != &dialog && active->frameGeometry().isValid()) {
    return active->frameGeometry();
  }

  if (auto* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
    return screen->availableGeometry();
  }
  return QRect(0, 0, 640, 480);
}

QPoint clamped_dialog_position(const QDialog& dialog, QPoint position) {
  const auto size = dialog_placement_size(dialog);
  QScreen* screen = QGuiApplication::screenAt(position + QPoint(size.width() / 2, size.height() / 2));
  if (screen == nullptr && dialog.parentWidget() != nullptr) {
    screen = dialog.parentWidget()->screen();
  }
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen == nullptr) {
    return position;
  }

  const QRect available = screen->availableGeometry();
  const auto dialog_width = std::min(size.width(), available.width());
  const auto dialog_height = std::min(size.height(), available.height());
  const auto max_x = available.left() + std::max(0, available.width() - dialog_width);
  const auto max_y = available.top() + std::max(0, available.height() - dialog_height);
  return QPoint(std::clamp(position.x(), available.left(), max_x), std::clamp(position.y(), available.top(), max_y));
}

QPoint centered_dialog_position(const QDialog& dialog) {
  const auto owner = dialog_owner_geometry(dialog);
  const auto size = dialog_placement_size(dialog);
  return clamped_dialog_position(
      dialog, owner.center() - QPoint(size.width() / 2, size.height() / 2));
}

bool restore_dialog_position(QDialog& dialog) {
  const auto key = dialog_position_key(dialog);
  const auto moved_key = dialog_position_moved_key(dialog);
  if (key.isEmpty() || moved_key.isEmpty()) {
    return false;
  }

  auto settings = app_settings();
  if (!settings.value(moved_key, false).toBool()) {
    return false;
  }
  const auto stored_position = settings.value(key);
  if (!stored_position.canConvert<QPoint>()) {
    return false;
  }
  dialog.move(clamped_dialog_position(dialog, stored_position.toPoint()));
  return true;
}

void place_dialog(QDialog& dialog) {
  if (!restore_dialog_position(dialog)) {
    dialog.move(centered_dialog_position(dialog));
  }
}

void save_dialog_position(const QDialog& dialog) {
  const auto key = dialog_position_key(dialog);
  const auto moved_key = dialog_position_moved_key(dialog);
  if (key.isEmpty() || moved_key.isEmpty()) {
    return;
  }

  auto settings = app_settings();
  settings.setValue(key, dialog.pos());
  settings.setValue(moved_key, true);
}

void clear_dialog_position(const QDialog& dialog) {
  const auto group = dialog_position_group(dialog);
  if (group.isEmpty()) {
    return;
  }

  auto settings = app_settings();
  settings.remove(group);
}

bool has_remembered_dialog_position(const QDialog& dialog) {
  const auto key = dialog_position_key(dialog);
  const auto moved_key = dialog_position_moved_key(dialog);
  if (key.isEmpty() || moved_key.isEmpty()) {
    return false;
  }

  auto settings = app_settings();
  return settings.value(moved_key, false).toBool() && settings.value(key).canConvert<QPoint>();
}

class DialogPositionMemoryFilter final : public QObject {
public:
  explicit DialogPositionMemoryFilter(QDialog& dialog, bool had_remembered_position, QObject* parent)
      : QObject(parent), dialog_(dialog), had_remembered_position_(had_remembered_position),
        placement_position_(dialog.pos()) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    switch (event->type()) {
      case QEvent::Show:
        shown_ = true;
        placement_position_ = dialog_.pos();
        break;
      case QEvent::Move:
        if (shown_ && (dialog_.pos() - placement_position_).manhattanLength() > 2) {
          user_moved_ = true;
        }
        break;
      case QEvent::Close:
      case QEvent::Hide:
        if (user_moved_) {
          save_dialog_position(dialog_);
        } else if (!had_remembered_position_) {
          clear_dialog_position(dialog_);
        }
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QDialog& dialog_;
  const bool had_remembered_position_;
  bool shown_{false};
  bool user_moved_{false};
  QPoint placement_position_;
};

void apply_file_dialog_initial_path(QFileDialog& dialog, const QString& path, QFileDialog::AcceptMode accept_mode) {
  if (path.isEmpty()) {
    return;
  }

  const QFileInfo info(path);
  if (accept_mode == QFileDialog::AcceptSave) {
    if (const auto directory = info.absoluteDir(); directory.exists()) {
      dialog.setDirectory(directory);
    }
    if (!info.fileName().isEmpty()) {
      dialog.selectFile(info.fileName());
    }
    return;
  }

  if (info.isDir()) {
    dialog.setDirectory(info.absoluteFilePath());
    return;
  }
  if (info.exists()) {
    dialog.setDirectory(info.absolutePath());
    dialog.selectFile(info.fileName());
    return;
  }
  dialog.setDirectory(path);
}

bool use_qt_file_dialog_controls() {
  // Native dialogs on every platform (Windows shell dialogs, macOS panels, portal
  // dialogs inside Flatpak); only the offscreen test platform forces Qt's own widget
  // dialog, which is what makes the file-dialog UI tests drivable.
  return QGuiApplication::platformName().compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) == 0;
}

void configure_file_dialog(QFileDialog& dialog, const QString& object_name, const QString& initial_path,
                           QFileDialog::AcceptMode accept_mode, QFileDialog::FileMode file_mode,
                           QString* selected_filter) {
  if (!object_name.isEmpty()) {
    dialog.setObjectName(object_name);
  }
  if (use_qt_file_dialog_controls()) {
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
  }
  dialog.setAcceptMode(accept_mode);
  dialog.setFileMode(file_mode);
  dialog.resize(760, 520);
  apply_file_dialog_initial_path(dialog, initial_path, accept_mode);
  if (selected_filter != nullptr && !selected_filter->isEmpty()) {
    dialog.selectNameFilter(*selected_filter);
  }
}

void install_save_file_recent_dropdown(QFileDialog& dialog, const QStringList& recent_files) {
  if (recent_files.isEmpty()) {
    return;
  }

  QStringList paths;
  for (const auto& path : recent_files) {
    const auto absolute_path = QFileInfo(path).absoluteFilePath();
    if (!absolute_path.isEmpty() && !paths.contains(absolute_path)) {
      paths.push_back(absolute_path);
    }
  }
  if (paths.isEmpty()) {
    return;
  }

  auto* file_name_edit = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
  if (file_name_edit == nullptr || file_name_edit->parentWidget() == nullptr) {
    return;
  }

  auto* combo = new QComboBox(file_name_edit->parentWidget());
  combo->setObjectName(QStringLiteral("saveAsRecentFileNameCombo"));
  combo->setEditable(true);
  combo->setInsertPolicy(QComboBox::NoInsert);
  combo->setSizePolicy(file_name_edit->sizePolicy());
  for (const auto& path : paths) {
    combo->addItem(path, path);
    combo->setItemData(combo->count() - 1, path, Qt::ToolTipRole);
  }
  combo->setEditText(file_name_edit->text());

  if (auto* parent_layout = file_name_edit->parentWidget()->layout(); parent_layout != nullptr) {
    if (auto* item = parent_layout->replaceWidget(file_name_edit, combo); item != nullptr) {
      delete item;
      file_name_edit->hide();
    }
  }

  QObject::connect(combo->lineEdit(), &QLineEdit::textChanged, &dialog, [file_name_edit](const QString& text) {
    if (file_name_edit->text() != text) {
      file_name_edit->setText(text);
    }
  });
  QObject::connect(file_name_edit, &QLineEdit::textChanged, combo, [combo](const QString& text) {
    if (combo->currentText() != text) {
      combo->setEditText(text);
    }
  });
  QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, [&dialog, combo](int index) {
    const auto path = combo->itemData(index).toString();
    if (path.isEmpty()) {
      return;
    }
    const QFileInfo info(path);
    if (info.absoluteDir().exists()) {
      dialog.setDirectory(info.absoluteDir());
    }
    if (!info.fileName().isEmpty()) {
      dialog.selectFile(info.fileName());
    }
  });
}

}  // namespace

void configure_toolbar_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setFixedWidth(width);
}

void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setFixedWidth(width);
}

void configure_dialog_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setMinimumWidth(width);
  spin->setMinimumHeight(24);
}

void configure_dialog_spinbox(QDoubleSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setMinimumWidth(width);
  spin->setMinimumHeight(24);
}

QString dialog_spinbox_button_style() {
  return QStringLiteral(R"(
    QSpinBox,
    QDoubleSpinBox {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
      color: #f0f0f0;
      min-height: 26px;
      padding-left: 6px;
      padding-right: 54px; /* keep text clear of the - / + buttons */
    }
    QSpinBox:disabled,
    QDoubleSpinBox:disabled {
      background: #2c2c2c;
      color: #767676;
    }
    /* The decrement button sits on the left, the increment button on the
       far right, so the right-hand button always raises the value. */
    QSpinBox::down-button,
    QDoubleSpinBox::down-button {
      subcontrol-origin: border;
      subcontrol-position: center right;
      right: 27px;
      width: 24px;
      height: 24px;
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
    }
    QSpinBox::up-button,
    QDoubleSpinBox::up-button {
      subcontrol-origin: border;
      subcontrol-position: center right;
      right: 1px;
      width: 24px;
      height: 24px;
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
    }
    QSpinBox::up-button:hover,
    QSpinBox::down-button:hover,
    QDoubleSpinBox::up-button:hover,
    QDoubleSpinBox::down-button:hover {
      background: #4a4a4a;
      border-color: #696969;
    }
    QSpinBox::up-button:pressed,
    QSpinBox::down-button:pressed,
    QDoubleSpinBox::up-button:pressed,
    QDoubleSpinBox::down-button:pressed {
      background: #2f75bd;
      border-color: #6bb3ff;
    }
    QSpinBox::up-button:disabled,
    QSpinBox::down-button:disabled,
    QDoubleSpinBox::up-button:disabled,
    QDoubleSpinBox::down-button:disabled {
      background: #2e2e2e;
      border-top-color: #444444;
    }
    QSpinBox::up-arrow,
    QDoubleSpinBox::up-arrow {
      image: url(:/patchy/icons/spin-plus.svg);
      width: 12px;
      height: 12px;
    }
    QSpinBox::up-arrow:disabled,
    QSpinBox::up-arrow:off,
    QDoubleSpinBox::up-arrow:disabled,
    QDoubleSpinBox::up-arrow:off {
      image: url(:/patchy/icons/spin-plus-disabled.svg);
    }
    QSpinBox::down-arrow,
    QDoubleSpinBox::down-arrow {
      image: url(:/patchy/icons/spin-minus.svg);
      width: 12px;
      height: 12px;
    }
    QSpinBox::down-arrow:disabled,
    QSpinBox::down-arrow:off,
    QDoubleSpinBox::down-arrow:disabled,
    QDoubleSpinBox::down-arrow:off {
      image: url(:/patchy/icons/spin-minus-disabled.svg);
    }
  )");
}

void configure_compact_symbol_button(QPushButton* button) {
  if (button == nullptr) {
    return;
  }
  button->setProperty("compactSymbolButton", true);
  button->style()->unpolish(button);
  button->style()->polish(button);

  const auto symbol = button->text().trimmed();
  if (symbol == QStringLiteral("+") || symbol == QStringLiteral("-")) {
    button->setText(QString());
    button->setIcon(compact_symbol_icon(symbol));
    button->setIconSize(QSize(16, 16));
  }
  button->setFixedSize(22, 22);
  button->update();
}

QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title,
                                        DialogChromeCloseMode close_mode) {
  dialog.setWindowTitle(title);
  dialog.setWindowFlag(Qt::FramelessWindowHint, true);
  dialog.setStyleSheet(dialog.styleSheet() + dialog_chrome_style());
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* title_bar = new QWidget(&dialog);
  title_bar->setObjectName(QStringLiteral("dialogChromeTitleBar"));
  title_bar->setFixedHeight(34);
  title_bar->installEventFilter(new DialogChromeDragFilter(dialog, title_bar));
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(9, 0, 0, 0);
  title_layout->setSpacing(8);

  auto* badge = new QLabel(title_bar);
  badge->setObjectName(QStringLiteral("dialogChromePatchyBadge"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setFixedSize(18, 18);
  badge->setPixmap(patchy_app_icon().pixmap(18, 18));
  title_layout->addWidget(badge);

  auto* label = new QLabel(title, title_bar);
  label->setObjectName(QStringLiteral("dialogChromeTitleLabel"));
  title_layout->addWidget(label, 1);

  auto* close = new QToolButton(title_bar);
  close->setObjectName(QStringLiteral("dialogChromeCloseButton"));
  close->setAutoRaise(false);
  close->setFocusPolicy(Qt::NoFocus);
  close->setIcon(dialog_close_icon());
  close->setIconSize(QSize(16, 16));
  const bool accept_on_close = close_mode == DialogChromeCloseMode::Accept;
  close->setToolTip(accept_on_close ? QObject::tr("Apply and Close") : QObject::tr("Close"));
  close->setFixedSize(46, 34);
  title_layout->addWidget(close);
  QObject::connect(close, &QToolButton::clicked, &dialog, accept_on_close ? &QDialog::accept : &QDialog::reject);

  auto* content = new QWidget(&dialog);
  content->setObjectName(QStringLiteral("dialogChromeContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(12, 12, 12, 12);
  content_layout->setSpacing(8);

  root->addWidget(title_bar);
  root->addWidget(content, 1);
  return content_layout;
}

void set_dialog_position_memory_id(QDialog& dialog, const QString& id) {
  dialog.setProperty(kDialogPositionMemoryIdProperty, id);
}

void remember_dialog_position(QDialog& dialog) {
  if (dialog.property(kDialogPositionMemoryInstalledProperty).toBool()) {
    return;
  }

  const auto had_remembered_position = has_remembered_dialog_position(dialog);
  place_dialog(dialog);
  dialog.installEventFilter(new DialogPositionMemoryFilter(dialog, had_remembered_position, &dialog));
  dialog.setProperty(kDialogPositionMemoryInstalledProperty, true);
}

int exec_dialog(QDialog& dialog) {
  remember_dialog_position(dialog);
  return dialog.exec();
}

#ifndef Q_OS_MACOS
void keep_dialog_above_parent_window(QDialog& dialog) {
  // Windows owned windows and X11/Wayland transients already stay above their
  // parent; only macOS needs the child-window anchor (dialog_utils_mac.mm).
  Q_UNUSED(dialog);
}
#endif

int run_non_modal_dialog(QDialog& dialog) {
  remember_dialog_position(dialog);
  keep_dialog_above_parent_window(dialog);
  dialog.setModal(false);
  dialog.setWindowModality(Qt::NonModal);
  // Non-modal means a parent dialog stays clickable, so the user can close it
  // while this dialog's nested loop is still running. Reject with the parent:
  // otherwise this dialog is orphaned, drops behind the main window on the next
  // click (its hidden owner no longer anchors it in the z-order), and its nested
  // loop, plus any state guarding it, never unwinds.
  if (auto* parent = dialog.parentWidget(); parent != nullptr) {
    if (auto* parent_dialog = qobject_cast<QDialog*>(parent->window());
        parent_dialog != nullptr && parent_dialog != &dialog) {
      QObject::connect(parent_dialog, &QDialog::finished, &dialog, &QDialog::reject);
    }
  }
  QEventLoop loop;
  QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
  dialog.show();
  dialog.raise();
  dialog.activateWindow();
  loop.exec();
  return dialog.result();
}

QMessageBox::StandardButton show_warning_message(QWidget* parent, const QString& title, const QString& text,
                                                 QMessageBox::StandardButtons buttons,
                                                 QMessageBox::StandardButton default_button,
                                                 const QString& object_name) {
  QMessageBox dialog(QMessageBox::Warning, title, text, buttons, parent);
  if (!object_name.isEmpty()) {
    dialog.setObjectName(object_name);
  }
  if (default_button != QMessageBox::NoButton) {
    dialog.setDefaultButton(default_button);
  }
  return static_cast<QMessageBox::StandardButton>(exec_dialog(dialog));
}

void show_information_message(QWidget* parent, const QString& title, const QString& text,
                              const QString& object_name) {
  QMessageBox dialog(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
  if (!object_name.isEmpty()) {
    dialog.setObjectName(object_name);
  }
  exec_dialog(dialog);
}

void show_critical_message(QWidget* parent, const QString& title, const QString& text, const QString& object_name) {
  QMessageBox dialog(QMessageBox::Critical, title, text, QMessageBox::Ok, parent);
  if (!object_name.isEmpty()) {
    dialog.setObjectName(object_name);
  }
  exec_dialog(dialog);
}

QString get_open_file_name(QWidget* parent, const QString& caption, const QString& dir, const QString& filter,
                           QString* selected_filter, const QString& object_name) {
  QFileDialog dialog(parent, caption, QString(), filter);
  configure_file_dialog(dialog, object_name, dir, QFileDialog::AcceptOpen, QFileDialog::ExistingFile, selected_filter);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return {};
  }
  if (selected_filter != nullptr) {
    *selected_filter = dialog.selectedNameFilter();
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString() : files.front();
}

QStringList get_open_file_names(QWidget* parent, const QString& caption, const QString& dir, const QString& filter,
                                QString* selected_filter, const QString& object_name) {
  QFileDialog dialog(parent, caption, QString(), filter);
  configure_file_dialog(dialog, object_name, dir, QFileDialog::AcceptOpen, QFileDialog::ExistingFiles, selected_filter);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return {};
  }
  if (selected_filter != nullptr) {
    *selected_filter = dialog.selectedNameFilter();
  }
  return dialog.selectedFiles();
}

QString get_save_file_name(QWidget* parent, const QString& caption, const QString& dir, const QString& filter,
                           QString* selected_filter, const QString& object_name, const QStringList& recent_files) {
  QFileDialog dialog(parent, caption, QString(), filter);
  configure_file_dialog(dialog, object_name, dir, QFileDialog::AcceptSave, QFileDialog::AnyFile, selected_filter);
  if (dialog.testOption(QFileDialog::DontUseNativeDialog)) {
    install_save_file_recent_dropdown(dialog, recent_files);
  }
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return {};
  }
  if (selected_filter != nullptr) {
    *selected_filter = dialog.selectedNameFilter();
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString() : files.front();
}

void hide_menu_action_icons(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  for (auto* action : menu->actions()) {
    action->setIconVisibleInMenu(false);
    if (auto* child_menu = action->menu(); child_menu != nullptr) {
      hide_menu_action_icons(child_menu);
    }
  }
}

}  // namespace patchy::ui
