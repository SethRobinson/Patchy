#include "ui/curves_editor.hpp"

#include "ui/dialog_utils.hpp"

#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>

namespace patchy::ui {

namespace {

constexpr std::array<CurvesChannel, 4> kCurvesChannels{
    CurvesChannel::Rgb, CurvesChannel::Red, CurvesChannel::Green, CurvesChannel::Blue};
constexpr std::size_t kMaximumCurvePoints = 19U;

int channel_index(CurvesChannel channel) {
  switch (channel) {
    case CurvesChannel::Red:
      return 1;
    case CurvesChannel::Green:
      return 2;
    case CurvesChannel::Blue:
      return 3;
    case CurvesChannel::Rgb:
      return 0;
  }
  return 0;
}

CurvesChannel channel_from_index(int index) {
  return kCurvesChannels[static_cast<std::size_t>(std::clamp(index, 0, 3))];
}

QColor channel_color(CurvesChannel channel, bool active) {
  QColor color;
  switch (channel) {
    case CurvesChannel::Red:
      color = QColor(246, 92, 92);
      break;
    case CurvesChannel::Green:
      color = QColor(82, 218, 126);
      break;
    case CurvesChannel::Blue:
      color = QColor(94, 145, 255);
      break;
    case CurvesChannel::Rgb:
      color = QColor(238, 242, 248);
      break;
  }
  color.setAlpha(active ? 255 : 105);
  return color;
}

QColor histogram_color(CurvesChannel channel) {
  auto color = channel_color(channel, true);
  color.setAlpha(channel == CurvesChannel::Rgb ? 78 : 64);
  return color;
}

CurvesAdjustment normalized_adjustment(const CurvesAdjustment& adjustment) {
  CurvesAdjustment result;
  for (const auto channel : kCurvesChannels) {
    set_curve_points_for_channel(result, channel, curve_points_for_channel(adjustment, channel));
  }
  return result;
}

CurveControlPoints identity_curve_points() {
  return CurveControlPoints{{0, 0}, {255, 255}};
}

const std::array<std::uint32_t, 256>& histogram_for_channel(const CurvesHistograms& histograms,
                                                            CurvesChannel channel) {
  switch (channel) {
    case CurvesChannel::Red:
      return histograms.red;
    case CurvesChannel::Green:
      return histograms.green;
    case CurvesChannel::Blue:
      return histograms.blue;
    case CurvesChannel::Rgb:
      return histograms.rgb;
  }
  return histograms.rgb;
}

}  // namespace

CurvesHistograms curves_histograms_from_pixels(const PixelBuffer* source,
                                               std::span<const std::uint8_t> external_alpha) {
  CurvesHistograms result;
  if (source == nullptr || source->empty() || source->format().bit_depth != BitDepth::UInt8 ||
      source->format().channels < 3) {
    return result;
  }

  const auto width = std::max<std::int32_t>(0, source->width());
  const auto height = std::max<std::int32_t>(0, source->height());
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (!external_alpha.empty() && external_alpha.size() != pixel_count) {
    return result;
  }
  const auto total_pixels = static_cast<double>(width) * static_cast<double>(height);
  const auto sample_step = std::max(1, static_cast<int>(std::ceil(std::sqrt(total_pixels / 262144.0))));
  const auto channels = source->format().channels;
  for (std::int32_t y = 0; y < height; y += sample_step) {
    for (std::int32_t x = 0; x < width; x += sample_step) {
      const auto* pixel = source->pixel(x, y);
      const auto pixel_index =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      if ((!external_alpha.empty() && external_alpha[pixel_index] == 0) ||
          (external_alpha.empty() && channels >= 4 && pixel[3] == 0)) {
        continue;
      }
      ++result.red[static_cast<std::size_t>(pixel[0])];
      ++result.green[static_cast<std::size_t>(pixel[1])];
      ++result.blue[static_cast<std::size_t>(pixel[2])];
      const auto luminance = std::clamp((54 * static_cast<int>(pixel[0]) + 183 * static_cast<int>(pixel[1]) +
                                         19 * static_cast<int>(pixel[2])) /
                                            256,
                                        0, 255);
      ++result.rgb[static_cast<std::size_t>(luminance)];
    }
  }
  return result;
}

class CurvesGraphWidget final : public QWidget {
public:
  explicit CurvesGraphWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("curvesGraph"));
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QObject::tr("Curves graph"));
    setToolTip(QObject::tr("Click to add a point. Drag points or use the arrow keys to adjust them."));
    set_adjustment(CurvesAdjustment{});
  }

  void set_adjustment(const CurvesAdjustment& adjustment) {
    adjustment_ = normalized_adjustment(adjustment);
    for (const auto channel : kCurvesChannels) {
      curve_luts_[static_cast<std::size_t>(channel_index(channel))] =
          build_curve_lut(curve_points_for_channel(adjustment_, channel));
    }
    clamp_selected_point();
    update();
  }

  void set_histograms(CurvesHistograms histograms) {
    histograms_ = std::move(histograms);
    update();
  }

  void set_active_channel(CurvesChannel channel) {
    active_channel_ = channel;
    clamp_selected_point();
    update();
  }

  void set_selected_point(int index) {
    selected_point_ = index;
    clamp_selected_point();
    update();
  }

  void set_sample_input(std::optional<int> input) {
    sample_input_ = input.has_value() ? std::optional<int>{std::clamp(*input, 0, 255)} : std::nullopt;
    update();
  }

  [[nodiscard]] QSize sizeHint() const override {
    return QSize(360, 360);
  }

  std::function<void(int)> point_selected;
  std::function<int(int, int)> point_add_requested;
  std::function<void(int, int, int, bool)> point_change_requested;
  std::function<void(int)> point_delete_requested;
  std::function<void(int)> point_cycle_requested;

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(43, 46, 53));

    const auto graph = graph_rect();
    painter.fillRect(graph, QColor(23, 26, 32));
    draw_histogram(painter, graph);
    draw_grid(painter, graph);

    for (const auto channel : kCurvesChannels) {
      if (channel != active_channel_) {
        draw_curve(painter, graph, channel, false);
      }
    }
    draw_curve(painter, graph, active_channel_, true);
    draw_sample_marker(painter, graph);
    draw_points(painter, graph);

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(hasFocus() ? QColor(91, 163, 255) : QColor(104, 111, 124), hasFocus() ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(graph.adjusted(0, 0, -1, -1));

    painter.setPen(QColor(181, 186, 197));
    painter.drawText(QRect(graph.left(), graph.bottom() + 5, 40, 18), Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("0"));
    painter.drawText(QRect(graph.right() - 40, graph.bottom() + 5, 40, 18), Qt::AlignRight | Qt::AlignTop,
                     QStringLiteral("255"));
    painter.drawText(QRect(graph.left() - 27, graph.top() - 2, 24, 18), Qt::AlignRight | Qt::AlignTop,
                     QStringLiteral("255"));
  }

  void focusInEvent(QFocusEvent* event) override {
    QWidget::focusInEvent(event);
    update();
  }

  void focusOutEvent(QFocusEvent* event) override {
    QWidget::focusOutEvent(event);
    update();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || !graph_rect().contains(event->position().toPoint())) {
      QWidget::mousePressEvent(event);
      return;
    }
    setFocus(Qt::MouseFocusReason);
    const auto position = event->position();
    auto index = hit_point(position);
    if (index < 0 && point_add_requested) {
      index = point_add_requested(input_for_x(position.x()), output_for_y(position.y()));
    }
    if (index >= 0) {
      active_drag_point_ = index;
      selected_point_ = index;
      if (point_selected) {
        point_selected(index);
      }
      update();
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (active_drag_point_ >= 0 && (event->buttons() & Qt::LeftButton) != 0 && point_change_requested) {
      point_change_requested(active_drag_point_, input_for_x(event->position().x()), output_for_y(event->position().y()),
                             false);
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && active_drag_point_ >= 0) {
      const auto index = active_drag_point_;
      active_drag_point_ = -1;
      if (point_change_requested) {
        point_change_requested(index, input_for_x(event->position().x()), output_for_y(event->position().y()), true);
      }
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if ((event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) && point_cycle_requested) {
      point_cycle_requested(event->key() == Qt::Key_PageUp ? -1 : 1);
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
      if (point_delete_requested) {
        point_delete_requested(selected_point_);
      }
      event->accept();
      return;
    }

    int input_delta = 0;
    int output_delta = 0;
    const auto step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10 : 1;
    switch (event->key()) {
      case Qt::Key_Left:
        input_delta = -step;
        break;
      case Qt::Key_Right:
        input_delta = step;
        break;
      case Qt::Key_Up:
        output_delta = step;
        break;
      case Qt::Key_Down:
        output_delta = -step;
        break;
      default:
        QWidget::keyPressEvent(event);
        return;
    }

    const auto& points = curve_points_for_channel(adjustment_, active_channel_);
    if (selected_point_ >= 0 && selected_point_ < static_cast<int>(points.size()) && point_change_requested) {
      const auto& point = points[static_cast<std::size_t>(selected_point_)];
      point_change_requested(selected_point_, point.input + input_delta, point.output + output_delta, true);
    }
    event->accept();
  }

private:
  [[nodiscard]] QRect graph_rect() const {
    const auto available = rect().adjusted(31, 10, -10, -28);
    const auto side = std::max(1, std::min(available.width(), available.height()));
    const auto left = available.left() + (available.width() - side) / 2;
    const auto top = available.top() + (available.height() - side) / 2;
    return QRect(left, top, side, side);
  }

  [[nodiscard]] double x_for_input(int input) const {
    const auto graph = graph_rect();
    return static_cast<double>(graph.left()) +
           static_cast<double>(std::clamp(input, 0, 255)) * static_cast<double>(graph.width() - 1) / 255.0;
  }

  [[nodiscard]] double y_for_output(int output) const {
    const auto graph = graph_rect();
    return static_cast<double>(graph.bottom()) -
           static_cast<double>(std::clamp(output, 0, 255)) * static_cast<double>(graph.height() - 1) / 255.0;
  }

  [[nodiscard]] int input_for_x(double x) const {
    const auto graph = graph_rect();
    const auto normalized =
        std::clamp((x - static_cast<double>(graph.left())) / static_cast<double>(std::max(1, graph.width() - 1)),
                   0.0, 1.0);
    return std::clamp(static_cast<int>(std::lround(normalized * 255.0)), 0, 255);
  }

  [[nodiscard]] int output_for_y(double y) const {
    const auto graph = graph_rect();
    const auto normalized =
        std::clamp((static_cast<double>(graph.bottom()) - y) /
                       static_cast<double>(std::max(1, graph.height() - 1)),
                   0.0, 1.0);
    return std::clamp(static_cast<int>(std::lround(normalized * 255.0)), 0, 255);
  }

  [[nodiscard]] int hit_point(QPointF position) const {
    const auto& points = curve_points_for_channel(adjustment_, active_channel_);
    int nearest = -1;
    auto nearest_distance = std::numeric_limits<double>::max();
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
      const auto& point = points[static_cast<std::size_t>(index)];
      const auto dx = position.x() - x_for_input(point.input);
      const auto dy = position.y() - y_for_output(point.output);
      const auto distance = dx * dx + dy * dy;
      if (distance <= 100.0 && distance < nearest_distance) {
        nearest = index;
        nearest_distance = distance;
      }
    }
    return nearest;
  }

  void draw_histogram(QPainter& painter, const QRect& graph) const {
    const auto& histogram = histogram_for_channel(histograms_, active_channel_);
    const auto maximum = *std::max_element(histogram.begin(), histogram.end());
    if (maximum == 0) {
      return;
    }
    const auto log_maximum = std::log(static_cast<double>(maximum) + 1.0);
    QPainterPath shape;
    shape.moveTo(graph.left(), graph.bottom());
    for (int x = 0; x < graph.width(); ++x) {
      const auto first_bin = std::clamp((x * 256) / std::max(1, graph.width()), 0, 255);
      const auto last_bin = std::clamp(((x + 1) * 256) / std::max(1, graph.width()), first_bin + 1, 256);
      std::uint32_t count = 0;
      for (int bin = first_bin; bin < last_bin; ++bin) {
        count = std::max(count, histogram[static_cast<std::size_t>(bin)]);
      }
      const auto scaled = std::log(static_cast<double>(count) + 1.0) / log_maximum;
      const auto y = static_cast<double>(graph.bottom()) - scaled * static_cast<double>(graph.height() - 1);
      shape.lineTo(graph.left() + x, y);
    }
    shape.lineTo(graph.right(), graph.bottom());
    shape.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(histogram_color(active_channel_));
    painter.drawPath(shape);
  }

  void draw_grid(QPainter& painter, const QRect& graph) const {
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int division = 1; division < 4; ++division) {
      const auto x = graph.left() + division * (graph.width() - 1) / 4;
      const auto y = graph.top() + division * (graph.height() - 1) / 4;
      const auto alpha = division == 2 ? 86 : 50;
      painter.setPen(QPen(QColor(157, 165, 178, alpha), 1));
      painter.drawLine(x, graph.top(), x, graph.bottom());
      painter.drawLine(graph.left(), y, graph.right(), y);
    }
    painter.setPen(QPen(QColor(210, 215, 224, 105), 1, Qt::DashLine));
    painter.drawLine(graph.bottomLeft(), graph.topRight());
    painter.setRenderHint(QPainter::Antialiasing, true);
  }

  void draw_curve(QPainter& painter, const QRect& graph, CurvesChannel channel, bool active) const {
    const auto& lut = curve_luts_[static_cast<std::size_t>(channel_index(channel))];
    QPainterPath path;
    path.moveTo(graph.left(), y_for_output(lut[0]));
    for (int input = 1; input < 256; ++input) {
      path.lineTo(x_for_input(input), y_for_output(lut[static_cast<std::size_t>(input)]));
    }
    painter.setBrush(Qt::NoBrush);
    if (active) {
      painter.setPen(QPen(QColor(5, 8, 12, 180), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(path);
    }
    painter.setPen(QPen(channel_color(channel, active), active ? 2.2 : 1.2, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.drawPath(path);
  }

  void draw_points(QPainter& painter, const QRect& graph) const {
    Q_UNUSED(graph);
    const auto& points = curve_points_for_channel(adjustment_, active_channel_);
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
      const auto& point = points[static_cast<std::size_t>(index)];
      const QPointF center(x_for_input(point.input), y_for_output(point.output));
      const auto selected = index == selected_point_;
      painter.setPen(QPen(selected ? QColor(91, 163, 255) : channel_color(active_channel_, true), selected ? 2.2 : 1.4));
      painter.setBrush(selected ? QColor(245, 248, 253) : QColor(31, 35, 43));
      painter.drawEllipse(center, selected ? 6.0 : 4.3, selected ? 6.0 : 4.3);
    }
  }

  void draw_sample_marker(QPainter& painter, const QRect& graph) const {
    if (!sample_input_.has_value()) {
      return;
    }
    const auto x = x_for_input(*sample_input_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(255, 198, 72, 205), 1, Qt::DashLine));
    painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    painter.setRenderHint(QPainter::Antialiasing, true);
  }

  void clamp_selected_point() {
    const auto& points = curve_points_for_channel(adjustment_, active_channel_);
    selected_point_ = points.empty() ? -1 : std::clamp(selected_point_, 0, static_cast<int>(points.size()) - 1);
  }

  CurvesAdjustment adjustment_{};
  CurvesHistograms histograms_{};
  CurvesChannel active_channel_{CurvesChannel::Rgb};
  std::array<std::array<std::uint8_t, 256>, 4> curve_luts_{};
  int selected_point_{0};
  int active_drag_point_{-1};
  std::optional<int> sample_input_{};
};

CurvesEditorWidget::CurvesEditorWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("curvesEditor"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(7);

  channel_tabs_ = new QTabBar(this);
  channel_tabs_->setObjectName(QStringLiteral("curvesChannelTabs"));
  channel_tabs_->setDrawBase(false);
  channel_tabs_->setExpanding(true);
  channel_tabs_->addTab(QObject::tr("RGB"));
  channel_tabs_->addTab(QObject::tr("Red"));
  channel_tabs_->addTab(QObject::tr("Green"));
  channel_tabs_->addTab(QObject::tr("Blue"));
  root->addWidget(channel_tabs_);

  graph_ = new CurvesGraphWidget(this);
  root->addWidget(graph_, 1);

  auto* numeric_row = new QHBoxLayout();
  numeric_row->setSpacing(6);
  auto* input_label = new QLabel(QObject::tr("Input:"), this);
  numeric_row->addWidget(input_label);
  input_spin_ = new QSpinBox(this);
  input_spin_->setObjectName(QStringLiteral("curvesInputSpin"));
  input_spin_->setRange(0, 255);
  configure_dialog_spinbox(input_spin_, 72);
  input_label->setBuddy(input_spin_);
  numeric_row->addWidget(input_spin_);
  numeric_row->addSpacing(14);
  auto* output_label = new QLabel(QObject::tr("Output:"), this);
  numeric_row->addWidget(output_label);
  output_spin_ = new QSpinBox(this);
  output_spin_->setObjectName(QStringLiteral("curvesOutputSpin"));
  output_spin_->setRange(0, 255);
  configure_dialog_spinbox(output_spin_, 72);
  output_label->setBuddy(output_spin_);
  numeric_row->addWidget(output_spin_);
  numeric_row->addStretch(1);
  root->addLayout(numeric_row);

  auto* action_row = new QHBoxLayout();
  action_row->addStretch(1);
  auto_button_ = new QPushButton(QObject::tr("Auto"), this);
  auto_button_->setObjectName(QStringLiteral("curvesAutoButton"));
  auto_button_->setToolTip(QObject::tr("Set the active channel from its histogram"));
  action_row->addWidget(auto_button_);
  reset_button_ = new QPushButton(QObject::tr("Reset"), this);
  reset_button_->setObjectName(QStringLiteral("curvesResetButton"));
  reset_button_->setToolTip(QObject::tr("Reset all channels"));
  action_row->addWidget(reset_button_);
  root->addLayout(action_row);

  graph_->point_selected = [this](int index) { select_point(index); };
  graph_->point_add_requested = [this](int input, int output) { return add_point(input, output); };
  graph_->point_change_requested =
      [this](int index, int input, int output, bool finished) { change_point(index, input, output, finished); };
  graph_->point_delete_requested = [this](int index) { delete_point(index); };
  graph_->point_cycle_requested = [this](int direction) { cycle_point(direction); };

  QObject::connect(channel_tabs_, &QTabBar::currentChanged, this, [this](int index) {
    if (updating_controls_) {
      return;
    }
    set_active_channel(channel_from_index(index));
    if (active_channel_changed) {
      active_channel_changed(active_channel_);
    }
  });
  QObject::connect(input_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
    if (updating_controls_) {
      return;
    }
    change_point(selected_point_, value, output_spin_->value(), false);
  });
  QObject::connect(output_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
    if (updating_controls_) {
      return;
    }
    change_point(selected_point_, input_spin_->value(), value, false);
  });
  QObject::connect(input_spin_, &QSpinBox::editingFinished, this,
                   [this] { propose_adjustment(adjustment_, true); });
  QObject::connect(output_spin_, &QSpinBox::editingFinished, this,
                   [this] { propose_adjustment(adjustment_, true); });
  QObject::connect(reset_button_, &QPushButton::clicked, this, [this] { reset_curves(); });
  QObject::connect(auto_button_, &QPushButton::clicked, this, [this] { auto_curve(); });

  normalize_and_store(CurvesAdjustment{});
  sync_children();
}

