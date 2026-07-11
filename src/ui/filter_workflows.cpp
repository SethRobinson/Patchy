#include "ui/filter_workflows.hpp"

#include "ui/curves_editor.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QPushButton>
#include <QRect>
#include <QSlider>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QValidator>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

FilterCancelled::FilterCancelled() : std::runtime_error("Filter cancelled") {}

namespace {

struct SliderRowSpec {
  QString label;
  QString object_prefix;
  int minimum{0};
  int maximum{100};
  int value{0};
  QString suffix;
};

constexpr int kLivePreviewCoalesceDelayMs = 33;

template <typename Settings>
class CoalescedPreviewEmitter {
public:
  CoalescedPreviewEmitter(QObject& owner, std::function<void(const Settings&)> callback)
      : callback_(std::move(callback)) {
    timer_ = new QTimer(&owner);
    timer_->setSingleShot(true);
    timer_->setInterval(kLivePreviewCoalesceDelayMs);
    QObject::connect(timer_, &QTimer::timeout, &owner, [this] { deliver(); });
  }

  void schedule(Settings settings) {
    pending_ = std::move(settings);
    timer_->start();
  }

  void flush(Settings settings) {
    timer_->stop();
    pending_ = std::move(settings);
    deliver();
  }

private:
  void deliver() {
    if (!pending_.has_value() || !callback_) {
      return;
    }
    auto settings = std::move(*pending_);
    pending_.reset();
    callback_(settings);
  }

  QTimer* timer_{nullptr};
  std::optional<Settings> pending_;
  std::function<void(const Settings&)> callback_;
};

template <typename Settings>
struct AdjustmentPreviewRequest {
  bool enabled{true};
  Settings settings{};
};

int levels_channel_combo_index(LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return 1;
    case LevelsChannel::Green:
      return 2;
    case LevelsChannel::Blue:
      return 3;
    case LevelsChannel::Rgb:
      return 0;
  }
  return 0;
}

LevelsChannel levels_channel_from_combo_index(int index) {
  switch (index) {
    case 1:
      return LevelsChannel::Red;
    case 2:
      return LevelsChannel::Green;
    case 3:
      return LevelsChannel::Blue;
    default:
      return LevelsChannel::Rgb;
  }
}

LevelsRecord clamp_levels_record(LevelsRecord record) {
  record.black_input = std::clamp(record.black_input, 0, 254);
  record.white_input = std::clamp(record.white_input, record.black_input + 1, 255);
  record.gamma_percent = std::clamp(record.gamma_percent, 10, 999);
  record.black_output = std::clamp(record.black_output, 0, 255);
  record.white_output = std::clamp(record.white_output, record.black_output, 255);
  return record;
}

LevelsRecord levels_master_record(LevelsSettings settings) {
  return clamp_levels_record(LevelsRecord{settings.black_input, settings.white_input, settings.gamma_percent,
                                          settings.black_output, settings.white_output});
}

void set_levels_master_record(LevelsSettings& settings, LevelsRecord record) {
  record = clamp_levels_record(record);
  settings.black_input = record.black_input;
  settings.white_input = record.white_input;
  settings.gamma_percent = record.gamma_percent;
  settings.black_output = record.black_output;
  settings.white_output = record.white_output;
}

LevelsRecord levels_record_for_channel(LevelsSettings settings, LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return clamp_levels_record(settings.red);
    case LevelsChannel::Green:
      return clamp_levels_record(settings.green);
    case LevelsChannel::Blue:
      return clamp_levels_record(settings.blue);
    case LevelsChannel::Rgb:
      return levels_master_record(settings);
  }
  return {};
}

void set_levels_record_for_channel(LevelsSettings& settings, LevelsChannel channel, LevelsRecord record) {
  record = clamp_levels_record(record);
  switch (channel) {
    case LevelsChannel::Red:
      settings.red = record;
      return;
    case LevelsChannel::Green:
      settings.green = record;
      return;
    case LevelsChannel::Blue:
      settings.blue = record;
      return;
    case LevelsChannel::Rgb:
      set_levels_master_record(settings, record);
      return;
  }
}

LevelsSettings clamp_levels_settings(LevelsSettings settings) {
  set_levels_master_record(settings, levels_master_record(settings));
  settings.red = clamp_levels_record(settings.red);
  settings.green = clamp_levels_record(settings.green);
  settings.blue = clamp_levels_record(settings.blue);
  return settings;
}

std::uint8_t map_levels_value(std::uint8_t value, LevelsRecord record) {
  record = clamp_levels_record(record);
  const auto input_range = static_cast<double>(record.white_input - record.black_input);
  const auto gamma = static_cast<double>(record.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;
  const auto output_range = static_cast<double>(record.white_output - record.black_output);
  const auto normalized =
      std::clamp((static_cast<double>(value) - static_cast<double>(record.black_input)) / input_range, 0.0, 1.0);
  const auto output = static_cast<double>(record.black_output) + std::pow(normalized, inverse_gamma) * output_range;
  return static_cast<std::uint8_t>(std::clamp(std::lround(output), 0L, 255L));
}

std::array<int, 256> default_levels_histogram() {
  std::array<int, 256> histogram{};
  for (int index = 0; index < static_cast<int>(histogram.size()); ++index) {
    const auto x = static_cast<double>(index) / 255.0;
    const auto shadow = std::exp(-std::pow((x - 0.18) / 0.17, 2.0)) * 1100.0;
    const auto midtone = std::exp(-std::pow((x - 0.52) / 0.28, 2.0)) * 1450.0;
    const auto highlight = std::exp(-std::pow((x - 0.82) / 0.08, 2.0)) * 2300.0;
    histogram[static_cast<std::size_t>(index)] =
        static_cast<int>(std::round(80.0 + shadow + midtone + highlight));
  }
  histogram[188] += 2400;
  histogram[239] += 3800;
  histogram[254] += 4200;
  return histogram;
}

int levels_histogram_value(const std::uint8_t* px, LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return px[0];
    case LevelsChannel::Green:
      return px[1];
    case LevelsChannel::Blue:
      return px[2];
    case LevelsChannel::Rgb:
      return std::clamp((54 * static_cast<int>(px[0]) + 183 * static_cast<int>(px[1]) +
                         19 * static_cast<int>(px[2])) /
                            256,
                        0, 255);
  }
  return 0;
}

std::array<int, 256> levels_histogram_from_pixels(const PixelBuffer* source,
                                                  LevelsChannel channel = LevelsChannel::Rgb) {
  if (source == nullptr || source->empty() || source->format().bit_depth != BitDepth::UInt8 ||
      source->format().channels < 3) {
    return default_levels_histogram();
  }

  std::array<int, 256> histogram{};
  const auto width = std::max<std::int32_t>(0, source->width());
  const auto height = std::max<std::int32_t>(0, source->height());
  const auto total_pixels = static_cast<double>(width) * static_cast<double>(height);
  const auto sample_step = std::max(1, static_cast<int>(std::sqrt(total_pixels / 262144.0)));
  const auto channels = source->format().channels;
  int samples = 0;
  for (std::int32_t y = 0; y < height; y += sample_step) {
    for (std::int32_t x = 0; x < width; x += sample_step) {
      const auto* px = source->pixel(x, y);
      if (channels >= 4 && px[3] == 0) {
        continue;
      }
      const auto value = levels_histogram_value(px, channel);
      ++histogram[static_cast<std::size_t>(value)];
      ++samples;
    }
  }
  return samples > 0 ? histogram : default_levels_histogram();
}

class LevelsGammaSpinBox final : public QSpinBox {
public:
  using QSpinBox::QSpinBox;

protected:
  QString textFromValue(int value) const override {
    return QString::number(static_cast<double>(value) / 100.0, 'f', 2);
  }

  int valueFromText(const QString& text) const override {
    bool ok = false;
    const auto value = text.trimmed().toDouble(&ok);
    return ok ? std::clamp(static_cast<int>(std::round(value * 100.0)), minimum(), maximum()) : this->value();
  }

  QValidator::State validate(QString& input, int& pos) const override {
    Q_UNUSED(pos);
    if (input.trimmed().isEmpty()) {
      return QValidator::Intermediate;
    }
    bool ok = false;
    const auto value = input.trimmed().toDouble(&ok);
    if (!ok) {
      return QValidator::Invalid;
    }
    return value >= 0.10 && value <= 9.99 ? QValidator::Acceptable : QValidator::Intermediate;
  }
};

class LevelsInputGraph final : public QWidget {
public:
  explicit LevelsInputGraph(QWidget* parent = nullptr) : QWidget(parent), histogram_(default_levels_histogram()) {
    setObjectName(QStringLiteral("levelsInputGraph"));
    setMinimumSize(272, 116);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
  }

  void set_histogram(std::array<int, 256> histogram) {
    histogram_ = histogram;
    update();
  }

  void set_levels(int black_input, int white_input, int gamma_percent) {
    auto settings = clamp_levels_settings(LevelsSettings{black_input, white_input, gamma_percent});
    black_input_ = settings.black_input;
    white_input_ = settings.white_input;
    gamma_percent_ = settings.gamma_percent;
    update();
  }

  void set_values_changed_callback(std::function<void(int, int, int)> callback) {
    values_changed_ = std::move(callback);
  }

  QSize sizeHint() const override {
    return QSize(272, 116);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto graph = graph_rect();
    painter.fillRect(rect(), QColor(73, 73, 73));
    painter.fillRect(graph, QColor(55, 55, 55));
    painter.setPen(QPen(QColor(46, 46, 46), 1));
    painter.drawRect(graph.adjusted(0, 0, -1, -1));

    const auto maximum = std::max(1, *std::max_element(histogram_.begin(), histogram_.end()));
    const auto log_max = std::log(static_cast<double>(maximum) + 1.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(218, 218, 218));
    for (int x = 0; x < graph.width(); ++x) {
      const auto first_bin = std::clamp((x * 256) / std::max(1, graph.width()), 0, 255);
      const auto last_bin = std::clamp(((x + 1) * 256) / std::max(1, graph.width()), first_bin + 1, 256);
      int count = 0;
      for (int bin = first_bin; bin < last_bin; ++bin) {
        count = std::max(count, histogram_[static_cast<std::size_t>(bin)]);
      }
      const auto scaled = log_max <= 0.0 ? 0.0 : std::log(static_cast<double>(count) + 1.0) / log_max;
      const auto bar_height = std::clamp(static_cast<int>(std::round(scaled * (graph.height() - 8))), 1,
                                         std::max(1, graph.height() - 4));
      painter.fillRect(QRect(graph.left() + x, graph.bottom() - bar_height, 1, bar_height), QColor(218, 218, 218));
    }

    painter.setPen(QPen(QColor(192, 192, 192, 160), 1));
    painter.drawLine(QPointF(x_for_value(black_input_), graph.top()), QPointF(x_for_value(black_input_), graph.bottom()));
    painter.drawLine(QPointF(gamma_x(), graph.top()), QPointF(gamma_x(), graph.bottom()));
    painter.drawLine(QPointF(x_for_value(white_input_), graph.top()), QPointF(x_for_value(white_input_), graph.bottom()));

    draw_handle(painter, x_for_value(black_input_), QColor(28, 28, 28), InputHandle::Black);
    draw_handle(painter, gamma_x(), QColor(154, 154, 154), InputHandle::Gamma);
    draw_handle(painter, x_for_value(white_input_), QColor(242, 242, 242), InputHandle::White);
  }

