#include "ui/dialog_utils.hpp"

#include "ui/app_settings.hpp"

#include "ui/action_icons.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace patchy::ui {

namespace {

constexpr auto kDialogPositionMemoryInstalledProperty = "patchy.dialogPositionMemoryInstalled";
constexpr auto kDialogPositionMemoryIdProperty = "patchy.dialogPositionMemoryId";

constexpr int kChevronAreaWidth = 14;

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

class NumericPopupChevron final : public QWidget {
public:
  NumericPopupChevron(QAction* action, QWidget* parent)
      : QWidget(parent), action_(action) {
    setCursor(Qt::PointingHandCursor);
    setToolTip(action_->text());
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
    auto pen = QPen(palette().color(group, QPalette::Text));
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const auto center_x = static_cast<qreal>(width()) / 2.0 - 0.5;
    const auto center_y = static_cast<qreal>(height()) / 2.0;
    painter.drawPolyline(QPolygonF{QPointF(center_x - 3.0, center_y - 1.5),
                                  QPointF(center_x, center_y + 1.5),
                                  QPointF(center_x + 3.0, center_y - 1.5)});
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      action_->trigger();
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

private:
  QAction* action_;
};

template <typename SpinBox>
class NumericPopupController final : public QObject {
public:
  explicit NumericPopupController(SpinBox* spin)
      : QObject(spin), spin_(spin) {
    popup_clock_.start();
    base_name_ = spin_->objectName();
    if (base_name_.endsWith(QStringLiteral("Spin"))) {
      base_name_.chop(4);
    } else {
      base_name_ += QStringLiteral("Value");
    }

    action_ = new QAction(QObject::tr("Open value slider"), spin_);
    action_->setObjectName(base_name_ + QStringLiteral("PopupAction"));
    spin_->addAction(action_);
    QObject::connect(action_, &QAction::triggered, this,
                     [this] { show_popup(); });

    editor_ = spin_->template findChild<QLineEdit*>();
    Q_ASSERT(editor_ != nullptr);
    const auto margins = editor_->textMargins();
    editor_->setTextMargins(margins.left(), margins.top(),
                            margins.right() + kChevronAreaWidth,
                            margins.bottom());
    chevron_ = new NumericPopupChevron(action_, editor_);
    chevron_->setObjectName(base_name_ + QStringLiteral("PopupButton"));
    spin_->installEventFilter(this);
    editor_->installEventFilter(this);
    update_chevron_geometry();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == spin_) {
      if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton &&
            mouse_event->position().x() >= spin_->width() - kChevronAreaWidth) {
          action_->trigger();
          return true;
        }
      } else if (event->type() == QEvent::LanguageChange) {
        action_->setText(QObject::tr("Open value slider"));
        chevron_->setToolTip(action_->text());
      }
    }
    if (watched == editor_) {
      if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        update_chevron_geometry();
      } else if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton &&
            mouse_event->position().x() >=
                editor_->width() - kChevronAreaWidth) {
          action_->trigger();
          return true;
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void update_chevron_geometry() {
    if (chevron_ == nullptr) {
      return;
    }
    const auto editor_rect = editor_->rect();
    chevron_->setGeometry(editor_rect.right() - kChevronAreaWidth + 1,
                          editor_rect.top(), kChevronAreaWidth,
                          editor_rect.height());
    chevron_->raise();
  }

  std::vector<double> quick_values() const {
    const double minimum = spin_->minimum();
    const double maximum = spin_->maximum();
    std::vector<double> candidates;
    if (spin_->suffix().trimmed() == QStringLiteral("%") && minimum >= 0.0 &&
        maximum <= 100.0) {
      candidates = minimum <= 0.0
                       ? std::vector<double>{0, 10, 25, 50, 75, 100}
                       : std::vector<double>{1, 5, 10, 25, 50, 75, 100};
    } else if (base_name_ == QStringLiteral("brushSize")) {
      candidates = {1, 5, 10, 25, 50, 100, 250};
    } else if (base_name_ == QStringLiteral("textSize")) {
      candidates = {8, 10, 12, 18, 24, 36, 72};
    } else if (maximum - minimum <= 10.0) {
      for (int value = static_cast<int>(std::ceil(minimum));
           value <= static_cast<int>(std::floor(maximum)); ++value) {
        candidates.push_back(value);
      }
    } else if (maximum <= 100.0) {
      candidates = {minimum, 25, 50, 75, maximum};
    } else if (maximum <= 1024.0) {
      candidates = {minimum, 25, 50, 100, 250, maximum};
    } else {
      candidates = {minimum, 100, 1000, 5000, maximum};
    }

    std::vector<double> result;
    for (const auto candidate : candidates) {
      if (candidate < minimum || candidate > maximum) {
        continue;
      }
      if (result.empty() || std::abs(result.back() - candidate) > 0.0001) {
        result.push_back(candidate);
      }
    }
    return result;
  }

  QString quick_label(double value) const {
    const int decimals = [&] {
      if constexpr (std::is_same_v<SpinBox, QDoubleSpinBox>) {
        return spin_->decimals();
      } else {
        return 0;
      }
    }();
    auto number = QString::number(value, 'f', decimals);
    if (decimals > 0) {
      while (number.endsWith(QLatin1Char('0'))) {
        number.chop(1);
      }
      if (number.endsWith(QLatin1Char('.'))) {
        number.chop(1);
      }
    }
    return number + spin_->suffix();
  }

  QString quick_object_token(double value) const {
    auto token = QString::number(value, 'f', 3);
    while (token.endsWith(QLatin1Char('0'))) {
      token.chop(1);
    }
    if (token.endsWith(QLatin1Char('.'))) {
      token.chop(1);
    }
    token.replace(QLatin1Char('-'), QStringLiteral("Minus"));
    token.replace(QLatin1Char('.'), QStringLiteral("Point"));
    return token;
  }

  void show_popup() {
    if (popup_ != nullptr) {
      popup_->close();
      return;
    }
    if (popup_dismissed_ms_ >= 0 &&
        popup_clock_.elapsed() - popup_dismissed_ms_ < 300) {
      popup_dismissed_ms_ = -1;
      return;
    }

    auto* popup = new QFrame(spin_, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName(base_name_ + QStringLiteral("Popup"));
    popup->setFrameShape(QFrame::StyledPanel);
    popup_ = popup;
    QObject::connect(popup, &QObject::destroyed, this, [this] {
      if (editor_->rect().contains(
              editor_->mapFromGlobal(QCursor::pos()))) {
        popup_dismissed_ms_ = popup_clock_.elapsed();
      }
    });

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto* slider = new QSlider(Qt::Horizontal, popup);
    slider->setObjectName(base_name_ + QStringLiteral("PopupSlider"));
    slider->setMinimumWidth(220);
    layout->addWidget(slider);

    const double slider_maximum = [this] {
      double maximum = static_cast<double>(spin_->maximum());
      const auto cap = spin_->property(kToolbarSpinboxSliderMaxProperty);
      bool cap_valid = false;
      if (const double cap_value = cap.toDouble(&cap_valid); cap_valid) {
        maximum = std::min(maximum, cap_value);
      }
      return std::max(maximum, static_cast<double>(spin_->value()));
    }();

    if constexpr (std::is_same_v<SpinBox, QSpinBox>) {
      const int maximum = static_cast<int>(std::lround(slider_maximum));
      slider->setRange(spin_->minimum(), maximum);
      slider->setPageStep(std::max(1, (maximum - spin_->minimum()) / 20));
      slider->setValue(spin_->value());
      QObject::connect(slider, &QSlider::valueChanged, spin_,
                       &QSpinBox::setValue);
      QObject::connect(spin_, &QSpinBox::valueChanged, popup,
                       [slider](int new_value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(new_value);
                       });
    } else {
      const int decimal_places = std::clamp(spin_->decimals(), 0, 3);
      const double scale = std::pow(10.0, decimal_places);
      const auto scaled = [scale](double value) {
        return static_cast<int>(std::clamp(
            static_cast<long long>(std::lround(value * scale)),
            static_cast<long long>(std::numeric_limits<int>::min()),
            static_cast<long long>(std::numeric_limits<int>::max())));
      };
      slider->setRange(scaled(spin_->minimum()), scaled(slider_maximum));
      slider->setPageStep(
          std::max(1, (slider->maximum() - slider->minimum()) / 20));
      slider->setValue(scaled(spin_->value()));
      QObject::connect(slider, &QSlider::valueChanged, spin_,
                       [spin = spin_, scale](int new_value) {
                         spin->setValue(static_cast<double>(new_value) / scale);
                       });
      QObject::connect(spin_, &QDoubleSpinBox::valueChanged, popup,
                       [slider, scaled](double new_value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(scaled(new_value));
                       });
    }

    const auto choices = quick_values();
    if (!choices.empty()) {
      auto* quick_row = new QHBoxLayout();
      quick_row->setContentsMargins(0, 0, 0, 0);
      quick_row->setSpacing(3);
      for (const auto quick_value : choices) {
        const auto label = quick_label(quick_value);
        auto* button = new QPushButton(label, popup);
        button->setObjectName(base_name_ + QStringLiteral("Quick") +
                              quick_object_token(quick_value));
        button->setFixedWidth(
            std::clamp(button->fontMetrics().horizontalAdvance(label) + 16,
                       38, 78));
        button->setFixedHeight(24);
        quick_row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, spin_,
                         [spin = spin_, quick_value] {
                           spin->setValue(quick_value);
                         });
      }
      layout->addLayout(quick_row);
    }

    popup->adjustSize();
    position_popup_below(*spin_, *popup);
    popup->show();
    slider->setFocus(Qt::PopupFocusReason);
  }

  SpinBox* spin_;
  QString base_name_;
  QAction* action_{nullptr};
  QLineEdit* editor_{nullptr};
  NumericPopupChevron* chevron_{nullptr};
  QPointer<QFrame> popup_{};
  QElapsedTimer popup_clock_{};
  qint64 popup_dismissed_ms_{-1};
};