void CurvesEditorWidget::set_adjustment(const CurvesAdjustment& adjustment) {
  normalize_and_store(adjustment);
  sync_children();
}

void CurvesEditorWidget::set_histograms(CurvesHistograms histograms) {
  histograms_ = std::move(histograms);
  graph_->set_histograms(histograms_);
  const auto& histogram = active_histogram();
  auto_button_->setEnabled(
      std::any_of(histogram.begin(), histogram.end(), [](std::uint32_t count) { return count != 0; }));
}

void CurvesEditorWidget::set_active_channel(CurvesChannel channel) {
  int previous_input = 0;
  const auto& previous_points = curve_points_for_channel(adjustment_, active_channel_);
  if (selected_point_ >= 0 && selected_point_ < static_cast<int>(previous_points.size())) {
    previous_input = previous_points[static_cast<std::size_t>(selected_point_)].input;
  }
  active_channel_ = channel;
  const auto& points = curve_points_for_channel(adjustment_, active_channel_);
  if (points.empty()) {
    selected_point_ = -1;
  } else {
    selected_point_ = 0;
    auto nearest_distance = std::numeric_limits<int>::max();
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
      const auto distance = std::abs(points[static_cast<std::size_t>(index)].input - previous_input);
      if (distance < nearest_distance) {
        selected_point_ = index;
        nearest_distance = distance;
      }
    }
  }
  sync_children();
}

