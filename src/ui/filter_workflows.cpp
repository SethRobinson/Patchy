#include "ui/filter_workflows.hpp"

#include "formats/acv_curves_io.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/dialog_utils.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QRect>
#include <QSignalBlocker>
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
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

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

namespace {

// Catalog strings are translated dynamically so the Qt-free filter library does
// not depend on Qt. Keep these markers so lupdate retains the existing QObject
// translation context and the hand-maintained Japanese entries.
[[maybe_unused]] constexpr const char* kFilterTranslationMarkers[] = {
    QT_TRANSLATE_NOOP("QObject", "Invert"),
    QT_TRANSLATE_NOOP("QObject", "Brightness/Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Grayscale"),
    QT_TRANSLATE_NOOP("QObject", "Desaturate"),
    QT_TRANSLATE_NOOP("QObject", "Auto Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Soft Glow"),
    QT_TRANSLATE_NOOP("QObject", "Punchy Color"),
    QT_TRANSLATE_NOOP("QObject", "Noir"),
    QT_TRANSLATE_NOOP("QObject", "Cinematic Matte"),
    QT_TRANSLATE_NOOP("QObject", "Vintage Fade"),
    QT_TRANSLATE_NOOP("QObject", "Vintage Sepia"),
    QT_TRANSLATE_NOOP("QObject", "Threshold"),
    QT_TRANSLATE_NOOP("QObject", "Posterize"),
    QT_TRANSLATE_NOOP("QObject", "Box Blur"),
    QT_TRANSLATE_NOOP("QObject", "Sharpen"),
    QT_TRANSLATE_NOOP("QObject", "Unsharp Mask"),
    QT_TRANSLATE_NOOP("QObject", "High Pass"),
    QT_TRANSLATE_NOOP("QObject", "Median"),
    QT_TRANSLATE_NOOP("QObject", "Gaussian Blur"),
    QT_TRANSLATE_NOOP("QObject", "Motion Blur"),
    QT_TRANSLATE_NOOP("QObject", "Radial Blur"),
    QT_TRANSLATE_NOOP("QObject", "Edge Detect"),
    QT_TRANSLATE_NOOP("QObject", "Emboss"),
    QT_TRANSLATE_NOOP("QObject", "Glowing Edges"),
    QT_TRANSLATE_NOOP("QObject", "Twirl"),
    QT_TRANSLATE_NOOP("QObject", "Wave"),
    QT_TRANSLATE_NOOP("QObject", "Pinch/Bloat"),
    QT_TRANSLATE_NOOP("QObject", "Clouds"),
    QT_TRANSLATE_NOOP("QObject", "Pixel Mosaic"),
    QT_TRANSLATE_NOOP("QObject", "Color Halftone"),
    QT_TRANSLATE_NOOP("QObject", "Analog Grain"),
    QT_TRANSLATE_NOOP("QObject", "Lens Vignette"),
    QT_TRANSLATE_NOOP("QObject", "Other"),
    QT_TRANSLATE_NOOP("QObject", "Adjustments"),
    QT_TRANSLATE_NOOP("QObject", "Photo Looks"),
    QT_TRANSLATE_NOOP("QObject", "Blur"),
    QT_TRANSLATE_NOOP("QObject", "Sharpen"),
    QT_TRANSLATE_NOOP("QObject", "Distort"),
    QT_TRANSLATE_NOOP("QObject", "Noise"),
    QT_TRANSLATE_NOOP("QObject", "Pixelate"),
    QT_TRANSLATE_NOOP("QObject", "Stylize"),
    QT_TRANSLATE_NOOP("QObject", "Render"),
    QT_TRANSLATE_NOOP("QObject", "Amount"),
    QT_TRANSLATE_NOOP("QObject", "Brightness"),
    QT_TRANSLATE_NOOP("QObject", "Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Glow"),
    QT_TRANSLATE_NOOP("QObject", "Intensity"),
    QT_TRANSLATE_NOOP("QObject", "Fade"),
    QT_TRANSLATE_NOOP("QObject", "Levels"),
    QT_TRANSLATE_NOOP("QObject", "Radius"),
    QT_TRANSLATE_NOOP("QObject", "Angle"),
    QT_TRANSLATE_NOOP("QObject", "Distance"),
    QT_TRANSLATE_NOOP("QObject", "Samples"),
    QT_TRANSLATE_NOOP("QObject", "Strength"),
    QT_TRANSLATE_NOOP("QObject", "Height"),
    QT_TRANSLATE_NOOP("QObject", "Edge Width"),
    QT_TRANSLATE_NOOP("QObject", "Smoothness"),
    QT_TRANSLATE_NOOP("QObject", "Amplitude"),
    QT_TRANSLATE_NOOP("QObject", "Wavelength"),
    QT_TRANSLATE_NOOP("QObject", "Phase"),
    QT_TRANSLATE_NOOP("QObject", "Scale"),
    QT_TRANSLATE_NOOP("QObject", "Detail"),
    QT_TRANSLATE_NOOP("QObject", "Seed"),
    QT_TRANSLATE_NOOP("QObject", "Block Size"),
    QT_TRANSLATE_NOOP("QObject", "Cell Size"),
    QT_TRANSLATE_NOOP("QObject", "Center X"),
    QT_TRANSLATE_NOOP("QObject", "Center Y"),
};

QString translate_filter_catalog_text(const std::string& source) {
  return QCoreApplication::translate("QObject", source.c_str());
}

double numeric_filter_value(const FilterParameterValue& value, double fallback = 0.0) {
  if (const auto* integer = std::get_if<std::int64_t>(&value); integer != nullptr) {
    return static_cast<double>(*integer);
  }
  if (const auto* number = std::get_if<double>(&value); number != nullptr) {
    return *number;
  }
  return fallback;
}

QString filter_parameter_suffix(FilterParameterUnit unit) {
  switch (unit) {
    case FilterParameterUnit::Percent:
      return QStringLiteral("%");
    case FilterParameterUnit::Pixels:
      return QStringLiteral(" px");
    case FilterParameterUnit::Degrees:
      return QStringLiteral(" deg");
    case FilterParameterUnit::None:
      return {};
  }
  return {};
}

}  // namespace

