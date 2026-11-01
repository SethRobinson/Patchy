// Patchy's settings dialog for a GIMP 3 plug-in (gimp_plugin_settings_dialog.hpp).

#include "ui/gimp_plugin_settings_dialog.hpp"

#include "plugins/gimp/gimp_procedure.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/filter_parameter_panel.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cmath>

namespace patchy::ui {

namespace {

// Object names for tests and themes: "gimpArg" plus the argument's name without dashes.
QString control_name(const QString& key) {
  QString name = QStringLiteral("gimpArg");
  bool upper = true;
  for (const auto c : key) {
    if (c == QLatin1Char('-') || c == QLatin1Char('_')) {
      upper = true;
      continue;
    }
    name += upper ? c.toUpper() : c;
    upper = false;
  }
  return name;
}

// A slider step that gives about a thousand positions over the practical range.
double double_step(double minimum, double maximum) {
  const double span = std::max(1e-9, maximum - minimum);
  double step = std::pow(10.0, std::floor(std::log10(span)) - 2.0);
  return std::clamp(step, 0.0001, 1.0);
}

}  // namespace

GimpPluginSettingsDialog::GimpPluginSettingsDialog(const QString& plugin_name, const QString& description,
                                                   GimpPluginArgumentSet arguments, GimpPluginValues values,
                                                   bool preview_on, QWidget* parent)
    : QDialog(parent), arguments_(std::move(arguments)), values_(std::move(values)) {
  setObjectName(QStringLiteral("gimpPluginSettingsDialog"));
  //: %1 is the GIMP plug-in's name ("Blinds").
  setWindowTitle(tr("%1 (GIMP 3 Plug-in)").arg(plugin_name));
  defaults_ = gimp_plugin_default_values(arguments_);
  auto* layout = new QVBoxLayout(this);
  if (!description.trimmed().isEmpty()) {
    auto* about = new QLabel(description.trimmed(), this);
    about->setObjectName(QStringLiteral("gimpPluginSettingsDescription"));
    about->setWordWrap(true);
    layout->addWidget(about);
  }

  // Every row shows at once; only a plug-in with very many settings scrolls.
  const bool scrolls = arguments_.editable.size() > 16;
  QScrollArea* scroll = nullptr;
  auto* rows = new QWidget(this);
  if (scrolls) {
    scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("gimpPluginSettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rows->setParent(scroll);
  }
  auto* rows_layout = new QVBoxLayout(rows);
  rows_layout->setContentsMargins(0, 0, 0, 0);
  panel_ = new FilterParameterPanel(rows);
  panel_->setObjectName(QStringLiteral("gimpPluginSettingsPanel"));
  rows_layout->addWidget(panel_);
  extra_rows_ = new QWidget(rows);
  extra_rows_->setObjectName(QStringLiteral("gimpPluginSettingsExtraRows"));
  rows_layout->addWidget(extra_rows_);
  rows_layout->addStretch(1);
  build_rows();
  if (scroll != nullptr) {
    scroll->setWidget(rows);
    scroll->setMinimumHeight(520);
    rows->adjustSize();
    scroll->setMinimumWidth(rows->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 8);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll, 1);
  } else {
    layout->addWidget(rows, 1);
  }

  auto* preview_row = new QHBoxLayout();
  preview_check_ = new QCheckBox(tr("Preview"), this);
  preview_check_->setObjectName(QStringLiteral("gimpPluginPreviewCheck"));
  preview_check_->setChecked(preview_on);
  preview_check_->setToolTip(tr("Shows the result on the canvas while you change the settings"));
  preview_row->addWidget(preview_check_);
  preview_busy_ = new QProgressBar(this);
  preview_busy_->setObjectName(QStringLiteral("gimpPluginPreviewBusy"));
  preview_busy_->setRange(0, 0);
  preview_busy_->setTextVisible(false);
  preview_busy_->setMaximumWidth(80);
  preview_busy_->setVisible(false);
  preview_row->addWidget(preview_busy_);
  preview_status_ = new QLabel(this);
  preview_status_->setObjectName(QStringLiteral("gimpPluginPreviewStatus"));
  preview_row->addWidget(preview_status_, 1);
  own_dialog_button_ = new QPushButton(tr("Use the Plug-in's Own Dialog"), this);
  own_dialog_button_->setObjectName(QStringLiteral("gimpPluginOwnDialogButton"));
  own_dialog_button_->setAutoDefault(false);
  own_dialog_button_->setToolTip(tr("Opens the plug-in's own window instead, and keeps using it for this plug-in"));
  connect(own_dialog_button_, &QPushButton::clicked, this, [this] {
    if (own_dialog_) own_dialog_();
  });
  preview_row->addWidget(own_dialog_button_);
  layout->addLayout(preview_row);
  connect(preview_check_, &QCheckBox::toggled, this, [this](bool on) {
    if (preview_toggled_) preview_toggled_(on);
  });

  error_label_ = new QLabel(this);
  error_label_->setObjectName(QStringLiteral("gimpPluginPreviewError"));
  error_label_->setWordWrap(true);
  error_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  error_label_->setVisible(false);
  layout->addWidget(error_label_);

  auto* buttons = new QDialogButtonBox(this);
  reset_button_ = buttons->addButton(QDialogButtonBox::Reset);
  reset_button_->setObjectName(QStringLiteral("gimpPluginResetButton"));
  connect(reset_button_, &QPushButton::clicked, this, [this] { reset_values(); });
  ok_button_ = buttons->addButton(QDialogButtonBox::Ok);
  ok_button_->setObjectName(QStringLiteral("gimpPluginOkButton"));
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName(QStringLiteral("gimpPluginCancelButton"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  remember_dialog_position(*this);
  resize(std::max(sizeHint().width(), 460), sizeHint().height());
  set_preview_state(preview_on ? PreviewState::Updating : PreviewState::Off);
}

bool GimpPluginSettingsDialog::preview_enabled() const { return preview_check_ != nullptr && preview_check_->isChecked(); }

void GimpPluginSettingsDialog::build_rows() {
  FilterDialogSpec spec;
  spec.identifier = QStringLiteral("gimp");
  FilterInvocation invocation;
  auto* form = new QFormLayout(extra_rows_);
  form->setContentsMargins(0, 0, 0, 0);
  for (const auto& a : arguments_.editable) {
    const auto key = a.key.toStdString();
    const auto current = values_.count(a.key) != 0 ? values_.at(a.key) : QVariant();
    const auto& def = a.def;
    switch (a.widget) {
      case GimpArgumentWidget::Integer: {
        FilterControlSpec control;
        control.label = a.label;
        control.object_name = control_name(a.key);
        control.parameter_key = key;
        control.kind = FilterParameterKind::Integer;
        const auto lo = static_cast<int>(std::clamp<std::int64_t>(def.int_min, INT_MIN, INT_MAX));
        const auto hi = static_cast<int>(std::clamp<std::int64_t>(def.int_max, INT_MIN, INT_MAX));
        // The slider covers a practical span; the spin box takes the whole range.
        control.minimum = std::max(lo, -10000);
        control.maximum = std::max(control.minimum, std::min(hi, 10000));
        control.typed_minimum = lo;
        control.typed_maximum = hi;
        control.value = current.toInt();
        control.default_value = std::int64_t{defaults_.at(a.key).toInt()};
        if (a.key.contains(QStringLiteral("seed"))) {
          control.presentation = FilterParameterPresentation::RandomSeed;
        }
        invocation.parameters[key] = std::int64_t{current.toInt()};
        spec.controls.push_back(std::move(control));
        break;
      }
      case GimpArgumentWidget::Double: {
        FilterControlSpec control;
        control.label = a.label;
        control.object_name = control_name(a.key);
        control.parameter_key = key;
        control.kind = FilterParameterKind::Double;
        const double lo = std::max(def.double_min, -1e6);
        const double hi = std::min(def.double_max, 1e6);
        control.minimum = static_cast<int>(std::floor(std::max(lo, -10000.0)));
        control.maximum = static_cast<int>(std::ceil(std::min(hi, 10000.0)));
        control.typed_minimum = lo;
        control.typed_maximum = hi;
        control.step = double_step(std::max(lo, -10000.0), std::min(hi, 10000.0));
        control.default_value = defaults_.at(a.key).toDouble();
        invocation.parameters[key] = current.toDouble();
        spec.controls.push_back(std::move(control));
        break;
      }
      case GimpArgumentWidget::Boolean: {
        FilterControlSpec control;
        control.label = a.label;
        control.object_name = control_name(a.key);
        control.parameter_key = key;
        control.kind = FilterParameterKind::Boolean;
        control.default_value = defaults_.at(a.key).toBool();
        invocation.parameters[key] = current.toBool();
        spec.controls.push_back(std::move(control));
        break;
      }
      case GimpArgumentWidget::Choice: {
        FilterControlSpec control;
        control.label = a.label;
        control.object_name = control_name(a.key);
        control.parameter_key = key;
        control.kind = FilterParameterKind::Option;
        for (const auto& c : def.choices) {
          const auto nick = c.nick.value_or("");
          // The plug-in's own label for the choice (data, in its language).
          const auto label = gimp::strip_mnemonic(c.label.value_or(nick));
          control.options.push_back({nick, label.empty() ? nick : label});
        }
        control.default_value = defaults_.at(a.key).toString().toStdString();
        invocation.parameters[key] = current.toString().toStdString();
        spec.controls.push_back(std::move(control));
        break;
      }
      case GimpArgumentWidget::Color: {
        auto* swatch = new QPushButton(extra_rows_);
        swatch->setObjectName(control_name(a.key) + QStringLiteral("Button"));
        swatch->setAccessibleName(a.label);
        swatch->setToolTip(a.help);
        const auto key_copy = a.key;
        const auto label = a.label;
        const auto show = [this, swatch, key_copy] {
          set_themed_style(*swatch, color_button_style(values_[key_copy].value<QColor>()));
        };
        show();
        sync_extra_rows_.push_back(show);
        connect(swatch, &QPushButton::clicked, this, [this, swatch, key_copy, label, show] {
          const QPointer<QPushButton> alive(swatch);
          const auto original = values_[key_copy].value<QColor>();
          const auto apply = [this, alive, key_copy, show](QColor picked) {
            if (alive == nullptr) {
              return;
            }
            values_[key_copy] = picked;
            show();
            value_changed();
          };
          const auto picked = request_patchy_color(this, original, label, apply);
          apply(picked.value_or(original));
        });
        form->addRow(a.label, swatch);
        break;
      }
      case GimpArgumentWidget::Text: {
        auto* edit = new QLineEdit(current.toString(), extra_rows_);
        edit->setObjectName(control_name(a.key) + QStringLiteral("Edit"));
        edit->setToolTip(a.help);
        const auto key_copy = a.key;
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key_copy] {
          if (values_[key_copy].toString() != edit->text()) {
            values_[key_copy] = edit->text();
            value_changed();
          }
        });
        sync_extra_rows_.push_back([this, edit, key_copy] { edit->setText(values_[key_copy].toString()); });
        form->addRow(a.label, edit);
        break;
      }
      case GimpArgumentWidget::Layer: {
        auto* combo = new QComboBox(extra_rows_);
        combo->setObjectName(control_name(a.key) + QStringLiteral("Combo"));
        combo->setToolTip(a.help);
        combo->addItem(tr("This layer"), QStringLiteral("layer"));
        combo->addItem(tr("None"), QStringLiteral("none"));
        combo->setCurrentIndex(std::max(0, combo->findData(current.toString())));
        const auto key_copy = a.key;
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo, key_copy](int) {
          values_[key_copy] = combo->currentData().toString();
          value_changed();
        });
        sync_extra_rows_.push_back([this, combo, key_copy] {
          const QSignalBlocker blocker(combo);
          combo->setCurrentIndex(std::max(0, combo->findData(values_[key_copy].toString())));
        });
        form->addRow(a.label, combo);
        break;
      }
    }
  }
  if (spec.controls.empty() && sync_extra_rows_.empty()) {
    panel_->clear(tr("This plug-in has no settings."));
  } else {
    FilterParameterPanelOptions options;
    options.two_columns_from = kFilterDialogTwoColumnControls;
    panel_->rebuild(spec, invocation, options);
    panel_->set_values_changed_callback([this](const FilterParameterPanel::ValueChanges& changes) {
      for (const auto& [key, value] : changes) {
        const auto name = QString::fromStdString(key);
        if (const auto* i = std::get_if<std::int64_t>(&value)) {
          values_[name] = static_cast<int>(*i);
        } else if (const auto* d = std::get_if<double>(&value)) {
          values_[name] = *d;
        } else if (const auto* b = std::get_if<bool>(&value)) {
          values_[name] = *b;
        } else if (const auto* s = std::get_if<std::string>(&value)) {
          values_[name] = QString::fromStdString(*s);
        }
      }
      value_changed();
    });
  }
  extra_rows_->setVisible(!sync_extra_rows_.empty());
  // Help texts from the plug-in on the generated rows.
  for (const auto& a : arguments_.editable) {
    for (auto* child : panel_->findChildren<QWidget*>()) {
      if (child->objectName().startsWith(control_name(a.key)) && child->toolTip().isEmpty()) {
        child->setToolTip(a.help);
      }
    }
  }
}

