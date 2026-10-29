#include "ui/animation_preview_window.hpp"

#include "formats/gif_document_io.hpp"
#include "formats/animation_timing.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QValidator>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace patchy::ui {

namespace {

// Frames declaring a 0 delay would rearm the timer instantly; the preview floors them
// near a video rate instead (GIF viewers apply similar minimums).
constexpr int kMinimumFrameDelayMs = 10;

constexpr std::array<int, 5> kFrameDelayPresetsMs{50, 100, 200, 500, 1000};

// The delay field's text: a number of seconds (the locale's decimal point, at most three
// decimals, 0 to kMaxFrameDelayMs), optionally followed by the unit suffix. nullopt for
// anything else, empty text included.
std::optional<int> parse_delay_field_text(QString text, const QString& suffix, const QLocale& locale) {
  text = text.trimmed();
  const auto unit = suffix.trimmed();
  if (!unit.isEmpty() && text.endsWith(unit, Qt::CaseInsensitive)) {
    text.chop(unit.size());
    text = text.trimmed();
  }
  if (text.isEmpty()) return std::nullopt;
  bool ok = false;
  double seconds = locale.toDouble(text, &ok);
  if (!ok) seconds = QLocale::c().toDouble(text, &ok);
  if (!ok || !std::isfinite(seconds) || seconds < 0.0) return std::nullopt;
  const auto delay_ms = std::llround(seconds * 1000.0);
  if (delay_ms > animation::kMaxFrameDelayMs) return std::nullopt;
  return static_cast<int>(delay_ms);
}

// Lets the delay field hold only a number of seconds and the unit while typing; fixup (Return
// or focus leaving with incomplete text) puts the last committed value back.
class DelayTextValidator : public QValidator {
public:
  DelayTextValidator(std::function<std::optional<int>(const QString&)> parse, std::function<QString()> fallback,
                     QObject* parent)
      : QValidator(parent), parse_(std::move(parse)), fallback_(std::move(fallback)) {}

  State validate(QString& input, int&) const override {
    if (parse_(input).has_value()) return Acceptable;
    // Partial entries ("", "0.", "1 ") stay editable; letters other than the unit do not.
    for (const QChar c : input) {
      if (!c.isDigit() && !c.isSpace() && !c.isLetter() && c != QLatin1Char('.') && c != QLatin1Char(',')) {
        return Invalid;
      }
    }
    return Intermediate;
  }
  void fixup(QString& input) const override { input = fallback_(); }

private:
  std::function<std::optional<int>(const QString&)> parse_;
  std::function<QString()> fallback_;
};

// Persisted settings keys (never rename one).
constexpr const char* kIgnoreNameDelaysKey = "animationPreview/ignoreNameDelays";
constexpr const char* kPlayHiddenLayersKey = "animationPreview/playHiddenLayers";
constexpr const char* kApplyToAllLayersKey = "animationPreview/applyToAllLayers";

}  // namespace

