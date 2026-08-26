#include "ui/animation_preview_window.hpp"

#include "formats/gif_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace patchy::ui {

namespace {

// Frames declaring a 0 delay would rearm the timer instantly; the preview floors them
// near a video rate instead (GIF viewers apply similar minimums).
constexpr int kMinimumFrameDelayMs = 10;

}  // namespace

AnimationPreviewWindow::AnimationPreviewWindow(
    std::function<Document*()> document_provider, std::function<void(bool)> visuals_changed,
    std::function<void(std::optional<std::uint16_t>)> apply_selection_frame_time, QWidget* parent)
    : QDialog(parent),
      provider_(std::move(document_provider)),
      visuals_changed_(std::move(visuals_changed)),
      apply_selection_frame_time_(std::move(apply_selection_frame_time)) {
  setObjectName(QStringLiteral("animationPreviewWindow"));
  setWindowFlag(Qt::Tool, true);
  auto* root = new QVBoxLayout(this);
  auto* content = install_dark_dialog_chrome(*this, root, tr("Animation Preview"));
  content->setSpacing(8);

  auto* play_row = new QHBoxLayout();
  play_row->setSpacing(8);
  play_button_ = new QPushButton(tr("Play"), this);
  play_button_->setObjectName(QStringLiteral("animationPlayButton"));
  play_row->addWidget(play_button_);
  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("animationFrameStatusLabel"));
  play_row->addWidget(status_label_, 1);
  content->addLayout(play_row);

  auto* delay_row = new QHBoxLayout();
  delay_row->setSpacing(8);
  delay_row->addWidget(new QLabel(tr("Frame delay:"), this));
  delay_spin_ = new QDoubleSpinBox(this);
  delay_spin_->setObjectName(QStringLiteral("animationFrameDelaySpin"));
  delay_spin_->setSuffix(tr(" s"));
  delay_spin_->setRange(0.0, 655.35);  // the GIF u16 centisecond wire range
  delay_spin_->setDecimals(2);
  delay_spin_->setSingleStep(0.05);
  delay_spin_->setValue(
      std::clamp(app_settings().value(QStringLiteral("saveOptions/gifFrameDelayCs"), 10).toInt(), 0, 0xffff) /
      100.0);
  configure_dialog_spinbox(delay_spin_, 96);
  delay_row->addWidget(delay_spin_);
  delay_row->addStretch(1);
  content->addLayout(delay_row);

  auto* selection_row = new QHBoxLayout();
  selection_row->setSpacing(8);
  selection_row->addWidget(new QLabel(tr("Selected layers:"), this));
  selection_time_spin_ = new QDoubleSpinBox(this);
  selection_time_spin_->setObjectName(QStringLiteral("animationSelectionTimeSpin"));
  selection_time_spin_->setSuffix(tr(" s"));
  selection_time_spin_->setRange(0.0, 655.35);  // the GIF u16 centisecond wire range
  selection_time_spin_->setDecimals(2);
  selection_time_spin_->setSingleStep(0.05);
  selection_time_spin_->setValue(delay_spin_->value());
  configure_dialog_spinbox(selection_time_spin_, 96);
  selection_row->addWidget(selection_time_spin_);
  auto* set_time_button = new QPushButton(tr("Set Time"), this);
  set_time_button->setObjectName(QStringLiteral("animationSetFrameTimeButton"));
  set_time_button->setToolTip(
      tr("Renames the selected layers to end with this frame time, like \"blink 0.25s\"."));
  selection_row->addWidget(set_time_button);
  auto* remove_time_button = new QPushButton(tr("Remove"), this);
  remove_time_button->setObjectName(QStringLiteral("animationRemoveFrameTimeButton"));
  remove_time_button->setToolTip(tr("Removes the trailing frame time from the selected layers' names."));
  selection_row->addWidget(remove_time_button);
  selection_row->addStretch(1);
  content->addLayout(selection_row);

  auto* hint = new QLabel(
      tr("Plays the visible top-level layers as frames, top layer first, exactly like the animated GIF "
         "export. A layer name ending in a time, like \"blink 0.25s\", sets that frame's delay."),
      this);
  hint->setObjectName(QStringLiteral("animationPreviewHintLabel"));
  hint->setWordWrap(true);
  content->addWidget(hint);
  append_themed_style(*this, dialog_spinbox_button_style());

  connect(play_button_, &QPushButton::clicked, this, [this] { toggle_playback(); });
  // Both name edits stop playback first: the caller pushes an undo snapshot, and one taken
  // mid-play would capture a preview frame's visibility.
  connect(set_time_button, &QPushButton::clicked, this, [this] {
    stop_playback(true);
    if (apply_selection_frame_time_ != nullptr) {
      apply_selection_frame_time_(static_cast<std::uint16_t>(
          std::clamp<long long>(std::llround(selection_time_spin_->value() * 100.0), 0, 0xffff)));
    }
  });
  connect(remove_time_button, &QPushButton::clicked, this, [this] {
    stop_playback(true);
    if (apply_selection_frame_time_ != nullptr) {
      apply_selection_frame_time_(std::nullopt);
    }
  });
  // The spin edits the shared default delay (the animated GIF export dialog reads the
  // same key); the next frame advance picks a change up immediately.
  connect(delay_spin_, &QDoubleSpinBox::valueChanged, this, [](double value) {
    app_settings().setValue(
        QStringLiteral("saveOptions/gifFrameDelayCs"),
        static_cast<int>(std::clamp<long long>(std::llround(value * 100.0), 0, 0xffff)));
  });
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] { advance_frame(); });

  // Size to the content rather than a hardcoded height. The hint wraps to a different
  // number of lines depending on the platform's font, and a fixed height cut its last
  // line off in the browser build. totalHeightForWidth asks the layout what the content
  // actually needs at the design width, the same question QWidget::adjustSize asks, and
  // pinning that width as the minimum means the text can never rewrap taller than the
  // height computed here.
  constexpr int kPreferredWidth = 360;
  const int needed_height = root->totalHeightForWidth(kPreferredWidth);
  setMinimumSize(kPreferredWidth, needed_height);
  resize(kPreferredWidth, needed_height);
}