QString filter_display_name(const FilterDefinition& filter) {
  return translate_filter_catalog_text(filter.display_name);
}

QString filter_category_display_name(FilterCategory category) {
  switch (category) {
    case FilterCategory::Uncategorized:
      return translate_filter_catalog_text("Other");
    case FilterCategory::Adjustment:
      return translate_filter_catalog_text("Adjustments");
    case FilterCategory::PhotoLooks:
      return translate_filter_catalog_text("Photo Looks");
    case FilterCategory::Blur:
      return translate_filter_catalog_text("Blur");
    case FilterCategory::Sharpen:
      return translate_filter_catalog_text("Sharpen");
    case FilterCategory::Distort:
      return translate_filter_catalog_text("Distort");
    case FilterCategory::Noise:
      return translate_filter_catalog_text("Noise");
    case FilterCategory::Pixelate:
      return translate_filter_catalog_text("Pixelate");
    case FilterCategory::Stylize:
      return translate_filter_catalog_text("Stylize");
    case FilterCategory::Render:
      return translate_filter_catalog_text("Render");
  }
  return translate_filter_catalog_text("Other");
}

QString filter_progress_stage_text(FilterProgressStage stage) {
  switch (stage) {
    case FilterProgressStage::Filtering:
      return QObject::tr("Filtering pixels");
    case FilterProgressStage::Blurring:
      return QObject::tr("Blurring pixels");
    case FilterProgressStage::Sharpening:
      return QObject::tr("Sharpening pixels");
    case FilterProgressStage::DetectingEdges:
      return QObject::tr("Detecting edges");
    case FilterProgressStage::Distorting:
      return QObject::tr("Distorting pixels");
    case FilterProgressStage::Twisting:
      return QObject::tr("Twisting pixels");
    case FilterProgressStage::Embossing:
      return QObject::tr("Embossing pixels");
    case FilterProgressStage::GeneratingClouds:
      return QObject::tr("Generating clouds");
    case FilterProgressStage::Pixelating:
      return QObject::tr("Pixelating blocks");
    case FilterProgressStage::RenderingHalftone:
      return QObject::tr("Rendering halftone");
    case FilterProgressStage::AddingGrain:
      return QObject::tr("Adding grain");
    case FilterProgressStage::ApplyingVignette:
      return QObject::tr("Applying vignette");
  }
  return QObject::tr("Filtering pixels");
}

bool is_adjustment_only_filter(const FilterDefinition& filter) {
  return filter.catalog.adjustment_only;
}

FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter) {
  FilterDialogSpec spec;
  spec.identifier = QString::fromStdString(filter.identifier);
  spec.display_name = filter_display_name(filter);
  spec.schema_version = filter.catalog.schema_version;
  spec.controls.reserve(filter.catalog.parameters.size());
  for (const auto& parameter : filter.catalog.parameters) {
    const auto default_number = numeric_filter_value(parameter.default_value);
    FilterControlSpec control{
        translate_filter_catalog_text(parameter.display_name),
        QString::fromStdString(parameter.control_object_name),
        static_cast<int>(std::lround(parameter.practical_minimum.value_or(
            parameter.minimum.value_or(0.0)))),
        static_cast<int>(std::lround(parameter.practical_maximum.value_or(
            parameter.maximum.value_or(100.0)))),
        static_cast<int>(std::lround(default_number)),
        filter_parameter_suffix(parameter.unit)};
    control.parameter_key = parameter.key;
    control.kind = parameter.kind;
    control.default_value = parameter.default_value;
    control.typed_minimum = parameter.minimum;
    control.typed_maximum = parameter.maximum;
    control.step = parameter.step;
    control.options = parameter.options;
    control.presentation = parameter.presentation;
    spec.controls.push_back(std::move(control));
  }
  return spec;
}