void CurvesEditorWidget::set_selected_point(int index) {
  select_point(index);
}

bool CurvesEditorWidget::begin_tonal_sample(int input) {
  if (tonal_sample_start_.has_value()) {
    cancel_tonal_sample();
  }
  input = std::clamp(input, 0, 255);
  tonal_sample_start_ = adjustment_;
  tonal_sample_input_ = input;
  tonal_sample_output_ = build_curve_lut(curve_points_for_channel(adjustment_, active_channel_))
                             [static_cast<std::size_t>(input)];
  tonal_sample_point_ = add_point(input, tonal_sample_output_);
  if (tonal_sample_point_ < 0) {
    tonal_sample_start_.reset();
    graph_->set_sample_input(std::nullopt);
    return false;
  }
  graph_->set_sample_input(input);
  return true;
}

void CurvesEditorWidget::update_tonal_sample(int output_delta, bool finished) {
  if (!tonal_sample_start_.has_value() || tonal_sample_point_ < 0) {
    return;
  }
  change_point(tonal_sample_point_, tonal_sample_input_, tonal_sample_output_ + output_delta, finished);
  if (finished) {
    tonal_sample_start_.reset();
    tonal_sample_point_ = -1;
    graph_->set_sample_input(std::nullopt);
  }
}

void CurvesEditorWidget::cancel_tonal_sample() {
  if (!tonal_sample_start_.has_value()) {
    graph_->set_sample_input(std::nullopt);
    return;
  }
  auto original = std::move(*tonal_sample_start_);
  tonal_sample_start_.reset();
  tonal_sample_point_ = -1;
  graph_->set_sample_input(std::nullopt);
  propose_adjustment(std::move(original), true);
}