template <typename SpinBox>
void install_numeric_popup(SpinBox* spin) {
  constexpr auto kInstalledProperty = "patchy.numericPopupInstalled";
  if (spin->property(kInstalledProperty).toBool()) {
    return;
  }
  spin->setProperty(kInstalledProperty, true);
  new NumericPopupController<SpinBox>(spin);
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

namespace {

// Frame, QSS padding, line-edit text margins, and cursor slack around the value
// text of an options-bar spin box (the stylesheet is not applied yet when the
// bar is built, so this cannot be read from the widget).
constexpr int kToolbarSpinboxChromeWidth = 14;

int toolbar_spinbox_width(int width, const QFontMetrics& metrics, const QString& min_text,
                          const QString& max_text) {
  const int text_width = std::max(metrics.horizontalAdvance(min_text),
                                  metrics.horizontalAdvance(max_text));
  return std::max(width, text_width + kChevronAreaWidth + kToolbarSpinboxChromeWidth);
}

}  // namespace

void configure_toolbar_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  const auto locale = spin->locale();
  spin->setFixedWidth(toolbar_spinbox_width(
      width, spin->fontMetrics(),
      spin->prefix() + locale.toString(spin->minimum()) + spin->suffix(),
      spin->prefix() + locale.toString(spin->maximum()) + spin->suffix()));
  install_numeric_popup(spin);
}