  void mousePressEvent(QMouseEvent* event) override {
    active_handle_ = nearest_handle(event->position().x());
    update_from_position(event->position().x());
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (active_handle_ != InputHandle::None) {
      update_from_position(event->position().x());
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    Q_UNUSED(event);
    active_handle_ = InputHandle::None;
    update();
  }

private:
  enum class InputHandle {
    None,
    Black,
    Gamma,
    White
  };

  QRect graph_rect() const {
    return rect().adjusted(8, 8, -8, -28);
  }

  double x_for_value(int value) const {
    const auto graph = graph_rect();
    return static_cast<double>(graph.left()) +
           (static_cast<double>(std::clamp(value, 0, 255)) / 255.0) * static_cast<double>(graph.width() - 1);
  }

  int value_for_x(double x) const {
    const auto graph = graph_rect();
    const auto normalized =
        std::clamp((x - static_cast<double>(graph.left())) / static_cast<double>(std::max(1, graph.width() - 1)),
                   0.0, 1.0);
    return std::clamp(static_cast<int>(std::round(normalized * 255.0)), 0, 255);
  }

  double gamma_x() const {
    const auto gamma = std::clamp(static_cast<double>(gamma_percent_) / 100.0, 0.10, 9.99);
    const auto normalized = std::pow(0.5, gamma);
    return x_for_value(black_input_) + normalized * (x_for_value(white_input_) - x_for_value(black_input_));
  }

  InputHandle nearest_handle(double x) const {
    const std::array<std::pair<InputHandle, double>, 3> handles{
        std::pair{InputHandle::Black, x_for_value(black_input_)},
        std::pair{InputHandle::Gamma, gamma_x()},
        std::pair{InputHandle::White, x_for_value(white_input_)},
    };
    auto nearest = handles[0];
    auto nearest_distance = std::abs(x - nearest.second);
    for (const auto& handle : handles) {
      const auto distance = std::abs(x - handle.second);
      if (distance < nearest_distance) {
        nearest = handle;
        nearest_distance = distance;
      }
    }
    return nearest.first;
  }

  void draw_handle(QPainter& painter, double x, QColor color, InputHandle handle) {
    const auto graph = graph_rect();
    const auto top = static_cast<double>(graph.bottom() + 4);
    const QPolygonF triangle{QPointF(x, top), QPointF(x - 5.0, top + 9.0), QPointF(x + 5.0, top + 9.0)};
    painter.setPen(QPen(handle == active_handle_ ? QColor(92, 164, 255) : QColor(118, 118, 118), 1));
    painter.setBrush(color);
    painter.drawPolygon(triangle);
  }

  void update_from_position(double x) {
    if (active_handle_ == InputHandle::Black) {
      black_input_ = std::clamp(value_for_x(x), 0, white_input_ - 1);
    } else if (active_handle_ == InputHandle::White) {
      white_input_ = std::clamp(value_for_x(x), black_input_ + 1, 255);
    } else if (active_handle_ == InputHandle::Gamma) {
      const auto left = x_for_value(black_input_);
      const auto right = x_for_value(white_input_);
      const auto normalized = std::clamp((x - left) / std::max(1.0, right - left), 0.001, 0.999);
      const auto gamma = std::log(normalized) / std::log(0.5);
      if (std::isfinite(gamma)) {
        gamma_percent_ = std::clamp(static_cast<int>(std::round(gamma * 100.0)), 10, 999);
      }
    }
    update();
    if (values_changed_) {
      values_changed_(black_input_, white_input_, gamma_percent_);
    }
  }

  std::array<int, 256> histogram_{};
  int black_input_{0};
  int white_input_{255};
  int gamma_percent_{100};
  InputHandle active_handle_{InputHandle::None};
  std::function<void(int, int, int)> values_changed_;
};

class LevelsOutputRange final : public QWidget {
public:
  explicit LevelsOutputRange(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("levelsOutputRange"));
    setMinimumSize(272, 48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
  }

  void set_levels(int black_output, int white_output) {
    auto settings = clamp_levels_settings(LevelsSettings{0, 255, 100, black_output, white_output});
    black_output_ = settings.black_output;
    white_output_ = settings.white_output;
    update();
  }

  void set_values_changed_callback(std::function<void(int, int)> callback) {
    values_changed_ = std::move(callback);
  }

  QSize sizeHint() const override {
    return QSize(272, 48);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(73, 73, 73));

    const auto bar = bar_rect();
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    for (int stop = 0; stop <= 8; ++stop) {
      const auto t = static_cast<double>(stop) / 8.0;
      const auto value = std::clamp(static_cast<int>(std::round(static_cast<double>(black_output_) +
                                                                t * static_cast<double>(white_output_ - black_output_))),
                                    0, 255);
      gradient.setColorAt(t, QColor(value, value, value));
    }
    painter.fillRect(bar, gradient);
    painter.setPen(QPen(QColor(46, 46, 46), 1));
    painter.drawRect(bar.adjusted(0, 0, -1, -1));
    draw_handle(painter, x_for_value(black_output_), QColor(28, 28, 28), OutputHandle::Black);
    draw_handle(painter, x_for_value(white_output_), QColor(242, 242, 242), OutputHandle::White);
  }

  void mousePressEvent(QMouseEvent* event) override {
    active_handle_ = std::abs(event->position().x() - x_for_value(black_output_)) <=
                             std::abs(event->position().x() - x_for_value(white_output_))
                         ? OutputHandle::Black
                         : OutputHandle::White;
    update_from_position(event->position().x());
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (active_handle_ != OutputHandle::None) {
      update_from_position(event->position().x());
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    Q_UNUSED(event);
    active_handle_ = OutputHandle::None;
    update();
  }

private:
  enum class OutputHandle {
    None,
    Black,
    White
  };

  QRect bar_rect() const {
    return rect().adjusted(8, 8, -8, -22);
  }

  double x_for_value(int value) const {
    const auto bar = bar_rect();
    return static_cast<double>(bar.left()) +
           (static_cast<double>(std::clamp(value, 0, 255)) / 255.0) * static_cast<double>(bar.width() - 1);
  }

  int value_for_x(double x) const {
    const auto bar = bar_rect();
    const auto normalized =
        std::clamp((x - static_cast<double>(bar.left())) / static_cast<double>(std::max(1, bar.width() - 1)),
                   0.0, 1.0);
    return std::clamp(static_cast<int>(std::round(normalized * 255.0)), 0, 255);
  }

  void draw_handle(QPainter& painter, double x, QColor color, OutputHandle handle) {
    const auto bar = bar_rect();
    const auto top = static_cast<double>(bar.bottom() + 4);
    const QPolygonF triangle{QPointF(x, top), QPointF(x - 5.0, top + 9.0), QPointF(x + 5.0, top + 9.0)};
    painter.setPen(QPen(handle == active_handle_ ? QColor(92, 164, 255) : QColor(118, 118, 118), 1));
    painter.setBrush(color);
    painter.drawPolygon(triangle);
  }

  void update_from_position(double x) {
    if (active_handle_ == OutputHandle::Black) {
      black_output_ = std::clamp(value_for_x(x), 0, white_output_);
    } else if (active_handle_ == OutputHandle::White) {
      white_output_ = std::clamp(value_for_x(x), black_output_, 255);
    }
    update();
    if (values_changed_) {
      values_changed_(black_output_, white_output_);
    }
  }

  int black_output_{0};
  int white_output_{255};
  OutputHandle active_handle_{OutputHandle::None};
  std::function<void(int, int)> values_changed_;
};

QSpinBox* add_slider_row(QDialog& dialog, QFormLayout* form, const SliderRowSpec& spec) {
  auto* container = new QWidget(&dialog);
  auto* row = new QHBoxLayout(container);
  row->setContentsMargins(0, 0, 0, 0);
  auto* slider = new QSlider(Qt::Horizontal, container);
  auto* spin = new QSpinBox(container);
  slider->setRange(spec.minimum, spec.maximum);
  spin->setRange(spec.minimum, spec.maximum);
  slider->setValue(spec.value);
  spin->setValue(spec.value);
  slider->setObjectName(spec.object_prefix + QStringLiteral("Slider"));
  spin->setObjectName(spec.object_prefix + QStringLiteral("Spin"));
  if (!spec.suffix.isEmpty()) {
    spin->setSuffix(spec.suffix);
  }
  configure_dialog_spinbox(spin, 72);
  row->addWidget(slider, 1);
  row->addWidget(spin);
  QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
  QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
  form->addRow(spec.label, container);
  return spin;
}

template <typename Settings>
std::optional<Settings> request_adjustment_settings_dialog(
    QWidget* parent, const QString& object_name, const QString& title, const QString& preview_object_name,
    const std::vector<SliderRowSpec>& row_specs,
    const std::function<Settings(const std::vector<QSpinBox*>&)>& build_settings,
    const std::function<void(bool, const Settings&)>& preview_changed,
    const std::function<void(QDialog&, const std::vector<QSpinBox*>&)>& connect_constraints = {},
    // Runs after the preview plumbing is wired (unlike connect_constraints), so
    // extra controls can add form rows and flush the live preview on change.
    const std::function<void(QDialog&, QFormLayout*, const std::vector<QSpinBox*>&, const std::function<void()>&)>&
        add_extras = {}) {
  QDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  std::vector<QSpinBox*> spins;
  spins.reserve(row_specs.size());
  for (const auto& spec : row_specs) {
    spins.push_back(add_slider_row(dialog, form, spec));
  }

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(preview_object_name);
  preview->setChecked(true);
  layout->addWidget(preview);

  if (connect_constraints) {
    connect_constraints(dialog, spins);
  }

  CoalescedPreviewEmitter<AdjustmentPreviewRequest<Settings>> preview_emitter(
      dialog, [&](const AdjustmentPreviewRequest<Settings>& request) {
        if (preview_changed) {
          preview_changed(request.enabled, request.settings);
        }
      });
  auto preview_request = [&] {
    return AdjustmentPreviewRequest<Settings>{preview->isChecked(), build_settings(spins)};
  };
  auto schedule_preview = [&] { preview_emitter.schedule(preview_request()); };
  auto flush_preview = [&] { preview_emitter.flush(preview_request()); };
  for (auto* spin : spins) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [&schedule_preview](int) { schedule_preview(); });
  }
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });

  if (add_extras) {
    add_extras(dialog, form, spins, flush_preview);
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return build_settings(spins);
}

}  // namespace

QString filter_action_object_name(const QString& identifier) {
  auto object_name = QStringLiteral("filterAction_") + identifier;
  object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
  return object_name;
}

QString filter_display_name(const FilterDefinition& filter) {
  const auto identifier = QString::fromStdString(filter.identifier);
  if (identifier == QStringLiteral("patchy.filters.invert")) {
    return QObject::tr("Invert");
  }
  if (identifier == QStringLiteral("patchy.filters.brightness_contrast")) {
    return QObject::tr("Brightness/Contrast");
  }
  if (identifier == QStringLiteral("patchy.filters.grayscale")) {
    return QObject::tr("Grayscale");
  }
  if (identifier == QStringLiteral("patchy.filters.desaturate")) {
    return QObject::tr("Desaturate");
  }
  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    return QObject::tr("Auto Contrast");
  }
  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    return QObject::tr("Soft Glow");
  }
  if (identifier == QStringLiteral("patchy.filters.punchy_color")) {
    return QObject::tr("Punchy Color");
  }
  if (identifier == QStringLiteral("patchy.filters.noir")) {
    return QObject::tr("Noir");
  }
  if (identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    return QObject::tr("Cinematic Matte");
  }
  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    return QObject::tr("Vintage Fade");
  }
  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    return QObject::tr("Vintage Sepia");
  }
  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    return QObject::tr("Threshold");
  }
  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    return QObject::tr("Posterize");
  }
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return QObject::tr("Box Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    return QObject::tr("Sharpen");
  }
  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    return QObject::tr("Unsharp Mask");
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return QObject::tr("Gaussian Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    return QObject::tr("Motion Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    return QObject::tr("Radial Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    return QObject::tr("Edge Detect");
  }
  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    return QObject::tr("Emboss");
  }
  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    return QObject::tr("Glowing Edges");
  }
  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    return QObject::tr("Twirl");
  }
  if (identifier == QStringLiteral("patchy.filters.wave")) {
    return QObject::tr("Wave");
  }
  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    return QObject::tr("Pinch/Bloat");
  }
  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    return QObject::tr("Clouds");
  }
  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    return QObject::tr("Pixel Mosaic");
  }
  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    return QObject::tr("Color Halftone");
  }
  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    return QObject::tr("Analog Grain");
  }
  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    return QObject::tr("Lens Vignette");
  }
  return QString::fromStdString(filter.display_name);
}

bool is_adjustment_only_filter(const QString& identifier) {
  return identifier == QStringLiteral("patchy.filters.invert") ||
         identifier == QStringLiteral("patchy.filters.brightness_contrast") ||
         identifier == QStringLiteral("patchy.filters.grayscale") ||
         identifier == QStringLiteral("patchy.filters.desaturate") ||
         identifier == QStringLiteral("patchy.filters.auto_contrast") ||
         identifier == QStringLiteral("patchy.filters.threshold") ||
         identifier == QStringLiteral("patchy.filters.posterize");
}

FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter) {
  const auto identifier = QString::fromStdString(filter.identifier);
  const auto display_name = filter_display_name(filter);
  const auto amount_control = [](int value = 100) {
    return FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, value,
                             QStringLiteral("%")};
  };

  if (identifier == QStringLiteral("patchy.filters.invert")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.brightness_contrast")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Brightness"), QStringLiteral("filterBrightness"), -100, 100, 0, {}},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), -100, 100, 0,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.grayscale") ||
      identifier == QStringLiteral("patchy.filters.desaturate")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Glow"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.punchy_color") ||
      identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Intensity"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.noir")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Fade"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Threshold"), QStringLiteral("filterThreshold"), 0, 255, 128, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Levels"), QStringLiteral("filterLevels"), 2, 16, 4, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 1,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 300, 150,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Threshold"), QStringLiteral("filterThreshold"), 0, 255, 8, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 2,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -180, 180, 0,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Distance"), QStringLiteral("filterDistance"), 1, 64, 12,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, 35,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Samples"), QStringLiteral("filterSamples"), 4, 32, 16, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Strength"), QStringLiteral("filterStrength"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -180, 180, 135,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Height"), QStringLiteral("filterHeight"), 1, 24, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterDepth"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Edge Width"), QStringLiteral("filterEdgeWidth"), 1, 12, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Brightness"), QStringLiteral("filterBrightness"), 0, 300, 140,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Smoothness"), QStringLiteral("filterSmoothness"), 0, 12, 2,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -720, 720, 180,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.wave")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amplitude"), QStringLiteral("filterAmplitude"), 0, 64, 12,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Wavelength"), QStringLiteral("filterWavelength"), 4, 256, 48,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Phase"), QStringLiteral("filterPhase"), 0, 360, 0,
                               QStringLiteral(" deg")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), -100, 100, 35,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Scale"), QStringLiteral("filterScale"), 12, 512, 96,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Detail"), QStringLiteral("filterDetail"), 1, 8, 6, {}},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), 0, 100, 40,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Seed"), QStringLiteral("filterSeed"), 1, 9999, 1, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Block Size"), QStringLiteral("filterBlockSize"), 2, 32, 4,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Cell Size"), QStringLiteral("filterCellSize"), 4, 64, 10,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Intensity"), QStringLiteral("filterIntensity"), 0, 100, 75,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), 0, 100, 60,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, 50,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Strength"), QStringLiteral("filterStrength"), 0, 100, 55,
                               QStringLiteral("%")}}};
  }

  return {identifier, display_name, {amount_control()}};
}

std::optional<std::vector<int>> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyFilterDialog"));
  dialog.setProperty("patchy.filterIdentifier", spec.identifier);
  dialog.setWindowTitle(spec.display_name);
  auto* layout = new QVBoxLayout(&dialog);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("filterPreviewCheck"));
  preview->setChecked(true);
  layout->addWidget(preview);

  auto* form = new QFormLayout();
  layout->addLayout(form);

  std::vector<QSpinBox*> spins;
  spins.reserve(spec.controls.size());
  std::vector<int> defaults;
  defaults.reserve(spec.controls.size());

  for (const auto& control : spec.controls) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(control.minimum, control.maximum);
    spin->setRange(control.minimum, control.maximum);
    slider->setValue(control.value);
    spin->setValue(control.value);
    slider->setObjectName(control.object_name + QStringLiteral("Slider"));
    spin->setObjectName(control.object_name + QStringLiteral("Spin"));
    if (!control.suffix.isEmpty()) {
      spin->setSuffix(control.suffix);
    }
    configure_dialog_spinbox(spin, 78);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    form->addRow(control.label, container);
    spins.push_back(spin);
    defaults.push_back(control.value);
  }

  auto build_settings = [&] {
    FilterPreviewSettings settings;
    settings.preview_enabled = preview->isChecked();
    settings.values.reserve(spins.size());
    for (const auto* spin : spins) {
      settings.values.push_back(spin->value());
    }
    return settings;
  };

  CoalescedPreviewEmitter<FilterPreviewSettings> preview_emitter(
      dialog, [&](const FilterPreviewSettings& settings) {
        if (preview_changed) {
          preview_changed(settings);
        }
      });
  auto schedule_preview = [&] { preview_emitter.schedule(build_settings()); };
  auto flush_preview = [&] { preview_emitter.flush(build_settings()); };

  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });
  for (auto* spin : spins) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [&schedule_preview](int) { schedule_preview(); });
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
                                       &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset); reset != nullptr) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      for (std::size_t index = 0; index < spins.size(); ++index) {
        spins[index]->setValue(defaults[index]);
      }
      preview->setChecked(true);
      flush_preview();
    });
  }

  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  std::vector<int> values;
  values.reserve(spins.size());
  for (const auto* spin : spins) {
    values.push_back(spin->value());
  }
  return values;
}

std::uint8_t filter_clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint8_t filter_clamp_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

int filter_luminance(const std::uint8_t* px) {
  return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
}

constexpr double kFilterPi = 3.14159265358979323846;

int filter_value(const std::vector<int>& values, std::size_t index, int fallback) {
  return index < values.size() ? values[index] : fallback;
}

void report_filter_progress(const FilterProgress* progress, int completed, int total,
                            const QString& detail = QString()) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  if (!progress->update(std::clamp(completed, 0, std::max(1, total)), std::max(1, total), detail)) {
    throw FilterCancelled();
  }
}

void report_filter_row_progress(const FilterProgress* progress, std::int32_t y, std::int32_t height,
                                int progress_offset = 0, int progress_total = 0) {
  report_filter_progress(progress, progress_offset + y, progress_total > 0 ? progress_total : height,
                         QObject::tr("Filtering pixels"));
}

void finish_filter_row_progress(const FilterProgress* progress, std::int32_t height, int progress_offset = 0,
                                int progress_total = 0) {
  report_filter_progress(progress, progress_offset + height, progress_total > 0 ? progress_total : height,
                         QObject::tr("Filtering pixels"));
}

QRegion layer_selection_region(const QRegion& selection, Rect bounds) {
  if (selection.isEmpty() || bounds.empty()) {
    return {};
  }
  return selection.intersected(QRegion(QRect(bounds.x, bounds.y, bounds.width, bounds.height)));
}

template <typename PixelFn>
void for_each_selected_pixel(PixelBuffer& pixels, Rect bounds, const QRegion& selection, const FilterProgress* progress,
                             PixelFn&& pixel_fn) {
  if (selection.isEmpty()) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        pixel_fn(x, y);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  int total_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    if (local_left < local_right && local_top < local_bottom) {
      total_rows += local_bottom - local_top;
    }
  }
  int completed_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      report_filter_progress(progress, completed_rows, total_rows, QObject::tr("Filtering pixels"));
      for (std::int32_t x = local_left; x < local_right; ++x) {
        pixel_fn(x, y);
      }
      ++completed_rows;
    }
  }
  report_filter_progress(progress, total_rows, total_rows, QObject::tr("Filtering pixels"));
}

void blend_filter_with_original(PixelBuffer& pixels, const PixelBuffer& original, int amount_percent,
                                const FilterProgress* progress = nullptr, int progress_offset = 0,
                                int progress_total = 0) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  if (amount_percent >= 100) {
    finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
    return;
  }
  if (amount_percent <= 0 || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    pixels = original;
    finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
    return;
  }

  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height(), progress_offset, progress_total);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        dst[channel] = filter_clamp_byte((static_cast<int>(src[channel]) * (100 - amount_percent) +
                                          static_cast<int>(dst[channel]) * amount_percent) /
                                         100);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
}

FilterProgress filter_progress_phase(const FilterProgress* progress, int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index, phase_count](int completed, int total, const QString& detail) {
    constexpr int kPhaseScale = 1000;
    const auto safe_phase_count = std::max(1, phase_count);
    const auto safe_total = std::max(1, total);
    const auto clamped_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed = (clamped_completed * kPhaseScale) / safe_total;
    return progress->update(std::clamp(phase_index, 0, safe_phase_count - 1) * kPhaseScale + phase_completed,
                            safe_phase_count * kPhaseScale, detail);
  }};
}

std::uint8_t filter_blend_byte(std::uint8_t base, std::uint8_t overlay, int amount_percent) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  return filter_clamp_byte((static_cast<int>(base) * (100 - amount_percent) +
                            static_cast<int>(overlay) * amount_percent + 50) /
                           100);
}

void adjust_contrast_filter_pixels(PixelBuffer& pixels, double factor, int midpoint, const FilterProgress* progress) {
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = filter_clamp_byte(static_cast<int>((static_cast<double>(px[channel]) -
                                                          static_cast<double>(midpoint)) *
                                                             factor +
                                                         static_cast<double>(midpoint)));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void adjust_saturation_filter_pixels(PixelBuffer& pixels, double factor, const FilterProgress* progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto luminance = static_cast<double>(filter_luminance(px));
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        px[channel] = filter_clamp_byte(
            static_cast<int>(luminance + (static_cast<double>(px[channel]) - luminance) * factor));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void tint_filter_pixels(PixelBuffer& pixels, int red, int green, int blue, const FilterProgress* progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red);
      px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green);
      px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue);
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void blend_overlay_filter_pixels(PixelBuffer& pixels, const PixelBuffer& overlay, int amount_percent,
                                 const FilterProgress* progress) {
  if (pixels.format() != overlay.format() || pixels.width() != overlay.width() || pixels.height() != overlay.height()) {
    return;
  }
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto* over = overlay.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = filter_blend_byte(px[channel], over[channel], amount_percent);
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

struct FilterPixelAccum {
  std::array<double, 3> premultiplied_color{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};

void filter_accumulate_pixel(FilterPixelAccum& accum, const PixelBuffer& original, const std::uint8_t* px,
                             double weight) {
  if (weight <= 0.0) {
    return;
  }
  const auto alpha = original.format().channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
  accum.weight += weight;
  accum.alpha += alpha * weight;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(original.format().channels, 3); ++channel) {
    accum.premultiplied_color[static_cast<std::size_t>(channel)] += static_cast<double>(px[channel]) * alpha * weight;
  }
}

void filter_accumulate_sample(FilterPixelAccum& accum, const PixelBuffer& original, double x, double y,
                              double weight = 1.0) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(original.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(original.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  filter_accumulate_pixel(accum, original, original.pixel(x0, y0), weight * (1.0 - tx) * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x1, y0), weight * tx * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x0, y1), weight * (1.0 - tx) * ty);
  filter_accumulate_pixel(accum, original, original.pixel(x1, y1), weight * tx * ty);
}

void filter_write_accumulated_pixel(PixelBuffer& pixels, std::int32_t x, std::int32_t y,
                                    const FilterPixelAccum& accum) {
  auto* dst = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  const auto normalized_alpha = channels >= 4 && accum.weight > 0.0 ? accum.alpha / accum.weight : 1.0;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(channels, 3); ++channel) {
    const auto value = accum.alpha > 0.000001
                           ? accum.premultiplied_color[static_cast<std::size_t>(channel)] / accum.alpha
                           : 0.0;
    dst[channel] = filter_clamp_byte(value);
  }
  if (channels >= 4) {
    dst[3] = filter_clamp_byte(normalized_alpha * 255.0);
  }
}

void filter_copy_sampled_pixel(PixelBuffer& pixels, const PixelBuffer& original, std::int32_t x, std::int32_t y,
                               double source_x, double source_y) {
  FilterPixelAccum accum;
  filter_accumulate_sample(accum, original, source_x, source_y);
  filter_write_accumulated_pixel(pixels, x, y, accum);
}