void CurvesEditorWidget::apply_external_adjustment(const CurvesAdjustment& adjustment, bool finished) {
  tonal_sample_start_.reset();
  tonal_sample_point_ = -1;
  graph_->set_sample_input(std::nullopt);
  propose_adjustment(adjustment, finished);
}

const CurvesAdjustment& CurvesEditorWidget::adjustment() const noexcept {
  return adjustment_;
}

CurvesChannel CurvesEditorWidget::active_channel() const noexcept {
  return active_channel_;
}

int CurvesEditorWidget::selected_point() const noexcept {
  return selected_point_;
}

QSize CurvesEditorWidget::sizeHint() const {
  return QSize(410, 445);
}

void CurvesEditorWidget::normalize_and_store(const CurvesAdjustment& adjustment) {
  adjustment_ = normalized_adjustment(adjustment);
  const auto& points = curve_points_for_channel(adjustment_, active_channel_);
  selected_point_ = points.empty() ? -1 : std::clamp(selected_point_, 0, static_cast<int>(points.size()) - 1);
}

void CurvesEditorWidget::sync_children() {
  updating_controls_ = true;
  {
    const QSignalBlocker blocker(channel_tabs_);
    channel_tabs_->setCurrentIndex(channel_index(active_channel_));
  }
  graph_->set_adjustment(adjustment_);
  graph_->set_histograms(histograms_);
  graph_->set_active_channel(active_channel_);
  graph_->set_selected_point(selected_point_);
  sync_numeric_controls();
  const auto& histogram = active_histogram();
  auto_button_->setEnabled(
      std::any_of(histogram.begin(), histogram.end(), [](std::uint32_t count) { return count != 0; }));
  updating_controls_ = false;
}

