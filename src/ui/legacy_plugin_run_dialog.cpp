// The companion box of a legacy plug-in run (legacy_plugin_run_dialog.hpp).

#include "ui/legacy_plugin_run_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QGuiApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

LegacyPluginRunDialog::LegacyPluginRunDialog(const QString& plugin_name, QWidget* parent)
    : QDialog(parent), plugin_name_(plugin_name) {
  setObjectName(QStringLiteral("legacyPluginRunDialog"));
  setWindowTitle(tr("Legacy Photoshop Plug-in"));
  // Window-modal like the filter progress boxes, but never focused: the
  // plug-in's window belongs to another process and must keep the foreground;
  // the buttons still take clicks.
  setWindowModality(Qt::WindowModal);
  setWindowFlag(Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setMinimumWidth(420);

  auto* layout = new QVBoxLayout(this);
  message_ = new QLabel(this);
  message_->setObjectName(QStringLiteral("legacyPluginRunMessage"));
  message_->setWordWrap(true);
  layout->addWidget(message_);
  hint_ = new QLabel(this);
  hint_->setObjectName(QStringLiteral("legacyPluginRunHint"));
  hint_->setWordWrap(true);
  hint_->setFont(scaled_font(hint_->font(), 0.9));
  hint_->setText(tr("Classic plug-ins show their preview inside their own window. The layer changes after you click OK."));
  layout->addWidget(hint_);
  bar_ = new QProgressBar(this);
  bar_->setObjectName(QStringLiteral("legacyPluginRunProgressBar"));
  bar_->setRange(0, 0);  // busy until the plug-in reports progress
  bar_->setTextVisible(false);
  layout->addWidget(bar_);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  show_window_button_ = new QPushButton(tr("Show Plug-in Window"), this);
  show_window_button_->setObjectName(QStringLiteral("legacyPluginShowWindowButton"));
  show_window_button_->setToolTip(tr("Brings the plug-in's window back in front of Patchy."));
  show_window_button_->setAutoDefault(false);
  connect(show_window_button_, &QPushButton::clicked, this, [this] { show_window_requested_ = true; });
  buttons->addWidget(show_window_button_);
  stop_button_ = new QPushButton(this);
  stop_button_->setObjectName(QStringLiteral("legacyPluginStopButton"));
  stop_button_->setAutoDefault(false);
  connect(stop_button_, &QPushButton::clicked, this, [this] { reject(); });
  buttons->addWidget(stop_button_);
  layout->addLayout(buttons);

  remember_dialog_position(*this);
  update_state();
}

void LegacyPluginRunDialog::set_phase(LegacyPluginPhase phase) {
  phase_ = phase;
  update_state();
}

void LegacyPluginRunDialog::set_plugin_window(const QRect& screen_rect) {
  plugin_window_rect_ = screen_rect;
  update_state();
  if (screen_rect.isValid()) {
    place_below_plugin_window();
    reveal();
  }
}

void LegacyPluginRunDialog::set_progress(int done, int total) {
  if (total <= 0) {
    return;
  }
  progress_seen_ = true;
  if (bar_->maximum() == 0) {
    bar_->setRange(0, 100);
    bar_->setTextVisible(true);
  }
  bar_->setValue(std::clamp(static_cast<int>((static_cast<long long>(done) * 100) / total), 0, 100));
  update_state();
}

void LegacyPluginRunDialog::reveal() {
  if (!isVisible() && !stop_requested_) {
    show();
  }
}

bool LegacyPluginRunDialog::take_show_window_request() noexcept {
  const bool requested = show_window_requested_;
  show_window_requested_ = false;
  return requested;
}

void LegacyPluginRunDialog::done(int result) {
  // Whatever closed the box (its stop button, the title-bar close button), the
  // user wants the run to end; the runner polls stop_requested().
  stop_requested_ = true;
  QDialog::done(result);
}

void LegacyPluginRunDialog::update_state() {
  const bool window_up = plugin_window_rect_.isValid();
  State state = State::Starting;
  if (progress_seen_) {
    state = State::Applying;
  } else if (window_up) {
    // A window during Continue is the plug-in's own progress display; during
    // Parameters (or Start, for plug-ins that open their dialog there) it is
    // the settings dialog waiting for the user.
    state = (phase_ == LegacyPluginPhase::Continue || phase_ == LegacyPluginPhase::Finish) ? State::Applying
                                                                                             : State::WaitingForUser;
  } else {
    switch (phase_) {
      case LegacyPluginPhase::Prepare:
      case LegacyPluginPhase::Start:
      case LegacyPluginPhase::Continue:
      case LegacyPluginPhase::Finish:
        state = State::Applying;
        break;
      case LegacyPluginPhase::Unknown:
      case LegacyPluginPhase::Parameters:
        state = State::Starting;
        break;
    }
  }
  state_ = state;
  switch (state_) {
    case State::Starting:
      message_->setText(tr("Starting %1...").arg(plugin_name_));
      hint_->setVisible(false);
      bar_->setVisible(true);
      stop_button_->setText(tr("Force Stop Plug-in"));
      //: Tooltip of the stop button while a plug-in is loading or waits for the user in its own window.
      stop_button_->setToolTip(
          tr("Ends the plug-in without applying it. Use it only if the plug-in's window has stopped responding."));
      break;
    case State::WaitingForUser:
      //: %1 is the plug-in's name; shown while its own settings window is open.
      message_->setText(
          tr("%1 is open in its own window. Adjust its settings there and click its OK button to apply it to this layer.")
              .arg(plugin_name_));
      hint_->setVisible(true);
      bar_->setVisible(false);
      stop_button_->setText(tr("Force Stop Plug-in"));
      stop_button_->setToolTip(
          tr("Ends the plug-in without applying it. Use it only if the plug-in's window has stopped responding."));
      break;
    case State::Applying:
      message_->setText(tr("Applying %1...").arg(plugin_name_));
      hint_->setVisible(false);
      bar_->setVisible(true);
      stop_button_->setText(tr("Cancel"));
      stop_button_->setToolTip(tr("Stops the plug-in. The layer stays unchanged."));
      break;
  }
  show_window_button_->setVisible(window_up);
  adjustSize();
}

void LegacyPluginRunDialog::place_below_plugin_window() {
  // Once, when the plug-in's window first appears; after that both windows
  // are the user's to arrange.
  if (!plugin_window_rect_.isValid() || placed_below_plugin_) {
    return;
  }
  placed_below_plugin_ = true;
  // Physical pixels from the helper; Qt positions in logical ones, equal at
  // 100 percent scaling. On a scaled monitor the box keeps its usual place.
  QScreen* screen = QGuiApplication::screenAt(plugin_window_rect_.center());
  if (screen == nullptr || screen->devicePixelRatio() != 1.0) {
    return;
  }
  const auto available = screen->availableGeometry();
  const QSize size = this->size().isValid() && this->size().width() > 0 ? this->size() : sizeHint();
  int x = plugin_window_rect_.center().x() - size.width() / 2;
  int y = plugin_window_rect_.bottom() + 8;
  if (y + size.height() > available.bottom()) {
    y = plugin_window_rect_.top() - size.height() - 8;  // no room below: above it
  }
  x = std::clamp(x, available.left(), std::max(available.left(), available.right() - size.width()));
  y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() - size.height()));
  move(x, y);
}

}  // namespace patchy::ui