AnimationPreviewWindow::AnimationPreviewWindow(
    std::function<Document*()> document_provider, std::function<void(bool)> visuals_changed,
    std::function<void(std::optional<std::uint32_t>, bool)> apply_frame_time, QWidget* parent)
    : QDialog(parent),
      provider_(std::move(document_provider)),
      visuals_changed_(std::move(visuals_changed)),
      apply_frame_time_(std::move(apply_frame_time)) {
  setObjectName(QStringLiteral("animationPreviewWindow"));
  setWindowFlag(Qt::Tool, true);
  auto* root = new QVBoxLayout(this);
  auto* content = install_dark_dialog_chrome(*this, root, tr("Animation Preview"));
  content->setSpacing(8);

  // Top to bottom: how frames play (delay, the two playback options, Play), then the
  // tools that write frame times into layer names.
  auto* playback_group = new QGroupBox(tr("Playback"), this);
  playback_group->setObjectName(QStringLiteral("animationPlaybackGroup"));
  auto* playback_layout = new QVBoxLayout(playback_group);
  playback_layout->setSpacing(6);

  auto* delay_row = new QHBoxLayout();
  delay_row->setSpacing(8);
  auto* delay_label = new QLabel(tr("Default frame delay:"), playback_group);
  delay_row->addWidget(delay_label);
  // One field (GitHub issue 198 follow-up): type any delay in seconds, or open the list for
  // a common one. The list never grows from typed values.
  delay_combo_ = new QComboBox(playback_group);
  delay_combo_->setObjectName(QStringLiteral("animationFrameDelayCombo"));
  delay_combo_->setEditable(true);
  delay_combo_->setInsertPolicy(QComboBox::NoInsert);
  delay_combo_->setCompleter(nullptr);
  for (const int preset_ms : kFrameDelayPresetsMs) {
    delay_combo_->addItem(format_delay(preset_ms), preset_ms);
  }
  delay_combo_->lineEdit()->setValidator(new DelayTextValidator(
      [this](const QString& text) { return parse_delay_text(text); },
      [this] { return format_delay(default_delay_ms_); }, delay_combo_));
  delay_combo_->setToolTip(tr("How long each frame shows when its layer name has no time. Type a value in "
                              "seconds or pick one from the list. Animated GIF, WebP and PNG export use the "
                              "same default."));
  delay_combo_->setMinimumContentsLength(7);
  delay_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  delay_label->setBuddy(delay_combo_);
  delay_row->addWidget(delay_combo_);
  delay_row->addStretch(1);
  playback_layout->addLayout(delay_row);
  set_default_delay_ms(static_cast<int>(load_image_save_option_defaults().animation_frame_delay_ms), false);

  ignore_name_delays_check_ = new QCheckBox(tr("Ignore delays in layer names"), playback_group);
  ignore_name_delays_check_->setObjectName(QStringLiteral("animationIgnoreNameDelaysCheck"));
  ignore_name_delays_check_->setToolTip(
      tr("Plays every frame for the frame delay above and skips times like \"0.25s\" at the end of "
         "layer names. Preview only: export still uses the times in the names."));
  ignore_name_delays_check_->setChecked(
      app_settings().value(QString::fromLatin1(kIgnoreNameDelaysKey), false).toBool());
  playback_layout->addWidget(ignore_name_delays_check_);

  play_hidden_check_ = new QCheckBox(tr("Play hidden layers"), playback_group);
  play_hidden_check_->setObjectName(QStringLiteral("animationPlayHiddenLayersCheck"));
  play_hidden_check_->setToolTip(
      tr("Includes hidden top-level layers as frames. Preview only: export still uses the visible "
         "layers."));
  play_hidden_check_->setChecked(app_settings().value(QString::fromLatin1(kPlayHiddenLayersKey), false).toBool());
  playback_layout->addWidget(play_hidden_check_);

  auto* play_row = new QHBoxLayout();
  play_row->setSpacing(8);
  play_button_ = new QPushButton(tr("Play"), playback_group);
  play_button_->setObjectName(QStringLiteral("animationPlayButton"));
  play_row->addWidget(play_button_);
  status_label_ = new QLabel(playback_group);
  status_label_->setObjectName(QStringLiteral("animationFrameStatusLabel"));
  play_row->addWidget(status_label_, 1);
  playback_layout->addLayout(play_row);
  content->addWidget(playback_group);

  auto* names_group = new QGroupBox(tr("Frame time in layer names"), this);
  names_group->setObjectName(QStringLiteral("animationFrameTimeGroup"));
  auto* names_layout = new QVBoxLayout(names_group);
  names_layout->setSpacing(6);
  auto* selection_row = new QHBoxLayout();
  selection_row->setSpacing(8);
  selection_time_spin_ = new QDoubleSpinBox(names_group);
  selection_time_spin_->setObjectName(QStringLiteral("animationSelectionTimeSpin"));
  selection_time_spin_->setSuffix(tr(" s"));
  selection_time_spin_->setRange(0.0, animation::kMaxFrameDelayMs / 1000.0);
  selection_time_spin_->setDecimals(3);
  selection_time_spin_->setSingleStep(0.05);
  selection_time_spin_->setValue(default_delay_ms_ / 1000.0);
  selection_time_spin_->setToolTip(tr("The time Set Time writes at the end of layer names."));
  configure_dialog_spinbox(selection_time_spin_, 96);
  selection_row->addWidget(selection_time_spin_);
  auto* set_time_button = new QPushButton(tr("Set Time"), names_group);
  set_time_button->setObjectName(QStringLiteral("animationSetFrameTimeButton"));
  set_time_button->setToolTip(
      tr("Renames the selected layers, or every top-level layer with Apply to all layers, to end with "
         "this frame time, like \"blink 0.25s\"."));
  selection_row->addWidget(set_time_button);
  auto* remove_time_button = new QPushButton(tr("Remove Time"), names_group);
  remove_time_button->setObjectName(QStringLiteral("animationRemoveFrameTimeButton"));
  remove_time_button->setToolTip(
      tr("Removes the frame time from the end of the selected layers' names, or of every top-level "
         "layer's name with Apply to all layers."));
  selection_row->addWidget(remove_time_button);
  selection_row->addStretch(1);
  names_layout->addLayout(selection_row);
  all_layers_check_ = new QCheckBox(tr("Apply to all layers"), names_group);
  all_layers_check_->setObjectName(QStringLiteral("animationFrameTimeAllLayersCheck"));
  all_layers_check_->setToolTip(
      tr("Set Time and Remove Time change every top-level layer instead of only the selected ones."));
  all_layers_check_->setChecked(app_settings().value(QString::fromLatin1(kApplyToAllLayersKey), false).toBool());
  names_layout->addWidget(all_layers_check_);
  content->addWidget(names_group);

  auto* hint = new QLabel(
      tr("Frames are the top-level layers, top layer first, as animated GIF, WebP and PNG export "
         "writes them. A layer name ending in a time, like \"blink 0.25s\", sets that frame's delay."),
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
    if (apply_frame_time_ != nullptr) {
      apply_frame_time_(static_cast<std::uint32_t>(std::clamp<long long>(
                            std::llround(selection_time_spin_->value() * 1000.0), 0, animation::kMaxFrameDelayMs)),
                        all_layers_check_->isChecked());
    }
  });
  connect(remove_time_button, &QPushButton::clicked, this, [this] {
    stop_playback(true);
    if (apply_frame_time_ != nullptr) {
      apply_frame_time_(std::nullopt, all_layers_check_->isChecked());
    }
  });
  // The field edits the shared default delay (the animated GIF export dialog reads the
  // same key); the next frame advance picks a change up immediately. activated is a pick
  // from the list; editingFinished is Return or focus leaving after typing (the validator's
  // fixup has already put the last valid value back for empty or out-of-range text).
  connect(delay_combo_, &QComboBox::activated, this, [this](int index) {
    set_default_delay_ms(delay_combo_->itemData(index).toInt(), true);
  });
  connect(delay_combo_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
    set_default_delay_ms(parse_delay_text(delay_combo_->currentText()).value_or(default_delay_ms_), true);
  });
  // Ignoring name delays applies from the next frame; a frame-list change restarts playback
  // so the new list takes effect at once.
  connect(ignore_name_delays_check_, &QCheckBox::toggled, this, [](bool checked) {
    app_settings().setValue(QString::fromLatin1(kIgnoreNameDelaysKey), checked);
  });
  connect(play_hidden_check_, &QCheckBox::toggled, this, [this](bool checked) {
    app_settings().setValue(QString::fromLatin1(kPlayHiddenLayersKey), checked);
    if (playing()) {
      stop_playback(true);
      start_playback();
    }
  });
  connect(all_layers_check_, &QCheckBox::toggled, this, [](bool checked) {
    app_settings().setValue(QString::fromLatin1(kApplyToAllLayersKey), checked);
  });
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] { advance_frame(); });
  // Return in the delay field (or a time box) only commits the value: a dialog otherwise
  // hands Return to its first auto-default button, which here is Play.
  for (auto* button : findChildren<QPushButton*>()) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }

  // Size to the content rather than a hardcoded height. The hint wraps to a different
  // number of lines depending on the platform's font, and a fixed height cut its last
  // line off in the browser build. totalHeightForWidth asks the layout what the content
  // actually needs at the design width, the same question QWidget::adjustSize asks, and
  // pinning that width as the minimum means the text can never rewrap taller than the
  // height computed here. The rows' own minimum widens it when a font or language needs.
  const int preferred_width = std::max(360, root->minimumSize().width());
  const int needed_height = root->totalHeightForWidth(preferred_width);
  setMinimumSize(preferred_width, needed_height);
  resize(preferred_width, needed_height);
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
  const bool include_hidden = play_hidden_check_->isChecked();
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if (include_hidden || it->visible()) {
      frame_ids_.push_back(it->id());
    }
  }
  if (frame_ids_.empty()) {
    saved_visibility_.clear();
    status_label_->setText(include_hidden ? tr("No layers") : tr("No visible layers"));
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

std::uint32_t AnimationPreviewWindow::frame_delay_ms(std::size_t index) const {
  const auto default_ms = static_cast<std::uint32_t>(default_delay_ms_);
  if (playback_document_ == nullptr || index >= frame_ids_.size() || ignore_name_delays_check_->isChecked()) {
    return default_ms;
  }
  const auto* layer = std::as_const(*playback_document_).find_layer(frame_ids_[index]);
  return layer == nullptr ? default_ms : animation::parse_layer_name_delay_ms(layer->name()).value_or(default_ms);
}

int AnimationPreviewWindow::current_frame_delay_ms() const {
  return std::max(kMinimumFrameDelayMs, static_cast<int>(frame_delay_ms(frame_index_)));
}

void AnimationPreviewWindow::update_playback_controls() {
  play_button_->setText(playing() ? tr("Stop") : tr("Play"));
  if (!playing()) {
    status_label_->clear();
  }
}

void AnimationPreviewWindow::set_default_delay_ms(int delay_ms, bool persist) {
  delay_ms = std::clamp(delay_ms, 0, static_cast<int>(animation::kMaxFrameDelayMs));
  const bool changed = delay_ms != default_delay_ms_;
  default_delay_ms_ = delay_ms;
  {
    const QSignalBlocker block(delay_combo_);
    // An index of -1 clears an editable combo's text, so the text is set after it.
    delay_combo_->setCurrentIndex(delay_combo_->findData(delay_ms));
    delay_combo_->setEditText(format_delay(delay_ms));
  }
  if (persist && changed) {
    app_settings().setValue(QStringLiteral("saveOptions/animationFrameDelayMs"), delay_ms);
    app_settings().setValue(QStringLiteral("saveOptions/gifFrameDelayCs"),
                            std::clamp((delay_ms + 5) / 10, 0, 0xffff));
  }
}

std::optional<int> AnimationPreviewWindow::parse_delay_text(const QString& text) const {
  return parse_delay_field_text(text, tr(" s"), locale());
}

QString AnimationPreviewWindow::format_delay(int delay_ms) const {
  return locale().toString(delay_ms / 1000.0, 'g', 6) + tr(" s");
}

void AnimationPreviewWindow::done(int result) {
  stop_playback(true);
  QDialog::done(result);
}

}  // namespace patchy::ui
