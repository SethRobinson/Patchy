#include "ui/gmic_parameter_panel.hpp"

#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <memory>

namespace patchy::ui {

namespace {

// One spin width for every slider row, so the sliders line up down the form.
constexpr int kSliderSpinWidth = 116;

[[nodiscard]] QString from_gmic(const std::string& text) {
  return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] std::string to_gmic(const QString& text) {
  const auto bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// "1.50" -> "1.5", "2.00" -> "2": the shortest text for a value at `decimals`.
[[nodiscard]] std::string number_text(double value, int decimals) {
  QString text = QString::number(value, 'f', decimals);
  if (text.contains(QLatin1Char('.'))) {
    while (text.endsWith(QLatin1Char('0'))) {
      text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
    }
  }
  if (text == QLatin1String("-0")) {
    text = QStringLiteral("0");
  }
  return to_gmic(text);
}

// A G'MIC text value ("quoted \"text\"") back to its content.
[[nodiscard]] QString unquoted_text(const std::string& value) {
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return from_gmic(value);
  }
  std::string out;
  for (std::size_t index = 1; index + 1 < value.size(); ++index) {
    if (value[index] == '\\' && index + 2 < value.size()) {
      ++index;
    }
    out.push_back(value[index]);
  }
  return from_gmic(out);
}

[[nodiscard]] QStringList value_parts(const std::string& value) {
  return from_gmic(value).split(QLatin1Char(','));
}

}  // namespace

QString gmic_note_rich_text(const std::string& note) {
  QString text = from_gmic(note);
  text.replace(QStringLiteral("\\n"), QStringLiteral("<br>"));
  // Pango's <span color="..."> / foreground="..." is CSS color in Qt rich text.
  static const QRegularExpression span_color(
      QStringLiteral("<span\\s+(?:color|foreground)\\s*=\\s*\"([^\"]*)\"\\s*>"));
  text.replace(span_color, QStringLiteral("<span style=\"color:\\1\">"));
  return text;
}

GmicParameterPanel::GmicParameterPanel(QWidget* parent) : QWidget(parent) {
  form_ = new QFormLayout(this);
  form_->setContentsMargins(0, 0, 0, 0);
  form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
}

void GmicParameterPanel::set_changed_callback(std::function<void(bool)> callback) {
  changed_callback_ = std::move(callback);
}

void GmicParameterPanel::changed(bool updates_preview) {
  if (!updating_ && changed_callback_) {
    changed_callback_(updates_preview);
  }
}

void GmicParameterPanel::set_points_changed_callback(std::function<void()> callback) {
  points_changed_callback_ = std::move(callback);
}

void GmicParameterPanel::points_changed() {
  if (points_changed_callback_) {
    points_changed_callback_();
  }
}

std::vector<QPointF> GmicParameterPanel::points() const {
  std::vector<QPointF> points;
  points.reserve(point_controls_.size());
  for (const auto& control : point_controls_) {
    points.emplace_back(control.x->value(), control.y->value());
  }
  return points;
}

void GmicParameterPanel::set_point(int index, QPointF percent) {
  if (index < 0 || index >= static_cast<int>(point_controls_.size())) {
    return;
  }
  const auto& control = point_controls_[static_cast<std::size_t>(index)];
  // One change report for the pair, not one per axis.
  const double old_x = control.x->value();
  const double old_y = control.y->value();
  {
    const QSignalBlocker block_x(control.x);
    control.x->setValue(percent.x());
  }
  control.y->setValue(percent.y());
  if (control.y->value() == old_y && control.x->value() != old_x) {
    // The Y field did not change (a horizontal move): report the X change by hand.
    emit control.x->valueChanged(control.x->value());
  }
}

void GmicParameterPanel::clear() {
  bindings_.clear();
  point_controls_.clear();
  while (form_->rowCount() > 0) {
    form_->removeRow(0);
  }
}

void GmicParameterPanel::set_filter(const gmic::Filter& filter, const std::vector<std::string>& values) {
  clear();
  int index = 0;
  for (const auto& parameter : filter.parameters) {
    const QString object_stem = QStringLiteral("gmicParameter%1").arg(index++);
    const QString label = from_gmic(parameter.name);
    QWidget* field = nullptr;   // the row's field, or the whole row for layout-only types
    bool spans_row = false;
    Binding binding;
    binding.default_value = parameter.default_value;
    const bool live = parameter.updates_preview;

    switch (parameter.type) {
      case gmic::ParameterType::Note: {
        auto* note = new QLabel(this);
        note->setTextFormat(Qt::RichText);
        note->setWordWrap(true);
        note->setText(gmic_note_rich_text(parameter.text));
        note->setTextInteractionFlags(Qt::TextBrowserInteraction);
        note->setOpenExternalLinks(true);
        field = note;
        spans_row = true;
        break;
      }
      case gmic::ParameterType::Link: {
        auto* link = new QLabel(this);
        link->setTextFormat(Qt::RichText);
        link->setOpenExternalLinks(true);
        set_themed_label_text(*link, QStringLiteral("<a style=\"color:@accent\" href=\"%1\">%2</a>")
                                         .arg(from_gmic(parameter.url).toHtmlEscaped(),
                                              gmic_note_rich_text(parameter.text)));
        field = link;
        spans_row = true;
        break;
      }
      case gmic::ParameterType::Separator: {
        auto* line = new QFrame(this);
        line->setFixedHeight(1);
        set_themed_style(*line, QStringLiteral("background: @option_separator; border: none;"));
        field = line;
        spans_row = true;
        break;
      }
      case gmic::ParameterType::Bool: {
        auto* check = new QCheckBox(this);
        connect(check, &QCheckBox::toggled, this, [this, live] { changed(live); });
        binding.read = [check] { return std::string(check->isChecked() ? "1" : "0"); };
        binding.write = [check](const std::string& value) { check->setChecked(value == "1" || value == "true"); };
        field = check;
        break;
      }
      case gmic::ParameterType::Button: {
        // A button is a one-shot flag: 1 on the run after it was pressed.
        auto* button = new QPushButton(label, this);
        auto pressed = std::make_shared<bool>(false);
        connect(button, &QPushButton::clicked, this, [this, live, pressed] {
          *pressed = true;
          changed(live);
        });
        binding.read = [pressed] { return std::string(*pressed ? "1" : "0"); };
        binding.write = [pressed](const std::string& value) { *pressed = value == "1"; };
        binding.button = true;
        field = button;
        break;
      }
      case gmic::ParameterType::Choice: {
        auto* combo = new QComboBox(this);
        for (const auto& choice : parameter.choices) {
          combo->addItem(from_gmic(choice));
        }
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, live](int) { changed(live); });
        binding.read = [combo] { return std::to_string(std::max(0, combo->currentIndex())); };
        binding.write = [combo](const std::string& value) {
          const int wanted = from_gmic(value).toInt();
          combo->setCurrentIndex(wanted >= 0 && wanted < combo->count() ? wanted : 0);
        };
        field = combo;
        break;
      }
      case gmic::ParameterType::Color: {
        auto* container = new QWidget(this);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        auto* swatch = new QPushButton(container);
        swatch->setAccessibleName(label);
        auto color = std::make_shared<QColor>(Qt::black);
        // The swatch style carries theme tokens, so it goes through the themed setter.
        const auto show = [swatch, color] { set_themed_style(*swatch, color_button_style(*color)); };
        row->addWidget(swatch);
        QSpinBox* alpha = nullptr;
        if (parameter.has_alpha) {
          auto* alpha_label = new QLabel(tr("Alpha:"), container);
          alpha = new QSpinBox(container);
          alpha->setRange(0, 255);
          configure_dialog_spinbox(alpha, 64);
          alpha_label->setBuddy(alpha);
          row->addSpacing(8);
          row->addWidget(alpha_label);
          row->addWidget(alpha);
          connect(alpha, qOverload<int>(&QSpinBox::valueChanged), this, [this, live](int) { changed(live); });
        }
        row->addStretch(1);
        connect(swatch, &QPushButton::clicked, this, [this, live, color, show, label, swatch] {
          // Every color picked while the picker is open previews at once (coalesced like a
          // slider drag), and Cancel puts the original back. The picker is non-modal: if the
          // panel moves to another filter meanwhile, this swatch is gone and picks are dropped.
          const QPointer<QPushButton> alive(swatch);
          const QColor original = *color;
          const auto apply = [this, live, color, show, alive](QColor picked) {
            if (alive == nullptr || picked == *color) {
              return;
            }
            *color = picked;
            show();
            changed(live);
          };
          const auto picked = request_patchy_color(this, original, label, apply);
          apply(picked.value_or(original));
        });
        binding.read = [color, alpha] {
          std::string value = std::to_string(color->red()) + "," + std::to_string(color->green()) + "," +
                              std::to_string(color->blue());
          if (alpha != nullptr) {
            value += ',';
            value += std::to_string(alpha->value());
          }
          return value;
        };
        binding.write = [color, alpha, show](const std::string& value) {
          const auto parts = value_parts(value);
          const auto channel = [&parts](int index) {
            return index < parts.size() ? std::clamp(static_cast<int>(std::lround(parts[index].toDouble())), 0, 255)
                                        : 0;
          };
          *color = QColor(channel(0), channel(1), channel(2));
          if (alpha != nullptr) {
            alpha->setValue(parts.size() > 3 ? channel(3) : 255);
          }
          show();
        };
        field = container;
        break;
      }
      case gmic::ParameterType::Point: {
        // Coordinates in percent of the image; a later phase puts a handle on the preview.
        auto* container = new QWidget(this);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        QDoubleSpinBox* spins[2] = {nullptr, nullptr};
        const QString names[2] = {tr("X:"), tr("Y:")};
        for (int axis = 0; axis < 2; ++axis) {
          auto* axis_label = new QLabel(names[axis], container);
          auto* spin = new QDoubleSpinBox(container);
          spin->setRange(-100.0, 200.0);
          spin->setDecimals(1);
          spin->setSuffix(percent_suffix());
          configure_dialog_spinbox(spin, 84);
          axis_label->setBuddy(spin);
          row->addWidget(axis_label);
          row->addWidget(spin);
          if (axis == 0) {
            row->addSpacing(8);
          }
          connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, live](double) {
            changed(live);
            if (!updating_) {
              points_changed();
            }
          });
          spins[axis] = spin;
        }
        row->addStretch(1);
        auto* x = spins[0];
        auto* y = spins[1];
        binding.read = [x, y] { return number_text(x->value(), 1) + "," + number_text(y->value(), 1); };
        binding.write = [x, y](const std::string& value) {
          const auto parts = value_parts(value);
          x->setValue(parts.size() > 0 ? parts[0].toDouble() : 50.0);
          y->setValue(parts.size() > 1 ? parts[1].toDouble() : 50.0);
        };
        // A point the filter declares hidden is its own bookkeeping, not a control.
        if (parameter.visibility != gmic::ParameterVisibility::Hidden) {
          point_controls_.push_back({x, y});
        }
        field = container;
        break;
      }
      case gmic::ParameterType::File:
      case gmic::ParameterType::Folder: {
        // A path typed or picked; "/" separators, as G'MIC takes them on every platform.
        auto* container = new QWidget(this);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(container);
        auto* browse = new QPushButton(tr("Browse..."), container);
        row->addWidget(edit, 1);
        row->addWidget(browse);
        const bool folder = parameter.type == gmic::ParameterType::Folder;
        connect(edit, &QLineEdit::textEdited, this, [this, live](const QString&) { changed(live); });
        connect(browse, &QPushButton::clicked, this, [this, live, edit, folder, label] {
          const QString start = edit->text();
          const QString picked = folder ? QFileDialog::getExistingDirectory(this, label, start)
                                        : get_open_file_name(this, label, start, QString());
          if (!picked.isEmpty()) {
            edit->setText(QDir::fromNativeSeparators(picked));
            changed(live);
          }
        });
        binding.read = [edit] { return gmic::quoted_text_value(to_gmic(edit->text())); };
        binding.write = [edit](const std::string& value) { edit->setText(unquoted_text(value)); };
        field = container;
        break;
      }
      case gmic::ParameterType::Text: {
        if (parameter.multiline) {
          auto* edit = new QPlainTextEdit(this);
          edit->setMaximumHeight(96);
          connect(edit, &QPlainTextEdit::textChanged, this, [this, live] { changed(live); });
          binding.read = [edit] { return gmic::quoted_text_value(to_gmic(edit->toPlainText())); };
          binding.write = [edit](const std::string& value) {
            const QSignalBlocker blocker(edit);
            edit->setPlainText(unquoted_text(value));
          };
          field = edit;
        } else {
          auto* edit = new QLineEdit(this);
          connect(edit, &QLineEdit::textEdited, this, [this, live](const QString&) { changed(live); });
          binding.read = [edit] { return gmic::quoted_text_value(to_gmic(edit->text())); };
          binding.write = [edit](const std::string& value) { edit->setText(unquoted_text(value)); };
          field = edit;
        }
        break;
      }
      case gmic::ParameterType::Float:
      case gmic::ParameterType::Int: {
        const bool is_float = parameter.type == gmic::ParameterType::Float;
        const double minimum = std::min(parameter.minimum, parameter.maximum);
        const double maximum = std::max(parameter.minimum, parameter.maximum);
        auto* container = new QWidget(this);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal, container);
        slider->setObjectName(object_stem + QStringLiteral("Slider"));
        row->addWidget(slider, 1);
        if (is_float) {
          const double range = maximum - minimum;
          // Fine enough for the range, and never coarser than the declaration writes its numbers.
          int decimals = range <= 2.0 ? 3 : 2;
          for (std::size_t argument = 0; argument < 3 && argument < parameter.arguments.size(); ++argument) {
            const auto& written = parameter.arguments[argument];
            const auto point = written.find('.');
            if (point != std::string::npos && written.find_first_of("eE") == std::string::npos) {
              decimals = std::clamp(static_cast<int>(written.size() - point - 1), decimals, 6);
            }
          }
          const double step = std::pow(10.0, -decimals);
          const int ticks = std::clamp(static_cast<int>(std::lround(range / step)), 1, 1'000'000);
          auto* spin = new QDoubleSpinBox(container);
          spin->setObjectName(object_stem + QStringLiteral("Spin"));
          spin->setRange(minimum, maximum);
          spin->setDecimals(decimals);
          spin->setSingleStep(std::max(step, range / 100.0));
          configure_dialog_spinbox(spin, kSliderSpinWidth);
          spin->setFixedWidth(kSliderSpinWidth);
          slider->setRange(0, ticks);
          connect(slider, &QSlider::valueChanged, spin, [spin, minimum, range, ticks](int tick) {
            spin->setValue(minimum + range * static_cast<double>(tick) / static_cast<double>(ticks));
          });
          connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                  [this, live, slider, minimum, range, ticks](double value) {
                    {
                      const QSignalBlocker blocker(slider);
                      slider->setValue(range > 0.0 ? static_cast<int>(std::lround((value - minimum) / range *
                                                                                   static_cast<double>(ticks)))
                                                   : 0);
                    }
                    changed(live);
                  });
          row->addWidget(spin);
          binding.read = [spin, decimals] { return number_text(spin->value(), decimals); };
          binding.write = [spin](const std::string& value) { spin->setValue(from_gmic(value).toDouble()); };
        } else {
          auto* spin = new QSpinBox(container);
          spin->setObjectName(object_stem + QStringLiteral("Spin"));
          const int low = static_cast<int>(std::lround(minimum));
          const int high = static_cast<int>(std::lround(maximum));
          spin->setRange(low, high);
          configure_dialog_spinbox(spin, kSliderSpinWidth);
          spin->setFixedWidth(kSliderSpinWidth);
          slider->setRange(low, high);
          connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
          connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, live, slider](int value) {
            {
              const QSignalBlocker blocker(slider);
              slider->setValue(value);
            }
            changed(live);
          });
          row->addWidget(spin);
          binding.read = [spin] { return std::to_string(spin->value()); };
          binding.write = [spin](const std::string& value) {
            spin->setValue(static_cast<int>(std::lround(from_gmic(value).toDouble())));
          };
        }
        field = container;
        break;
      }
      case gmic::ParameterType::Value: {
        // Not shown: a fixed value the filter passes to itself.
        auto stored = std::make_shared<std::string>(parameter.default_value);
        binding.read = [stored] { return *stored; };
        binding.write = [stored](const std::string& value) { *stored = value; };
        break;
      }
    }

    if (field != nullptr) {
      field->setObjectName(object_stem);
      if (spans_row) {
        form_->addRow(field);
      } else if (parameter.type == gmic::ParameterType::Button) {
        form_->addRow(QString(), field);
      } else {
        form_->addRow(label + QLatin1Char(':'), field);
      }
      const int row_index = form_->rowCount() - 1;
      if (parameter.visibility == gmic::ParameterVisibility::Hidden) {
        form_->setRowVisible(row_index, false);
      } else if (parameter.visibility == gmic::ParameterVisibility::Disabled) {
        field->setEnabled(false);
        if (auto* row_label = form_->labelForField(field); row_label != nullptr) {
          row_label->setEnabled(false);
        }
      }
    }
    if (parameter.carries_value()) {
      binding.field = field;
      bindings_.push_back(std::move(binding));
    }
  }
  reset_to_defaults();
  if (!values.empty()) {
    set_values(values);
  }
  // The dialog is already open when a filter is picked, after exec_dialog's own
  // scrub-label pass, so each rebuild pairs its labels itself.
  install_scrub_labels_in(this);
  points_changed();
}