void CurvesEditorWidget::sync_numeric_controls() {
  const auto& points = curve_points_for_channel(adjustment_, active_channel_);
  const auto valid = selected_point_ >= 0 && selected_point_ < static_cast<int>(points.size());
  const QSignalBlocker input_blocker(input_spin_);
  const QSignalBlocker output_blocker(output_spin_);
  input_spin_->setEnabled(valid);
  output_spin_->setEnabled(valid);
  if (!valid) {
    input_spin_->setRange(0, 255);
    input_spin_->setValue(0);
    output_spin_->setValue(0);
    return;
  }
  const auto& point = points[static_cast<std::size_t>(selected_point_)];
  const auto minimum_input =
      selected_point_ > 0 ? points[static_cast<std::size_t>(selected_point_ - 1)].input + 1 : 0;
  const auto maximum_input = selected_point_ + 1 < static_cast<int>(points.size())
                                 ? points[static_cast<std::size_t>(selected_point_ + 1)].input - 1
                                 : 255;
  input_spin_->setRange(minimum_input, maximum_input);
  input_spin_->setValue(point.input);
  output_spin_->setRange(0, 255);
  output_spin_->setValue(point.output);
}

void CurvesEditorWidget::propose_adjustment(CurvesAdjustment adjustment, bool gesture_finished) {
  normalize_and_store(adjustment);
  sync_children();
  if (adjustment_changed) {
    adjustment_changed(adjustment_, gesture_finished);
  }
}

