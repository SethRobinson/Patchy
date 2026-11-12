// Select > Color Range... (GitHub issue 709; docs/color-range.md).
#include "ui/color_range_dialog.hpp"

#include "core/pixel_tools.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"
#include "ui/tool_cursors.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScopeGuard>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The preview is a mask of a copy of the picture no larger than this on its longer side, so a
// change costs the same for any document size.
constexpr int kPreviewExtent = 300;
constexpr int kSampleSquare = 3;
constexpr int kChipSize = 16;
constexpr int kChipGap = 3;
constexpr int kVisibleChips = 9;

enum class PickerMode { Pick, Add, Subtract };

struct ModeEntry {
  ColorRangeMode mode;
  const char* name;
  bool separator_before;
};

constexpr std::array<ModeEntry, kColorRangeModeCount> kModeEntries{{
    {ColorRangeMode::SampledColors, QT_TRANSLATE_NOOP("ColorRangeDialog", "Sampled Colors"), false},
    {ColorRangeMode::Reds, QT_TRANSLATE_NOOP("ColorRangeDialog", "Reds"), true},
    {ColorRangeMode::Yellows, QT_TRANSLATE_NOOP("ColorRangeDialog", "Yellows"), false},
    {ColorRangeMode::Greens, QT_TRANSLATE_NOOP("ColorRangeDialog", "Greens"), false},
    {ColorRangeMode::Cyans, QT_TRANSLATE_NOOP("ColorRangeDialog", "Cyans"), false},
    {ColorRangeMode::Blues, QT_TRANSLATE_NOOP("ColorRangeDialog", "Blues"), false},
    {ColorRangeMode::Magentas, QT_TRANSLATE_NOOP("ColorRangeDialog", "Magentas"), false},
    {ColorRangeMode::Highlights, QT_TRANSLATE_NOOP("ColorRangeDialog", "Highlights"), true},
    {ColorRangeMode::Midtones, QT_TRANSLATE_NOOP("ColorRangeDialog", "Midtones"), false},
    {ColorRangeMode::Shadows, QT_TRANSLATE_NOOP("ColorRangeDialog", "Shadows"), false},
}};

struct ResultEntry {
  SelectionMode mode;
  const char* name;
};

constexpr std::array<ResultEntry, 4> kResultEntries{{
    {SelectionMode::Replace, QT_TRANSLATE_NOOP("ColorRangeDialog", "New Selection")},
    {SelectionMode::Add, QT_TRANSLATE_NOOP("ColorRangeDialog", "Add to Selection")},
    {SelectionMode::Subtract, QT_TRANSLATE_NOOP("ColorRangeDialog", "Subtract from Selection")},
    {SelectionMode::Intersect, QT_TRANSLATE_NOOP("ColorRangeDialog", "Intersect with Selection")},
}};

// The black and white mask of the bounded copy. It only draws the cached image: the mask is
// rebuilt when a setting changes, never in paintEvent.
class ColorRangePreview final : public QWidget {
public:
  explicit ColorRangePreview(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(eyedropper_cursor());
  }

  void set_mask(QImage mask) {
    mask_ = std::move(mask);
    int selected = 0;
    for (int y = 0; y < mask_.height(); ++y) {
      const auto* row = mask_.constScanLine(y);
      selected += static_cast<int>(std::count_if(row, row + mask_.width(), [](std::uint8_t value) { return value != 0U; }));
    }
    // For automation: how many preview pixels are selected at all, and how many rebuilds ran.
    setProperty("selectedPixels", selected);
    setProperty("maskRevision", property("maskRevision").toInt() + 1);
    update();
  }

  void set_source_size(QSize size) { source_size_ = size; }

  void set_click_handler(std::function<void(QPoint, Qt::KeyboardModifiers)> handler) {
    click_handler_ = std::move(handler);
  }