std::vector<std::string> GmicParameterPanel::values() const {
  std::vector<std::string> values;
  values.reserve(bindings_.size());
  for (const auto& binding : bindings_) {
    values.push_back(binding.read());
  }
  return values;
}

void GmicParameterPanel::set_values(const std::vector<std::string>& values) {
  updating_ = true;
  for (std::size_t index = 0; index < bindings_.size() && index < values.size(); ++index) {
    bindings_[index].write(values[index]);
  }
  updating_ = false;
  points_changed();
}

bool GmicParameterPanel::apply_status(const std::string& status) {
  struct Entry {
    std::string value;
    int visibility{-1};  // -1: not stated
  };
  std::vector<Entry> entries;
  std::size_t at = 0;
  while (at < status.size()) {
    if (status[at] != '{') {
      return false;
    }
    const auto close = status.find('}', at);
    if (close == std::string::npos) {
      return false;
    }
    Entry entry;
    entry.value = status.substr(at + 1, close - at - 1);
    at = close + 1;
    if (at + 1 < status.size() && status[at] == '_' && status[at + 1] >= '0' && status[at + 1] <= '2') {
      entry.visibility = status[at + 1] - '0';
      at += 2;
    }
    entries.push_back(std::move(entry));
  }
  if (entries.empty() || entries.size() != bindings_.size()) {
    return false;
  }
  updating_ = true;
  for (std::size_t index = 0; index < bindings_.size(); ++index) {
    auto& binding = bindings_[index];
    binding.write(entries[index].value);
    if (binding.field != nullptr && entries[index].visibility >= 0) {
      form_->setRowVisible(binding.field, entries[index].visibility != 0);
      binding.field->setEnabled(entries[index].visibility != 1);
      if (auto* row_label = form_->labelForField(binding.field); row_label != nullptr) {
        row_label->setEnabled(entries[index].visibility != 1);
      }
    }
  }
  updating_ = false;
  points_changed();
  return true;
}

void GmicParameterPanel::release_buttons() {
  for (auto& binding : bindings_) {
    if (binding.button) {
      binding.write("0");
    }
  }
}

void GmicParameterPanel::reset_to_defaults() {
  updating_ = true;
  for (auto& binding : bindings_) {
    binding.write(binding.default_value);
  }
  updating_ = false;
  points_changed();
}

}  // namespace patchy::ui