std::optional<FilterInvocation> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed,
    FilterInvocation initial) {
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

  if (initial.filter_id.empty()) {
    initial.filter_id = spec.identifier.toStdString();
    initial.schema_version = spec.schema_version;
  }

  struct ControlBinding {
    std::string key;
    std::function<FilterParameterValue()> read;
    std::function<void()> reset;
  };
  std::vector<ControlBinding> bindings;
  bindings.reserve(spec.controls.size());
  std::function<void()> schedule_preview_callback;

  const auto stable_key_for = [](const FilterControlSpec& control) {
    if (!control.parameter_key.empty()) {
      return control.parameter_key;
    }
    auto legacy = control.object_name;
    if (legacy.startsWith(QStringLiteral("filter"))) {
      legacy.remove(0, 6);
    }
    QString key;
    for (const auto character : legacy) {
      if (character.isUpper() && !key.isEmpty()) {
        key += QLatin1Char('_');
      }
      key += character.toLower();
    }
    return key.toStdString();
  };
  const auto initial_value_for = [&](const FilterControlSpec& control) -> FilterParameterValue {
    const auto key = stable_key_for(control);
    if (const auto found = initial.parameters.find(key); found != initial.parameters.end()) {
      return found->second;
    }
    // Legacy hand-built specs predate typed defaults. Catalog specs always have
    // a stable key, so this branch is only for old helpers and tests.
    return control.parameter_key.empty() ? FilterParameterValue{std::int64_t{control.value}}
                                         : control.default_value;
  };
  const auto changed = [&schedule_preview_callback] {
    if (schedule_preview_callback) {
      schedule_preview_callback();
    }
  };

  for (const auto& control : spec.controls) {
    const auto key = stable_key_for(control);
    const auto initial_value = initial_value_for(control);
    const auto reset_value = control.parameter_key.empty()
                                 ? FilterParameterValue{std::int64_t{control.value}}
                                 : control.default_value;
    if (control.kind == FilterParameterKind::Boolean) {
      auto* check = new QCheckBox(&dialog);
      check->setObjectName(control.object_name + QStringLiteral("Check"));
      const auto checked = std::get_if<bool>(&initial_value);
      check->setChecked(checked != nullptr ? *checked : false);
      form->addRow(control.label, check);
      QObject::connect(check, &QCheckBox::toggled, &dialog, [changed](bool) { changed(); });
      bindings.push_back(ControlBinding{
          key,
          [check] { return FilterParameterValue{check->isChecked()}; },
          [check, reset_value] {
            const auto* value = std::get_if<bool>(&reset_value);
            check->setChecked(value != nullptr ? *value : false);
          }});
      continue;
    }

    if (control.kind == FilterParameterKind::Option) {
      auto* combo = new QComboBox(&dialog);
      combo->setObjectName(control.object_name + QStringLiteral("Combo"));
      for (const auto& option : control.options) {
        combo->addItem(translate_filter_catalog_text(option.display_name), QString::fromStdString(option.value));
      }
      const auto option_value = std::get_if<std::string>(&initial_value);
      const auto selected = option_value == nullptr ? QString() : QString::fromStdString(*option_value);
      const auto selected_index = combo->findData(selected);
      combo->setCurrentIndex(selected_index >= 0 ? selected_index : (combo->count() > 0 ? 0 : -1));
      form->addRow(control.label, combo);
      QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                       [changed](int) { changed(); });
      bindings.push_back(ControlBinding{
          key,
          [combo] { return FilterParameterValue{combo->currentData().toString().toUtf8().toStdString()}; },
          [combo, reset_value] {
            const auto* value = std::get_if<std::string>(&reset_value);
            const auto index = value == nullptr ? -1 : combo->findData(QString::fromStdString(*value));
            combo->setCurrentIndex(index >= 0 ? index : (combo->count() > 0 ? 0 : -1));
          }});
      continue;
    }

    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setObjectName(control.object_name + QStringLiteral("Slider"));
    row->addWidget(slider, 1);

    if (control.kind == FilterParameterKind::Double) {
      const auto minimum = control.typed_minimum.value_or(static_cast<double>(control.minimum));
      const auto maximum = control.typed_maximum.value_or(static_cast<double>(control.maximum));
      const auto slider_minimum =
          std::clamp(static_cast<double>(control.minimum), minimum, maximum);
      const auto slider_maximum =
          std::clamp(static_cast<double>(control.maximum), slider_minimum,
                     maximum);
      const auto step = std::max(0.000001, control.step.value_or(0.01));
      const auto ticks = std::clamp(
          static_cast<int>(
              std::lround((slider_maximum - slider_minimum) / step)),
          1, 1'000'000);
      auto* spin = new QDoubleSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      int decimals = 0;
      for (auto probe = step; decimals < 6 && std::abs(probe - std::round(probe)) > 0.0000001;
           probe *= 10.0) {
        ++decimals;
      }
      spin->setDecimals(decimals);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, 78);
      slider->setRange(0, ticks);
      const auto number = std::clamp(numeric_filter_value(initial_value, numeric_filter_value(control.default_value)),
                                     minimum, maximum);
      spin->setValue(number);
      slider->setValue(std::clamp(
          static_cast<int>(
              std::lround((number - slider_minimum) / step)),
          0, ticks));
      QObject::connect(slider, &QSlider::valueChanged, spin,
                       [spin, slider_minimum, slider_maximum, step](int tick) {
                         spin->setValue(std::clamp(
                             slider_minimum + static_cast<double>(tick) * step,
                             slider_minimum, slider_maximum));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                       [slider, slider_minimum, step, ticks](double value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(
                             std::clamp(
                                 static_cast<int>(std::lround(
                                     (value - slider_minimum) / step)),
                                 0, ticks));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                       [changed](double) { changed(); });
      row->addWidget(spin);
      bindings.push_back(ControlBinding{
          key,
          [spin] { return FilterParameterValue{spin->value()}; },
          [spin, reset_value] {
            spin->setValue(numeric_filter_value(reset_value, spin->minimum()));
          }});
    } else {
      auto* spin = new QSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      slider->setRange(control.minimum, control.maximum);
      spin->setRange(control.minimum, control.maximum);
      const auto number = std::clamp(static_cast<int>(std::lround(numeric_filter_value(initial_value, control.value))),
                                     control.minimum, control.maximum);
      slider->setValue(number);
      spin->setValue(number);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, 78);
      QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [changed](int) { changed(); });
      row->addWidget(spin);
      bindings.push_back(ControlBinding{
          key,
          [spin] { return FilterParameterValue{static_cast<std::int64_t>(spin->value())}; },
          [spin, reset_value, legacy_default = control.value] {
            spin->setValue(static_cast<int>(std::lround(numeric_filter_value(reset_value, legacy_default))));
          }});
    }
    form->addRow(control.label, container);
  }

  auto build_settings = [&] {
    auto invocation = initial;
    invocation.filter_id = spec.identifier.toStdString();
    invocation.schema_version = spec.schema_version;
    for (const auto& binding : bindings) {
      invocation.parameters[binding.key] = binding.read();
    }
    return FilterPreviewSettings{preview->isChecked(), std::move(invocation)};
  };

  CoalescedPreviewEmitter<FilterPreviewSettings> preview_emitter(
      dialog, [&](const FilterPreviewSettings& settings) {
        if (preview_changed) {
          preview_changed(settings);
        }
      });
  auto schedule_preview = [&] { preview_emitter.schedule(build_settings()); };
  auto flush_preview = [&] { preview_emitter.flush(build_settings()); };
  schedule_preview_callback = schedule_preview;

  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
                                       &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset); reset != nullptr) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      for (const auto& binding : bindings) {
        binding.reset();
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

  return build_settings().invocation;
}

