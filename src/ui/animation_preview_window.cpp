#include "ui/animation_preview_window.hpp"

#include "formats/gif_document_io.hpp"
#include "formats/animation_timing.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QToolButton>
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

// The - and + buttons step a delay field by a tenth of a second.
constexpr double kFrameDelayStepSeconds = 0.1;

// A delay field's text: a number of seconds (the locale's decimal point, at most three
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

// Seconds with at most three decimals and no trailing zeros ("0.1", "0.125", "2"), and no
// group separator, so the text always parses back.
QString format_delay_seconds(double seconds, QLocale locale) {
  locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
  auto text = locale.toString(seconds, 'f', 3);
  const auto point = locale.decimalPoint();
  if (text.contains(point)) {
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(point)) text.chop(point.size());
  }
  return text;
}

// One delay field (GitHub issue 198 follow-up): type seconds, with or without the unit, or
// step with the - and + buttons of the shared dialog spin style. Text it cannot use (letters,
// nothing, past the maximum) puts the last value back when the edit is committed.
class DelaySecondsSpinBox final : public QDoubleSpinBox {
public:
  DelaySecondsSpinBox(const QString& suffix, QWidget* parent) : QDoubleSpinBox(parent) {
    setDecimals(3);
    setRange(0.0, animation::kMaxFrameDelayMs / 1000.0);
    setSingleStep(kFrameDelayStepSeconds);
    setSuffix(suffix);
    // valueChanged only on Return, focus leaving, a step or a preset, never per keystroke.
    setKeyboardTracking(false);
    setCorrectionMode(QAbstractSpinBox::CorrectToPreviousValue);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // The field stretches so its drop-down ends at the right edge of the buttons below it.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  // At least as wide as a long delay ("9999.19 s") plus the shared spin style's padding (6 px
  // left, 54 px for the - and + buttons), its border and a little air. Measured when the
  // layout asks, so the font is the polished one.
  QSize sizeHint() const override {
    auto size = QDoubleSpinBox::sizeHint();
    const auto sample = format_delay_seconds(9999.19, locale()) + suffix();
    size.setWidth(fontMetrics().horizontalAdvance(sample) + 6 + 54 + 2 + 6);
    return size;
  }
  QSize minimumSizeHint() const override { return sizeHint(); }

  QString textFromValue(double value) const override { return format_delay_seconds(value, locale()); }

  double valueFromText(const QString& text) const override {
    return parse_delay_field_text(text, suffix(), locale())
               .value_or(static_cast<int>(std::llround(value() * 1000.0))) /
           1000.0;
  }

  QValidator::State validate(QString& input, int& pos) const override {
    // Qt's spin validator keeps the suffix on the end of the text, so a unit typed by hand
    // lands in front of it ("0.125 s s"): the typed one replaces the kept one.
    const auto unit = suffix().trimmed();
    if (!unit.isEmpty() && input.endsWith(suffix())) {
      const auto head = input.left(input.size() - suffix().size());
      if (head.trimmed().endsWith(unit, Qt::CaseInsensitive)) {
        input = head;
        pos = std::min(pos, static_cast<int>(input.size()));
      }
    }
    if (parse_delay_field_text(input, suffix(), locale()).has_value()) return QValidator::Acceptable;
    // Partial entries ("", "0.", "1 ") stay editable; symbols do not.
    for (const QChar c : input) {
      if (!c.isDigit() && !c.isSpace() && !c.isLetter() && c != QLatin1Char('.') && c != QLatin1Char(',')) {
        return QValidator::Invalid;
      }
    }
    return QValidator::Intermediate;
  }
};

// The presets drop-down that sits against a delay field's right edge (the interface-scale
// field's pattern in Preferences): picking one sets the field, and the preset the field
// holds is ticked when the list opens.
QToolButton* add_delay_presets_button(QDoubleSpinBox* field, const QString& object_name, const QString& tooltip,
                                      QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setObjectName(object_name);
  button->setPopupMode(QToolButton::InstantPopup);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setIcon(themed_svg_icon(QStringLiteral("chevron-down")));
  button->setToolTip(tooltip);
  button->setAccessibleName(tooltip);
  auto* menu = new QMenu(button);
  menu->setObjectName(object_name + QStringLiteral("Menu"));
  for (const int preset_ms : kFrameDelayPresetsMs) {
    auto* action = menu->addAction(field->textFromValue(preset_ms / 1000.0) + field->suffix());
    action->setData(preset_ms);
    action->setCheckable(true);
    QObject::connect(action, &QAction::triggered, field, [field, preset_ms] { field->setValue(preset_ms / 1000.0); });
  }
  QObject::connect(menu, &QMenu::aboutToShow, field, [field, menu] {
    field->interpretText();
    const auto field_ms = std::llround(field->value() * 1000.0);
    for (auto* action : menu->actions()) {
      action->setChecked(action->data().toInt() == field_ms);
    }
  });
  button->setMenu(menu);
  return button;
}

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
  // The presets drop-downs: field-faced chevron buttons as tall as the fields beside them
  // (Preferences' interface-scale presets button). The spin style comes last.
  // Qt re-reads a changed sheet only when the dialog or a child is already polished; under
  // the main window's sheet the chrome-only sheet otherwise stays cached and the fields
  // lose their - and + buttons (no field polished itself before the append).
  ensurePolished();
  append_themed_style(*this, ThemedQss(QStringLiteral(R"(
    QDialog#animationPreviewWindow QToolButton#animationFrameDelayPresetsButton,
    QDialog#animationPreviewWindow QToolButton#animationSelectionTimePresetsButton {
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      border-radius: 0;
      padding: 0;
      min-width: 16px;
      max-width: 16px;
      min-height: 26px;
    }
    QDialog#animationPreviewWindow QToolButton#animationFrameDelayPresetsButton:hover,
    QDialog#animationPreviewWindow QToolButton#animationSelectionTimePresetsButton:hover {
      border-color: @button_hover_border;
    }
    /* The edit area already stops at the + button, so the right padding only has to clear
       the - button (Preferences' interface-scale field): the value sits against it. */
    QDialog#animationPreviewWindow QDoubleSpinBox#animationFrameDelaySpin,
    QDialog#animationPreviewWindow QDoubleSpinBox#animationSelectionTimeSpin {
      padding-right: 30px;
    }
    QDialog#animationPreviewWindow QToolButton#animationFrameDelayPresetsButton::menu-indicator,
    QDialog#animationPreviewWindow QToolButton#animationSelectionTimePresetsButton::menu-indicator {
      image: none;
      width: 0;
    }
  )")) + dialog_spinbox_button_style());

  // Top to bottom: how frames play, then the tools that write frame times into layer
  // names. Each group is a grid: the delay label and field on the first row, the options
  // under the field, and the group's buttons at the right of its last row (GitHub issue 198
  // follow-up, after a contributor's mockup).
  auto* playback_group = new QGroupBox(tr("Playback"), this);
  playback_group->setObjectName(QStringLiteral("animationPlaybackGroup"));
  auto* playback_layout = new QGridLayout(playback_group);
  playback_layout->setHorizontalSpacing(8);
  playback_layout->setVerticalSpacing(6);
  playback_layout->setColumnStretch(1, 1);

  auto* delay_label = new QLabel(tr("Default frame delay:"), playback_group);
  delay_label->setObjectName(QStringLiteral("animationFrameDelayLabel"));
  delay_spin_ = new DelaySecondsSpinBox(tr(" s"), playback_group);
  delay_spin_->setObjectName(QStringLiteral("animationFrameDelaySpin"));
  delay_spin_->setToolTip(tr("How long each frame shows when its layer name has no time. Type a value in "
                             "seconds, use the - and + buttons, or pick one from the list. Animated GIF, WebP "
                             "and PNG export use the same default."));
  delay_label->setBuddy(delay_spin_);
  auto* delay_presets = add_delay_presets_button(delay_spin_, QStringLiteral("animationFrameDelayPresetsButton"),
                                                 tr("Frame delay presets"), playback_group);
  // The presets button sits against the field, as its drop-down.
  auto* delay_field = new QHBoxLayout();
  delay_field->setContentsMargins(0, 0, 0, 0);
  delay_field->setSpacing(1);
  delay_field->addWidget(delay_spin_);
  delay_field->addWidget(delay_presets);
  playback_layout->addWidget(delay_label, 0, 0);
  playback_layout->addLayout(delay_field, 0, 1);
  set_default_delay_ms(static_cast<int>(load_image_save_option_defaults().animation_frame_delay_ms), false);

  ignore_name_delays_check_ = new QCheckBox(tr("Ignore delays in layer names"), playback_group);
  ignore_name_delays_check_->setObjectName(QStringLiteral("animationIgnoreNameDelaysCheck"));
  ignore_name_delays_check_->setToolTip(
      tr("Plays every frame for the frame delay above and skips times like \"0.25s\" at the end of "
         "layer names. Preview only: export still uses the times in the names."));
  ignore_name_delays_check_->setChecked(
      app_settings().value(QString::fromLatin1(kIgnoreNameDelaysKey), false).toBool());
  playback_layout->addWidget(ignore_name_delays_check_, 1, 1);

  play_hidden_check_ = new QCheckBox(tr("Play hidden layers"), playback_group);
  play_hidden_check_->setObjectName(QStringLiteral("animationPlayHiddenLayersCheck"));
  play_hidden_check_->setToolTip(
      tr("Includes hidden top-level layers as frames. Preview only: export still uses the visible "
         "layers."));
  play_hidden_check_->setChecked(app_settings().value(QString::fromLatin1(kPlayHiddenLayersKey), false).toBool());
  playback_layout->addWidget(play_hidden_check_, 2, 1);

  // The frame counter reads beside Play while it runs. Each group's button row sits a little
  // further from the checkbox above it than the option rows sit from each other (GitHub issue
  // 198 follow-up: Set Time read as part of Apply to all layers).
  constexpr int kButtonRowExtraGap = 6;
  auto* play_row = new QHBoxLayout();
  play_row->setContentsMargins(0, kButtonRowExtraGap, 0, 0);
  play_row->setSpacing(8);
  status_label_ = new QLabel(playback_group);
  status_label_->setObjectName(QStringLiteral("animationFrameStatusLabel"));
  status_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  play_row->addWidget(status_label_, 1);
  play_button_ = new QPushButton(tr("Play"), playback_group);
  play_button_->setObjectName(QStringLiteral("animationPlayButton"));
  play_row->addWidget(play_button_);
  playback_layout->addLayout(play_row, 3, 0, 1, 2);
  content->addWidget(playback_group);

  auto* names_group = new QGroupBox(tr("Layer names"), this);
  names_group->setObjectName(QStringLiteral("animationFrameTimeGroup"));
  auto* names_layout = new QGridLayout(names_group);
  names_layout->setHorizontalSpacing(8);
  names_layout->setVerticalSpacing(6);
  names_layout->setColumnStretch(1, 1);
  auto* time_label = new QLabel(tr("Frame delay:"), names_group);
  time_label->setObjectName(QStringLiteral("animationSelectionTimeLabel"));
  selection_time_spin_ = new DelaySecondsSpinBox(tr(" s"), names_group);
  selection_time_spin_->setObjectName(QStringLiteral("animationSelectionTimeSpin"));
  selection_time_spin_->setValue(default_delay_ms_ / 1000.0);
  selection_time_spin_->setToolTip(tr("The time Set Time writes at the end of layer names."));
  time_label->setBuddy(selection_time_spin_);
  auto* time_presets = add_delay_presets_button(
      selection_time_spin_, QStringLiteral("animationSelectionTimePresetsButton"), tr("Frame delay presets"),
      names_group);
  auto* time_field = new QHBoxLayout();
  time_field->setContentsMargins(0, 0, 0, 0);
  time_field->setSpacing(1);
  time_field->addWidget(selection_time_spin_);
  time_field->addWidget(time_presets);
  names_layout->addWidget(time_label, 0, 0);
  names_layout->addLayout(time_field, 0, 1);
  all_layers_check_ = new QCheckBox(tr("Apply to all layers"), names_group);
  all_layers_check_->setObjectName(QStringLiteral("animationFrameTimeAllLayersCheck"));
  all_layers_check_->setToolTip(
      tr("Set Time and Remove Time change every top-level layer instead of only the selected ones."));
  all_layers_check_->setChecked(app_settings().value(QString::fromLatin1(kApplyToAllLayersKey), false).toBool());
  names_layout->addWidget(all_layers_check_, 1, 1);
  auto* names_buttons = new QHBoxLayout();
  names_buttons->setContentsMargins(0, kButtonRowExtraGap, 0, 0);
  names_buttons->setSpacing(8);
  names_buttons->addStretch(1);
  auto* set_time_button = new QPushButton(tr("Set Time"), names_group);
  set_time_button->setObjectName(QStringLiteral("animationSetFrameTimeButton"));
  set_time_button->setToolTip(
      tr("Renames the selected layers, or every top-level layer with Apply to all layers, to end with "
         "this frame time, like \"blink 0.25s\"."));
  names_buttons->addWidget(set_time_button);
  auto* remove_time_button = new QPushButton(tr("Remove Time"), names_group);
  remove_time_button->setObjectName(QStringLiteral("animationRemoveFrameTimeButton"));
  remove_time_button->setToolTip(
      tr("Removes the frame time from the end of the selected layers' names, or of every top-level "
         "layer's name with Apply to all layers."));
  names_buttons->addWidget(remove_time_button);
  names_layout->addLayout(names_buttons, 2, 0, 1, 2);
  content->addWidget(names_group);

  auto* hint = new QLabel(
      tr("Frames are the top-level layers, top layer first, as animated GIF, WebP and PNG export "
         "writes them. A layer name ending in a time, like \"blink 0.25s\", sets that frame's delay."),
      this);
  hint->setObjectName(QStringLiteral("animationPreviewHintLabel"));
  hint->setWordWrap(true);
  content->addWidget(hint);

  delay_label_ = delay_label;
  time_label_ = time_label;

  connect(play_button_, &QPushButton::clicked, this, [this] { toggle_playback(); });
  // Both name edits stop playback first: the caller pushes an undo snapshot, and one taken
  // mid-play would capture a preview frame's visibility.
  connect(set_time_button, &QPushButton::clicked, this, [this] {
    stop_playback(true);
    selection_time_spin_->interpretText();
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
  // same key); the next frame advance picks a change up immediately. valueChanged fires on
  // Return, focus leaving, a step button or a preset (no keyboard tracking).
  connect(delay_spin_, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
    set_default_delay_ms(static_cast<int>(std::llround(seconds * 1000.0)), true);
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
  // Return in a delay field only commits the value: a dialog otherwise hands Return to its
  // first auto-default button, which here is Play.
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
    const QSignalBlocker block(delay_spin_);
    delay_spin_->setValue(delay_ms / 1000.0);
  }
  if (persist && changed) {
    app_settings().setValue(QStringLiteral("saveOptions/animationFrameDelayMs"), delay_ms);
    app_settings().setValue(QStringLiteral("saveOptions/gifFrameDelayCs"),
                            std::clamp((delay_ms + 5) / 10, 0, 0xffff));
  }
}

void AnimationPreviewWindow::showEvent(QShowEvent* event) {
  // The two delay labels share one width so both fields start at the same x; measured here,
  // once the labels carry their styled font.
  const int label_width = std::max(delay_label_->sizeHint().width(), time_label_->sizeHint().width());
  delay_label_->setMinimumWidth(label_width);
  time_label_->setMinimumWidth(label_width);
  QDialog::showEvent(event);
}

void AnimationPreviewWindow::done(int result) {
  stop_playback(true);
  QDialog::done(result);
}

}  // namespace patchy::ui