int CurvesEditorWidget::add_point(int input, int output) {
  auto points = curve_points_for_channel(adjustment_, active_channel_);
  input = std::clamp(input, 0, 255);
  output = std::clamp(output, 0, 255);
  for (int index = 0; index < static_cast<int>(points.size()); ++index) {
    if (points[static_cast<std::size_t>(index)].input == input) {
      select_point(index);
      return index;
    }
  }
  if (points.size() >= kMaximumCurvePoints) {
    return -1;
  }
  const auto insertion = std::lower_bound(points.begin(), points.end(), input,
                                          [](const CurveControlPoint& point, int value) { return point.input < value; });
  const auto index = static_cast<int>(std::distance(points.begin(), insertion));
  points.insert(insertion, CurveControlPoint{input, output});
  auto proposed = adjustment_;
  set_curve_points_for_channel(proposed, active_channel_, std::move(points));
  selected_point_ = index;
  propose_adjustment(std::move(proposed), false);
  return selected_point_;
}

void CurvesEditorWidget::select_point(int index) {
  const auto& points = curve_points_for_channel(adjustment_, active_channel_);
  selected_point_ = points.empty() ? -1 : std::clamp(index, 0, static_cast<int>(points.size()) - 1);
  graph_->set_selected_point(selected_point_);
  updating_controls_ = true;
  sync_numeric_controls();
  updating_controls_ = false;
}