void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  const auto locale = spin->locale();
  spin->setFixedWidth(toolbar_spinbox_width(
      width, spin->fontMetrics(),
      spin->prefix() + locale.toString(spin->minimum(), 'f', spin->decimals()) + spin->suffix(),
      spin->prefix() + locale.toString(spin->maximum(), 'f', spin->decimals()) + spin->suffix()));
  install_numeric_popup(spin);
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

QSpinBox* add_dialog_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                     const QString& slider_object_name, const QString& spin_object_name,
                                     int minimum, int maximum, int value, const QString& suffix,
                                     int spin_width, int row_spacing) {
  auto* row = new QWidget(parent);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 0);
  if (row_spacing >= 0) {
    row_layout->setSpacing(row_spacing);
  }
  auto* slider = new QSlider(Qt::Horizontal, row);
  slider->setObjectName(slider_object_name);
  slider->setRange(minimum, maximum);
  slider->setValue(value);
  auto* spin = new QSpinBox(row);
  spin->setObjectName(spin_object_name);
  spin->setRange(minimum, maximum);
  spin->setValue(value);
  if (!suffix.isEmpty()) {
    spin->setSuffix(suffix);
  }
  configure_dialog_spinbox(spin, spin_width);
  row_layout->addWidget(slider, 1);
  row_layout->addWidget(spin);
  QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
  QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
  form->addRow(label, row);
  return spin;
}

void position_popup_below(const QWidget& anchor, QWidget& popup) {
  auto position = anchor.mapToGlobal(QPoint(0, anchor.height()));
  if (const auto* screen = anchor.screen(); screen != nullptr) {
    const auto available = screen->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() - popup.width() + 1)));
    if (position.y() + popup.height() > available.bottom() + 1) {
      position.setY(std::max(available.top(), anchor.mapToGlobal(QPoint(0, 0)).y() - popup.height()));
    }
  }
  popup.move(position);
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

VisibleSizeGrip::VisibleSizeGrip(QWidget* parent) : QSizeGrip(parent) {
  setFixedSize(16, 16);
}

void VisibleSizeGrip::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(0x9A, 0x9A, 0x9A), 1.6));
  for (int line = 0; line < 3; ++line) {
    const auto offset = 3.0 + line * 4.0;
    painter.drawLine(QPointF(width() - 2.0, height() - 2.0 - offset),
                     QPointF(width() - 2.0 - offset, height() - 2.0));
  }
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

void suppress_native_tab_bar_base(QTabWidget& tabs) {
#ifdef Q_OS_MACOS
  if (auto* tab_bar = tabs.tabBar(); tab_bar != nullptr) {
    tab_bar->setDrawBase(false);
  }
#else
  Q_UNUSED(tabs);
#endif
}

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

namespace {

// Native Windows message boxes accept plain Y/N as accelerators for Yes/No;
// Qt only wires the Alt+mnemonic. An event filter rather than QShortcut so a
// key press reaching the box (directly or by propagating up from a focused
// button) behaves the same for real input and synthetic events in offscreen
// tests, which never go through the platform shortcut map.
class MessageBoxYesNoKeyFilter : public QObject {
 public:
  explicit MessageBoxYesNoKeyFilter(QMessageBox& dialog) : QObject(&dialog), dialog_(dialog) {}

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key_event = static_cast<const QKeyEvent*>(event);
      if (key_event->modifiers() == Qt::NoModifier) {
        QAbstractButton* button = nullptr;
        if (key_event->key() == Qt::Key_Y) {
          button = dialog_.button(QMessageBox::Yes);
        } else if (key_event->key() == Qt::Key_N) {
          button = dialog_.button(QMessageBox::No);
        }
        if (button != nullptr && button->isEnabled()) {
          button->click();
          return true;
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QMessageBox& dialog_;
};

}  // namespace

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
  dialog.installEventFilter(new MessageBoxYesNoKeyFilter(dialog));
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