std::optional<SmartFilterBlendingSettings> request_smart_filter_blending_settings(
    QWidget* parent, std::function<void(bool, const SmartFilterBlendingSettings&)> preview_changed,
    SmartFilterBlendingSettings initial) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("smartFilterBlendingDialog"));
  dialog.setWindowTitle(QObject::tr("Smart Filter Blending Options"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* blend_mode = new QComboBox(&dialog);
  blend_mode->setObjectName(QStringLiteral("smartFilterBlendModeCombo"));
  add_blend_mode_items(blend_mode);
  const auto blend_mode_index = blend_mode->findData(static_cast<int>(initial.blend_mode));
  blend_mode->setCurrentIndex(blend_mode_index >= 0 ? blend_mode_index
                                                    : blend_mode->findData(static_cast<int>(BlendMode::Normal)));
  form->addRow(QObject::tr("Mode:"), blend_mode);

  auto* opacity_row = new QWidget(&dialog);
  auto* opacity_layout = new QHBoxLayout(opacity_row);
  opacity_layout->setContentsMargins(0, 0, 0, 0);
  auto* opacity_slider = new QSlider(Qt::Horizontal, opacity_row);
  opacity_slider->setObjectName(QStringLiteral("smartFilterOpacitySlider"));
  opacity_slider->setRange(0, 100);
  auto* opacity_spin = new QDoubleSpinBox(opacity_row);
  opacity_spin->setObjectName(QStringLiteral("smartFilterOpacitySpin"));
  opacity_spin->setRange(0.0, 100.0);
  opacity_spin->setDecimals(0);
  opacity_spin->setSingleStep(1.0);
  opacity_spin->setSuffix(QObject::tr("%"));
  configure_dialog_spinbox(opacity_spin, 90);
  const auto initial_opacity = std::clamp(initial.opacity, 0.0, 1.0);
  opacity_spin->setValue(initial_opacity * 100.0);
  opacity_slider->setValue(static_cast<int>(std::lround(initial_opacity * 100.0)));
  opacity_layout->addWidget(opacity_slider, 1);
  opacity_layout->addWidget(opacity_spin);
  form->addRow(QObject::tr("Opacity:"), opacity_row);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("smartFilterBlendingPreviewCheck"));
  preview->setChecked(true);
  layout->addWidget(preview);

  auto build_settings = [&] {
    return SmartFilterBlendingSettings{
        static_cast<BlendMode>(blend_mode->currentData().toInt()),
        std::clamp(opacity_spin->value() / 100.0, 0.0, 1.0)};
  };
  CoalescedPreviewEmitter<AdjustmentPreviewRequest<SmartFilterBlendingSettings>> preview_emitter(
      dialog, [&](const AdjustmentPreviewRequest<SmartFilterBlendingSettings>& request) {
        if (preview_changed) {
          preview_changed(request.enabled, request.settings);
        }
      });
  auto preview_request = [&] {
    return AdjustmentPreviewRequest<SmartFilterBlendingSettings>{preview->isChecked(), build_settings()};
  };
  auto schedule_preview = [&] { preview_emitter.schedule(preview_request()); };
  auto flush_preview = [&] { preview_emitter.flush(preview_request()); };

  QObject::connect(opacity_slider, &QSlider::valueChanged, opacity_spin, [opacity_spin](int value) {
    opacity_spin->setValue(static_cast<double>(value));
  });
  QObject::connect(opacity_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), opacity_slider,
                   [opacity_slider](double value) {
                     const QSignalBlocker blocker(opacity_slider);
                     opacity_slider->setValue(static_cast<int>(std::lround(value)));
                   });
  QObject::connect(opacity_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                   [&schedule_preview](double) { schedule_preview(); });
  QObject::connect(blend_mode, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                   [&schedule_preview](int) { schedule_preview(); });
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("smartFilterBlendingButtonBox"));
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return build_settings();
}