void CurvesEditorWidget::change_point(int index, int input, int output, bool gesture_finished) {
  auto points = curve_points_for_channel(adjustment_, active_channel_);
  if (index < 0 || index >= static_cast<int>(points.size())) {
    return;
  }
  output = std::clamp(output, 0, 255);
  const auto minimum_input = index > 0 ? points[static_cast<std::size_t>(index - 1)].input + 1 : 0;
  const auto maximum_input = index + 1 < static_cast<int>(points.size())
                                 ? points[static_cast<std::size_t>(index + 1)].input - 1
                                 : 255;
  input = std::clamp(input, minimum_input, maximum_input);
  points[static_cast<std::size_t>(index)] = CurveControlPoint{input, output};
  auto proposed = adjustment_;
  set_curve_points_for_channel(proposed, active_channel_, std::move(points));
  selected_point_ = index;
  propose_adjustment(std::move(proposed), gesture_finished);
}

void CurvesEditorWidget::delete_point(int index) {
  auto points = curve_points_for_channel(adjustment_, active_channel_);
  if (index <= 0 || index + 1 >= static_cast<int>(points.size())) {
    return;
  }
  points.erase(points.begin() + index);
  auto proposed = adjustment_;
  set_curve_points_for_channel(proposed, active_channel_, std::move(points));
  selected_point_ = std::max(0, index - 1);
  propose_adjustment(std::move(proposed), true);
}