// The box (weighted=false) and tent/"gaussian" (weighted=true) kernels are
// products wx(dx)*wy(dy), so the 2D convolution factors exactly into a
// horizontal pass followed by a vertical pass over the horizontal sums:
// (2r+1)^2 taps per pixel become 2*(2r+1). Accumulation matches
// filter_accumulate_pixel/filter_write_accumulated_pixel (premultiplied-alpha
// doubles, total weight is the constant Wx*Wy since edge clamping reuses
// samples without dropping weights); results differ from the naive loop only
// by floating-point summation order.
void apply_separable_tent_blur(PixelBuffer& pixels, const PixelBuffer& original, int radius, bool weighted,
                               const FilterProgress* progress) {
  radius = std::clamp(radius, 1, 32);
  const auto width = original.width();
  const auto height = original.height();
  if (width == 0 || height == 0) {
    return;
  }
  const auto channels = original.format().channels;
  const auto color_channels = std::min<std::uint16_t>(channels, 3);
  const auto taps = 2 * radius + 1;
  double axis_weight_sum = 0.0;
  std::vector<double> axis_weights(static_cast<std::size_t>(taps));
  for (int offset = -radius; offset <= radius; ++offset) {
    const auto weight = weighted ? static_cast<double>(radius + 1 - std::abs(offset)) : 1.0;
    axis_weights[static_cast<std::size_t>(offset + radius)] = weight;
    axis_weight_sum += weight;
  }
  const auto total_weight = axis_weight_sum * axis_weight_sum;

  // Ring buffer of horizontally-blurred rows: per pixel {premul r,g,b, alpha}.
  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  std::vector<double> h_rows(row_stride * static_cast<std::size_t>(taps), 0.0);
  int h_rows_built_through = -1;

  const auto build_h_row = [&](std::int32_t source_y, double* out) {
    std::fill(out, out + row_stride, 0.0);
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto weight = axis_weights[static_cast<std::size_t>(dx + radius)];
      for (std::int32_t x = 0; x < width; ++x) {
        const auto sx = std::clamp<std::int32_t>(x + dx, 0, width - 1);
        const auto* px = original.pixel(sx, source_y);
        const auto alpha = channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
        auto* accum = out + static_cast<std::size_t>(x) * 4U;
        const auto alpha_weight = alpha * weight;
        for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
          accum[channel] += static_cast<double>(px[channel]) * alpha_weight;
        }
        accum[3] += alpha_weight;
      }
    }
  };
  const auto h_row_for = [&](std::int32_t source_y) -> const double* {
    return h_rows.data() + static_cast<std::size_t>(source_y % taps) * row_stride;
  };

  std::vector<double> v_accum(row_stride);
  for (std::int32_t y = 0; y < height; ++y) {
    report_filter_progress(progress, y, height, QObject::tr("Blurring pixels"));
    // Ensure horizontal rows up to y+radius exist (clamped); rows below
    // y-radius in the ring are stale but never read for this output row.
    const auto needed_through = std::min<std::int32_t>(height - 1, y + radius);
    while (h_rows_built_through < needed_through) {
      ++h_rows_built_through;
      build_h_row(h_rows_built_through,
                  h_rows.data() + static_cast<std::size_t>(h_rows_built_through % taps) * row_stride);
    }
    std::fill(v_accum.begin(), v_accum.end(), 0.0);
    for (int dy = -radius; dy <= radius; ++dy) {
      const auto sy = std::clamp<std::int32_t>(y + dy, 0, height - 1);
      const auto weight = axis_weights[static_cast<std::size_t>(dy + radius)];
      const auto* h_row = h_row_for(sy);
      for (std::size_t i = 0; i < row_stride; ++i) {
        v_accum[i] += h_row[i] * weight;
      }
    }
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* accum = v_accum.data() + static_cast<std::size_t>(x) * 4U;
      auto* dst = pixels.pixel(x, y);
      const auto alpha_sum = accum[3];
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto value = alpha_sum > 0.000001 ? accum[channel] / alpha_sum : 0.0;
        dst[channel] = filter_clamp_byte(value);
      }
      if (channels >= 4) {
        dst[3] = filter_clamp_byte(alpha_sum / total_weight * 255.0);
      }
    }
  }
  report_filter_progress(progress, height, height, QObject::tr("Blurring pixels"));
}

void apply_builtin_gaussian_blur_filter_pixels(PixelBuffer& pixels, const FilterProgress* progress) {
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::array<int, 5> kWeights = {1, 4, 6, 4, 1};
  const auto original = pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      FilterPixelAccum accum;
      for (int ky = -2; ky <= 2; ++ky) {
        const auto sy = std::clamp<std::int32_t>(y + ky, 0, pixels.height() - 1);
        for (int kx = -2; kx <= 2; ++kx) {
          const auto sx = std::clamp<std::int32_t>(x + kx, 0, pixels.width() - 1);
          filter_accumulate_pixel(accum, original, original.pixel(sx, sy),
                                  static_cast<double>(kWeights[static_cast<std::size_t>(kx + 2)] *
                                                      kWeights[static_cast<std::size_t>(ky + 2)]));
        }
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

std::uint32_t filter_coordinate_hash(std::int32_t x, std::int32_t y, std::uint16_t channel) noexcept {
  auto value = static_cast<std::uint32_t>(x + 1) * 73856093U;
  value ^= static_cast<std::uint32_t>(y + 1) * 19349663U;
  value ^= static_cast<std::uint32_t>(channel + 1) * 83492791U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

std::uint32_t filter_noise_hash(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept {
  auto value = static_cast<std::uint32_t>(x + 16384) * 374761393U;
  value ^= static_cast<std::uint32_t>(y + 8192) * 668265263U;
  value ^= seed * 2246822519U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

double filter_smooth_step(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3.0 - 2.0 * value);
}

double filter_lattice_noise(double x, double y, std::uint32_t seed) {
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto tx = filter_smooth_step(x - static_cast<double>(x0));
  const auto ty = filter_smooth_step(y - static_cast<double>(y0));
  const auto sample = [seed](std::int32_t sx, std::int32_t sy) {
    return static_cast<double>(filter_noise_hash(sx, sy, seed) & 0xffffU) / 65535.0;
  };
  const auto top = sample(x0, y0) * (1.0 - tx) + sample(x0 + 1, y0) * tx;
  const auto bottom = sample(x0, y0 + 1) * (1.0 - tx) + sample(x0 + 1, y0 + 1) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

double filter_cloud_noise(std::int32_t x, std::int32_t y, int scale, int detail, int contrast, int seed) {
  scale = std::clamp(scale, 12, 512);
  detail = std::clamp(detail, 1, 8);
  contrast = std::clamp(contrast, 0, 100);
  double value = 0.0;
  double amplitude = 1.0;
  double amplitude_sum = 0.0;
  double frequency = 1.0;
  for (int octave = 0; octave < detail; ++octave) {
    value += filter_lattice_noise((static_cast<double>(x) * frequency) / static_cast<double>(scale),
                                  (static_cast<double>(y) * frequency) / static_cast<double>(scale),
                                  static_cast<std::uint32_t>(seed + octave * 101)) *
             amplitude;
    amplitude_sum += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  value /= std::max(0.0001, amplitude_sum);
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 65.0;
  return std::clamp((value - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
}

void apply_clouds_to_pixels(PixelBuffer& pixels, QColor foreground, QColor background, int scale, int detail,
                            int contrast, int seed, const FilterProgress* progress) {
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Generating clouds"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto amount = filter_cloud_noise(x, y, scale, detail, contrast, seed);
      auto* px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<double>(background.red()) * (1.0 - amount) +
                                static_cast<double>(foreground.red()) * amount);
      px[1] = filter_clamp_byte(static_cast<double>(background.green()) * (1.0 - amount) +
                                static_cast<double>(foreground.green()) * amount);
      px[2] = filter_clamp_byte(static_cast<double>(background.blue()) * (1.0 - amount) +
                                static_cast<double>(foreground.blue()) * amount);
      if (pixels.format().channels >= 4) {
        px[3] = 255;
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Generating clouds"));
}

void apply_twirl_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int radius_percent,
                           const FilterProgress* progress) {
  const auto channels = pixels.format().channels;
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto radius = std::max(1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) * 0.5 *
                                        static_cast<double>(std::clamp(radius_percent, 1, 100)) / 100.0);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -720, 720)) * 3.14159265358979323846 / 180.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Twisting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance > radius) {
        continue;
      }
      const auto falloff = 1.0 - distance / radius;
      const auto source_angle = std::atan2(dy, dx) - angle * falloff * falloff;
      const auto source_x = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_x + std::cos(source_angle) * distance)), 0,
          pixels.width() - 1);
      const auto source_y = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_y + std::sin(source_angle) * distance)), 0,
          pixels.height() - 1);
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(source_x, source_y);
      std::copy(src, src + channels, dst);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Twisting pixels"));
}

double filter_sampled_luminance(const PixelBuffer& pixels, double x, double y) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(pixels.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(pixels.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  const auto l00 = static_cast<double>(filter_luminance(pixels.pixel(x0, y0)));
  const auto l10 = static_cast<double>(filter_luminance(pixels.pixel(x1, y0)));
  const auto l01 = static_cast<double>(filter_luminance(pixels.pixel(x0, y1)));
  const auto l11 = static_cast<double>(filter_luminance(pixels.pixel(x1, y1)));
  const auto top = l00 * (1.0 - tx) + l10 * tx;
  const auto bottom = l01 * (1.0 - tx) + l11 * tx;
  return top * (1.0 - ty) + bottom * ty;
}

void apply_emboss_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int height,
                            int amount, const FilterProgress* progress) {
  const auto angle = static_cast<double>(angle_degrees) * kFilterPi / 180.0;
  const auto distance = static_cast<double>(std::clamp(height, 1, 24));
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  amount = std::clamp(amount, 0, 300);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Embossing pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto highlight = filter_sampled_luminance(original, static_cast<double>(x) - offset_x,
                                                      static_cast<double>(y) - offset_y);
      const auto shadow = filter_sampled_luminance(original, static_cast<double>(x) + offset_x,
                                                   static_cast<double>(y) + offset_y);
      const auto value = filter_clamp_byte(128.0 + (highlight - shadow) * static_cast<double>(amount) / 100.0);
      auto* px = pixels.pixel(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Embossing pixels"));
}

void apply_unsharp_mask_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int radius,
                                  int threshold, const FilterProgress* progress) {
  amount = std::clamp(amount, 0, 300);
  radius = std::clamp(radius, 1, 12);
  threshold = std::clamp(threshold, 0, 255);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  const auto total = pixels.height() * 2;
  auto blurred = original;
  // The separable blur is far faster than the sharpen loop below; report it as
  // the first half in one coarse tick so progress stays monotonic.
  report_filter_progress(progress, 0, total, QObject::tr("Blurring pixels"));
  apply_separable_tent_blur(blurred, original, radius, true, nullptr);
  report_filter_progress(progress, pixels.height(), total, QObject::tr("Blurring pixels"));

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, pixels.height() + y, total, QObject::tr("Sharpening pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      const auto* soft = blurred.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        const auto detail = static_cast<int>(src[channel]) - static_cast<int>(soft[channel]);
        dst[channel] = std::abs(detail) < threshold
                           ? src[channel]
                           : filter_clamp_byte(static_cast<int>(src[channel]) + (detail * amount) / 100);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, total, total, QObject::tr("Sharpening pixels"));
}

