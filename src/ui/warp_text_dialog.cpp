#include "ui/warp_text_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

namespace patchy::ui {

namespace {

struct StyleEntry {
  const char* label;
  const char* token;
};

// Photoshop's Warp Text style list, in its menu order and grouping.
constexpr std::array<StyleEntry, 15> kWarpStyles = {{
    {QT_TRANSLATE_NOOP("QObject", "Arc"), "warpArc"},
    {QT_TRANSLATE_NOOP("QObject", "Arc Lower"), "warpArcLower"},
    {QT_TRANSLATE_NOOP("QObject", "Arc Upper"), "warpArcUpper"},
    {QT_TRANSLATE_NOOP("QObject", "Arch"), "warpArch"},
    {QT_TRANSLATE_NOOP("QObject", "Bulge"), "warpBulge"},
    {QT_TRANSLATE_NOOP("QObject", "Shell Lower"), "warpShellLower"},
    {QT_TRANSLATE_NOOP("QObject", "Shell Upper"), "warpShellUpper"},
    {QT_TRANSLATE_NOOP("QObject", "Flag"), "warpFlag"},
    {QT_TRANSLATE_NOOP("QObject", "Wave"), "warpWave"},
    {QT_TRANSLATE_NOOP("QObject", "Fish"), "warpFish"},
    {QT_TRANSLATE_NOOP("QObject", "Rise"), "warpRise"},
    {QT_TRANSLATE_NOOP("QObject", "Fisheye"), "warpFisheye"},
    {QT_TRANSLATE_NOOP("QObject", "Inflate"), "warpInflate"},
    {QT_TRANSLATE_NOOP("QObject", "Squeeze"), "warpSqueeze"},
    {QT_TRANSLATE_NOOP("QObject", "Twist"), "warpTwist"},
}};

}  // namespace

std::optional<TextWarp> request_text_warp(QWidget* parent, const TextWarp& initial,
                                          const std::function<void(const TextWarp&)>& preview) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("warpTextDialog"));
  dialog.setWindowTitle(QObject::tr("Warp Text"));

  auto* layout = new QVBoxLayout(&dialog);

  auto* form = new QFormLayout();
  auto* style_combo = new QComboBox(&dialog);
  style_combo->setObjectName(QStringLiteral("warpTextStyleCombo"));
  style_combo->addItem(QObject::tr("None"), QStringLiteral("warpNone"));
  style_combo->insertSeparator(style_combo->count());
  int index = 0;
  for (const auto& entry : kWarpStyles) {
    style_combo->addItem(QObject::tr(entry.label), QLatin1String(entry.token));
    // Photoshop groups arc-like, banner-like, and lens-like styles.
    if (index == 6 || index == 10) {
      style_combo->insertSeparator(style_combo->count());
    }
    ++index;
  }
  form->addRow(QObject::tr("Style:"), style_combo);

  auto* direction_row = new QHBoxLayout();
  auto* horizontal_radio = new QRadioButton(QObject::tr("Horizontal"), &dialog);
  horizontal_radio->setObjectName(QStringLiteral("warpTextHorizontalRadio"));
  auto* vertical_radio = new QRadioButton(QObject::tr("Vertical"), &dialog);
  vertical_radio->setObjectName(QStringLiteral("warpTextVerticalRadio"));
  direction_row->addWidget(horizontal_radio);
  direction_row->addWidget(vertical_radio);
  direction_row->addStretch(1);
  form->addRow(QString(), direction_row);
  layout->addLayout(form);

  struct SliderRow {
    QSlider* slider{nullptr};
    QSpinBox* spin{nullptr};
  };
  const auto make_slider_row = [&dialog, layout](const QString& label, const char* slider_name,
                                                 const char* spin_name) {
    auto* caption = new QLabel(label, &dialog);
    layout->addWidget(caption);
    auto* row = new QHBoxLayout();
    auto* slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setObjectName(QLatin1String(slider_name));
    slider->setRange(-100, 100);
    slider->setValue(0);
    auto* spin = new QSpinBox(&dialog);
    spin->setObjectName(QLatin1String(spin_name));
    spin->setRange(-100, 100);
    spin->setSuffix(QStringLiteral(" %"));
    row->addWidget(slider, 1);
    row->addWidget(spin);
    layout->addLayout(row);
    return SliderRow{slider, spin};
  };
  const auto bend = make_slider_row(QObject::tr("Bend:"), "warpTextBendSlider", "warpTextBendSpin");
  const auto horizontal_distort = make_slider_row(QObject::tr("Horizontal Distortion:"),
                                                  "warpTextHorizontalDistortionSlider",
                                                  "warpTextHorizontalDistortionSpin");
  const auto vertical_distort = make_slider_row(QObject::tr("Vertical Distortion:"),
                                                "warpTextVerticalDistortionSlider",
                                                "warpTextVerticalDistortionSpin");

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  // Populate from the initial warp.
  const auto initial_index = style_combo->findData(QString::fromStdString(initial.style));
  style_combo->setCurrentIndex(initial_index >= 0 ? initial_index : 0);
  (initial.rotate == "Vrtc" ? vertical_radio : horizontal_radio)->setChecked(true);
  bend.spin->setValue(static_cast<int>(std::lround(initial.value)));
  bend.slider->setValue(bend.spin->value());
  horizontal_distort.spin->setValue(static_cast<int>(std::lround(initial.perspective)));
  horizontal_distort.slider->setValue(horizontal_distort.spin->value());
  vertical_distort.spin->setValue(static_cast<int>(std::lround(initial.perspective_other)));
  vertical_distort.slider->setValue(vertical_distort.spin->value());

  const auto current_warp = [&]() {
    TextWarp warp;
    const auto token = style_combo->currentData().toString();
    warp.style = token.isEmpty() ? std::string("warpNone") : token.toStdString();
    warp.rotate = vertical_radio->isChecked() ? "Vrtc" : "Hrzn";
    warp.value = bend.spin->value();
    warp.perspective = horizontal_distort.spin->value();
    warp.perspective_other = vertical_distort.spin->value();
    // The reference box is re-derived from the live text layout by the caller.
    return warp;
  };

  const auto sync_enabled = [&]() {
    const bool active = style_combo->currentData().toString() != QLatin1String("warpNone");
    horizontal_radio->setEnabled(active);
    vertical_radio->setEnabled(active);
    bend.slider->setEnabled(active);
    bend.spin->setEnabled(active);
    horizontal_distort.slider->setEnabled(active);
    horizontal_distort.spin->setEnabled(active);
    vertical_distort.slider->setEnabled(active);
    vertical_distort.spin->setEnabled(active);
  };

  bool updating = false;
  const auto notify = [&]() {
    if (updating) {
      return;
    }
    sync_enabled();
    if (preview) {
      preview(current_warp());
    }
  };
  const auto link_pair = [&](const SliderRow& pair) {
    QObject::connect(pair.slider, &QSlider::valueChanged, &dialog, [&, pair](int value) {
      if (pair.spin->value() != value) {
        updating = true;
        pair.spin->setValue(value);
        updating = false;
      }
      notify();
    });
    QObject::connect(pair.spin, &QSpinBox::valueChanged, &dialog, [&, pair](int value) {
      if (pair.slider->value() != value) {
        updating = true;
        pair.slider->setValue(value);
        updating = false;
      }
      notify();
    });
  };
  link_pair(bend);
  link_pair(horizontal_distort);
  link_pair(vertical_distort);
  QObject::connect(style_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { notify(); });
  QObject::connect(horizontal_radio, &QRadioButton::toggled, &dialog, [&](bool) { notify(); });

  sync_enabled();
  if (preview) {
    preview(current_warp());
  }

  // Keep readable - / + buttons on the spin boxes (sub-control gotcha: apply the
  // style after all children exist, with unprefixed selectors; see dialog_utils).
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  dialog.adjustSize();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return current_warp();
}

}  // namespace patchy::ui