void CurvesEditorWidget::cycle_point(int direction) {
  const auto& points = curve_points_for_channel(adjustment_, active_channel_);
  if (points.empty()) {
    return;
  }
  const auto count = static_cast<int>(points.size());
  selected_point_ = (selected_point_ + (direction < 0 ? -1 : 1) + count) % count;
  select_point(selected_point_);
}

void CurvesEditorWidget::reset_curves() {
  CurvesAdjustment reset;
  for (const auto channel : kCurvesChannels) {
    set_curve_points_for_channel(reset, channel, identity_curve_points());
  }
  selected_point_ = 0;
  propose_adjustment(std::move(reset), true);
}

void CurvesEditorWidget::auto_curve() {
  const auto& histogram = active_histogram();
  const auto total = std::accumulate(histogram.begin(), histogram.end(), std::uint64_t{0});
  if (total == 0) {
    return;
  }
  const auto threshold = std::max<std::uint64_t>(1, total / 1000U);
  std::uint64_t cumulative = 0;
  int black = 0;
  for (; black < 255; ++black) {
    cumulative += histogram[static_cast<std::size_t>(black)];
    if (cumulative > threshold) {
      break;
    }
  }
  cumulative = 0;
  int white = 255;
  for (; white > black; --white) {
    cumulative += histogram[static_cast<std::size_t>(white)];
    if (cumulative > threshold) {
      break;
    }
  }
  if (white <= black) {
    return;
  }

  CurveControlPoints points{{black, 0}, {white, 255}};
  auto proposed = adjustment_;
  set_curve_points_for_channel(proposed, active_channel_, std::move(points));
  selected_point_ = 0;
  propose_adjustment(std::move(proposed), true);
}

const std::array<std::uint32_t, 256>& CurvesEditorWidget::active_histogram() const {
  return histogram_for_channel(histograms_, active_channel_);
}

}  // namespace patchy::ui