void apply_motion_blur_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int distance,
                                 const FilterProgress* progress) {
  distance = std::clamp(distance, 1, 64);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -180, 180)) * kFilterPi / 180.0;
  const auto step_x = std::cos(angle);
  const auto step_y = -std::sin(angle);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      FilterPixelAccum accum;
      for (int sample = -distance; sample <= distance; ++sample) {
        filter_accumulate_sample(accum, original, static_cast<double>(x) + step_x * static_cast<double>(sample),
                                 static_cast<double>(y) + step_y * static_cast<double>(sample));
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

void apply_radial_blur_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int samples,
                                 const FilterProgress* progress) {
  amount = std::clamp(amount, 0, 100);
  samples = std::clamp(samples, 4, 32);
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto sweep = static_cast<double>(amount) * 3.6 * kFilterPi / 180.0;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      FilterPixelAccum accum;
      for (int sample = 0; sample < samples; ++sample) {
        const auto t = samples <= 1 ? 0.0 : static_cast<double>(sample) / static_cast<double>(samples - 1) - 0.5;
        const auto angle = sweep * t;
        const auto source_x = center_x + dx * std::cos(angle) - dy * std::sin(angle);
        const auto source_y = center_y + dx * std::sin(angle) + dy * std::cos(angle);
        filter_accumulate_sample(accum, original, source_x, source_y);
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

void apply_wave_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amplitude, int wavelength, int phase,
                          const FilterProgress* progress) {
  amplitude = std::clamp(amplitude, 0, 64);
  wavelength = std::clamp(wavelength, 4, 256);
  const auto phase_radians = static_cast<double>(std::clamp(phase, 0, 360)) * kFilterPi / 180.0;
  const auto frequency = 2.0 * kFilterPi / static_cast<double>(wavelength);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Distorting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto source_x = static_cast<double>(x) +
                            std::sin(static_cast<double>(y) * frequency + phase_radians) *
                                static_cast<double>(amplitude);
      const auto source_y = static_cast<double>(y) +
                            std::sin(static_cast<double>(x) * frequency + phase_radians + kFilterPi * 0.5) *
                                static_cast<double>(amplitude) * 0.5;
      filter_copy_sampled_pixel(pixels, original, x, y, source_x, source_y);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Distorting pixels"));
}

void apply_pinch_bloat_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int radius_percent,
                                 const FilterProgress* progress) {
  amount = std::clamp(amount, -100, 100);
  radius_percent = std::clamp(radius_percent, 1, 100);
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto radius = std::max(1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) * 0.5 *
                                        static_cast<double>(radius_percent) / 100.0);
  const auto strength = static_cast<double>(amount) / 100.0;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Distorting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance <= 0.0001 || distance > radius) {
        continue;
      }
      const auto normalized = distance / radius;
      const auto falloff = (1.0 - normalized) * (1.0 - normalized);
      const auto source_distance = std::clamp(distance * (1.0 - strength * falloff * 0.75), 0.0, radius);
      const auto scale = source_distance / distance;
      filter_copy_sampled_pixel(pixels, original, x, y, center_x + dx * scale, center_y + dy * scale);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Distorting pixels"));
}