  [[nodiscard]] QSize sizeHint() const override { return {kPreviewExtent + 2, kPreviewExtent + 2}; }
  [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), theme().panel_inset_bg);
    if (!mask_.isNull()) {
      const auto target = image_rect();
      painter.setRenderHint(QPainter::SmoothPixmapTransform, mask_.width() > target.width());
      painter.drawImage(target, mask_);
    }
    painter.setPen(QPen(theme().panel_inset_border, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }

  void mousePressEvent(QMouseEvent* event) override {
    const auto target = image_rect();
    if (event->button() != Qt::LeftButton || !click_handler_ || source_size_.isEmpty() || target.isEmpty() ||
        !target.contains(event->position().toPoint())) {
      QWidget::mousePressEvent(event);
      return;
    }
    const auto u = (event->position().x() - target.x()) / target.width();
    const auto v = (event->position().y() - target.y()) / target.height();
    const QPoint point(std::clamp(static_cast<int>(std::floor(u * source_size_.width())), 0, source_size_.width() - 1),
                       std::clamp(static_cast<int>(std::floor(v * source_size_.height())), 0, source_size_.height() - 1));
    click_handler_(point, event->modifiers());
    event->accept();
  }

private:
  // Where the mask is drawn: fitted inside the frame, centered, aspect kept.
  [[nodiscard]] QRect image_rect() const {
    const auto inner = rect().adjusted(1, 1, -1, -1);
    if (mask_.isNull() || inner.isEmpty()) {
      return {};
    }
    const auto size = mask_.size().scaled(inner.size(), Qt::KeepAspectRatio);
    return QRect(inner.x() + (inner.width() - size.width()) / 2, inner.y() + (inner.height() - size.height()) / 2,
                 size.width(), size.height());
  }

  QImage mask_;
  QSize source_size_;
  std::function<void(QPoint, Qt::KeyboardModifiers)> click_handler_;
};

// One chip per sampled color, newest last; a subtract sample carries a minus bar.
class ColorRangeSampleStrip final : public QWidget {
public:
  explicit ColorRangeSampleStrip(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty("sampleCount", 0);
  }

  void set_samples(const std::vector<ColorRangeSample>& samples) {
    samples_ = samples;
    setProperty("sampleCount", static_cast<int>(samples_.size()));
    update();
  }

  [[nodiscard]] QSize sizeHint() const override {
    return {kVisibleChips * (kChipSize + kChipGap) - kChipGap, kChipSize};
  }
  [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    // The newest chips that fit.
    const auto first = samples_.size() > static_cast<std::size_t>(kVisibleChips)
                           ? samples_.size() - static_cast<std::size_t>(kVisibleChips)
                           : std::size_t{0};
    int x = 0;
    for (auto index = first; index < samples_.size(); ++index) {
      const auto& sample = samples_[index];
      const QRect chip(x, 0, kChipSize, kChipSize);
      painter.fillRect(chip, QColor(sample.red, sample.green, sample.blue));
      painter.setPen(QPen(isEnabled() ? theme().swatch_border : theme().field_border_disabled, 1));
      painter.drawRect(chip.adjusted(0, 0, -1, -1));
      if (sample.subtract) {
        // A light bar inside a dark one reads on any sampled color.
        const auto middle = chip.center().y();
        painter.fillRect(QRect(chip.x() + 3, middle - 1, kChipSize - 6, 4), theme().window_bg);
        painter.fillRect(QRect(chip.x() + 4, middle, kChipSize - 8, 2), theme().text_primary);
      }
      x += kChipSize + kChipGap;
    }
  }

private:
  std::vector<ColorRangeSample> samples_;
};

QImage as_rgba8888(const QImage& image) {
  return image.isNull() || image.format() == QImage::Format_RGBA8888 ? image
                                                                     : image.convertToFormat(QImage::Format_RGBA8888);
}

}  // namespace

QImage color_range_mask_image(const QImage& rgba8888, const ColorRangeOptions& options,
                              const std::function<void()>& tick) {
  const auto source = as_rgba8888(rgba8888);
  if (source.isNull()) {
    return {};
  }
  QImage mask(source.size(), QImage::Format_Grayscale8);
  if (mask.isNull()) {
    return {};
  }
  const ColorRangeClassifier classifier(options);
  const auto width = static_cast<std::size_t>(source.width());
  for (int y = 0; y < source.height(); ++y) {
    classifier.classify_row(source.constScanLine(y), width, mask.scanLine(y));
    if (tick) {
      tick();
    }
  }
  return mask;
}