void AnimationPreviewWindow::toggle_playback() {
  if (playing()) {
    stop_playback(true);
  } else {
    start_playback();
  }
}

void AnimationPreviewWindow::start_playback() {
  if (playing()) {
    return;
  }
  auto* document = provider_ != nullptr ? provider_() : nullptr;
  if (document == nullptr) {
    return;
  }
  saved_visibility_.clear();
  frame_ids_.clear();
  const auto& layers = std::as_const(*document).layers();
  for (const auto& layer : layers) {
    saved_visibility_.emplace_back(layer.id(), layer.visible());
  }
  // Top to bottom, the animated GIF export's frame order (index 0 is the bottom layer).
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if (it->visible()) {
      frame_ids_.push_back(it->id());
    }
  }
  if (frame_ids_.empty()) {
    saved_visibility_.clear();
    status_label_->setText(tr("No visible layers"));
    return;
  }
  playback_document_ = document;
  frame_index_ = 0;
  update_playback_controls();
  show_current_frame();
}

void AnimationPreviewWindow::stop_playback(bool restore_visibility) {
  timer_.stop();
  if (playback_document_ == nullptr) {
    return;
  }
  auto* document = playback_document_;
  playback_document_ = nullptr;
  // Restore only while the playback document is verifiably the live active one; a
  // mismatch means a lifecycle path was missed and the pointer cannot be trusted.
  if (restore_visibility && provider_ != nullptr && provider_() == document) {
    for (const auto& [id, visible] : saved_visibility_) {
      // Writes go through find_layer: mutable children() bumps revisions.
      if (auto* layer = document->find_layer(id); layer != nullptr) {
        layer->set_visible(visible);
      }
    }
    if (visuals_changed_ != nullptr) {
      visuals_changed_(true);
    }
  }
  saved_visibility_.clear();
  frame_ids_.clear();
  frame_index_ = 0;
  update_playback_controls();
}

void AnimationPreviewWindow::stop_playback_for(const Document* document) {
  if (playback_document_ != nullptr && playback_document_ == document) {
    stop_playback(true);
  }
}

void AnimationPreviewWindow::advance_frame() {
  if (playback_document_ == nullptr) {
    return;
  }
  auto* document = provider_ != nullptr ? provider_() : nullptr;
  if (document != playback_document_) {
    stop_playback(false);
    return;
  }
  // Layers deleted mid-play drop out of the cycle.
  std::erase_if(frame_ids_,
                [&](LayerId id) { return std::as_const(*document).find_layer(id) == nullptr; });
  if (frame_ids_.empty()) {
    stop_playback(true);
    return;
  }
  frame_index_ = (frame_index_ + 1) % frame_ids_.size();
  show_current_frame();
}

void AnimationPreviewWindow::show_current_frame() {
  auto* document = playback_document_;
  const auto current = frame_ids_[frame_index_];
  for (const auto& [id, visible] : saved_visibility_) {
    if (auto* layer = document->find_layer(id); layer != nullptr) {
      layer->set_visible(id == current);
    }
  }
  if (visuals_changed_ != nullptr) {
    visuals_changed_(false);
  }
  status_label_->setText(tr("Frame %1/%2").arg(frame_index_ + 1).arg(frame_ids_.size()));
  timer_.start(current_frame_delay_ms());
}

std::uint16_t AnimationPreviewWindow::frame_delay_cs(std::size_t index) const {
  const auto default_cs = static_cast<std::uint16_t>(
      std::clamp<long long>(std::llround(delay_spin_->value() * 100.0), 0, 0xffff));
  if (playback_document_ == nullptr || index >= frame_ids_.size()) {
    return default_cs;
  }
  const auto* layer = std::as_const(*playback_document_).find_layer(frame_ids_[index]);
  if (layer == nullptr) {
    return default_cs;
  }
  return gif::parse_layer_name_delay_cs(layer->name()).value_or(default_cs);
}

int AnimationPreviewWindow::current_frame_delay_ms() const {
  return std::max(kMinimumFrameDelayMs, static_cast<int>(frame_delay_cs(frame_index_)) * 10);
}

void AnimationPreviewWindow::update_playback_controls() {
  play_button_->setText(playing() ? tr("Stop") : tr("Play"));
  if (!playing()) {
    status_label_->clear();
  }
}

void AnimationPreviewWindow::done(int result) {
  stop_playback(true);
  QDialog::done(result);
}

}  // namespace patchy::ui