void apply_glowing_edges_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int edge_width, int brightness,
                                   int smoothness, const FilterProgress* progress) {
  edge_width = std::clamp(edge_width, 1, 12);
  brightness = std::clamp(brightness, 0, 300);
  smoothness = std::clamp(smoothness, 0, 12);
  auto source = original;
  const auto color_channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  const auto total = pixels.height() * (smoothness > 0 ? 2 : 1);
  int progress_offset = 0;

  if (smoothness > 0) {
    report_filter_progress(progress, 0, total, QObject::tr("Blurring pixels"));
    apply_separable_tent_blur(source, original, smoothness, true, nullptr);
    report_filter_progress(progress, pixels.height(), total, QObject::tr("Blurring pixels"));
    progress_offset = pixels.height();
  }

  const auto luminance_at = [&source](std::int32_t x, std::int32_t y) {
    x = std::clamp<std::int32_t>(x, 0, source.width() - 1);
    y = std::clamp<std::int32_t>(y, 0, source.height() - 1);
    return filter_luminance(source.pixel(x, y));
  };
  constexpr std::array<int, 9> kSobelX = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
  constexpr std::array<int, 9> kSobelY = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, progress_offset + y, total, QObject::tr("Detecting edges"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      int gx = 0;
      int gy = 0;
      int index = 0;
      for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
          const auto luminance = luminance_at(x + kx, y + ky);
          gx += luminance * kSobelX[static_cast<std::size_t>(index)];
          gy += luminance * kSobelY[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const auto magnitude = std::sqrt(static_cast<double>(gx * gx + gy * gy));
      const auto glow = filter_clamp_byte(magnitude * static_cast<double>(brightness) / 100.0 *
                                          (0.75 + static_cast<double>(edge_width) * 0.25));
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto colorize = 0.35 + 0.65 * static_cast<double>(src[channel]) / 255.0;
        dst[channel] = filter_clamp_byte(static_cast<double>(glow) * colorize);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, total, total, QObject::tr("Detecting edges"));
}

double halftone_cell_offset(double value, double cell_size) {
  auto offset = std::fmod(value, cell_size);
  if (offset < 0.0) {
    offset += cell_size;
  }
  return offset - cell_size * 0.5;
}

void apply_color_halftone_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int cell_size, int intensity,
                                    int contrast, const FilterProgress* progress) {
  cell_size = std::clamp(cell_size, 4, 64);
  intensity = std::clamp(intensity, 0, 100);
  contrast = std::clamp(contrast, 0, 100);
  constexpr std::array<double, 3> kAngles = {15.0, 75.0, 0.0};
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 50.0;
  const auto cell = static_cast<double>(cell_size);
  const auto color_channels = std::min<std::uint16_t>(pixels.format().channels, 3);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Rendering halftone"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto angle = kAngles[static_cast<std::size_t>(channel)] * kFilterPi / 180.0;
        const auto rotated_x = static_cast<double>(x) * std::cos(angle) + static_cast<double>(y) * std::sin(angle);
        const auto rotated_y = -static_cast<double>(x) * std::sin(angle) + static_cast<double>(y) * std::cos(angle);
        const auto local_x = halftone_cell_offset(rotated_x, cell);
        const auto local_y = halftone_cell_offset(rotated_y, cell);
        auto coverage = 1.0 - static_cast<double>(src[channel]) / 255.0;
        coverage = std::clamp((coverage - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
        const auto radius = std::sqrt(coverage) * cell * 0.48;
        const auto distance = std::sqrt(local_x * local_x + local_y * local_y);
        const auto dot = std::clamp(radius - distance + 1.0, 0.0, 1.0);
        const auto screen = filter_clamp_byte(255.0 * (1.0 - dot));
        dst[channel] = filter_blend_byte(src[channel], screen, intensity);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Rendering halftone"));
}

void apply_filter_with_settings(const QString& identifier, const FilterRegistry& registry, PixelBuffer& pixels,
                                const std::vector<int>& values, QColor foreground, QColor background,
                                const FilterProgress* progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Filter previews support UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    return;
  }

  const auto original = pixels;
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);

  if (identifier == QStringLiteral("patchy.filters.invert")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.brightness_contrast")) {
    const auto brightness = std::clamp(filter_value(values, 0, 0), -100, 100);
    const auto contrast = std::clamp(filter_value(values, 1, 0), -100, 100);
    const auto factor = 1.0 + static_cast<double>(contrast) / 100.0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] =
              filter_clamp_byte((static_cast<double>(px[channel]) - 128.0) * factor + 128.0 + brightness);
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.grayscale") ||
      identifier == QStringLiteral("patchy.filters.desaturate")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto luminance = filter_clamp_byte(filter_luminance(px));
        px[0] = luminance;
        px[1] = luminance;
        px[2] = luminance;
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    const auto amount = filter_value(values, 0, 100);
    std::array<int, 3> min_channel = {255, 255, 255};
    std::array<int, 3> max_channel = {0, 0, 0};
    const auto total = pixels.height() * 3;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, total, QObject::tr("Filtering pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          min_channel[static_cast<std::size_t>(channel)] =
              std::min(min_channel[static_cast<std::size_t>(channel)], static_cast<int>(px[channel]));
          max_channel[static_cast<std::size_t>(channel)] =
              std::max(max_channel[static_cast<std::size_t>(channel)], static_cast<int>(px[channel]));
        }
      }
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, pixels.height() + y, total, QObject::tr("Filtering pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto index = static_cast<std::size_t>(channel);
          const auto range = max_channel[index] - min_channel[index];
          if (range > 0) {
            px[channel] = filter_clamp_byte(((static_cast<int>(px[channel]) - min_channel[index]) * 255) / range);
          }
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height() * 2, total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    constexpr int kPhaseCount = 6;
    const auto amount = filter_value(values, 0, 100);
    auto glow = pixels;
    auto blur_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_builtin_gaussian_blur_filter_pixels(glow, &blur_progress);
    auto glow_tint_progress = filter_progress_phase(progress, 1, kPhaseCount);
    tint_filter_pixels(glow, 26, 14, -4, &glow_tint_progress);
    auto glow_contrast_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(glow, 0.9, 128, &glow_contrast_progress);
    auto blend_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_overlay_filter_pixels(pixels, glow, 38, &blend_progress);
    auto tint_progress = filter_progress_phase(progress, 4, kPhaseCount);
    tint_filter_pixels(pixels, 8, 4, 0, &tint_progress);
    auto amount_progress = filter_progress_phase(progress, 5, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.punchy_color")) {
    constexpr int kPhaseCount = 4;
    const auto amount = filter_value(values, 0, 100);
    auto contrast_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.28, 128, &contrast_progress);
    auto saturation_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 1.26, &saturation_progress);
    auto sharpen_progress = filter_progress_phase(progress, 2, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.sharpen"), registry, pixels, {100}, foreground,
                               background, &sharpen_progress);
    auto amount_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.noir")) {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(values, 0, 100);
    auto grain_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.film_grain"), registry, pixels, {50}, foreground,
                               background, &grain_progress);
    auto grayscale_progress = filter_progress_phase(progress, 1, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.grayscale"), registry, pixels, {100}, foreground,
                               background, &grayscale_progress);
    auto contrast_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.55, 128, &contrast_progress);
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.vignette"), registry, pixels, {55}, foreground,
                               background, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(values, 0, 100);
    auto saturation_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.82, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.92, 128, &contrast_progress);
    auto matte_progress = filter_progress_phase(progress, 2, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&matte_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto luminance = filter_luminance(px);
        const auto shadow = std::clamp(160 - luminance, 0, 160);
        const auto highlight = std::clamp(luminance - 96, 0, 159);
        px[0] = filter_clamp_byte(static_cast<int>(px[0]) + 18 - shadow / 14 + highlight / 18);
        px[1] = filter_clamp_byte(static_cast<int>(px[1]) + 12 + shadow / 18 + highlight / 30);
        px[2] = filter_clamp_byte(static_cast<int>(px[2]) + 18 + shadow / 11 - highlight / 24);
      }
    }
    finish_filter_row_progress(&matte_progress, pixels.height());
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.vignette"), registry, pixels, {55}, foreground,
                               background, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    constexpr int kPhaseCount = 7;
    const auto amount = filter_value(values, 0, 100);
    const auto effect_original = pixels;
    auto tinted = pixels;
    auto sepia_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.sepia"), registry, tinted, {100}, foreground, background,
                               &sepia_progress);
    auto tint_blend_progress = filter_progress_phase(progress, 1, kPhaseCount);
    blend_overlay_filter_pixels(pixels, tinted, 45, &tint_blend_progress);
    auto saturation_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.72, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 3, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.86, 112, &contrast_progress);
    auto fade_progress = filter_progress_phase(progress, 4, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&fade_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto* src = effect_original.pixel(x, y);
        for (std::uint16_t channel = 0; channel < 3; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + 16);
          px[channel] = filter_blend_byte(px[channel], src[channel], 12);
        }
      }
    }
    finish_filter_row_progress(&fade_progress, pixels.height());
    auto grain_progress = filter_progress_phase(progress, 5, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.film_grain"), registry, pixels, {50}, foreground,
                               background, &grain_progress);
    auto amount_progress = filter_progress_phase(progress, 6, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto r = static_cast<int>(px[0]);
        const auto g = static_cast<int>(px[1]);
        const auto b = static_cast<int>(px[2]);
        px[0] = filter_clamp_byte((r * 393 + g * 769 + b * 189) / 1000);
        px[1] = filter_clamp_byte((r * 349 + g * 686 + b * 168) / 1000);
        px[2] = filter_clamp_byte((r * 272 + g * 534 + b * 131) / 1000);
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    const auto threshold = std::clamp(filter_value(values, 0, 128), 0, 255);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto value = filter_luminance(px) >= threshold ? 255 : 0;
        px[0] = static_cast<std::uint8_t>(value);
        px[1] = static_cast<std::uint8_t>(value);
        px[2] = static_cast<std::uint8_t>(value);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    const auto levels = std::clamp(filter_value(values, 0, 4), 2, 16);
    const auto denominator = std::max(1, levels - 1);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto bucket = static_cast<int>(std::round(static_cast<double>(px[channel]) * denominator / 255.0));
          px[channel] = filter_clamp_byte(std::round(static_cast<double>(bucket) * 255.0 / denominator));
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.box_blur") ||
      identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    const auto radius = std::clamp(filter_value(values, 0,
                                               identifier == QStringLiteral("patchy.filters.gaussian_blur") ? 2 : 1),
                                   1, 12);
    const auto weighted = identifier == QStringLiteral("patchy.filters.gaussian_blur");
    apply_separable_tent_blur(pixels, original, radius, weighted, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    const auto amount = std::clamp(filter_value(values, 0, 100), 0, 300);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Sharpening pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* dst = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto center = static_cast<int>(original.pixel(x, y)[channel]) * 5;
          const auto left = x > 0 ? static_cast<int>(original.pixel(x - 1, y)[channel])
                                  : static_cast<int>(original.pixel(x, y)[channel]);
          const auto right = x + 1 < pixels.width() ? static_cast<int>(original.pixel(x + 1, y)[channel])
                                                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto up = y > 0 ? static_cast<int>(original.pixel(x, y - 1)[channel])
                                : static_cast<int>(original.pixel(x, y)[channel]);
          const auto down = y + 1 < pixels.height() ? static_cast<int>(original.pixel(x, y + 1)[channel])
                                                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto sharpened = center - left - right - up - down;
          dst[channel] = filter_clamp_byte(static_cast<int>(original.pixel(x, y)[channel]) +
                                           ((sharpened - static_cast<int>(original.pixel(x, y)[channel])) * amount) /
                                               100);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Sharpening pixels"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    const auto amount = std::clamp(filter_value(values, 0, 150), 0, 300);
    const auto radius = std::clamp(filter_value(values, 1, 2), 1, 12);
    const auto threshold = std::clamp(filter_value(values, 2, 8), 0, 255);
    apply_unsharp_mask_to_pixels(pixels, original, amount, radius, threshold, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    const auto angle = std::clamp(filter_value(values, 0, 0), -180, 180);
    const auto distance = std::clamp(filter_value(values, 1, 12), 1, 64);
    apply_motion_blur_to_pixels(pixels, original, angle, distance, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    const auto amount = std::clamp(filter_value(values, 0, 35), 0, 100);
    const auto samples = std::clamp(filter_value(values, 1, 16), 4, 32);
    apply_radial_blur_to_pixels(pixels, original, amount, samples, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    constexpr std::array<int, 9> sobel_x = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    constexpr std::array<int, 9> sobel_y = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    const auto strength = std::clamp(filter_value(values, 0, 100), 0, 300);
    const auto luminance_at = [&original](std::int32_t x, std::int32_t y) {
      x = std::clamp<std::int32_t>(x, 0, original.width() - 1);
      y = std::clamp<std::int32_t>(y, 0, original.height() - 1);
      return filter_luminance(original.pixel(x, y));
    };
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Detecting edges"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        int gx = 0;
        int gy = 0;
        int index = 0;
        for (int ky = -1; ky <= 1; ++ky) {
          for (int kx = -1; kx <= 1; ++kx) {
            const auto luminance = luminance_at(x + kx, y + ky);
            gx += luminance * sobel_x[static_cast<std::size_t>(index)];
            gy += luminance * sobel_y[static_cast<std::size_t>(index)];
            ++index;
          }
        }
        const auto magnitude = filter_clamp_byte(std::sqrt(gx * gx + gy * gy) * static_cast<double>(strength) / 100.0);
        auto* px = pixels.pixel(x, y);
        px[0] = magnitude;
        px[1] = magnitude;
        px[2] = magnitude;
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Detecting edges"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    const auto edge_width = std::clamp(filter_value(values, 0, 2), 1, 12);
    const auto brightness = std::clamp(filter_value(values, 1, 140), 0, 300);
    const auto smoothness = std::clamp(filter_value(values, 2, 2), 0, 12);
    apply_glowing_edges_to_pixels(pixels, original, edge_width, brightness, smoothness, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    const auto angle = std::clamp(filter_value(values, 0, 135), -180, 180);
    const auto height = std::clamp(filter_value(values, 1, 2), 1, 24);
    const auto amount = std::clamp(filter_value(values, 2, 100), 0, 300);
    apply_emboss_to_pixels(pixels, original, angle, height, amount, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    const auto angle = std::clamp(filter_value(values, 0, 180), -720, 720);
    const auto radius = std::clamp(filter_value(values, 1, 100), 1, 100);
    apply_twirl_to_pixels(pixels, original, angle, radius, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.wave")) {
    const auto amplitude = std::clamp(filter_value(values, 0, 12), 0, 64);
    const auto wavelength = std::clamp(filter_value(values, 1, 48), 4, 256);
    const auto phase = std::clamp(filter_value(values, 2, 0), 0, 360);
    apply_wave_to_pixels(pixels, original, amplitude, wavelength, phase, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    const auto amount = std::clamp(filter_value(values, 0, 35), -100, 100);
    const auto radius = std::clamp(filter_value(values, 1, 100), 1, 100);
    apply_pinch_bloat_to_pixels(pixels, original, amount, radius, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    const auto scale = std::clamp(filter_value(values, 0, 96), 12, 512);
    const auto detail = std::clamp(filter_value(values, 1, 6), 1, 8);
    const auto contrast = std::clamp(filter_value(values, 2, 40), 0, 100);
    const auto seed = std::clamp(filter_value(values, 3, 1), 1, 9999);
    apply_clouds_to_pixels(pixels, foreground, background, scale, detail, contrast, seed, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    const auto block_size = std::clamp(filter_value(values, 0, 4), 2, 32);
    for (std::int32_t block_y = 0; block_y < pixels.height(); block_y += block_size) {
      report_filter_progress(progress, block_y, pixels.height(), QObject::tr("Pixelating blocks"));
      for (std::int32_t block_x = 0; block_x < pixels.width(); block_x += block_size) {
        const auto block_width = std::min(block_size, pixels.width() - block_x);
        const auto block_height = std::min(block_size, pixels.height() - block_y);
        FilterPixelAccum accum;
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_accumulate_pixel(accum, original, original.pixel(x, y), 1.0);
          }
        }
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_write_accumulated_pixel(pixels, x, y, accum);
          }
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Pixelating blocks"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    const auto cell_size = std::clamp(filter_value(values, 0, 10), 4, 64);
    const auto intensity = std::clamp(filter_value(values, 1, 75), 0, 100);
    const auto contrast = std::clamp(filter_value(values, 2, 60), 0, 100);
    apply_color_halftone_to_pixels(pixels, original, cell_size, intensity, contrast, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    const auto amount = std::clamp(filter_value(values, 0, 50), 0, 100);
    const auto amplitude = static_cast<int>(std::round(static_cast<double>(amount) * 0.3));
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Adding grain"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto span = amplitude * 2 + 1;
          const auto grain = span <= 1 ? 0
                                       : static_cast<int>(filter_coordinate_hash(x, y, channel) %
                                                          static_cast<std::uint32_t>(span)) -
                                             amplitude;
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + grain);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Adding grain"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    const auto strength = std::clamp(filter_value(values, 0, 55), 0, 100);
    const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
    const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
    const auto max_distance = std::sqrt(center_x * center_x + center_y * center_y);
    if (max_distance <= 0.0) {
      return;
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Applying vignette"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto dx = static_cast<double>(x) - center_x;
        const auto dy = static_cast<double>(y) - center_y;
        const auto distance = std::sqrt(dx * dx + dy * dy) / max_distance;
        const auto darken = 1.0 - (static_cast<double>(strength) / 100.0) * std::clamp(distance * distance, 0.0, 1.0);
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<double>(px[channel]) * darken);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Applying vignette"));
    return;
  }

  report_filter_progress(progress, 0, 1, QObject::tr("Filtering pixels"));
  registry.apply(identifier.toStdString(), pixels);
  report_filter_progress(progress, 1, 1, QObject::tr("Filtering pixels"));
  blend_filter_with_original(pixels, original, filter_value(values, 0, 100), progress);
}

void restore_pixels_outside_selection(PixelBuffer& pixels, const PixelBuffer& original, const QRegion& selection,
                                      Rect bounds) {
  if (selection.isEmpty() || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  if (selected.isEmpty()) {
    pixels = original;
    return;
  }

  auto filtered = std::move(pixels);
  pixels = original;
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    const auto row_bytes = static_cast<std::size_t>(std::max(0, local_right - local_left)) * pixel_bytes;
    if (row_bytes == 0U) {
      continue;
    }
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      const auto* src = filtered.pixel(local_left, y);
      auto* dst = pixels.pixel(local_left, y);
      std::copy(src, src + row_bytes, dst);
    }
  }
}

namespace {

// Blur-family filters translate opaque content outward, so they need transparent
// margin around the layer to bleed into. Returns how many pixels of transparent
// padding to add on every side before filtering (0 for non-spreading filters).
int spreading_filter_margin(const QString& identifier, const std::vector<int>& values, std::int32_t width,
                            std::int32_t height) {
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return std::clamp(filter_value(values, 0, 1), 1, 12);
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return std::clamp(filter_value(values, 0, 2), 1, 12);
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    return std::clamp(filter_value(values, 1, 12), 1, 64);
  }
  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    const auto amount = std::clamp(filter_value(values, 0, 35), 0, 100);
    if (amount <= 0) {
      return 0;
    }
    // Spinning the layer sweeps its corners around the centre; the farthest a
    // corner can travel is bounded by the circumscribed circle (half-diagonal).
    const auto diagonal = std::sqrt(static_cast<double>(width) * static_cast<double>(width) +
                                    static_cast<double>(height) * static_cast<double>(height));
    const auto reach = (diagonal - static_cast<double>(std::min(width, height))) * 0.5 *
                       static_cast<double>(amount) / 100.0;
    return std::clamp(static_cast<int>(std::ceil(reach)), 0, 256);
  }
  return 0;
}

// Kernel halo in pixels for filters whose output at a pixel depends only on
// inputs within a fixed translation-invariant distance. nullopt for filters
// that read absolute coordinates (clouds/grain noise hashes, pixelate's grid,
// halftone) or the buffer centre (vignette, twirl, pinch, radial blur) - those
// must always see the whole layer or their output would shift with the crop.
std::optional<int> translation_invariant_filter_support(const QString& identifier, const std::vector<int>& values) {
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return std::clamp(filter_value(values, 0, 1), 1, 12);
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return std::clamp(filter_value(values, 0, 2), 1, 12);
  }
  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    return std::clamp(filter_value(values, 1, 2), 1, 12);
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    // Bilinear sampling reads one pixel past the farthest sample.
    return std::clamp(filter_value(values, 1, 12), 1, 64) + 1;
  }
  if (identifier == QStringLiteral("patchy.filters.sharpen") ||
      identifier == QStringLiteral("patchy.filters.edge_detect")) {
    return 1;
  }
  return std::nullopt;
}

PixelBuffer copy_buffer_rect(const PixelBuffer& src, Rect rect) {
  PixelBuffer out(rect.width, rect.height, src.format());
  const auto row_bytes = static_cast<std::size_t>(rect.width) * bytes_per_pixel(src.format());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    const auto* source_row = src.pixel(rect.x, rect.y + y);
    auto* dest_row = out.pixel(0, y);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
  return out;
}

void blit_buffer_rect(PixelBuffer& dst, const PixelBuffer& src, std::int32_t dst_x, std::int32_t dst_y) {
  const auto row_bytes = static_cast<std::size_t>(src.width()) * bytes_per_pixel(src.format());
  for (std::int32_t y = 0; y < src.height(); ++y) {
    const auto* source_row = src.pixel(0, y);
    auto* dest_row = dst.pixel(dst_x, dst_y + y);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
}

// Returns a copy of src grown by margin on every side, with the new border left
// fully transparent (the constructor zero-fills, which is RGBA(0,0,0,0)).
PixelBuffer pad_buffer_transparent(const PixelBuffer& src, int margin) {
  PixelBuffer padded(src.width() + margin * 2, src.height() + margin * 2, src.format());
  padded.clear(0);
  const auto row_bytes = static_cast<std::size_t>(src.width()) * bytes_per_pixel(src.format());
  for (std::int32_t y = 0; y < src.height(); ++y) {
    const auto* source_row = src.pixel(0, y);
    auto* dest_row = padded.pixel(margin, y + margin);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
  return padded;
}

// Crops fully-transparent rows/columns off the borders of buffer (matching how
// Photoshop trims a layer back to its content after a filter), returning the
// document-space bounds of the cropped result. buffer is replaced in place.
Rect trim_transparent_border(PixelBuffer& buffer, Rect bounds) {
  if (buffer.format().channels < 4 || buffer.empty()) {
    return bounds;
  }
  std::int32_t min_x = buffer.width();
  std::int32_t min_y = buffer.height();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < buffer.height(); ++y) {
    for (std::int32_t x = 0; x < buffer.width(); ++x) {
      if (buffer.pixel(x, y)[3] != 0) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return bounds;  // Fully transparent: leave as-is rather than collapse to nothing.
  }
  if (min_x == 0 && min_y == 0 && max_x == buffer.width() - 1 && max_y == buffer.height() - 1) {
    return bounds;  // Nothing to trim.
  }
  const auto new_width = max_x - min_x + 1;
  const auto new_height = max_y - min_y + 1;
  PixelBuffer cropped(new_width, new_height, buffer.format());
  const auto row_bytes = static_cast<std::size_t>(new_width) * bytes_per_pixel(buffer.format());
  for (std::int32_t y = 0; y < new_height; ++y) {
    const auto* source_row = buffer.pixel(min_x, min_y + y);
    auto* dest_row = cropped.pixel(0, y);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
  buffer = std::move(cropped);
  return Rect{bounds.x + min_x, bounds.y + min_y, new_width, new_height};
}

}  // namespace

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                        const QString& identifier, const FilterRegistry& registry,
                                        const FilterPreviewSettings& settings, QColor foreground, QColor background,
                                        const FilterProgress* progress, Rect* result_bounds) {
  if (result_bounds != nullptr) {
    *result_bounds = bounds;
  }
  auto pixels = original;
  if (!settings.preview_enabled) {
    return pixels;
  }
  if (!selection.isEmpty() && layer_selection_region(selection, bounds).isEmpty()) {
    return pixels;
  }

  // With no active selection, blur-family filters grow the layer so their result
  // can fade into the surrounding transparency instead of being clamped to a hard
  // edge at the layer's bounding box (the way Photoshop expands the layer). With a
  // selection the filter stays confined to it, so the layer keeps its bounds.
  const auto margin = selection.isEmpty() && original.format().channels >= 4
                          ? spreading_filter_margin(identifier, settings.values, original.width(), original.height())
                          : 0;
  if (margin > 0) {
    auto padded = pad_buffer_transparent(original, margin);
    apply_filter_with_settings(identifier, registry, padded, settings.values, foreground, background, progress);
    const Rect grown{bounds.x - margin, bounds.y - margin, padded.width(), padded.height()};
    const auto trimmed = trim_transparent_border(padded, grown);
    if (result_bounds != nullptr) {
      *result_bounds = trimmed;
    }
    return padded;
  }

  // A selection confines the visible result to its bounds, so for filters with
  // a known translation-invariant kernel support only the selection's bounding
  // box plus that halo needs filtering: every kernel tap for a selected pixel
  // lands inside the window, so the selected output is identical to filtering
  // the whole layer (edge clamping only differs outside the halo).
  if (!selection.isEmpty()) {
    if (const auto support = translation_invariant_filter_support(identifier, settings.values);
        support.has_value()) {
      const auto selected = layer_selection_region(selection, bounds);
      const QRect layer_rect(bounds.x, bounds.y, bounds.width, bounds.height);
      const auto work =
          selected.boundingRect().adjusted(-*support, -*support, *support, *support).intersected(layer_rect);
      if (!work.isEmpty() && work != layer_rect) {
        const Rect work_local{work.x() - bounds.x, work.y() - bounds.y, work.width(), work.height()};
        auto window = copy_buffer_rect(original, work_local);
        apply_filter_with_settings(identifier, registry, window, settings.values, foreground, background, progress);
        blit_buffer_rect(pixels, window, work_local.x, work_local.y);
        restore_pixels_outside_selection(pixels, original, selection, bounds);
        return pixels;
      }
    }
  }

  apply_filter_with_settings(identifier, registry, pixels, settings.values, foreground, background, progress);
  restore_pixels_outside_selection(pixels, original, selection, bounds);
  return pixels;
}

bool pixel_buffers_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.format() == rhs.format() && lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.data().size() == rhs.data().size() && std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

bool editable_rgb8_layer(const Layer* layer) {
  return layer != nullptr && layer->kind() == LayerKind::Pixel && layer->pixels().format().bit_depth == BitDepth::UInt8 &&
         layer->pixels().format().channels >= 3;
}

void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings,
                            const FilterProgress* progress) {
  settings = clamp_levels_settings(settings);
  const auto master = levels_master_record(settings);

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = map_levels_value(px[0], master);
    px[1] = map_levels_value(px[1], master);
    px[2] = map_levels_value(px[2], master);
    px[0] = map_levels_value(px[0], settings.red);
    px[1] = map_levels_value(px[1], settings.green);
    px[2] = map_levels_value(px[2], settings.blue);
  });
}

void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings,
                            const FilterProgress* progress) {
  const auto lut = build_curves_lut(settings);

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = lut.red[px[0]];
    px[1] = lut.green[px[1]];
    px[2] = lut.blue[px[2]];
  });
}