QRect color_range_mask_bounds(const QImage& mask) {
  if (mask.isNull() || mask.format() != QImage::Format_Grayscale8) {
    return {};
  }
  int left = mask.width();
  int right = -1;
  int top = -1;
  int bottom = -1;
  for (int y = 0; y < mask.height(); ++y) {
    const auto* row = mask.constScanLine(y);
    int first = 0;
    while (first < mask.width() && row[first] == 0U) {
      ++first;
    }
    if (first == mask.width()) {
      continue;
    }
    int last = mask.width() - 1;
    while (row[last] == 0U) {
      --last;
    }
    left = std::min(left, first);
    right = std::max(right, last);
    top = top < 0 ? y : top;
    bottom = y;
  }
  return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

std::optional<ColorRangeSample> color_range_sample_at(const QImage& rgba8888, QPoint point) {
  if (rgba8888.isNull() || rgba8888.format() != QImage::Format_RGBA8888 || !rgba8888.rect().contains(point)) {
    return std::nullopt;
  }
  const auto color = patchy::wand_seed_color(rgba8888.constBits(), rgba8888.width(), rgba8888.height(),
                                             static_cast<std::size_t>(rgba8888.bytesPerLine()), point.x(), point.y(),
                                             kSampleSquare);
  if (color[3] == 0U) {
    return std::nullopt;
  }
  return ColorRangeSample{color[0], color[1], color[2], false};
}

std::optional<ColorRangeSettings> request_color_range_settings(QWidget* parent, const ColorRangeSettings& initial,
                                                               const ColorRangeDialogHooks& hooks) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyColorRangeDialog"));
  dialog.setWindowTitle(QCoreApplication::translate("ColorRangeDialog", "Color Range"));
  auto* root = new QVBoxLayout(&dialog);
  auto* body = new QHBoxLayout();
  root->addLayout(body);

  auto* preview = new ColorRangePreview(&dialog);
  preview->setObjectName(QStringLiteral("colorRangePreview"));
  preview->setToolTip(QCoreApplication::translate(
      "ColorRangeDialog", "White is selected, black is not. Click here or on the image to sample a color."));
  body->addWidget(preview, 0, Qt::AlignTop);

  auto* controls = new QVBoxLayout();
  body->addLayout(controls, 1);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  controls->addLayout(form);

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("colorRangeModeCombo"));
  for (const auto& entry : kModeEntries) {
    if (entry.separator_before) {
      mode_combo->insertSeparator(mode_combo->count());
    }
    mode_combo->addItem(QCoreApplication::translate("ColorRangeDialog", entry.name), static_cast<int>(entry.mode));
  }
  mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(initial.range.mode))));
  fit_combo_to_entries(*mode_combo);
  mode_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  form->addRow(QCoreApplication::translate("ColorRangeDialog", "Select:"), mode_combo);

  auto* fuzziness_spin = add_dialog_slider_spin_row(
      form, &dialog, QCoreApplication::translate("ColorRangeDialog", "Fuzziness:"),
      QStringLiteral("colorRangeFuzzinessSlider"), QStringLiteral("colorRangeFuzzinessSpin"), kMinColorRangeFuzziness,
      kMaxColorRangeFuzziness,
      std::clamp(initial.range.fuzziness, kMinColorRangeFuzziness, kMaxColorRangeFuzziness));

  // What a click on the image does. Shift and Alt pick Add and Subtract for one click.
  auto* picker_row = new QHBoxLayout();
  auto* picker_group = new QButtonGroup(&dialog);
  picker_group->setExclusive(true);
  const auto add_picker_button = [&](PickerMode mode, const QString& text, const QString& object_name,
                                     const QString& tooltip) {
    auto* button = new QPushButton(text, &dialog);
    button->setObjectName(object_name);
    button->setCheckable(true);
    button->setAutoDefault(false);
    button->setToolTip(tooltip);
    picker_group->addButton(button, static_cast<int>(mode));
    picker_row->addWidget(button);
    return button;
  };
  auto* pick_button = add_picker_button(
      PickerMode::Pick, QCoreApplication::translate("ColorRangeDialog", "Pick"), QStringLiteral("colorRangePickButton"),
      QCoreApplication::translate("ColorRangeDialog", "A click starts over from the color under it"));
  add_picker_button(PickerMode::Add, QCoreApplication::translate("ColorRangeDialog", "Add"),
                    QStringLiteral("colorRangeAddButton"),
                    QCoreApplication::translate("ColorRangeDialog", "A click adds the color under it (Shift+click)"));
  add_picker_button(
      PickerMode::Subtract, QCoreApplication::translate("ColorRangeDialog", "Subtract"),
      QStringLiteral("colorRangeSubtractButton"),
      QCoreApplication::translate("ColorRangeDialog", "A click takes the color under it out (Alt+click)"));
  pick_button->setChecked(true);
  picker_row->addStretch(1);
  auto* picker_holder = new QWidget(&dialog);
  picker_row->setContentsMargins(0, 0, 0, 0);
  picker_holder->setLayout(picker_row);
  form->addRow(QCoreApplication::translate("ColorRangeDialog", "Sample:"), picker_holder);

  auto* strip_row = new QHBoxLayout();
  strip_row->setContentsMargins(0, 0, 0, 0);
  auto* strip = new ColorRangeSampleStrip(&dialog);
  strip->setObjectName(QStringLiteral("colorRangeSampleStrip"));
  strip_row->addWidget(strip);
  strip_row->addStretch(1);
  auto* clear_button = new QPushButton(QCoreApplication::translate("ColorRangeDialog", "Clear"), &dialog);
  clear_button->setObjectName(QStringLiteral("colorRangeClearSamplesButton"));
  clear_button->setAutoDefault(false);
  strip_row->addWidget(clear_button);
  auto* strip_holder = new QWidget(&dialog);
  strip_holder->setLayout(strip_row);
  form->addRow(QCoreApplication::translate("ColorRangeDialog", "Colors:"), strip_holder);

  auto* result_combo = new QComboBox(&dialog);
  result_combo->setObjectName(QStringLiteral("colorRangeResultCombo"));
  for (const auto& entry : kResultEntries) {
    result_combo->addItem(QCoreApplication::translate("ColorRangeDialog", entry.name), static_cast<int>(entry.mode));
  }
  result_combo->setCurrentIndex(std::max(0, result_combo->findData(static_cast<int>(initial.result))));
  fit_combo_to_entries(*result_combo);
  result_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  form->addRow(QCoreApplication::translate("ColorRangeDialog", "Result:"), result_combo);

  auto* sample_all_check =
      new QCheckBox(QCoreApplication::translate("ColorRangeDialog", "Sample All Layers"), &dialog);
  sample_all_check->setObjectName(QStringLiteral("colorRangeSampleAllLayersCheck"));
  sample_all_check->setChecked(initial.sample_all_layers || !hooks.layer_source_available);
  sample_all_check->setEnabled(hooks.layer_source_available);
  form->addRow(QString(), sample_all_check);
  auto* invert_check = new QCheckBox(QCoreApplication::translate("ColorRangeDialog", "Invert"), &dialog);
  invert_check->setObjectName(QStringLiteral("colorRangeInvertCheck"));
  invert_check->setChecked(initial.range.invert);
  form->addRow(QString(), invert_check);

  auto* hint = new QLabel(QCoreApplication::translate(
                              "ColorRangeDialog",
                              "Click the image or the preview to sample a color. Shift adds a color, Alt takes one out."),
                          &dialog);
  hint->setObjectName(QStringLiteral("colorRangeHintLabel"));
  hint->setWordWrap(true);
  controls->addWidget(hint);
  controls->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  match_ok_cancel_widths(*buttons);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  // The dialog's state: the samples, the picture they come from and its bounded copy.
  auto samples = initial.range.samples;
  if (samples.size() > kMaxColorRangeSamples) {
    samples.erase(samples.begin(), samples.end() - static_cast<std::ptrdiff_t>(kMaxColorRangeSamples));
  }
  QImage source;
  QImage proxy;

  const auto current_mode = [mode_combo] { return static_cast<ColorRangeMode>(mode_combo->currentData().toInt()); };
  const auto current_options = [&] {
    ColorRangeOptions options;
    options.mode = current_mode();
    options.fuzziness = fuzziness_spin->value();
    options.samples = samples;
    options.invert = invert_check->isChecked();
    return options;
  };
  // One mask of the bounded copy per change; the preview widget only draws it.
  const auto refresh_preview = [&] {
    preview->set_mask(proxy.isNull() ? QImage() : color_range_mask_image(proxy, current_options()));
  };
  const auto reload_source = [&] {
    source = hooks.source_image ? as_rgba8888(hooks.source_image(sample_all_check->isChecked())) : QImage();
    proxy = source;
    if (!source.isNull() && std::max(source.width(), source.height()) > kPreviewExtent) {
      proxy = source.scaled(QSize(kPreviewExtent, kPreviewExtent), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_RGBA8888);
    }
    preview->set_source_size(source.size());
    refresh_preview();
  };
  const auto take_sample = [&](QPoint point, Qt::KeyboardModifiers modifiers) {
    if (current_mode() != ColorRangeMode::SampledColors) {
      return;
    }
    auto sample = color_range_sample_at(source, point);
    if (!sample.has_value()) {
      return;
    }
    auto picker = static_cast<PickerMode>(picker_group->checkedId());
    if ((modifiers & Qt::AltModifier) != 0) {
      picker = PickerMode::Subtract;
    } else if ((modifiers & Qt::ShiftModifier) != 0) {
      picker = PickerMode::Add;
    }
    if (picker == PickerMode::Pick) {
      samples.clear();
    }
    sample->subtract = picker == PickerMode::Subtract;
    if (std::find(samples.begin(), samples.end(), *sample) == samples.end()) {
      if (samples.size() >= kMaxColorRangeSamples) {
        samples.erase(samples.begin());
      }
      samples.push_back(*sample);
    }
    strip->set_samples(samples);
    refresh_preview();
  };
  // Sampling belongs to Sampled Colors: the other ranges take nothing from a click.
  const auto apply_mode = [&] {
    const bool sampled = current_mode() == ColorRangeMode::SampledColors;
    for (auto* widget : {static_cast<QWidget*>(picker_holder), static_cast<QWidget*>(strip_holder),
                         static_cast<QWidget*>(hint)}) {
      widget->setEnabled(sampled);
    }
    for (auto* holder : {picker_holder, strip_holder}) {
      if (auto* label = form->labelForField(holder); label != nullptr) {
        label->setEnabled(sampled);
      }
    }
    if (hooks.set_canvas_picker) {
      hooks.set_canvas_picker(sampled ? std::function<void(QPoint, Qt::KeyboardModifiers)>(take_sample)
                                      : std::function<void(QPoint, Qt::KeyboardModifiers)>(),
                              [&dialog] { dialog.reject(); });
    }
  };
  // Declared after the dialog and its state: the canvas stops calling take_sample before
  // either is destroyed, on every way out of this function.
  const auto disarm_canvas = qScopeGuard([&hooks] {
    if (hooks.set_canvas_picker) {
      hooks.set_canvas_picker({}, {});
    }
  });

  preview->set_click_handler(take_sample);
  QObject::connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    apply_mode();
    refresh_preview();
  });
  QObject::connect(fuzziness_spin, &QSpinBox::valueChanged, &dialog, [&](int) { refresh_preview(); });
  QObject::connect(invert_check, &QCheckBox::toggled, &dialog, [&](bool) { refresh_preview(); });
  QObject::connect(sample_all_check, &QCheckBox::toggled, &dialog, [&](bool) { reload_source(); });
  QObject::connect(clear_button, &QPushButton::clicked, &dialog, [&] {
    samples.clear();
    strip->set_samples(samples);
    refresh_preview();
  });

  strip->set_samples(samples);
  apply_mode();
  reload_source();
  mode_combo->setFocus();
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  ColorRangeSettings chosen;
  chosen.range = current_options();
  chosen.result = static_cast<SelectionMode>(result_combo->currentData().toInt());
  chosen.sample_all_layers = sample_all_check->isChecked();
  return chosen;
}

}  // namespace patchy::ui