void GimpPluginSettingsDialog::value_changed() {
  if (values_changed_) values_changed_();
}

void GimpPluginSettingsDialog::reset_values() {
  values_ = defaults_;
  panel_->reset_to_defaults();
  for (const auto& sync : sync_extra_rows_) {
    sync();
  }
  value_changed();
}

void GimpPluginSettingsDialog::set_preview_state(PreviewState state, const QString& detail) {
  preview_busy_->setVisible(state == PreviewState::Updating);
  error_label_->setVisible(state == PreviewState::Failed && !detail.isEmpty());
  switch (state) {
    case PreviewState::Off:
      preview_status_->setText(QString());
      break;
    case PreviewState::Updating:
      preview_status_->setText(tr("Updating preview..."));
      break;
    case PreviewState::Quick:
      //: The canvas shows a quick, lower-resolution preview while the full one is made.
      preview_status_->setText(tr("Quick preview; refining..."));
      break;
    case PreviewState::Ready:
      preview_status_->setText(QString());
      break;
    case PreviewState::Failed:
      preview_status_->setText(tr("The preview failed."));
      error_label_->setText(detail);
      break;
  }
}

void GimpPluginSettingsDialog::set_applying(bool applying) {
  applying_ = applying;
  panel_->setEnabled(!applying);
  extra_rows_->setEnabled(!applying);
  preview_check_->setEnabled(!applying);
  own_dialog_button_->setEnabled(!applying);
  reset_button_->setEnabled(!applying);
  ok_button_->setEnabled(!applying);
  if (applying) {
    preview_busy_->setVisible(true);
    preview_status_->setText(tr("Applying..."));
  }
}

void GimpPluginSettingsDialog::finish_accept() {
  closing_ = true;
  QDialog::done(QDialog::Accepted);
}

void GimpPluginSettingsDialog::done(int result) {
  if (result == QDialog::Accepted && !closing_ && accept_ && !accept_()) {
    return;  // the host finishes the result first, then calls finish_accept()
  }
  closing_ = true;
  QDialog::done(result);
}

}  // namespace patchy::ui