void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings, const FilterProgress* progress) {
  if (settings.colorize) {
    // Route through the core math so the destructive filter matches an
    // adjustment layer with identical settings exactly.
    AdjustmentSettings adjustment;
    adjustment.kind = AdjustmentKind::HueSaturation;
    adjustment.hue_saturation = to_hue_saturation_adjustment(settings);
    const auto channels = pixels.format().channels;
    for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
      auto* px = pixels.pixel(x, y);
      const auto adjusted = apply_adjustment_to_color(RgbColor{px[0], px[1], px[2]}, adjustment);
      px[0] = adjusted.red;
      px[1] = adjusted.green;
      px[2] = adjusted.blue;
      static_cast<void>(channels);  // alpha (px[3]) is left untouched
    });
    return;
  }
  settings.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  const auto channels = pixels.format().channels;
  const auto saturation_offset =
      static_cast<int>(std::round(static_cast<double>(settings.saturation_delta) * 255.0 / 100.0));
  const auto lightness_offset =
      static_cast<int>(std::round(static_cast<double>(settings.lightness_delta) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    QColor color(px[0], px[1], px[2]);
    const auto original_alpha = channels >= 4 ? px[3] : 255;
    auto hue = color.hslHue();
    if (hue < 0) {
      hue = 0;
    }
    hue = (hue + settings.hue_shift) % 360;
    if (hue < 0) {
      hue += 360;
    }
    const auto saturation = std::clamp(color.hslSaturation() + saturation_offset, 0, 255);
    const auto lightness = std::clamp(color.lightness() + lightness_offset, 0, 255);
    const auto adjusted = QColor::fromHsl(hue, saturation, lightness);
    px[0] = static_cast<std::uint8_t>(adjusted.red());
    px[1] = static_cast<std::uint8_t>(adjusted.green());
    px[2] = static_cast<std::uint8_t>(adjusted.blue());
    if (channels >= 4) {
      px[3] = original_alpha;
    }
  });
}

void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings, const FilterProgress* progress) {
  settings.cyan_red = std::clamp(settings.cyan_red, -100, 100);
  settings.magenta_green = std::clamp(settings.magenta_green, -100, 100);
  settings.yellow_blue = std::clamp(settings.yellow_blue, -100, 100);
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(settings.cyan_red) * 255.0 / 100.0));
  const auto green_delta =
      static_cast<int>(std::round(static_cast<double>(settings.magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(settings.yellow_blue) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red_delta);
    px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green_delta);
    px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue_delta);
  });
}


std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed, LevelsSettings initial,
    const PixelBuffer* histogram_source) {
  initial = clamp_levels_settings(initial);
  auto histogram_for_channel = [histogram_source](LevelsChannel channel) {
    return levels_histogram_from_pixels(histogram_source, channel);
  };

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyLevelsDialog"));
  dialog.setWindowTitle(QObject::tr("Levels"));
  auto* root = new QHBoxLayout(&dialog);

  auto* controls = new QVBoxLayout();
  controls->setSpacing(6);
  root->addLayout(controls, 1);

  auto* preset_row = new QHBoxLayout();
  preset_row->addWidget(new QLabel(QObject::tr("Preset:"), &dialog));
  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("levelsPresetCombo"));
  preset->addItem(QObject::tr("Default"));
  preset_row->addWidget(preset, 1);
  controls->addLayout(preset_row);

  auto* channel_row = new QHBoxLayout();
  channel_row->addSpacing(20);
  channel_row->addWidget(new QLabel(QObject::tr("Channel:"), &dialog));
  auto* channel = new QComboBox(&dialog);
  channel->setObjectName(QStringLiteral("levelsChannelCombo"));
  channel->addItem(QObject::tr("RGB"));
  channel->addItem(QObject::tr("Red"));
  channel->addItem(QObject::tr("Green"));
  channel->addItem(QObject::tr("Blue"));
  channel_row->addWidget(channel, 1);
  controls->addLayout(channel_row);

  auto* input_label = new QLabel(QObject::tr("Input Levels:"), &dialog);
  controls->addWidget(input_label);

  auto* input_graph = new LevelsInputGraph(&dialog);
  input_graph->set_histogram(histogram_for_channel(initial.channel));
  controls->addWidget(input_graph);

  auto* black_input = new QSpinBox(&dialog);
  auto* gamma = new LevelsGammaSpinBox(&dialog);
  auto* white_input = new QSpinBox(&dialog);
  black_input->setObjectName(QStringLiteral("levelsBlackInputSpin"));
  gamma->setObjectName(QStringLiteral("levelsGammaSpin"));
  white_input->setObjectName(QStringLiteral("levelsWhiteInputSpin"));
  black_input->setRange(0, 254);
  gamma->setRange(10, 999);
  white_input->setRange(1, 255);
  configure_dialog_spinbox(black_input, 64);
  configure_dialog_spinbox(gamma, 64);
  configure_dialog_spinbox(white_input, 64);

  auto* input_values = new QHBoxLayout();
  input_values->addWidget(black_input);
  input_values->addStretch(1);
  input_values->addWidget(gamma);
  input_values->addStretch(1);
  input_values->addWidget(white_input);
  controls->addLayout(input_values);

  auto* output_label = new QLabel(QObject::tr("Output Levels:"), &dialog);
  controls->addWidget(output_label);

  auto* output_range = new LevelsOutputRange(&dialog);
  controls->addWidget(output_range);

  auto* black_output = new QSpinBox(&dialog);
  auto* white_output = new QSpinBox(&dialog);
  black_output->setObjectName(QStringLiteral("levelsBlackOutputSpin"));
  white_output->setObjectName(QStringLiteral("levelsWhiteOutputSpin"));
  black_output->setRange(0, 255);
  white_output->setRange(0, 255);
  configure_dialog_spinbox(black_output, 64);
  configure_dialog_spinbox(white_output, 64);

  auto* output_values = new QHBoxLayout();
  output_values->addWidget(black_output);
  output_values->addStretch(1);
  output_values->addWidget(white_output);
  controls->addLayout(output_values);

  auto* side = new QVBoxLayout();
  side->setSpacing(10);
  root->addLayout(side);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Vertical, &dialog);
  side->addWidget(buttons);

  auto* auto_button = new QPushButton(QObject::tr("Auto"), &dialog);
  auto_button->setObjectName(QStringLiteral("levelsAutoButton"));
  side->addWidget(auto_button);
  side->addStretch(1);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("levelsPreviewCheck"));
  preview->setChecked(true);
  side->addWidget(preview);

  bool updating = false;
  LevelsSettings dialog_settings = clamp_levels_settings(initial);
  LevelsChannel visible_channel = dialog_settings.channel;
  auto widget_record = [&] {
    return clamp_levels_record(LevelsRecord{black_input->value(), white_input->value(), gamma->value(),
                                           black_output->value(), white_output->value()});
  };
  auto committed_settings = [&] {
    auto settings = dialog_settings;
    settings.channel = visible_channel;
    set_levels_record_for_channel(settings, visible_channel, widget_record());
    return clamp_levels_settings(settings);
  };

  auto apply_settings_to_widgets = [&](LevelsSettings settings) {
    settings = clamp_levels_settings(settings);
    dialog_settings = settings;
    visible_channel = settings.channel;
    const auto record = levels_record_for_channel(settings, visible_channel);
    updating = true;
    black_input->setRange(0, 254);
    white_input->setRange(1, 255);
    black_output->setRange(0, 255);
    white_output->setRange(0, 255);
    channel->setCurrentIndex(levels_channel_combo_index(visible_channel));
    black_input->setValue(record.black_input);
    white_input->setValue(record.white_input);
    gamma->setValue(record.gamma_percent);
    black_output->setValue(record.black_output);
    white_output->setValue(record.white_output);
    black_input->setMaximum(record.white_input - 1);
    white_input->setMinimum(record.black_input + 1);
    black_output->setMaximum(record.white_output);
    white_output->setMinimum(record.black_output);
    input_graph->set_histogram(histogram_for_channel(visible_channel));
    input_graph->set_levels(record.black_input, record.white_input, record.gamma_percent);
    output_range->set_levels(record.black_output, record.white_output);
    updating = false;
  };

  CoalescedPreviewEmitter<AdjustmentPreviewRequest<LevelsSettings>> preview_emitter(
      dialog, [&](const AdjustmentPreviewRequest<LevelsSettings>& request) {
        if (preview_changed) {
          preview_changed(request.enabled, request.settings);
        }
      });
  auto preview_request = [&] {
    return AdjustmentPreviewRequest<LevelsSettings>{preview->isChecked(), committed_settings()};
  };
  auto schedule_preview = [&] { preview_emitter.schedule(preview_request()); };
  auto flush_preview = [&] { preview_emitter.flush(preview_request()); };

  auto sync_from_widgets = [&] {
    if (updating) {
      return;
    }
    apply_settings_to_widgets(committed_settings());
    schedule_preview();
  };

  QObject::connect(black_input, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sync_from_widgets](int) {
    sync_from_widgets();
  });
  QObject::connect(white_input, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sync_from_widgets](int) {
    sync_from_widgets();
  });
  QObject::connect(gamma, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sync_from_widgets](int) {
    sync_from_widgets();
  });
  QObject::connect(black_output, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sync_from_widgets](int) {
    sync_from_widgets();
  });
  QObject::connect(white_output, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sync_from_widgets](int) {
    sync_from_widgets();
  });
  QObject::connect(channel, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
    if (updating) {
      return;
    }
    dialog_settings = committed_settings();
    visible_channel = levels_channel_from_combo_index(index);
    dialog_settings.channel = visible_channel;
    apply_settings_to_widgets(dialog_settings);
    schedule_preview();
  });
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });
  input_graph->set_values_changed_callback([&](int black, int white, int gamma_percent) {
    if (updating) {
      return;
    }
    auto settings = committed_settings();
    auto record = levels_record_for_channel(settings, visible_channel);
    record.black_input = black;
    record.white_input = white;
    record.gamma_percent = gamma_percent;
    set_levels_record_for_channel(settings, visible_channel, record);
    apply_settings_to_widgets(settings);
    schedule_preview();
  });
  output_range->set_values_changed_callback([&](int black, int white) {
    if (updating) {
      return;
    }
    auto settings = committed_settings();
    auto record = levels_record_for_channel(settings, visible_channel);
    record.black_output = black;
    record.white_output = white;
    set_levels_record_for_channel(settings, visible_channel, record);
    apply_settings_to_widgets(settings);
    schedule_preview();
  });
  QObject::connect(auto_button, &QPushButton::clicked, &dialog, [&] {
    const auto histogram = histogram_for_channel(visible_channel);
    std::int64_t total = 0;
    for (const auto count : histogram) {
      total += count;
    }
    const auto threshold = std::max<std::int64_t>(1, total / 1000);
    std::int64_t cumulative = 0;
    int black = 0;
    for (; black < 255; ++black) {
      cumulative += histogram[static_cast<std::size_t>(black)];
      if (cumulative > threshold) {
        break;
      }
    }
    cumulative = 0;
    int white = 255;
    for (; white > black + 1; --white) {
      cumulative += histogram[static_cast<std::size_t>(white)];
      if (cumulative > threshold) {
        break;
      }
    }
    auto settings = committed_settings();
    set_levels_record_for_channel(settings, visible_channel, LevelsRecord{black, white, 100, 0, 255});
    apply_settings_to_widgets(settings);
    flush_preview();
  });

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  apply_settings_to_widgets(initial);
  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return committed_settings();
}

