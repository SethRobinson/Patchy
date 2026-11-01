// The companion box of a GIMP 3 plug-in run (gimp_plugin_run_dialog.hpp).

#include "ui/gimp_plugin_run_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

GimpPluginRunDialog::GimpPluginRunDialog(const QString& plugin_name, QWidget* parent)
    : QDialog(parent), plugin_name_(plugin_name) {
  setObjectName(QStringLiteral("gimpPluginRunDialog"));
  setWindowTitle(tr("GIMP 3 Plug-in"));
  setWindowModality(Qt::WindowModal);
  setWindowFlag(Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setMinimumWidth(420);

  auto* layout = new QVBoxLayout(this);
  message_ = new QLabel(this);
  message_->setObjectName(QStringLiteral("gimpPluginRunMessage"));
  message_->setWordWrap(true);
  layout->addWidget(message_);
  hint_ = new QLabel(this);
  hint_->setObjectName(QStringLiteral("gimpPluginRunHint"));
  hint_->setWordWrap(true);
  hint_->setFont(scaled_font(hint_->font(), 0.9));
  hint_->setText(tr("The plug-in's own preview updates as you change its settings. Cancel there leaves the layer as it was."));
  layout->addWidget(hint_);
  bar_ = new QProgressBar(this);
  bar_->setObjectName(QStringLiteral("gimpPluginRunProgressBar"));
  bar_->setRange(0, 0);
  bar_->setTextVisible(false);
  layout->addWidget(bar_);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  stop_button_ = new QPushButton(this);
  stop_button_->setObjectName(QStringLiteral("gimpPluginStopButton"));
  stop_button_->setAutoDefault(false);
  connect(stop_button_, &QPushButton::clicked, this, [this] { reject(); });
  buttons->addWidget(stop_button_);
  layout->addLayout(buttons);

  remember_dialog_position(*this);
  update_state();
}

void GimpPluginRunDialog::set_plugin_window(const QRect& screen_rect) {
  plugin_window_rect_ = screen_rect;
  update_state();
  if (screen_rect.isValid()) {
    place_below_plugin_window();
    reveal();
  }
}

void GimpPluginRunDialog::set_progress(double fraction, const QString& text) {
  if (!text.isEmpty()) {
    progress_text_ = text;
  }
  // A plug-in reports progress while its dialog previews, too; only progress with the
  // dialog gone is the result being applied.
  if (!plugin_window_rect_.isValid()) {
    progress_seen_ = true;
  }
  if (fraction >= 0) {
    if (bar_->maximum() == 0) {
      bar_->setRange(0, 100);
      bar_->setTextVisible(true);
    }
    bar_->setValue(std::clamp(static_cast<int>(std::lround(fraction * 100.0)), 0, 100));
  } else if (bar_->maximum() != 0) {
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
  }
  update_state();
}

void GimpPluginRunDialog::reveal() {
  if (!isVisible() && !stop_requested_) {
    show();
  }
}

void GimpPluginRunDialog::done(int result) {
  stop_requested_ = true;
  QDialog::done(result);
}

void GimpPluginRunDialog::update_state() {
  const bool window_up = plugin_window_rect_.isValid();
  state_ = window_up ? State::WaitingForUser : progress_seen_ ? State::Applying : State::Starting;
  switch (state_) {
    case State::Starting:
      message_->setText(tr("Starting %1...").arg(plugin_name_));
      hint_->setVisible(false);
      bar_->setVisible(true);
      stop_button_->setText(tr("Stop Plug-in"));
      stop_button_->setToolTip(tr("Ends the plug-in. The layer stays as it was."));
      break;
    case State::WaitingForUser:
      //: %1 is the plug-in's name; shown while its own settings window is open.
      message_->setText(
          tr("%1 is open in its own window. Adjust its settings there and click its OK button to apply it to this layer.")
              .arg(plugin_name_));
      hint_->setVisible(true);
      bar_->setVisible(false);
      stop_button_->setText(tr("Stop Plug-in"));
      stop_button_->setToolTip(tr("Ends the plug-in. The layer stays as it was."));
      break;
    case State::Applying: {
      //: %1 is the plug-in's name, %2 its own progress message.
      const auto with_text = tr("%1: %2").arg(plugin_name_, progress_text_);
      message_->setText(progress_text_.isEmpty() ? tr("Applying %1...").arg(plugin_name_) : with_text);
      hint_->setVisible(false);
      bar_->setVisible(true);
      stop_button_->setText(tr("Cancel"));
      stop_button_->setToolTip(tr("Ends the plug-in. The layer stays as it was."));
      break;
    }
  }
  adjustSize();
}

void GimpPluginRunDialog::place_below_plugin_window() {
  if (!plugin_window_rect_.isValid() || placed_below_plugin_) {
    return;
  }
  placed_below_plugin_ = true;
  QScreen* screen = QGuiApplication::screenAt(plugin_window_rect_.center());
  if (screen == nullptr || screen->devicePixelRatio() != 1.0) {
    return;
  }
  const auto available = screen->availableGeometry();
  const QSize size = this->size().isValid() && this->size().width() > 0 ? this->size() : sizeHint();
  int x = plugin_window_rect_.center().x() - size.width() / 2;
  int y = plugin_window_rect_.bottom() + 8;
  if (y + size.height() > available.bottom()) {
    y = plugin_window_rect_.top() - size.height() - 8;
  }
  x = std::clamp(x, available.left(), std::max(available.left(), available.right() - size.width()));
  y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() - size.height()));
  move(x, y);
}

}  // namespace patchy::ui