std::uint8_t filter_clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void report_filter_progress(const FilterProgress* progress, int completed, int total,
                            FilterProgressStage stage = FilterProgressStage::Filtering) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  if (!progress->update(std::clamp(completed, 0, std::max(1, total)), std::max(1, total), stage)) {
    throw FilterCancelled();
  }
}

void report_filter_row_progress(const FilterProgress* progress, std::int32_t y, std::int32_t height) {
  report_filter_progress(progress, y, height);
}

void finish_filter_row_progress(const FilterProgress* progress, std::int32_t height) {
  report_filter_progress(progress, height, height);
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
      report_filter_progress(progress, completed_rows, total_rows);
      for (std::int32_t x = local_left; x < local_right; ++x) {
        pixel_fn(x, y);
      }
      ++completed_rows;
    }
  }
  report_filter_progress(progress, total_rows, total_rows);
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

namespace {

template <typename Filter>
PixelBuffer build_filter_preview_pixels_impl(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                              const FilterRegistry& registry, const Filter& filter,
                                              const FilterProgress* progress, Rect* result_bounds) {
  if (result_bounds != nullptr) {
    *result_bounds = bounds;
  }
  if (!selection.isEmpty() && layer_selection_region(selection, bounds).isEmpty()) {
    return original;
  }

  if (selection.isEmpty()) {
    auto rendered = registry.render(filter, original, bounds, true, progress);
    if (result_bounds != nullptr) {
      *result_bounds = rendered.bounds;
    }
    return std::move(rendered.pixels);
  }

  auto pixels = original;
  if (const auto support = registry.translation_invariant_support(filter); support.has_value()) {
    const auto selected = layer_selection_region(selection, bounds);
    const QRect layer_rect(bounds.x, bounds.y, bounds.width, bounds.height);
    const auto work =
        selected.boundingRect().adjusted(-*support, -*support, *support, *support).intersected(layer_rect);
    if (!work.isEmpty() && work != layer_rect) {
      const Rect work_local{work.x() - bounds.x, work.y() - bounds.y, work.width(), work.height()};
      auto window = copy_buffer_rect(original, work_local);
      registry.apply(filter, window, progress);
      blit_buffer_rect(pixels, window, work_local.x, work_local.y);
      restore_pixels_outside_selection(pixels, original, selection, bounds);
      return pixels;
    }
  }

  registry.apply(filter, pixels, progress);
  restore_pixels_outside_selection(pixels, original, selection, bounds);
  return pixels;
}

}  // namespace

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                         const FilterRegistry& registry, const FilterPreviewSettings& settings,
                                         const FilterProgress* progress, Rect* result_bounds) {
  if (!settings.preview_enabled) {
    if (result_bounds != nullptr) {
      *result_bounds = bounds;
    }
    return original;
  }
  return build_filter_preview_pixels_impl(original, selection, bounds, registry, settings.invocation, progress,
                                          result_bounds);
}

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                         const FilterRegistry& registry, const FilterRecipe& recipe,
                                         const FilterProgress* progress, Rect* result_bounds) {
  return build_filter_preview_pixels_impl(original, selection, bounds, registry, recipe, progress, result_bounds);
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
    CurvesHistograms histograms, CurvesDialogHooks hooks) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyCurvesDialog"));
  dialog.setWindowTitle(QObject::tr("Curves"));
  dialog.setMinimumSize(640, 630);

  auto* root = new QVBoxLayout(&dialog);
  auto* canvas_tools = new QHBoxLayout();
  auto* targeted_button = new QPushButton(QObject::tr("Target"), &dialog);
  targeted_button->setObjectName(QStringLiteral("curvesTargetedAdjustmentButton"));
  targeted_button->setCheckable(true);
  targeted_button->setToolTip(QObject::tr("Click and drag on the image to adjust the selected channel"));
  canvas_tools->addWidget(targeted_button);
  auto* black_button = new QPushButton(QObject::tr("Black"), &dialog);
  black_button->setObjectName(QStringLiteral("curvesBlackPointButton"));
  black_button->setCheckable(true);
  black_button->setToolTip(QObject::tr("Set the black point from the image"));
  canvas_tools->addWidget(black_button);
  auto* gray_button = new QPushButton(QObject::tr("Gray"), &dialog);
  gray_button->setObjectName(QStringLiteral("curvesGrayPointButton"));
  gray_button->setCheckable(true);
  gray_button->setToolTip(QObject::tr("Neutralize a gray point from the image"));
  canvas_tools->addWidget(gray_button);
  auto* white_button = new QPushButton(QObject::tr("White"), &dialog);
  white_button->setObjectName(QStringLiteral("curvesWhitePointButton"));
  white_button->setCheckable(true);
  white_button->setToolTip(QObject::tr("Set the white point from the image"));
  canvas_tools->addWidget(white_button);
  canvas_tools->addSpacing(12);
  auto* shadow_clipping_button = new QPushButton(QObject::tr("Shadows"), &dialog);
  shadow_clipping_button->setObjectName(QStringLiteral("curvesShadowClippingButton"));
  shadow_clipping_button->setCheckable(true);
  shadow_clipping_button->setToolTip(QObject::tr("Show pixels clipped to black"));
  canvas_tools->addWidget(shadow_clipping_button);
  auto* highlight_clipping_button = new QPushButton(QObject::tr("Highlights"), &dialog);
  highlight_clipping_button->setObjectName(QStringLiteral("curvesHighlightClippingButton"));
  highlight_clipping_button->setCheckable(true);
  highlight_clipping_button->setToolTip(QObject::tr("Show pixels clipped to white"));
  canvas_tools->addWidget(highlight_clipping_button);
  auto* clipping_button = new QPushButton(QObject::tr("Both"), &dialog);
  clipping_button->setObjectName(QStringLiteral("curvesClippingPreviewButton"));
  clipping_button->setCheckable(true);
  clipping_button->setToolTip(QObject::tr("Show shadow and highlight clipping together"));
  canvas_tools->addWidget(clipping_button);
  canvas_tools->addStretch(1);
  root->addLayout(canvas_tools);

  const std::array canvas_tool_buttons{targeted_button, black_button, gray_button, white_button};
  for (auto* button : canvas_tool_buttons) {
    button->setEnabled(static_cast<bool>(hooks.set_canvas_mode));
  }
  const std::array clipping_buttons{shadow_clipping_button, highlight_clipping_button, clipping_button};
  for (auto* button : clipping_buttons) {
    button->setEnabled(static_cast<bool>(hooks.clipping_changed));
  }

  auto* editor = new CurvesEditorWidget(&dialog);
  editor->set_adjustment(initial);
  editor->set_histograms(std::move(histograms));
  root->addWidget(editor, 1);

  constexpr int kPresetIdRole = Qt::UserRole + 1;
  constexpr QSize kPresetThumbnailSize(72, 48);
  auto* preset_list = new QListWidget(&dialog);
  preset_list->setObjectName(QStringLiteral("curvesPresetList"));
  preset_list->setAccessibleName(QObject::tr("Curves presets"));
  preset_list->setViewMode(QListView::IconMode);
  preset_list->setFlow(QListView::LeftToRight);
  preset_list->setWrapping(false);
  preset_list->setResizeMode(QListView::Adjust);
  preset_list->setMovement(QListView::Static);
  preset_list->setSelectionMode(QAbstractItemView::SingleSelection);
  preset_list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  preset_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  preset_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  preset_list->setIconSize(kPresetThumbnailSize);
  preset_list->setGridSize(QSize(98, 78));
  preset_list->setUniformItemSizes(true);
  preset_list->setFixedHeight(92);

  auto* custom_preset_item = new QListWidgetItem(QObject::tr("Custom"), preset_list);
  custom_preset_item->setData(kPresetIdRole, QString());
  custom_preset_item->setToolTip(QObject::tr("Current custom curve"));
  for (const auto& preset : builtin_curves_presets()) {
    const auto display_name = curves_preset_display_name(preset);
    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(
                                         curves_adjustment_thumbnail(preset.adjustment, kPresetThumbnailSize))),
                                     display_name, preset_list);
    item->setData(kPresetIdRole, preset.id);
    item->setToolTip(display_name);
  }
  root->addWidget(preset_list);

  auto* footer = new QHBoxLayout();
  auto* load_preset = new QPushButton(QObject::tr("Load..."), &dialog);
  load_preset->setObjectName(QStringLiteral("curvesLoadPresetButton"));
  load_preset->setToolTip(QObject::tr("Load a Photoshop Curves preset"));
  footer->addWidget(load_preset);
  auto* save_preset = new QPushButton(QObject::tr("Save..."), &dialog);
  save_preset->setObjectName(QStringLiteral("curvesSavePresetButton"));
  save_preset->setToolTip(QObject::tr("Save the current curves as a Photoshop preset"));
  footer->addWidget(save_preset);
  footer->addSpacing(12);
  auto* before = new QPushButton(QObject::tr("Before"), &dialog);
  before->setObjectName(QStringLiteral("curvesBeforeButton"));
  before->setToolTip(QObject::tr("Hold to compare with the unadjusted image"));
  footer->addWidget(before);
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

  CurvesEyedropperSamples eyedropper_samples;
  bool applying_eyedropper_adjustment = false;
  bool tonal_sample_active = false;
  int tonal_sample_start_global_y = 0;
  auto activate_canvas_mode = [&](CurvesCanvasMode mode) {
    if (!hooks.set_canvas_mode) {
      return;
    }
    hooks.set_canvas_mode(mode, [&, mode](const CurvesCanvasSample& sample) {
      const auto& gesture = sample.gesture;
      if (gesture.phase == CanvasReadPhase::Dismiss) {
        dialog.reject();
        return;
      }
      if (gesture.phase == CanvasReadPhase::Cancel) {
        if (tonal_sample_active) {
          editor->cancel_tonal_sample();
          tonal_sample_active = false;
        }
        return;
      }

      if (gesture.phase == CanvasReadPhase::Press) {
        if (!sample.input_color.isValid() || sample.input_color.alpha() == 0) {
          tonal_sample_active = false;
          return;
        }
        const auto color = RgbColor{static_cast<std::uint8_t>(sample.input_color.red()),
                                    static_cast<std::uint8_t>(sample.input_color.green()),
                                    static_cast<std::uint8_t>(sample.input_color.blue())};
        if (mode == CurvesCanvasMode::Targeted) {
          int input = 0;
          switch (editor->active_channel()) {
            case CurvesChannel::Red:
              input = color.red;
              break;
            case CurvesChannel::Green:
              input = color.green;
              break;
            case CurvesChannel::Blue:
              input = color.blue;
              break;
            case CurvesChannel::Rgb:
              input = (54 * static_cast<int>(color.red) + 183 * static_cast<int>(color.green) +
                       19 * static_cast<int>(color.blue)) /
                      256;
              break;
          }
          tonal_sample_start_global_y = gesture.global_position.y();
          tonal_sample_active = editor->begin_tonal_sample(input);
          return;
        }

        if (mode == CurvesCanvasMode::BlackPoint) {
          eyedropper_samples.black = color;
        } else if (mode == CurvesCanvasMode::GrayPoint) {
          eyedropper_samples.gray = color;
        } else if (mode == CurvesCanvasMode::WhitePoint) {
          eyedropper_samples.white = color;
        }
        applying_eyedropper_adjustment = true;
        editor->apply_external_adjustment(curves_adjustment_from_eyedropper_samples(eyedropper_samples));
        applying_eyedropper_adjustment = false;
        return;
      }

      if (mode != CurvesCanvasMode::Targeted || !tonal_sample_active) {
        return;
      }
      const auto output_delta = tonal_sample_start_global_y - gesture.global_position.y();
      if (gesture.phase == CanvasReadPhase::Drag) {
        editor->update_tonal_sample(output_delta, false);
      } else if (gesture.phase == CanvasReadPhase::Release) {
        editor->update_tonal_sample(output_delta, true);
        tonal_sample_active = false;
      }
    });
  };

  auto* canvas_tool_group = new QButtonGroup(&dialog);
  canvas_tool_group->setExclusive(false);
  for (int index = 0; index < static_cast<int>(canvas_tool_buttons.size()); ++index) {
    canvas_tool_group->addButton(canvas_tool_buttons[static_cast<std::size_t>(index)], index);
  }
  QObject::connect(canvas_tool_group, &QButtonGroup::idToggled, &dialog, [&](int id, bool checked) {
    if (checked) {
      for (int index = 0; index < static_cast<int>(canvas_tool_buttons.size()); ++index) {
        if (index == id) {
          continue;
        }
        const QSignalBlocker blocker(canvas_tool_buttons[static_cast<std::size_t>(index)]);
        canvas_tool_buttons[static_cast<std::size_t>(index)]->setChecked(false);
      }
      constexpr std::array modes{CurvesCanvasMode::Targeted, CurvesCanvasMode::BlackPoint,
                                 CurvesCanvasMode::GrayPoint, CurvesCanvasMode::WhitePoint};
      activate_canvas_mode(modes[static_cast<std::size_t>(std::clamp(id, 0, 3))]);
    } else if (std::none_of(canvas_tool_buttons.begin(), canvas_tool_buttons.end(),
                            [](const QPushButton* button) { return button->isChecked(); })) {
      if (hooks.clear_canvas_mode) {
        hooks.clear_canvas_mode();
      }
    }
  });

  std::optional<CurvesClippingMode> active_clipping_mode;
  auto* clipping_group = new QButtonGroup(&dialog);
  clipping_group->setExclusive(false);
  for (int index = 0; index < static_cast<int>(clipping_buttons.size()); ++index) {
    clipping_group->addButton(clipping_buttons[static_cast<std::size_t>(index)], index);
  }
  QObject::connect(clipping_group, &QButtonGroup::idToggled, &dialog, [&](int id, bool checked) {
    if (checked) {
      for (int index = 0; index < static_cast<int>(clipping_buttons.size()); ++index) {
        if (index == id) {
          continue;
        }
        const QSignalBlocker blocker(clipping_buttons[static_cast<std::size_t>(index)]);
        clipping_buttons[static_cast<std::size_t>(index)]->setChecked(false);
      }
      constexpr std::array modes{CurvesClippingMode::Shadows, CurvesClippingMode::Highlights,
                                 CurvesClippingMode::Both};
      active_clipping_mode = modes[static_cast<std::size_t>(std::clamp(id, 0, 2))];
    } else if (std::none_of(clipping_buttons.begin(), clipping_buttons.end(),
                            [](const QPushButton* button) { return button->isChecked(); })) {
      active_clipping_mode.reset();
    }
    if (hooks.clipping_changed) {
      hooks.clipping_changed(active_clipping_mode, editor->active_channel());
    }
  });
  editor->active_channel_changed = [&](CurvesChannel channel) {
    if (active_clipping_mode.has_value() && hooks.clipping_changed) {
      hooks.clipping_changed(active_clipping_mode, channel);
    }
  };

  auto update_custom_preset_thumbnail = [&] {
    custom_preset_item->setIcon(
        QIcon(QPixmap::fromImage(curves_adjustment_thumbnail(dialog_settings, kPresetThumbnailSize))));
  };
  auto select_custom_preset = [&] {
    update_custom_preset_thumbnail();
    const QSignalBlocker blocker(preset_list);
    preset_list->setCurrentItem(custom_preset_item);
  };
  auto sync_curves_preset_selection = [&] {
    update_custom_preset_thumbnail();
    QListWidgetItem* selected = custom_preset_item;
    if (const auto* matching = find_curves_preset(dialog_settings); matching != nullptr) {
      for (int row = 1; row < preset_list->count(); ++row) {
        auto* item = preset_list->item(row);
        if (item != nullptr && item->data(kPresetIdRole).toString() == matching->id) {
          selected = item;
          break;
        }
      }
    }
    const QSignalBlocker blocker(preset_list);
    preset_list->setCurrentItem(selected);
  };

  editor->adjustment_changed = [&](const CurvesSettings& settings, bool gesture_finished) {
    if (!applying_eyedropper_adjustment) {
      eyedropper_samples = {};
    }
    dialog_settings = settings;
    editor->set_adjustment(dialog_settings);
    select_custom_preset();
    if (gesture_finished) {
      flush_preview();
    } else {
      schedule_preview();
    }
  };
  QObject::connect(preset_list, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem* current, QListWidgetItem*) {
                     if (current == nullptr) {
                       return;
                     }
                     const auto id = current->data(kPresetIdRole).toString();
                     if (id.isEmpty()) {
                       return;
                     }
                     const auto* preset = find_curves_preset(id);
                     if (preset == nullptr) {
                       return;
                     }
                     eyedropper_samples = {};
                     dialog_settings = preset->adjustment;
                     editor->set_adjustment(dialog_settings);
                     update_custom_preset_thumbnail();
                     flush_preview();
                   });
  sync_curves_preset_selection();
  QObject::connect(load_preset, &QPushButton::clicked, &dialog, [&] {
    const auto path = get_open_file_name(
        &dialog, QObject::tr("Load Curves Preset"), QString(),
        QObject::tr("Photoshop Curves Preset (*.acv)"), nullptr,
        QStringLiteral("curvesPresetOpenFileDialog"));
    if (path.isEmpty()) {
      return;
    }
    try {
      eyedropper_samples = {};
      dialog_settings = acv::read_file(std::filesystem::path(path.toStdU16String()));
      editor->set_adjustment(dialog_settings);
      sync_curves_preset_selection();
      flush_preview();
    } catch (const std::exception&) {
      show_critical_message(
          &dialog, QObject::tr("Load Curves Preset"),
          QObject::tr("The Curves preset could not be loaded. The file may be damaged or unsupported."),
          QStringLiteral("curvesPresetLoadErrorMessageBox"));
    }
  });
  QObject::connect(save_preset, &QPushButton::clicked, &dialog, [&] {
    auto path = get_save_file_name(
        &dialog, QObject::tr("Save Curves Preset"), QString(),
        QObject::tr("Photoshop Curves Preset (*.acv)"), nullptr,
        QStringLiteral("curvesPresetSaveFileDialog"));
    if (path.isEmpty()) {
      return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
      path += QStringLiteral(".acv");
    }
    try {
      acv::write_file(std::filesystem::path(path.toStdU16String()), dialog_settings);
    } catch (const std::exception&) {
      show_critical_message(&dialog, QObject::tr("Save Curves Preset"),
                            QObject::tr("The Curves preset could not be saved."),
                            QStringLiteral("curvesPresetSaveErrorMessageBox"));
    }
  });
  QObject::connect(before, &QPushButton::pressed, &dialog, [&] {
    preview_emitter.flush(AdjustmentPreviewRequest<CurvesSettings>{false, dialog_settings});
    if (active_clipping_mode.has_value() && hooks.clipping_changed) {
      hooks.clipping_changed(std::nullopt, editor->active_channel());
    }
  });
  QObject::connect(before, &QPushButton::released, &dialog, [&] {
    flush_preview();
    if (active_clipping_mode.has_value() && hooks.clipping_changed) {
      hooks.clipping_changed(active_clipping_mode, editor->active_channel());
    }
  });
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&](bool enabled) {
    before->setEnabled(enabled);
    if (!enabled) {
      before->setDown(false);
    }
    flush_preview();
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(&dialog, &QDialog::finished, &dialog, [&] {
    if (hooks.clear_canvas_mode) {
      hooks.clear_canvas_mode();
    }
    if (hooks.clipping_changed) {
      hooks.clipping_changed(std::nullopt, editor->active_channel());
    }
  });

  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  const auto result = run_non_modal_dialog(dialog);
  if (hooks.clear_canvas_mode) {
    hooks.clear_canvas_mode();
  }
  if (hooks.clipping_changed) {
    hooks.clipping_changed(std::nullopt, editor->active_channel());
  }
  if (result != QDialog::Accepted) {
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