std::optional<CurvesSettings> request_curves_settings(
    QWidget* parent, std::function<void(bool, const CurvesSettings&)> preview_changed, CurvesSettings initial,
    CurvesHistograms histograms) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyCurvesDialog"));
  dialog.setWindowTitle(QObject::tr("Curves"));
  dialog.setMinimumSize(450, 540);

  auto* root = new QVBoxLayout(&dialog);
  auto* editor = new CurvesEditorWidget(&dialog);
  editor->set_adjustment(initial);
  editor->set_histograms(std::move(histograms));
  root->addWidget(editor, 1);

  auto* footer = new QHBoxLayout();
  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("curvesPreviewCheck"));
  preview->setChecked(true);
  footer->addWidget(preview);
  footer->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  footer->addWidget(buttons);
  root->addLayout(footer);

  CurvesSettings dialog_settings = editor->adjustment();
  CoalescedPreviewEmitter<AdjustmentPreviewRequest<CurvesSettings>> preview_emitter(
      dialog, [&](const AdjustmentPreviewRequest<CurvesSettings>& request) {
        if (preview_changed) {
          preview_changed(request.enabled, request.settings);
        }
      });
  auto preview_request = [&] {
    return AdjustmentPreviewRequest<CurvesSettings>{preview->isChecked(), dialog_settings};
  };
  auto schedule_preview = [&] { preview_emitter.schedule(preview_request()); };
  auto flush_preview = [&] { preview_emitter.flush(preview_request()); };

  editor->adjustment_changed = [&](const CurvesSettings& settings, bool gesture_finished) {
    dialog_settings = settings;
    editor->set_adjustment(dialog_settings);
    if (gesture_finished) {
      flush_preview();
    } else {
      schedule_preview();
    }
  };
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog_settings;
}

HueSaturationAdjustment to_hue_saturation_adjustment(const HueSaturationSettings& settings) {
  HueSaturationAdjustment adjustment;
  adjustment.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  adjustment.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  adjustment.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  adjustment.colorize = settings.colorize;
  adjustment.colorize_hue = std::clamp(settings.colorize_hue, 0, 360) % 360;
  adjustment.colorize_saturation = std::clamp(settings.colorize_saturation, 0, 100);
  adjustment.colorize_lightness = std::clamp(settings.colorize_lightness, -100, 100);
  return adjustment;
}

HueSaturationSettings to_hue_saturation_settings(const HueSaturationAdjustment& adjustment) {
  HueSaturationSettings settings;
  settings.hue_shift = std::clamp(adjustment.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(adjustment.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(adjustment.lightness_delta, -100, 100);
  settings.colorize = adjustment.colorize;
  settings.colorize_hue = std::clamp(adjustment.colorize_hue, 0, 360) % 360;
  settings.colorize_saturation = std::clamp(adjustment.colorize_saturation, 0, 100);
  settings.colorize_lightness = std::clamp(adjustment.colorize_lightness, -100, 100);
  return settings;
}

std::optional<HueSaturationSettings> request_hue_saturation_settings(
    QWidget* parent, std::function<void(bool, const HueSaturationSettings&)> preview_changed,
    HueSaturationSettings initial) {
  initial = to_hue_saturation_settings(to_hue_saturation_adjustment(initial));

  // The three sliders show master or colorize values depending on the Colorize
  // checkbox; the inactive triple is stashed so toggling round-trips both sets.
  auto stash = std::make_shared<HueSaturationSettings>(initial);
  auto colorize_check = std::make_shared<QCheckBox*>(nullptr);

  const auto master_rows = [](const HueSaturationSettings& value) {
    return std::vector<SliderRowSpec>{
        {QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), -180, 180, value.hue_shift, {}},
        {QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), -100, 100, value.saturation_delta, {}},
        {QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100, value.lightness_delta, {}}};
  };
  const auto colorize_rows = [](const HueSaturationSettings& value) {
    return std::vector<SliderRowSpec>{
        {QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), 0, 360, value.colorize_hue, {}},
        {QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), 0, 100, value.colorize_saturation, {}},
        {QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100, value.colorize_lightness, {}}};
  };

  const auto build_settings = [stash, colorize_check](const std::vector<QSpinBox*>& spins) {
    auto settings = *stash;
    settings.colorize = *colorize_check != nullptr && (*colorize_check)->isChecked();
    if (settings.colorize) {
      settings.colorize_hue = spins[0]->value();
      settings.colorize_saturation = spins[1]->value();
      settings.colorize_lightness = spins[2]->value();
    } else {
      settings.hue_shift = spins[0]->value();
      settings.saturation_delta = spins[1]->value();
      settings.lightness_delta = spins[2]->value();
    }
    return settings;
  };

  return request_adjustment_settings_dialog<HueSaturationSettings>(
      parent, QStringLiteral("patchyHueSaturationDialog"), QObject::tr("Hue/Saturation"),
      QStringLiteral("hueSaturationPreviewCheck"), initial.colorize ? colorize_rows(initial) : master_rows(initial),
      build_settings, std::move(preview_changed), {},
      [stash, colorize_check, master_rows, colorize_rows](QDialog& dialog, QFormLayout* form,
                                                          const std::vector<QSpinBox*>& spins,
                                                          const std::function<void()>& flush_preview) {
        auto* check = new QCheckBox(QObject::tr("Colorize"), &dialog);
        check->setObjectName(QStringLiteral("hueSaturationColorizeCheck"));
        check->setChecked(stash->colorize);
        *colorize_check = check;
        form->addRow(QString(), check);
        QObject::connect(
            check, &QCheckBox::toggled, &dialog,
            [&dialog, stash, spins, master_rows, colorize_rows, flush_preview](bool colorize) {
              // Stash the outgoing triple, then retarget slider ranges/values.
              if (colorize) {
                stash->hue_shift = spins[0]->value();
                stash->saturation_delta = spins[1]->value();
                stash->lightness_delta = spins[2]->value();
              } else {
                stash->colorize_hue = spins[0]->value();
                stash->colorize_saturation = spins[1]->value();
                stash->colorize_lightness = spins[2]->value();
              }
              const auto rows = colorize ? colorize_rows(*stash) : master_rows(*stash);
              for (std::size_t index = 0; index < spins.size() && index < rows.size(); ++index) {
                auto* slider =
                    dialog.findChild<QSlider*>(rows[index].object_prefix + QStringLiteral("Slider"));
                if (slider != nullptr) {
                  const QSignalBlocker block_slider(slider);
                  slider->setRange(rows[index].minimum, rows[index].maximum);
                  slider->setValue(rows[index].value);
                }
                const QSignalBlocker block_spin(spins[index]);
                spins[index]->setRange(rows[index].minimum, rows[index].maximum);
                spins[index]->setValue(rows[index].value);
              }
              flush_preview();
            });
      });
}

std::optional<ColorBalanceSettings> request_color_balance_settings(
    QWidget* parent, std::function<void(bool, const ColorBalanceSettings&)> preview_changed,
    ColorBalanceSettings initial) {
  initial.cyan_red = std::clamp(initial.cyan_red, -100, 100);
  initial.magenta_green = std::clamp(initial.magenta_green, -100, 100);
  initial.yellow_blue = std::clamp(initial.yellow_blue, -100, 100);
  return request_adjustment_settings_dialog<ColorBalanceSettings>(
      parent, QStringLiteral("patchyColorBalanceDialog"), QObject::tr("Color Balance"),
      QStringLiteral("colorBalancePreviewCheck"),
      {{QObject::tr("Cyan / Red"), QStringLiteral("colorBalanceCyanRed"), -100, 100, initial.cyan_red, {}},
       {QObject::tr("Magenta / Green"), QStringLiteral("colorBalanceMagentaGreen"), -100, 100,
        initial.magenta_green, {}},
       {QObject::tr("Yellow / Blue"), QStringLiteral("colorBalanceYellowBlue"), -100, 100, initial.yellow_blue, {}}},
      [](const std::vector<QSpinBox*>& spins) {
        return ColorBalanceSettings{spins[0]->value(), spins[1]->value(), spins[2]->value()};
      },
      std::move(preview_changed));
}

}  // namespace patchy::ui
