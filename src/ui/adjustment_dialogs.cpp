// Per-adjustment dialog half of filter_workflows.cpp, split out as a pure move:
// the Levels dialog widgets (gamma spin box, histogram input graph, output range),
// the request_levels/curves/hue_saturation/color_balance settings dialogs, and the
// hue/saturation converters. Declarations stay in ui/filter_workflows.hpp.

#include "ui/filter_workflows.hpp"

#include "formats/acv_curves_io.hpp"
#include "formats/photoshop_adjustment_presets.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/coalesced_preview_emitter.hpp"
#include "ui/color_panel.hpp"
#include "ui/curves_editor.hpp"
#include "ui/adjustment_presets.hpp"
#include "ui/curves_presets.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_qss.hpp"
#include "ui/theme_palette.hpp"
#include "core/pixel_depth.hpp"
#include "ui/filter_workflows_internal.hpp"
#include "ui/icon_theme.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/layer_style_gradient_controls.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QGridLayout>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSignalBlocker>
#include <QSlider>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabBar>
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
#include <initializer_list>
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
  // A painted track (GradientTrackSlider) instead of a plain slider.
  bool gradient_track{false};
};

// A slider whose track shows what it picks (#588, #591). The track colors are content, like a
// color swatch, so they stay the same in every theme; the frame and the handle use theme
// roles. A press jumps to the pointer and a drag follows it; the keys work as on any slider.
// The first, middle and last track colors ride the patchy.trackStart/Middle/End properties.
class GradientTrackSlider final : public QSlider {
public:
  GradientTrackSlider(const QString& object_name, QWidget* parent) : QSlider(Qt::Horizontal, parent) {
    setObjectName(object_name);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(20);
    setMinimumWidth(120);
  }

  void set_stops(QGradientStops stops) {
    if (stops == stops_) {
      return;
    }
    stops_ = std::move(stops);
    setProperty("patchy.trackStart", color_at(0.0));
    setProperty("patchy.trackMiddle", color_at(0.5));
    setProperty("patchy.trackEnd", color_at(1.0));
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) {
      painter.setOpacity(0.35);
    }
    const auto track = track_rect();
    QLinearGradient gradient(track.topLeft(), track.topRight());
    gradient.setStops(stops_.isEmpty()
                          ? QGradientStops{{0.0, theme().slider_groove_bg}, {1.0, theme().slider_groove_bg}}
                          : stops_);
    painter.setPen(QPen(theme().slider_groove_border, 1.0));
    painter.setBrush(gradient);
    painter.drawRoundedRect(track, 3.0, 3.0);
    const auto x = x_for_value(value());
    const QRectF handle(x - 4.0, 1.5, 8.0, height() - 3.0);
    painter.setPen(QPen(hasFocus() ? theme().accent : theme().slider_handle_border, 1.0));
    painter.setBrush(theme().slider_handle_bg);
    painter.drawRoundedRect(handle, 2.0, 2.0);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QSlider::mousePressEvent(event);
      return;
    }
    setFocus(Qt::MouseFocusReason);
    setSliderDown(true);
    setValue(value_for_x(event->position().x()));
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (isSliderDown()) {
      setValue(value_for_x(event->position().x()));
      event->accept();
      return;
    }
    QSlider::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (isSliderDown() && event->button() == Qt::LeftButton) {
      setSliderDown(false);
      event->accept();
      return;
    }
    QSlider::mouseReleaseEvent(event);
  }

private:
  [[nodiscard]] QRectF track_rect() const {
    return QRectF(5.5, height() / 2.0 - 4.0, std::max(1.0, width() - 11.0), 8.0);
  }

  [[nodiscard]] double x_for_value(int value) const {
    const auto track = track_rect();
    const auto span = std::max(1, maximum() - minimum());
    return track.left() + track.width() * static_cast<double>(value - minimum()) / span;
  }

  [[nodiscard]] int value_for_x(double x) const {
    const auto track = track_rect();
    const auto fraction = std::clamp((x - track.left()) / std::max(1.0, track.width()), 0.0, 1.0);
    return minimum() + static_cast<int>(std::lround(fraction * (maximum() - minimum())));
  }

  [[nodiscard]] QColor color_at(double position) const {
    if (stops_.isEmpty()) {
      return {};
    }
    if (position <= stops_.front().first) {
      return stops_.front().second;
    }
    for (qsizetype index = 1; index < stops_.size(); ++index) {
      const auto& right = stops_[index];
      if (position <= right.first) {
        const auto& left = stops_[index - 1];
        const auto t = right.first > left.first ? (position - left.first) / (right.first - left.first) : 1.0;
        const auto mix = [t](double from, double to) { return static_cast<float>(from + (to - from) * t); };
        return QColor::fromRgbF(mix(left.second.redF(), right.second.redF()),
                                mix(left.second.greenF(), right.second.greenF()),
                                mix(left.second.blueF(), right.second.blueF()));
      }
    }
    return stops_.back().second;
  }

  QGradientStops stops_;
};

// Track colors (content): a hue at the given strength, the hue wheel around `center`
// degrees, and an even ramp through the given colors.
QColor hue_track_color(double degrees, double saturation = 1.0, double value = 1.0) {
  const auto wrapped = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
  return QColor::fromHsvF(static_cast<float>(wrapped / 360.0), static_cast<float>(saturation),
                          static_cast<float>(value));
}

QGradientStops hue_wheel_stops(double center) {
  QGradientStops stops;
  for (int step = 0; step <= 12; ++step) {
    const auto t = step / 12.0;
    stops.append({t, hue_track_color(center - 180.0 + 360.0 * t)});
  }
  return stops;
}

QGradientStops ramp_stops(std::initializer_list<QColor> colors) {
  QGradientStops stops;
  const auto count = static_cast<int>(colors.size());
  int index = 0;
  for (const auto& color : colors) {
    stops.append({count > 1 ? static_cast<double>(index) / (count - 1) : 0.0, color});
    ++index;
  }
  return stops;
}

// The Preset combo of the Hue/Saturation and Levels dialogs: Custom (what the sliders hold
// when it matches no preset), the shipped presets, then the user's own presets after a
// separator. Item data: the shipped id, or "user:" and the user preset's name.
template <typename Settings>
class AdjustmentPresetCombo {
public:
  struct Shipped {
    QString id;
    QString name;
    Settings settings;
  };

  AdjustmentPresetCombo(QComboBox* combo, AdjustmentPresetKind kind, std::vector<Shipped> shipped,
                        std::function<Settings(const QString& path)> read_user,
                        std::function<bool(const Settings&, const Settings&)> same)
      : combo_(combo), kind_(kind), shipped_(std::move(shipped)), read_user_(std::move(read_user)),
        same_(std::move(same)) {
    reload();
  }

  // Rebuilds the items from the shipped list and the user's folder (a file that does not
  // read as a preset is left out).
  void reload() {
    const QSignalBlocker blocker(combo_);
    combo_->clear();
    user_.clear();
    combo_->addItem(QObject::tr("Custom"), QStringLiteral("custom"));
    for (const auto& preset : shipped_) {
      combo_->addItem(preset.name, preset.id);
    }
    for (const auto& preset : user_adjustment_presets(kind_)) {
      try {
        user_.push_back({preset.name, read_user_(preset.path)});
      } catch (const std::exception&) {
      }
    }
    if (!user_.empty()) {
      combo_->insertSeparator(combo_->count());
      for (const auto& user : user_) {
        combo_->addItem(user.first, QStringLiteral("user:") + user.first);
      }
    }
  }

  // Keeps the selected preset while the settings still equal it (a just-saved user preset
  // that equals a shipped one stays selected); otherwise selects the first preset the
  // settings equal (shipped first), or Custom.
  void select_matching(const Settings& current) {
    if (const auto selected = settings_at(combo_->currentIndex()); selected.has_value() && same_(*selected, current)) {
      return;
    }
    int index = 0;
    for (std::size_t row = 0; row < shipped_.size() && index == 0; ++row) {
      if (same_(shipped_[row].settings, current)) {
        index = combo_->findData(shipped_[row].id);
      }
    }
    for (std::size_t row = 0; row < user_.size() && index == 0; ++row) {
      if (same_(user_[row].second, current)) {
        index = combo_->findData(QStringLiteral("user:") + user_[row].first);
      }
    }
    const QSignalBlocker blocker(combo_);
    combo_->setCurrentIndex(std::max(0, index));
  }

  void select_user(const QString& name) {
    const QSignalBlocker blocker(combo_);
    combo_->setCurrentIndex(std::max(0, combo_->findData(QStringLiteral("user:") + name)));
  }

  [[nodiscard]] std::optional<Settings> settings_at(int index) const {
    const auto data = combo_->itemData(index).toString();
    for (const auto& preset : shipped_) {
      if (preset.id == data) {
        return preset.settings;
      }
    }
    for (const auto& user : user_) {
      if (QStringLiteral("user:") + user.first == data) {
        return user.second;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<QString> selected_user() const {
    const auto data = combo_->currentData().toString();
    if (data.startsWith(QStringLiteral("user:"))) {
      return data.mid(5);
    }
    return std::nullopt;
  }

private:
  QComboBox* combo_;
  AdjustmentPresetKind kind_;
  std::vector<Shipped> shipped_;
  std::function<Settings(const QString& path)> read_user_;
  std::function<bool(const Settings&, const Settings&)> same_;
  std::vector<std::pair<QString, Settings>> user_;
};

// Keeps the dialog buttons last in the tab order, after controls added once they existed;
// every other widget keeps its place in the chain.
void keep_buttons_last_in_tab_order(QDialog& dialog, const QDialogButtonBox& buttons) {
  std::vector<QWidget*> controls;
  std::vector<QWidget*> trailing;
  for (auto* widget = dialog.nextInFocusChain(); widget != nullptr && widget != &dialog;
       widget = widget->nextInFocusChain()) {
    if ((widget->focusPolicy() & Qt::TabFocus) == 0) {
      continue;
    }
    (buttons.isAncestorOf(widget) ? trailing : controls).push_back(widget);
  }
  controls.insert(controls.end(), trailing.begin(), trailing.end());
  for (std::size_t index = 1; index < controls.size(); ++index) {
    QWidget::setTabOrder(controls[index - 1], controls[index]);
  }
}

// Gives a dialog's OK and Cancel one width (#591): the wider of the two.
void match_ok_cancel_widths(QDialogButtonBox& buttons) {
  auto* ok = buttons.button(QDialogButtonBox::Ok);
  auto* cancel = buttons.button(QDialogButtonBox::Cancel);
  if (ok == nullptr || cancel == nullptr) {
    return;
  }
  const auto width = std::max(ok->sizeHint().width(), cancel->sizeHint().width());
  ok->setMinimumWidth(width);
  cancel->setMinimumWidth(width);
}

int levels_channel_tab_index(LevelsChannel channel) {
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

LevelsChannel levels_channel_from_tab_index(int index) {
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

CurvesChannel curves_channel_for_levels(LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return CurvesChannel::Red;
    case LevelsChannel::Green:
      return CurvesChannel::Green;
    case LevelsChannel::Blue:
      return CurvesChannel::Blue;
    case LevelsChannel::Rgb:
      break;
  }
  return CurvesChannel::Rgb;
}

std::array<int, 256> default_levels_histogram() {
  std::array<int, 256> histogram{};
  for (int index = 0; index < static_cast<int>(histogram.size()); ++index) {
    const auto x = static_cast<double>(index) / 255.0;
    // Shaped for the clipped-linear display (ceiling = 4x the mean bin): the
    // humps peak at roughly 35%, 50% and 60% of the graph and the spikes at
    // 50% to 75%, so the placeholder still reads as a photo histogram.
    const auto shadow = std::exp(-std::pow((x - 0.18) / 0.09, 2.0)) * 1900.0;
    const auto midtone = std::exp(-std::pow((x - 0.50) / 0.14, 2.0)) * 3000.0;
    const auto highlight = std::exp(-std::pow((x - 0.82) / 0.05, 2.0)) * 3400.0;
    histogram[static_cast<std::size_t>(index)] =
        static_cast<int>(std::round(50.0 + shadow + midtone + highlight));
  }
  histogram[188] += 2400;
  histogram[239] += 3800;
  histogram[254] += 4200;
  return histogram;
}

std::array<int, 256> levels_display_histogram(const CurvesHistograms& histograms, LevelsChannel channel) {
  // The composite is the sum of the channel counts, so an all-zero composite
  // means no usable samples anywhere and the decorative fallback applies.
  if (std::all_of(histograms.rgb.begin(), histograms.rgb.end(),
                  [](std::uint32_t count) { return count == 0; })) {
    return default_levels_histogram();
  }
  const auto& source = [&]() -> const std::array<std::uint32_t, 256>& {
    switch (channel) {
      case LevelsChannel::Red:
        return histograms.red;
      case LevelsChannel::Green:
        return histograms.green;
      case LevelsChannel::Blue:
        return histograms.blue;
      case LevelsChannel::Rgb:
        break;
    }
    return histograms.rgb;
  }();
  std::array<int, 256> histogram{};
  for (std::size_t index = 0; index < histogram.size(); ++index) {
    histogram[index] = static_cast<int>(source[index]);
  }
  return histogram;
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

  // A color channel's bars take that channel's hue, as in Curves; the
  // composite keeps the neutral bars.
  void set_channel(LevelsChannel channel) {
    if (channel_ != channel) {
      channel_ = channel;
      update();
    }
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

    std::uint64_t total = 0;
    std::uint64_t non_empty = 0;
    for (const auto count : histogram_) {
      total += static_cast<std::uint64_t>(std::max(0, count));
      non_empty += count > 0 ? 1 : 0;
    }
    const auto ceiling = histogram_display_ceiling(total, non_empty);
    const auto bar_color = channel_ == LevelsChannel::Rgb
                               ? QColor(218, 218, 218)
                               : channel_histogram_color(curves_channel_for_levels(channel_));
    painter.setPen(Qt::NoPen);
    for (int x = 0; x < graph.width(); ++x) {
      const auto first_bin = std::clamp((x * 256) / std::max(1, graph.width()), 0, 255);
      const auto last_bin = std::clamp(((x + 1) * 256) / std::max(1, graph.width()), first_bin + 1, 256);
      int count = 0;
      for (int bin = first_bin; bin < last_bin; ++bin) {
        count = std::max(count, histogram_[static_cast<std::size_t>(bin)]);
      }
      // Linear with a ceiling of four times the mean non-empty bin, like the
      // Curves graph: matches Photoshop's Levels dialog (issue 32).
      const auto scaled = histogram_display_fraction(static_cast<std::uint64_t>(std::max(0, count)), ceiling);
      const auto bar_height = std::clamp(static_cast<int>(std::round(scaled * (graph.height() - 8))), 1,
                                         std::max(1, graph.height() - 4));
      painter.fillRect(QRect(graph.left() + x, graph.bottom() - bar_height, 1, bar_height), bar_color);
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
  LevelsChannel channel_{LevelsChannel::Rgb};
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
  if (spec.gradient_track) {
    return add_dialog_slider_spin_row(form, &dialog, spec.label,
                                      new GradientTrackSlider(spec.object_prefix + QStringLiteral("Slider"), &dialog),
                                      spec.object_prefix + QStringLiteral("Spin"), spec.minimum, spec.maximum,
                                      spec.value, spec.suffix);
  }
  return add_dialog_slider_spin_row(form, &dialog, spec.label,
                                    spec.object_prefix + QStringLiteral("Slider"),
                                    spec.object_prefix + QStringLiteral("Spin"), spec.minimum,
                                    spec.maximum, spec.value, spec.suffix);
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

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(object_name + QStringLiteral("Buttons"));
  match_ok_cancel_widths(*buttons);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // Extras run once the whole dialog exists, so they can add rows, buttons and tab order.
  if (add_extras) {
    add_extras(dialog, form, spins, flush_preview);
    keep_buttons_last_in_tab_order(dialog, *buttons);
  }

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

namespace {

// A Levels eyedropper (#589): the Pick tool's icon with a corner square of the tone it sets
// (black, gray or white: content colors).
QToolButton* make_levels_eyedropper(QWidget* parent, const QString& object_name, QColor tone, const QString& tip) {
  auto* button = new QToolButton(parent);
  button->setObjectName(object_name);
  button->setCheckable(true);
  button->setAutoRaise(true);
  button->setToolTip(tip);
  button->setAccessibleName(tip);
  constexpr int kIcon = 16;
  const auto ratio = std::max(1.0, parent->devicePixelRatioF());
  auto pixmap = themed_svg_icon(QStringLiteral("tool-eyedropper")).pixmap(QSize(kIcon, kIcon), ratio);
  if (pixmap.isNull()) {
    pixmap = QPixmap(QSize(kIcon, kIcon) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
  }
  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(theme().field_inset_border, 1.0));
    painter.setBrush(tone);
    painter.drawRect(QRectF(kIcon - 7.5, kIcon - 7.5, 6.0, 6.0));
  }
  button->setIcon(QIcon(pixmap));
  button->setIconSize(QSize(kIcon, kIcon));
  return button;
}

}  // namespace

std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed, LevelsSettings initial,
    const PixelBuffer* histogram_source, CurvesDialogHooks hooks) {
  initial = clamp_levels_settings(initial);
  const auto sampled_histograms = curves_histograms_from_pixels(histogram_source);
  auto histogram_for_channel = [&sampled_histograms](LevelsChannel channel) {
    return levels_display_histogram(sampled_histograms, channel);
  };

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyLevelsDialog"));
  dialog.setWindowTitle(QObject::tr("Levels"));
  auto* root = new QHBoxLayout(&dialog);

  auto* controls = new QVBoxLayout();
  controls->setSpacing(6);
  root->addLayout(controls, 1);

  auto* preset_row = new QHBoxLayout();
  preset_row->setSpacing(10);
  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("levelsPresetCombo"));
  auto* preset_label = new QLabel(QObject::tr("Preset:"), &dialog);
  preset_label->setBuddy(preset);
  preset_row->addWidget(preset_label);
  preset_row->addWidget(preset, 1);
  controls->addLayout(preset_row);
  std::vector<AdjustmentPresetCombo<LevelsAdjustment>::Shipped> shipped_presets;
  for (const auto& entry : builtin_levels_presets()) {
    shipped_presets.push_back(
        {QString::fromLatin1(entry.id), QCoreApplication::translate("QObject", entry.name), entry.settings});
  }
  AdjustmentPresetCombo<LevelsAdjustment> presets(
      preset, AdjustmentPresetKind::Levels, std::move(shipped_presets),
      [](const QString& path) { return alv::read_file(to_filesystem_path(path)); }, levels_presets_match);

  // One click per channel, the same tab bar the Curves editor uses (issue 453).
  auto* channel = new QTabBar(&dialog);
  channel->setObjectName(QStringLiteral("levelsChannelTabs"));
  channel->setDrawBase(false);
  channel->setExpanding(true);
  channel->addTab(QObject::tr("RGB"));
  channel->addTab(QObject::tr("Red"));
  channel->addTab(QObject::tr("Green"));
  channel->addTab(QObject::tr("Blue"));
  set_channel_tab_swatches(*channel);
  controls->addWidget(channel);

  auto* input_label = new QLabel(QObject::tr("Input Levels:"), &dialog);
  controls->addWidget(input_label);

  auto* input_graph = new LevelsInputGraph(&dialog);
  input_graph->set_histogram(histogram_for_channel(initial.channel));
  input_graph->set_channel(initial.channel);
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
  configure_dialog_spinbox(black_input, 40);
  configure_dialog_spinbox(gamma, 40);
  configure_dialog_spinbox(white_input, 40);
  black_input->setToolTip(QObject::tr("Black input level"));
  gamma->setToolTip(QObject::tr("Gamma (midtones)"));
  white_input->setToolTip(QObject::tr("White input level"));

  // B:, G: and W: fields, each with the eyedropper that sets it from the image (#589).
  auto* black_pick = make_levels_eyedropper(&dialog, QStringLiteral("levelsBlackPointButton"), QColor(0, 0, 0),
                                            QObject::tr("Set the black point from the image"));
  auto* gray_pick = make_levels_eyedropper(&dialog, QStringLiteral("levelsGrayPointButton"), QColor(128, 128, 128),
                                           QObject::tr("Neutralize a gray point from the image"));
  auto* white_pick = make_levels_eyedropper(&dialog, QStringLiteral("levelsWhitePointButton"),
                                            QColor(255, 255, 255), QObject::tr("Set the white point from the image"));
  const std::array<QToolButton*, 3> eyedroppers{black_pick, gray_pick, white_pick};
  for (auto* button : eyedroppers) {
    button->setEnabled(static_cast<bool>(hooks.set_canvas_mode));
  }
  auto* input_values = new QHBoxLayout();
  input_values->setSpacing(4);
  const auto add_input_field = [&](const QString& text, QSpinBox* spin, QToolButton* pick, const QString& label_name) {
    auto* label = new QLabel(text, &dialog);
    label->setObjectName(label_name);
    label->setToolTip(spin->toolTip());
    label->setBuddy(spin);
    input_values->addWidget(label);
    input_values->addWidget(spin);
    input_values->addWidget(pick);
  };
  add_input_field(QObject::tr("B:"), black_input, black_pick, QStringLiteral("levelsBlackInputLabel"));
  input_values->addStretch(1);
  add_input_field(QObject::tr("G:"), gamma, gray_pick, QStringLiteral("levelsGammaLabel"));
  input_values->addStretch(1);
  add_input_field(QObject::tr("W:"), white_input, white_pick, QStringLiteral("levelsWhiteInputLabel"));
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
  side->addSpacing(8);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("levelsPreviewCheck"));
  preview->setChecked(true);

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
    channel->setCurrentIndex(levels_channel_tab_index(visible_channel));
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
    input_graph->set_channel(visible_channel);
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

  // Presets (#589): the combo follows the settings; Save, Delete, Import and Export sit in
  // the button column, all as wide as OK.
  AdjustmentPresetButtons* preset_buttons = nullptr;
  const auto sync_preset = [&] {
    presets.select_matching(committed_settings());
    if (preset_buttons != nullptr) {
      preset_buttons->refresh();
    }
  };
  const auto apply_preset = [&](LevelsAdjustment settings) {
    settings.channel = visible_channel;
    apply_settings_to_widgets(settings);
    sync_preset();
    flush_preview();
  };
  AdjustmentPresetButtons::Hooks preset_hooks;
  preset_hooks.write_current = [&](const QString& path) {
    alv::write_file(to_filesystem_path(path), committed_settings());
  };
  preset_hooks.check_file = [](const QString& path) { (void)alv::read_file(to_filesystem_path(path)); };
  preset_hooks.selected_user_preset = [&] { return presets.selected_user(); };
  preset_hooks.user_presets_changed = [&](const QString& name) {
    presets.reload();
    if (!name.isEmpty()) {
      presets.select_user(name);
      if (const auto chosen = presets.settings_at(preset->currentIndex());
          chosen.has_value() && !levels_presets_match(*chosen, committed_settings())) {
        apply_preset(*chosen);
        return;
      }
    }
    sync_preset();
  };
  preset_buttons = new AdjustmentPresetButtons(AdjustmentPresetKind::Levels, QStringLiteral("levels"),
                                               AdjustmentPresetButtons::Arrangement::Column, preset_hooks, &dialog);
  side->addWidget(preset_buttons);
  side->addStretch(1);
  side->addWidget(preview);
  {
    // OK, Cancel, Auto and the preset buttons share one width.
    std::vector<QPushButton*> column{buttons->button(QDialogButtonBox::Ok), buttons->button(QDialogButtonBox::Cancel),
                                     auto_button, preset_buttons->save_button(), preset_buttons->delete_button(),
                                     preset_buttons->import_button(), preset_buttons->export_button()};
    int width = 0;
    for (auto* button : column) {
      width = std::max(width, button->sizeHint().width());
    }
    for (auto* button : column) {
      button->setMinimumWidth(width);
    }
  }
  QObject::connect(preset, &QComboBox::activated, &dialog, [&](int index) {
    if (const auto chosen = presets.settings_at(index); chosen.has_value()) {
      apply_preset(*chosen);
    }
  });

  auto sync_from_widgets = [&] {
    if (updating) {
      return;
    }
    apply_settings_to_widgets(committed_settings());
    sync_preset();
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
  QObject::connect(channel, &QTabBar::currentChanged, &dialog, [&](int index) {
    if (updating) {
      return;
    }
    dialog_settings = committed_settings();
    visible_channel = levels_channel_from_tab_index(index);
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
    sync_preset();
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
    sync_preset();
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
    sync_preset();
    flush_preview();
  });

  // The eyedroppers sample the image under the pointer; one is active at a time, and each
  // pick sets every color channel's record from the samples so far (#589).
  CurvesEyedropperSamples eyedropper_samples;
  const auto activate_eyedropper = [&](CurvesCanvasMode mode) {
    if (!hooks.set_canvas_mode) {
      return;
    }
    hooks.set_canvas_mode(mode, [&, mode](const CurvesCanvasSample& sample) {
      const auto& gesture = sample.gesture;
      if (gesture.phase == CanvasReadPhase::Dismiss) {
        dialog.reject();
        return;
      }
      if (gesture.phase != CanvasReadPhase::Press || !sample.input_color.isValid() ||
          sample.input_color.alpha() == 0) {
        return;
      }
      const RgbColor color{static_cast<std::uint8_t>(sample.input_color.red()),
                           static_cast<std::uint8_t>(sample.input_color.green()),
                           static_cast<std::uint8_t>(sample.input_color.blue())};
      if (mode == CurvesCanvasMode::BlackPoint) {
        eyedropper_samples.black = color;
      } else if (mode == CurvesCanvasMode::GrayPoint) {
        eyedropper_samples.gray = color;
      } else {
        eyedropper_samples.white = color;
      }
      apply_settings_to_widgets(levels_adjustment_from_eyedropper_samples(committed_settings(), eyedropper_samples));
      sync_preset();
      flush_preview();
    });
  };
  for (std::size_t index = 0; index < eyedroppers.size(); ++index) {
    QObject::connect(eyedroppers[index], &QToolButton::toggled, &dialog, [&, index](bool checked) {
      constexpr std::array modes{CurvesCanvasMode::BlackPoint, CurvesCanvasMode::GrayPoint,
                                 CurvesCanvasMode::WhitePoint};
      if (checked) {
        for (std::size_t other = 0; other < eyedroppers.size(); ++other) {
          if (other != index) {
            const QSignalBlocker blocker(eyedroppers[other]);
            eyedroppers[other]->setChecked(false);
          }
        }
        activate_eyedropper(modes[index]);
      } else if (std::none_of(eyedroppers.begin(), eyedroppers.end(),
                              [](const QToolButton* button) { return button->isChecked(); }) &&
                 hooks.clear_canvas_mode) {
        hooks.clear_canvas_mode();
      }
    });
  }

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(&dialog, &QDialog::finished, &dialog, [&] {
    if (hooks.clear_canvas_mode) {
      hooks.clear_canvas_mode();
    }
  });

  apply_settings_to_widgets(initial);
  presets.select_matching(committed_settings());
  preset_buttons->refresh();
  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  const auto result = run_non_modal_dialog(dialog);
  if (hooks.clear_canvas_mode) {
    hooks.clear_canvas_mode();
  }
  if (result != QDialog::Accepted) {
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
  dialog.setMinimumSize(560, 560);

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
  // The user's own presets follow the shipped ones (item id "user:" and the name).
  std::vector<std::pair<QString, CurvesAdjustment>> user_curves;
  const auto rebuild_user_items = [&] {
    const QSignalBlocker blocker(preset_list);
    for (int row = preset_list->count() - 1; row > 0; --row) {
      if (preset_list->item(row)->data(kPresetIdRole).toString().startsWith(QStringLiteral("user:"))) {
        delete preset_list->takeItem(row);
      }
    }
    user_curves.clear();
    for (const auto& user : user_adjustment_presets(AdjustmentPresetKind::Curves)) {
      try {
        auto curves = acv::read_file(to_filesystem_path(user.path));
        auto* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(curves_adjustment_thumbnail(curves, kPresetThumbnailSize))), user.name,
            preset_list);
        item->setData(kPresetIdRole, QStringLiteral("user:") + user.name);
        item->setToolTip(user.name);
        user_curves.emplace_back(user.name, std::move(curves));
      } catch (const std::exception&) {
      }
    }
  };
  rebuild_user_items();
  const auto preset_curves_for = [&](const QString& id) -> std::optional<CurvesAdjustment> {
    if (id.startsWith(QStringLiteral("user:"))) {
      for (const auto& [name, curves] : user_curves) {
        if (QStringLiteral("user:") + name == id) {
          return curves;
        }
      }
      return std::nullopt;
    }
    if (const auto* builtin = find_curves_preset(id); builtin != nullptr) {
      return builtin->adjustment;
    }
    return std::nullopt;
  };

  // The preset strip on top with its buttons beside it (#592).
  auto* preset_row = new QHBoxLayout();
  preset_row->setSpacing(6);
  preset_row->addWidget(preset_list, 1);
  root->insertLayout(1, preset_row);

  auto* footer = new QHBoxLayout();
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
  AdjustmentPresetButtons* preset_buttons = nullptr;
  auto sync_curves_preset_selection = [&] {
    update_custom_preset_thumbnail();
    QString wanted;
    const auto* current_item = preset_list->currentItem();
    const auto current_id = current_item != nullptr ? current_item->data(kPresetIdRole).toString() : QString();
    if (const auto selected = preset_curves_for(current_id); selected.has_value() && *selected == dialog_settings) {
      wanted = current_id;
    } else if (const auto* matching = find_curves_preset(dialog_settings); matching != nullptr) {
      wanted = matching->id;
    } else {
      for (const auto& [name, curves] : user_curves) {
        if (curves == dialog_settings) {
          wanted = QStringLiteral("user:") + name;
          break;
        }
      }
    }
    QListWidgetItem* selected = custom_preset_item;
    for (int row = 1; row < preset_list->count() && !wanted.isEmpty(); ++row) {
      auto* item = preset_list->item(row);
      if (item != nullptr && item->data(kPresetIdRole).toString() == wanted) {
        selected = item;
        break;
      }
    }
    {
      const QSignalBlocker blocker(preset_list);
      preset_list->setCurrentItem(selected);
    }
    if (preset_buttons != nullptr) {
      preset_buttons->refresh();
    }
  };

  editor->adjustment_changed = [&](const CurvesSettings& settings, bool gesture_finished) {
    if (!applying_eyedropper_adjustment) {
      eyedropper_samples = {};
    }
    dialog_settings = settings;
    editor->set_adjustment(dialog_settings);
    select_custom_preset();
    if (preset_buttons != nullptr) {
      preset_buttons->refresh();
    }
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
                     if (preset_buttons != nullptr) {
                       preset_buttons->refresh();
                     }
                     if (id.isEmpty()) {
                       return;
                     }
                     const auto curves = preset_curves_for(id);
                     if (!curves.has_value()) {
                       return;
                     }
                     eyedropper_samples = {};
                     dialog_settings = *curves;
                     editor->set_adjustment(dialog_settings);
                     update_custom_preset_thumbnail();
                     flush_preview();
                   });

  // Save..., Delete, Import... and Export... (.acv) for the user's presets (#592).
  AdjustmentPresetButtons::Hooks preset_hooks;
  preset_hooks.write_current = [&](const QString& path) { acv::write_file(to_filesystem_path(path), dialog_settings); };
  preset_hooks.check_file = [](const QString& path) { (void)acv::read_file(to_filesystem_path(path)); };
  preset_hooks.selected_user_preset = [&]() -> std::optional<QString> {
    const auto* item = preset_list->currentItem();
    const auto id = item != nullptr ? item->data(kPresetIdRole).toString() : QString();
    if (id.startsWith(QStringLiteral("user:"))) {
      return id.mid(5);
    }
    return std::nullopt;
  };
  preset_hooks.user_presets_changed = [&](const QString& name) {
    rebuild_user_items();
    if (!name.isEmpty()) {
      const auto id = QStringLiteral("user:") + name;
      if (const auto curves = preset_curves_for(id); curves.has_value() && *curves != dialog_settings) {
        eyedropper_samples = {};
        dialog_settings = *curves;
        editor->set_adjustment(dialog_settings);
        flush_preview();
      }
      for (int row = 1; row < preset_list->count(); ++row) {
        if (preset_list->item(row)->data(kPresetIdRole).toString() == id) {
          const QSignalBlocker blocker(preset_list);
          preset_list->setCurrentRow(row);
          preset_list->scrollToItem(preset_list->item(row));
          break;
        }
      }
    }
    sync_curves_preset_selection();
  };
  preset_buttons = new AdjustmentPresetButtons(AdjustmentPresetKind::Curves, QStringLiteral("curves"),
                                               AdjustmentPresetButtons::Arrangement::Grid, preset_hooks, &dialog);
  preset_row->addWidget(preset_buttons, 0, Qt::AlignVCenter);
  {
    // Tab order follows the layout: the canvas tools, the presets and their buttons, then
    // the editor.
    QWidget* previous = clipping_button;
    for (QWidget* next : {static_cast<QWidget*>(preset_list), static_cast<QWidget*>(preset_buttons->save_button()),
                          static_cast<QWidget*>(preset_buttons->delete_button()),
                          static_cast<QWidget*>(preset_buttons->import_button()),
                          static_cast<QWidget*>(preset_buttons->export_button())}) {
      QWidget::setTabOrder(previous, next);
      previous = next;
    }
  }
  sync_curves_preset_selection();
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
  adjustment.bands = settings.bands;
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
  settings.bands = adjustment.bands;
  return settings;
}

namespace {

// The middle of a range's full-strength span, in degrees; Master and Colorize use red.
double hue_range_center(const HueSaturationSettings& settings, int range) {
  if (range <= 0 || range > 6) {
    return 0.0;
  }
  const auto& band = settings.bands[static_cast<std::size_t>(range - 1)];
  const auto span = ((band.inner_end - band.inner_start) % 360 + 360) % 360;
  return band.inner_start + span / 2.0;
}

// What a mid gray turns into under the colorize triple: the swatch beside Colorize (#588).
QColor colorize_swatch_color(const HueSaturationSettings& settings) {
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::HueSaturation;
  adjustment.hue_saturation = to_hue_saturation_adjustment(settings);
  adjustment.hue_saturation.colorize = true;
  const auto color = apply_adjustment_to_color(RgbColor{128, 128, 128}, adjustment);
  return QColor(color.red, color.green, color.blue);
}

// A color (content) in a theme-colored frame, faded while disabled. Its color rides the
// patchy.swatchColor property.
class ColorSwatchLabel final : public QWidget {
public:
  explicit ColorSwatchLabel(QWidget* parent) : QWidget(parent) {
    setFixedSize(34, 18);
  }

  void set_color(QColor color) {
    if (color != color_) {
      color_ = color;
      setProperty("patchy.swatchColor", color);
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) {
      painter.setOpacity(0.35);
    }
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(theme().field_inset_border, 1.0));
    painter.setBrush(color_);
    painter.drawRoundedRect(box, 4.0, 4.0);
  }

private:
  QColor color_{128, 128, 128};
};

GradientTrackSlider* gradient_slider(QDialog& dialog, const QString& object_name) {
  return dynamic_cast<GradientTrackSlider*>(dialog.findChild<QSlider*>(object_name));
}

}  // namespace

std::optional<HueSaturationSettings> request_hue_saturation_settings(
    QWidget* parent, std::function<void(bool, const HueSaturationSettings&)> preview_changed,
    HueSaturationSettings initial) {
  initial = to_hue_saturation_settings(to_hue_saturation_adjustment(initial));

  // The three sliders edit one target at a time: the colorize triple while
  // Colorize is checked, otherwise whichever entry the Edit combo selects
  // (Master, then Photoshop's six hue ranges). Every inactive triple is stashed
  // so switching targets round-trips all of them.
  auto stash = std::make_shared<HueSaturationSettings>(initial);
  auto colorize_check = std::make_shared<QCheckBox*>(nullptr);

  const auto rows_for = [](const HueSaturationSettings& value, bool colorize) {
    if (colorize) {
      return std::vector<SliderRowSpec>{
          {QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), 0, 360, value.colorize_hue, {}, true},
          {QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), 0, 100, value.colorize_saturation,
           {}, true},
          {QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100, value.colorize_lightness,
           {}, true}};
    }
    const auto range = std::clamp(value.edit_range, 0, 6);
    const auto hue = range == 0 ? value.hue_shift : value.bands[static_cast<std::size_t>(range - 1)].hue_shift;
    const auto saturation = range == 0 ? value.saturation_delta
                                       : value.bands[static_cast<std::size_t>(range - 1)].saturation_delta;
    const auto lightness = range == 0 ? value.lightness_delta
                                      : value.bands[static_cast<std::size_t>(range - 1)].lightness_delta;
    return std::vector<SliderRowSpec>{
        {QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), -180, 180, hue, {}, true},
        {QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), -100, 100, saturation, {}, true},
        {QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100, lightness, {}, true}};
  };

  // Reads the three spins back into whichever triple is currently selected.
  const auto capture = [](HueSaturationSettings& value, bool colorize, const std::vector<QSpinBox*>& spins) {
    if (colorize) {
      value.colorize_hue = spins[0]->value();
      value.colorize_saturation = spins[1]->value();
      value.colorize_lightness = spins[2]->value();
      return;
    }
    const auto range = std::clamp(value.edit_range, 0, 6);
    if (range == 0) {
      value.hue_shift = spins[0]->value();
      value.saturation_delta = spins[1]->value();
      value.lightness_delta = spins[2]->value();
      return;
    }
    auto& band = value.bands[static_cast<std::size_t>(range - 1)];
    band.hue_shift = spins[0]->value();
    band.saturation_delta = spins[1]->value();
    band.lightness_delta = spins[2]->value();
  };

  const auto build_settings = [stash, colorize_check, capture](const std::vector<QSpinBox*>& spins) {
    auto settings = *stash;
    settings.colorize = *colorize_check != nullptr && (*colorize_check)->isChecked();
    capture(settings, settings.colorize, spins);
    return settings;
  };

  // Retargets the slider ranges and values after the target changes.
  const auto retarget = [rows_for](QDialog& dialog, const HueSaturationSettings& value, bool colorize,
                                   const std::vector<QSpinBox*>& spins) {
    const auto rows = rows_for(value, colorize);
    for (std::size_t index = 0; index < spins.size() && index < rows.size(); ++index) {
      auto* slider = dialog.findChild<QSlider*>(rows[index].object_prefix + QStringLiteral("Slider"));
      if (slider != nullptr) {
        const QSignalBlocker block_slider(slider);
        slider->setRange(rows[index].minimum, rows[index].maximum);
        slider->setValue(rows[index].value);
      }
      const QSignalBlocker block_spin(spins[index]);
      spins[index]->setRange(rows[index].minimum, rows[index].maximum);
      spins[index]->setValue(rows[index].value);
    }
  };

  return request_adjustment_settings_dialog<HueSaturationSettings>(
      parent, QStringLiteral("patchyHueSaturationDialog"), QObject::tr("Hue/Saturation"),
      QStringLiteral("hueSaturationPreviewCheck"), rows_for(initial, initial.colorize), build_settings,
      std::move(preview_changed), {},
      [stash, colorize_check, capture, retarget, build_settings](QDialog& dialog, QFormLayout* form,
                                                                const std::vector<QSpinBox*>& spins,
                                                                const std::function<void()>& flush_preview) {
        auto* combo = new QComboBox(&dialog);
        combo->setObjectName(QStringLiteral("hueSaturationRangeCombo"));
        combo->addItems({QObject::tr("Master"), QObject::tr("Reds"), QObject::tr("Yellows"), QObject::tr("Greens"),
                         QObject::tr("Cyans"), QObject::tr("Blues"), QObject::tr("Magentas")});
        combo->setCurrentIndex(std::clamp(stash->edit_range, 0, 6));
        combo->setEnabled(!stash->colorize);
        form->insertRow(0, QObject::tr("Edit:"), combo);

        // Colorize, with a swatch of the color it gives a mid gray.
        auto* colorize_row = new QWidget(&dialog);
        auto* colorize_layout = new QHBoxLayout(colorize_row);
        colorize_layout->setContentsMargins(0, 0, 0, 0);
        auto* check = new QCheckBox(QObject::tr("Colorize"), colorize_row);
        check->setObjectName(QStringLiteral("hueSaturationColorizeCheck"));
        check->setChecked(stash->colorize);
        *colorize_check = check;
        auto* swatch = new ColorSwatchLabel(colorize_row);
        swatch->setObjectName(QStringLiteral("hueSaturationColorizeSwatch"));
        swatch->setToolTip(QObject::tr("The color Colorize gives a mid gray"));
        swatch->setAccessibleName(QObject::tr("Colorize color"));
        colorize_layout->addWidget(check);
        colorize_layout->addWidget(swatch);
        colorize_layout->addStretch(1);
        form->addRow(QString(), colorize_row);

        // Presets on top (#588): the combo, then Save, Delete, Import and Export.
        auto* preset_area = new QWidget(&dialog);
        preset_area->setObjectName(QStringLiteral("hueSaturationPresetArea"));
        auto* preset_layout = new QVBoxLayout(preset_area);
        preset_layout->setContentsMargins(0, 0, 0, 4);
        preset_layout->setSpacing(6);
        auto* combo_row = new QHBoxLayout();
        combo_row->setSpacing(10);
        auto* preset_combo = new QComboBox(preset_area);
        preset_combo->setObjectName(QStringLiteral("hueSaturationPresetCombo"));
        auto* preset_label = new QLabel(QObject::tr("Preset:"), preset_area);
        preset_label->setBuddy(preset_combo);
        combo_row->addWidget(preset_label);
        combo_row->addWidget(preset_combo, 1);
        preset_layout->addLayout(combo_row);
        std::vector<AdjustmentPresetCombo<HueSaturationAdjustment>::Shipped> shipped;
        for (const auto& preset : builtin_hue_saturation_presets()) {
          shipped.push_back({QString::fromLatin1(preset.id), QCoreApplication::translate("QObject", preset.name),
                             preset.settings});
        }
        auto presets = std::make_shared<AdjustmentPresetCombo<HueSaturationAdjustment>>(
            preset_combo, AdjustmentPresetKind::HueSaturation, std::move(shipped),
            [](const QString& path) { return ahu::read_file(to_filesystem_path(path)); },
            hue_saturation_presets_match);
        const auto current = [build_settings, spins] { return build_settings(spins); };

        // Everything that shows the settings: the tracks, the swatch, the preset selection.
        auto preset_buttons = std::make_shared<AdjustmentPresetButtons*>(nullptr);
        const auto refresh = [&dialog, current, swatch, presets, preset_buttons] {
          const auto settings = current();
          auto* hue = gradient_slider(dialog, QStringLiteral("hueSaturationHueSlider"));
          auto* saturation = gradient_slider(dialog, QStringLiteral("hueSaturationSaturationSlider"));
          auto* lightness = gradient_slider(dialog, QStringLiteral("hueSaturationLightnessSlider"));
          if (hue != nullptr && saturation != nullptr && lightness != nullptr) {
            const QColor gray(128, 128, 128);
            if (settings.colorize) {
              hue->set_stops(hue_wheel_stops(180.0));
              saturation->set_stops(ramp_stops({gray, hue_track_color(settings.colorize_hue)}));
              auto mid = settings;
              mid.colorize_lightness = 0;
              lightness->set_stops(ramp_stops({QColor(0, 0, 0), colorize_swatch_color(mid), QColor(255, 255, 255)}));
            } else {
              const auto range = std::clamp(settings.edit_range, 0, 6);
              const auto center = hue_range_center(settings, range);
              const auto shift = range == 0 ? settings.hue_shift
                                            : settings.bands[static_cast<std::size_t>(range - 1)].hue_shift;
              hue->set_stops(hue_wheel_stops(center));
              const auto shifted = center + shift;
              saturation->set_stops(
                  ramp_stops({gray, hue_track_color(shifted, 0.5, 0.8), hue_track_color(shifted)}));
              lightness->set_stops(
                  ramp_stops({QColor(0, 0, 0), hue_track_color(shifted, 0.75, 0.9), QColor(255, 255, 255)}));
            }
          }
          swatch->set_color(colorize_swatch_color(settings));
          swatch->setEnabled(settings.colorize);
          presets->select_matching(to_hue_saturation_adjustment(settings));
          if (*preset_buttons != nullptr) {
            (*preset_buttons)->refresh();
          }
        };

        // A preset replaces every triple; a range preset shows its range in Edit.
        const auto apply_preset = [&dialog, stash, combo, check, spins, retarget, refresh,
                                   flush_preview](const HueSaturationAdjustment& adjustment) {
          auto next = to_hue_saturation_settings(adjustment);
          next.edit_range = 0;
          if (!next.colorize && next.hue_shift == 0 && next.saturation_delta == 0 && next.lightness_delta == 0) {
            for (std::size_t band = 0; band < next.bands.size(); ++band) {
              if (next.bands[band].has_effect()) {
                next.edit_range = static_cast<int>(band) + 1;
                break;
              }
            }
          }
          *stash = next;
          {
            const QSignalBlocker block_check(check);
            const QSignalBlocker block_combo(combo);
            check->setChecked(next.colorize);
            combo->setEnabled(!next.colorize);
            combo->setCurrentIndex(next.edit_range);
          }
          retarget(dialog, *stash, next.colorize, spins);
          refresh();
          flush_preview();
        };

        AdjustmentPresetButtons::Hooks hooks;
        hooks.write_current = [current](const QString& path) {
          ahu::write_file(to_filesystem_path(path), to_hue_saturation_adjustment(current()));
        };
        hooks.check_file = [](const QString& path) { (void)ahu::read_file(to_filesystem_path(path)); };
        hooks.selected_user_preset = [presets] { return presets->selected_user(); };
        hooks.user_presets_changed = [presets, current, apply_preset, refresh, preset_combo](const QString& name) {
          presets->reload();
          if (!name.isEmpty()) {
            presets->select_user(name);
            if (const auto chosen = presets->settings_at(preset_combo->currentIndex());
                chosen.has_value() &&
                !hue_saturation_presets_match(*chosen, to_hue_saturation_adjustment(current()))) {
              apply_preset(*chosen);
              return;
            }
          }
          refresh();
        };
        auto* buttons = new AdjustmentPresetButtons(AdjustmentPresetKind::HueSaturation,
                                                    QStringLiteral("hueSaturation"),
                                                    AdjustmentPresetButtons::Arrangement::Row, hooks, preset_area);
        *preset_buttons = buttons;
        preset_layout->addWidget(buttons);
        if (auto* layout = qobject_cast<QVBoxLayout*>(dialog.layout()); layout != nullptr) {
          layout->insertWidget(0, preset_area);
        }
        refresh();

        QObject::connect(preset_combo, &QComboBox::activated, &dialog, [presets, apply_preset](int index) {
          if (const auto chosen = presets->settings_at(index); chosen.has_value()) {
            apply_preset(*chosen);
          }
        });
        for (auto* spin : spins) {
          QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [refresh](int) { refresh(); });
        }
        QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog,
                         [&dialog, stash, spins, capture, retarget, refresh, flush_preview](int index) {
                           capture(*stash, false, spins);
                           stash->edit_range = std::clamp(index, 0, 6);
                           retarget(dialog, *stash, false, spins);
                           refresh();
                           flush_preview();
                         });
        QObject::connect(check, &QCheckBox::toggled, &dialog,
                         [&dialog, stash, combo, spins, capture, retarget, refresh, flush_preview](bool colorize) {
                           // Stash the outgoing triple, then retarget the sliders.
                           capture(*stash, !colorize, spins);
                           combo->setEnabled(!colorize);
                           retarget(dialog, *stash, colorize, spins);
                           refresh();
                           flush_preview();
                         });
        // Top to bottom: presets, Edit, the sliders, Colorize, Preview.
        QWidget* previous = preset_combo;
        for (QWidget* next : {static_cast<QWidget*>(buttons->save_button()), static_cast<QWidget*>(buttons->delete_button()),
                              static_cast<QWidget*>(buttons->import_button()),
                              static_cast<QWidget*>(buttons->export_button()), static_cast<QWidget*>(combo)}) {
          QWidget::setTabOrder(previous, next);
          previous = next;
        }
        for (auto* spin : spins) {
          if (auto* slider = spin->parentWidget()->findChild<QSlider*>()) {
            QWidget::setTabOrder(previous, slider);
            previous = slider;
          }
          QWidget::setTabOrder(previous, spin);
          previous = spin;
        }
        QWidget::setTabOrder(previous, check);
        if (auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("hueSaturationPreviewCheck"))) {
          QWidget::setTabOrder(check, preview);
        }
      });
}

std::optional<ColorBalanceSettings> request_color_balance_settings(
    QWidget* parent, std::function<void(bool, const ColorBalanceSettings&)> preview_changed,
    ColorBalanceSettings initial) {
  const auto clamp_tone = [](ColorBalanceTone tone) {
    return ColorBalanceTone{std::clamp(tone.cyan_red, -100, 100), std::clamp(tone.magenta_green, -100, 100),
                            std::clamp(tone.yellow_blue, -100, 100)};
  };
  for (const auto range :
       {ColorBalanceToneRange::Shadows, ColorBalanceToneRange::Midtones, ColorBalanceToneRange::Highlights}) {
    initial.set_tone(range, clamp_tone(initial.tone(range)));
  }

  // The three sliders edit the tone range the tabs select (Midtones first, as in
  // Photoshop); the other two triples are stashed so switching tones round-trips all of
  // them.
  struct State {
    ColorBalanceSettings settings;
    ColorBalanceToneRange range{ColorBalanceToneRange::Midtones};
    QCheckBox* preserve{nullptr};
  };
  auto state = std::make_shared<State>(State{initial});

  const auto capture = [](State& value, const std::vector<QSpinBox*>& spins) {
    value.settings.set_tone(value.range,
                            ColorBalanceTone{spins[0]->value(), spins[1]->value(), spins[2]->value()});
    if (value.preserve != nullptr) {
      value.settings.preserve_luminosity = value.preserve->isChecked();
    }
  };
  const auto build_settings = [state, capture](const std::vector<QSpinBox*>& spins) {
    auto copy = *state;
    capture(copy, spins);
    return copy.settings;
  };

  const auto midtones = initial.tone(ColorBalanceToneRange::Midtones);
  return request_adjustment_settings_dialog<ColorBalanceSettings>(
      parent, QStringLiteral("patchyColorBalanceDialog"), QObject::tr("Color Balance"),
      QStringLiteral("colorBalancePreviewCheck"),
      {{QObject::tr("Cyan / Red"), QStringLiteral("colorBalanceCyanRed"), -100, 100, midtones.cyan_red, {}, true},
       {QObject::tr("Magenta / Green"), QStringLiteral("colorBalanceMagentaGreen"), -100, 100,
        midtones.magenta_green, {}, true},
       {QObject::tr("Yellow / Blue"), QStringLiteral("colorBalanceYellowBlue"), -100, 100, midtones.yellow_blue, {},
        true}},
      build_settings, std::move(preview_changed), {},
      [state, capture, initial](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>& spins,
                                const std::function<void()>& flush_preview) {
        // One tab per tone range (#591), in tonal order.
        constexpr std::array<ColorBalanceToneRange, 3> kRanges{
            ColorBalanceToneRange::Shadows, ColorBalanceToneRange::Midtones, ColorBalanceToneRange::Highlights};
        auto* tabs = new QTabBar(&dialog);
        tabs->setObjectName(QStringLiteral("colorBalanceToneTabs"));
        tabs->setDrawBase(false);
        tabs->setExpanding(true);
        tabs->addTab(QObject::tr("Shadows"));
        tabs->addTab(QObject::tr("Midtones"));
        tabs->addTab(QObject::tr("Highlights"));
        tabs->setCurrentIndex(static_cast<int>(std::find(kRanges.begin(), kRanges.end(), state->range) -
                                               kRanges.begin()));
        if (auto* layout = qobject_cast<QVBoxLayout*>(dialog.layout()); layout != nullptr) {
          layout->insertWidget(0, tabs);
        }

        auto* preserve = new QCheckBox(QObject::tr("Preserve Luminosity"), &dialog);
        preserve->setObjectName(QStringLiteral("colorBalancePreserveLuminosityCheck"));
        preserve->setChecked(state->settings.preserve_luminosity);
        state->preserve = preserve;
        form->addRow(QString(), preserve);

        // Each track runs between the two colors it trades (content colors).
        const std::array<QString, 3> prefixes{QStringLiteral("colorBalanceCyanRed"),
                                              QStringLiteral("colorBalanceMagentaGreen"),
                                              QStringLiteral("colorBalanceYellowBlue")};
        const std::array<std::pair<QColor, QColor>, 3> ends{
            std::pair{QColor(0, 255, 255), QColor(255, 0, 0)}, std::pair{QColor(255, 0, 255), QColor(0, 255, 0)},
            std::pair{QColor(255, 255, 0), QColor(0, 0, 255)}};
        for (std::size_t row = 0; row < prefixes.size(); ++row) {
          if (auto* slider = gradient_slider(dialog, prefixes[row] + QStringLiteral("Slider"))) {
            slider->set_stops(ramp_stops({ends[row].first, ends[row].second}));
          }
        }

        const auto show_tone = [&dialog, state, spins, prefixes] {
          const auto tone = state->settings.tone(state->range);
          const std::array<int, 3> values{tone.cyan_red, tone.magenta_green, tone.yellow_blue};
          for (std::size_t row = 0; row < spins.size() && row < values.size(); ++row) {
            if (auto* slider = dialog.findChild<QSlider*>(prefixes[row] + QStringLiteral("Slider"))) {
              const QSignalBlocker block_slider(slider);
              slider->setValue(values[row]);
            }
            const QSignalBlocker block_spin(spins[row]);
            spins[row]->setValue(values[row]);
          }
        };
        QObject::connect(tabs, &QTabBar::currentChanged, &dialog,
                         [state, spins, capture, show_tone, flush_preview, kRanges](int index) {
                           capture(*state, spins);
                           state->range = kRanges[static_cast<std::size_t>(std::clamp(index, 0, 2))];
                           show_tone();
                           flush_preview();
                         });
        QObject::connect(preserve, &QCheckBox::toggled, &dialog,
                         [flush_preview](bool) { flush_preview(); });

        // Reset, bottom left on the OK/Cancel line: back to the settings the dialog opened with.
        if (auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("patchyColorBalanceDialogButtons"))) {
          auto* reset = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
          reset->setObjectName(QStringLiteral("colorBalanceResetButton"));
          reset->setToolTip(QObject::tr("Restore the settings the dialog opened with"));
          reset->setAutoDefault(false);
          QObject::connect(reset, &QPushButton::clicked, &dialog,
                           [state, preserve, initial, show_tone, flush_preview] {
                             state->settings = initial;
                             {
                               const QSignalBlocker block(preserve);
                               preserve->setChecked(initial.preserve_luminosity);
                             }
                             show_tone();
                             flush_preview();
                           });
        }

        // Top to bottom (#591): tone tabs, the sliders, Preserve Luminosity, Preview.
        QWidget* previous = tabs;
        tabs->setFocusPolicy(Qt::StrongFocus);
        for (auto* spin : spins) {
          if (auto* slider = spin->parentWidget()->findChild<QSlider*>()) {
            QWidget::setTabOrder(previous, slider);
            previous = slider;
          }
          QWidget::setTabOrder(previous, spin);
          previous = spin;
        }
        QWidget::setTabOrder(previous, preserve);
        if (auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("colorBalancePreviewCheck"))) {
          QWidget::setTabOrder(preserve, preview);
        }
      });
}

std::optional<PosterizeSettings> request_posterize_settings(
    QWidget* parent, std::function<void(bool, const PosterizeSettings&)> preview_changed,
    PosterizeSettings initial) {
  initial.levels = std::clamp(initial.levels, 2, 255);
  return request_adjustment_settings_dialog<PosterizeSettings>(
      parent, QStringLiteral("patchyPosterizeDialog"), QObject::tr("Posterize"),
      QStringLiteral("posterizePreviewCheck"),
      {{QObject::tr("Levels"), QStringLiteral("posterizeLevels"), 2, 255, initial.levels, {}}},
      [](const std::vector<QSpinBox*>& spins) { return PosterizeSettings{spins[0]->value()}; },
      std::move(preview_changed));
}

std::optional<ThresholdSettings> request_threshold_settings(
    QWidget* parent, std::function<void(bool, const ThresholdSettings&)> preview_changed,
    ThresholdSettings initial) {
  initial.level = std::clamp(initial.level, 1, 255);
  return request_adjustment_settings_dialog<ThresholdSettings>(
      parent, QStringLiteral("patchyThresholdDialog"), QObject::tr("Threshold"),
      QStringLiteral("thresholdPreviewCheck"),
      {{QObject::tr("Threshold Level"), QStringLiteral("thresholdLevel"), 1, 255, initial.level, {}}},
      [](const std::vector<QSpinBox*>& spins) { return ThresholdSettings{spins[0]->value()}; },
      std::move(preview_changed));
}

std::optional<ExposureSettings> request_exposure_settings(
    QWidget* parent, std::function<void(bool, const ExposureSettings&)> preview_changed, ExposureSettings initial) {
  initial = clamp_exposure(initial);
  // Photoshop's three fields are decimals, so the shared integer slider rows do not
  // fit; the rows are added as extras and read back through this holder.
  struct Fields {
    QDoubleSpinBox* exposure{nullptr};
    QDoubleSpinBox* offset{nullptr};
    QDoubleSpinBox* gamma{nullptr};
  };
  auto fields = std::make_shared<Fields>();
  const auto build_settings = [fields, initial](const std::vector<QSpinBox*>&) {
    if (fields->exposure == nullptr || fields->offset == nullptr || fields->gamma == nullptr) {
      return initial;
    }
    return clamp_exposure(ExposureSettings{static_cast<int>(std::lround(fields->exposure->value() * 100.0)),
                                           static_cast<int>(std::lround(fields->offset->value() * 10000.0)),
                                           static_cast<int>(std::lround(fields->gamma->value() * 100.0))});
  };
  return request_adjustment_settings_dialog<ExposureSettings>(
      parent, QStringLiteral("patchyExposureDialog"), QObject::tr("Exposure"),
      QStringLiteral("exposurePreviewCheck"), {}, build_settings, std::move(preview_changed), {},
      [fields, initial](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>&,
                        const std::function<void()>& flush_preview) {
        const auto add_row = [&dialog, form, flush_preview](const QString& label, const QString& object_name,
                                                             double minimum, double maximum, int decimals,
                                                             double step, double value) {
          auto* spin = new QDoubleSpinBox(&dialog);
          spin->setObjectName(object_name);
          spin->setDecimals(decimals);
          spin->setRange(minimum, maximum);
          spin->setSingleStep(step);
          spin->setValue(value);
          form->addRow(label, spin);
          QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                           [flush_preview](double) { flush_preview(); });
          return spin;
        };
        fields->exposure = add_row(QObject::tr("Exposure:"), QStringLiteral("exposureValueSpin"),
                                   -kExposureValueRange / 100.0, kExposureValueRange / 100.0, 2, 0.1,
                                   initial.exposure_hundredths / 100.0);
        fields->offset = add_row(QObject::tr("Offset:"), QStringLiteral("exposureOffsetSpin"),
                                 -kExposureOffsetRange / 10000.0, kExposureOffsetRange / 10000.0, 4, 0.01,
                                 initial.offset_ten_thousandths / 10000.0);
        fields->gamma = add_row(QObject::tr("Gamma Correction:"), QStringLiteral("exposureGammaSpin"),
                                kExposureGammaMin / 100.0, kExposureGammaMax / 100.0, 2, 0.05,
                                initial.gamma_hundredths / 100.0);
      });
}

std::optional<ChannelMixerSettings> request_channel_mixer_settings(
    QWidget* parent, std::function<void(bool, const ChannelMixerSettings&)> preview_changed,
    ChannelMixerSettings initial) {
  initial.red = clamp_channel_mixer_row(initial.red);
  initial.green = clamp_channel_mixer_row(initial.green);
  initial.blue = clamp_channel_mixer_row(initial.blue);
  initial.gray = clamp_channel_mixer_row(initial.gray);

  // The four sliders edit one output row at a time: the Output Channel combo picks red,
  // green or blue, and Monochrome switches them to the gray row. The other rows are
  // stashed so switching round-trips all of them.
  struct State {
    ChannelMixerSettings settings;  // settings.monochrome is the mode the sliders show
    int output{0};                  // 0 red, 1 green, 2 blue
  };
  auto state = std::make_shared<State>(State{initial});
  const auto row_of = [](ChannelMixerSettings& value, int output, bool monochrome) -> ChannelMixerRow& {
    if (monochrome) {
      return value.gray;
    }
    return output == 1 ? value.green : output == 2 ? value.blue : value.red;
  };
  const auto capture = [row_of](State& value, const std::vector<QSpinBox*>& spins) {
    row_of(value.settings, value.output, value.settings.monochrome) =
        ChannelMixerRow{spins[0]->value(), spins[1]->value(), spins[2]->value(), spins[3]->value()};
  };
  const auto build_settings = [state, capture](const std::vector<QSpinBox*>& spins) {
    auto copy = *state;
    capture(copy, spins);
    return copy.settings;
  };
  const auto show_row = [row_of](QDialog& dialog, State& value, const std::vector<QSpinBox*>& spins) {
    const auto& row = row_of(value.settings, value.output, value.settings.monochrome);
    const std::array<int, 4> values{row.red, row.green, row.blue, row.constant};
    const std::array<QString, 4> prefixes{QStringLiteral("channelMixerRed"), QStringLiteral("channelMixerGreen"),
                                          QStringLiteral("channelMixerBlue"), QStringLiteral("channelMixerConstant")};
    for (std::size_t index = 0; index < spins.size() && index < values.size(); ++index) {
      if (auto* slider = dialog.findChild<QSlider*>(prefixes[index] + QStringLiteral("Slider"))) {
        const QSignalBlocker block_slider(slider);
        slider->setValue(values[index]);
      }
      const QSignalBlocker block_spin(spins[index]);
      spins[index]->setValue(values[index]);
    }
  };

  const auto& first = initial.monochrome ? initial.gray : initial.red;
  const auto percent = QStringLiteral(" %");
  return request_adjustment_settings_dialog<ChannelMixerSettings>(
      parent, QStringLiteral("patchyChannelMixerDialog"), QObject::tr("Channel Mixer"),
      QStringLiteral("channelMixerPreviewCheck"),
      {{QObject::tr("Red"), QStringLiteral("channelMixerRed"), -200, 200, first.red, percent},
       {QObject::tr("Green"), QStringLiteral("channelMixerGreen"), -200, 200, first.green, percent},
       {QObject::tr("Blue"), QStringLiteral("channelMixerBlue"), -200, 200, first.blue, percent},
       {QObject::tr("Constant"), QStringLiteral("channelMixerConstant"), -200, 200, first.constant, percent}},
      build_settings, std::move(preview_changed), {},
      [state, capture, show_row](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>& spins,
                                 const std::function<void()>& flush_preview) {
        auto* combo = new QComboBox(&dialog);
        combo->setObjectName(QStringLiteral("channelMixerOutputCombo"));
        combo->addItems({QObject::tr("Red"), QObject::tr("Green"), QObject::tr("Blue")});
        form->insertRow(0, QObject::tr("Output Channel:"), combo);

        auto* monochrome = new QCheckBox(QObject::tr("Monochrome"), &dialog);
        monochrome->setObjectName(QStringLiteral("channelMixerMonochromeCheck"));
        monochrome->setChecked(state->settings.monochrome);
        combo->setEnabled(!state->settings.monochrome);
        form->addRow(QString(), monochrome);

        QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog,
                         [&dialog, state, spins, capture, show_row, flush_preview](int index) {
                           capture(*state, spins);
                           state->output = std::clamp(index, 0, 2);
                           show_row(dialog, *state, spins);
                           flush_preview();
                         });
        QObject::connect(monochrome, &QCheckBox::toggled, &dialog,
                         [&dialog, state, combo, spins, capture, show_row, flush_preview](bool checked) {
                           // Stash the outgoing row under the old mode, then show the other.
                           capture(*state, spins);
                           state->settings.monochrome = checked;
                           combo->setEnabled(!checked);
                           show_row(dialog, *state, spins);
                           flush_preview();
                         });
      });
}

std::optional<PhotoFilterSettings> request_photo_filter_settings(
    QWidget* parent, std::function<void(bool, const PhotoFilterSettings&)> preview_changed,
    PhotoFilterSettings initial) {
  // Color and Preserve Luminosity live outside the slider rows; the density spin is the
  // only row, so build_settings reads the rest from this shared state.
  auto state = std::make_shared<PhotoFilterSettings>(clamp_photo_filter(initial));
  const auto build_settings = [state](const std::vector<QSpinBox*>& spins) {
    auto settings = *state;
    settings.density = spins.front()->value();
    return clamp_photo_filter(settings);
  };
  return request_adjustment_settings_dialog<PhotoFilterSettings>(
      parent, QStringLiteral("patchyPhotoFilterDialog"), QObject::tr("Photo Filter"),
      QStringLiteral("photoFilterPreviewCheck"),
      {{QObject::tr("Density"), QStringLiteral("photoFilterDensity"), 1, 100, state->density, QStringLiteral(" %")}},
      build_settings, std::move(preview_changed), {},
      [state](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>&,
              const std::function<void()>& flush_preview) {
        auto* swatch = new QPushButton(&dialog);
        swatch->setObjectName(QStringLiteral("photoFilterColorButton"));
        swatch->setAccessibleName(QObject::tr("Filter color"));
        const auto show = [swatch, state] {
          const auto color = photo_filter_color(*state);
          set_themed_style(*swatch, color_button_style(QColor(color.red, color.green, color.blue)));
        };
        show();
        form->insertRow(0, QObject::tr("Filter Color:"), swatch);

        auto* preserve = new QCheckBox(QObject::tr("Preserve Luminosity"), &dialog);
        preserve->setObjectName(QStringLiteral("photoFilterPreserveCheck"));
        preserve->setChecked(state->preserve_luminosity);
        form->addRow(QString(), preserve);

        QObject::connect(preserve, &QCheckBox::toggled, &dialog, [state, flush_preview](bool checked) {
          state->preserve_luminosity = checked;
          flush_preview();
        });
        QObject::connect(swatch, &QPushButton::clicked, &dialog, [&dialog, swatch, state, show, flush_preview] {
          // Picks preview live; Cancel puts the original color back.
          const QPointer<QPushButton> alive(swatch);
          const auto original = *state;
          const auto apply = [alive, state, show, flush_preview](QColor picked) {
            if (alive == nullptr) {
              return;
            }
            *state = photo_filter_with_color(*state, RgbColor{static_cast<std::uint8_t>(picked.red()),
                                                              static_cast<std::uint8_t>(picked.green()),
                                                              static_cast<std::uint8_t>(picked.blue())});
            show();
            flush_preview();
          };
          const auto current = photo_filter_color(original);
          const auto picked = request_patchy_color(&dialog, QColor(current.red, current.green, current.blue),
                                                   QObject::tr("Filter Color"), apply);
          if (picked.has_value()) {
            apply(*picked);
          } else if (alive != nullptr) {
            state->lab_l = original.lab_l;
            state->lab_a = original.lab_a;
            state->lab_b = original.lab_b;
            show();
            flush_preview();
          }
        });
      });
}

std::optional<SelectiveColorSettings> request_selective_color_settings(
    QWidget* parent, std::function<void(bool, const SelectiveColorSettings&)> preview_changed,
    SelectiveColorSettings initial) {
  struct State {
    SelectiveColorSettings settings;
    int range{0};  // SelectiveColorRange index the sliders show
  };
  auto state = std::make_shared<State>(State{clamp_selective_color(initial)});
  const auto capture = [](State& value, const std::vector<QSpinBox*>& spins) {
    auto& range = value.settings.ranges[static_cast<std::size_t>(value.range)];
    range = SelectiveColorCorrection{spins[0]->value(), spins[1]->value(), spins[2]->value(), spins[3]->value()};
  };
  const auto build_settings = [state, capture](const std::vector<QSpinBox*>& spins) {
    auto copy = *state;
    capture(copy, spins);
    return copy.settings;
  };
  const auto show_range = [](QDialog& dialog, State& value, const std::vector<QSpinBox*>& spins) {
    const auto& range = value.settings.ranges[static_cast<std::size_t>(value.range)];
    const std::array<int, 4> values{range.cyan, range.magenta, range.yellow, range.black};
    const std::array<QString, 4> prefixes{QStringLiteral("selectiveColorCyan"), QStringLiteral("selectiveColorMagenta"),
                                          QStringLiteral("selectiveColorYellow"), QStringLiteral("selectiveColorBlack")};
    for (std::size_t index = 0; index < spins.size() && index < values.size(); ++index) {
      if (auto* slider = dialog.findChild<QSlider*>(prefixes[index] + QStringLiteral("Slider"))) {
        const QSignalBlocker block_slider(slider);
        slider->setValue(values[index]);
      }
      const QSignalBlocker block_spin(spins[index]);
      spins[index]->setValue(values[index]);
    }
  };
  const auto& reds = state->settings.ranges[0];
  const auto percent = QStringLiteral(" %");
  return request_adjustment_settings_dialog<SelectiveColorSettings>(
      parent, QStringLiteral("patchySelectiveColorDialog"), QObject::tr("Selective Color"),
      QStringLiteral("selectiveColorPreviewCheck"),
      {{QObject::tr("Cyan"), QStringLiteral("selectiveColorCyan"), -100, 100, reds.cyan, percent},
       {QObject::tr("Magenta"), QStringLiteral("selectiveColorMagenta"), -100, 100, reds.magenta, percent},
       {QObject::tr("Yellow"), QStringLiteral("selectiveColorYellow"), -100, 100, reds.yellow, percent},
       {QObject::tr("Black", "ink"), QStringLiteral("selectiveColorBlack"), -100, 100, reds.black, percent}},
      build_settings, std::move(preview_changed), {},
      [state, capture, show_range](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>& spins,
                                   const std::function<void()>& flush_preview) {
        auto* colors = new QComboBox(&dialog);
        colors->setObjectName(QStringLiteral("selectiveColorRangeCombo"));
        colors->addItems({QObject::tr("Reds"), QObject::tr("Yellows"), QObject::tr("Greens"), QObject::tr("Cyans"),
                          QObject::tr("Blues"), QObject::tr("Magentas"), QObject::tr("Whites"),
                          QObject::tr("Neutrals"), QObject::tr("Blacks")});
        form->insertRow(0, QObject::tr("Colors:", "selective color range"), colors);

        auto* methods = new QWidget(&dialog);
        auto* method_layout = new QHBoxLayout(methods);
        method_layout->setContentsMargins(0, 0, 0, 0);
        auto* relative = new QRadioButton(QObject::tr("Relative"), methods);
        relative->setObjectName(QStringLiteral("selectiveColorRelativeRadio"));
        auto* absolute = new QRadioButton(QObject::tr("Absolute"), methods);
        absolute->setObjectName(QStringLiteral("selectiveColorAbsoluteRadio"));
        (state->settings.absolute ? absolute : relative)->setChecked(true);
        method_layout->addWidget(relative);
        method_layout->addWidget(absolute);
        method_layout->addStretch(1);
        form->addRow(QObject::tr("Method:"), methods);

        QObject::connect(colors, &QComboBox::currentIndexChanged, &dialog,
                         [&dialog, state, spins, capture, show_range, flush_preview](int index) {
                           capture(*state, spins);
                           state->range = std::clamp(index, 0, 8);
                           show_range(dialog, *state, spins);
                           flush_preview();
                         });
        QObject::connect(absolute, &QRadioButton::toggled, &dialog, [state, flush_preview](bool checked) {
          state->settings.absolute = checked;
          flush_preview();
        });
      });
}

std::optional<VibranceSettings> request_vibrance_settings(
    QWidget* parent, std::function<void(bool, const VibranceSettings&)> preview_changed, VibranceSettings initial) {
  initial = clamp_vibrance(initial);
  return request_adjustment_settings_dialog<VibranceSettings>(
      parent, QStringLiteral("patchyVibranceDialog"), QObject::tr("Vibrance"), QStringLiteral("vibrancePreviewCheck"),
      {{QObject::tr("Vibrance"), QStringLiteral("vibranceAmount"), -100, 100, initial.vibrance, {}},
       {QObject::tr("Saturation"), QStringLiteral("vibranceSaturation"), -100, 100, initial.saturation, {}}},
      [](const std::vector<QSpinBox*>& spins) { return VibranceSettings{spins[0]->value(), spins[1]->value()}; },
      std::move(preview_changed));
}

std::optional<ColorLookupSettings> request_color_lookup_settings(
    QWidget* parent, std::function<void(bool, const ColorLookupSettings&)> preview_changed,
    ColorLookupSettings initial) {
  auto state = std::make_shared<ColorLookupSettings>(std::move(initial));
  const auto build_settings = [state](const std::vector<QSpinBox*>&) { return *state; };
  return request_adjustment_settings_dialog<ColorLookupSettings>(
      parent, QStringLiteral("patchyColorLookupDialog"), QObject::tr("Color Lookup"),
      QStringLiteral("colorLookupPreviewCheck"), {}, build_settings, std::move(preview_changed), {},
      [state](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>&,
              const std::function<void()>& flush_preview) {
        auto* row = new QWidget(&dialog);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(&dialog);
        name->setObjectName(QStringLiteral("colorLookupNameLabel"));
        name->setMinimumWidth(240);
        auto* load = new QPushButton(QObject::tr("Load LUT..."), &dialog);
        load->setObjectName(QStringLiteral("colorLookupLoadButton"));
        row_layout->addWidget(name, 1);
        row_layout->addWidget(load);
        form->insertRow(0, QObject::tr("3D LUT:"), row);
        // Without a table the lookup is identity, as an empty Photoshop Color Lookup is.
        // Long file names elide in the middle (keeping the extension); the tooltip has the whole name.
        const auto show = [state, name] {
          const auto full = state->table != nullptr ? QString::fromStdString(state->name) : QObject::tr("None");
          name->setText(name->fontMetrics().elidedText(full, Qt::ElideMiddle, name->minimumWidth()));
          name->setToolTip(full);
        };
        show();
        QObject::connect(load, &QPushButton::clicked, &dialog, [&dialog, state, show, flush_preview] {
          const auto path = get_open_file_name(&dialog, QObject::tr("Load 3D LUT"), QString(),
                                               QObject::tr("3D LUT files (*.cube *.3dl)"), nullptr,
                                               QStringLiteral("colorLookupFileDialog"));
          if (path.isEmpty()) {
            return;
          }
          QFile file(path);
          QByteArray bytes;
          if (file.open(QIODevice::ReadOnly)) {
            bytes = file.readAll();
          }
          const QFileInfo info(path);
          const auto parsed = parse_color_lookup_file(
              std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                            static_cast<std::size_t>(bytes.size())),
              info.suffix().toStdString());
          if (parsed.table == nullptr) {
            (void)show_warning_message(&dialog, QObject::tr("Color Lookup"),
                                       QObject::tr("%1 is not a 3D LUT Patchy can read.").arg(info.fileName()),
                                       QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("colorLookupLoadErrorBox"));
            return;
          }
          register_color_lookup_table(parsed.table);
          state->table = parsed.table;
          state->name = info.fileName().toStdString();
          show();
          flush_preview();
        });
      });
}

namespace {

// Black & White dialog cues (#528). The six hue colors stand for the hues the sliders weigh
// (content, like a color swatch), so they stay the same in every theme; frames, tracks and
// text use theme roles.
constexpr QSize kBlackWhiteThumbnailSize(64, 48);
constexpr std::array<RgbColor, 6> kBlackWhiteSliderHues{{
    {255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}}};

QColor black_white_gray_for_hue(const BlackWhiteSettings& settings, RgbColor hue) {
  auto gray_settings = settings;
  gray_settings.tint = false;
  const auto out = black_white_transfer(gray_settings, hue.red, hue.green, hue.blue, true);
  const auto level = static_cast<int>(std::clamp(out[0], 0.0, 255.0));
  return QColor(level, level, level);
}

// A hue next to the gray the current weights turn it into.
class BlackWhiteHueChip final : public QWidget {
public:
  BlackWhiteHueChip(RgbColor hue, QWidget* parent) : QWidget(parent), hue_(hue) {
    setFixedSize(34, 18);
  }

  void set_gray(QColor gray) {
    if (gray != gray_) {
      gray_ = gray;
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath frame;
    frame.addRoundedRect(box, 4.0, 4.0);
    painter.save();
    painter.setClipPath(frame);
    const auto half = box.width() / 2.0;
    painter.fillRect(QRectF(box.left(), box.top(), half, box.height()), QColor(hue_.red, hue_.green, hue_.blue));
    painter.fillRect(QRectF(box.left() + half, box.top(), box.width() - half, box.height()), gray_);
    painter.restore();
    painter.setPen(QPen(theme().field_inset_border, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(frame);
  }

private:
  RgbColor hue_;
  QColor gray_{128, 128, 128};
};

// How much of the image this slider acts on: a bar in the slider's hue, scaled so the
// strongest hue fills the track, and the share as a percentage.
class BlackWhiteInfluenceMeter final : public QWidget {
public:
  BlackWhiteInfluenceMeter(RgbColor hue, double share, double largest_share, QWidget* parent)
      : QWidget(parent), hue_(hue), share_(share), largest_share_(largest_share) {
    setFixedSize(84, 18);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto text = QObject::tr("%1%").arg(static_cast<int>(std::lround(share_ * 100.0)));
    constexpr int kTextWidth = 34;
    const QRect text_rect(width() - kTextWidth, 0, kTextWidth, height());
    painter.setPen(theme().text_secondary);
    painter.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, text);
    const QRectF track(1.5, height() / 2.0 - 3.0, width() - kTextWidth - 6.0, 6.0);
    painter.setPen(QPen(theme().slider_groove_border, 1.0));
    painter.setBrush(theme().slider_groove_bg);
    painter.drawRoundedRect(track, 3.0, 3.0);
    const auto fraction = largest_share_ > 0.0 ? std::clamp(share_ / largest_share_, 0.0, 1.0) : 0.0;
    if (fraction > 0.0) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(hue_.red, hue_.green, hue_.blue));
      painter.drawRoundedRect(QRectF(track.left(), track.top(), std::max(track.height(), track.width() * fraction),
                                     track.height()),
                              3.0, 3.0);
    }
  }

private:
  RgbColor hue_;
  double share_;
  double largest_share_;
};

// The preset thumbnails' source: the image fitted into `bounds` (display-encoded, over
// white), or without an image a hue sweep that fades to dark, so every preset still shows
// its character.
QImage black_white_thumbnail_source(const PixelBuffer* source, QSize bounds) {
  if (source == nullptr || source->empty() || source->format().channels < 3) {
    QImage sweep(bounds, QImage::Format_RGB888);
    for (int y = 0; y < bounds.height(); ++y) {
      for (int x = 0; x < bounds.width(); ++x) {
        const auto hue = static_cast<float>(x) / static_cast<float>(std::max(1, bounds.width()));
        const auto value = 1.0F - 0.65F * static_cast<float>(y) / static_cast<float>(std::max(1, bounds.height() - 1));
        sweep.setPixelColor(x, y, QColor::fromHsvF(hue, 0.85F, value));
      }
    }
    return sweep;
  }
  const auto scale = std::min(static_cast<double>(bounds.width()) / source->width(),
                              static_cast<double>(bounds.height()) / source->height());
  const auto width = std::clamp(static_cast<int>(std::lround(source->width() * scale)), 1, bounds.width());
  const auto height = std::clamp(static_cast<int>(std::lround(source->height() * scale)), 1, bounds.height());
  QImage fitted(width, height, QImage::Format_RGB888);
  for (int y = 0; y < height; ++y) {
    const auto source_y = std::clamp(static_cast<int>((y + 0.5) * source->height() / height), 0, source->height() - 1);
    for (int x = 0; x < width; ++x) {
      const auto source_x =
          std::clamp(static_cast<int>((x + 0.5) * source->width() / width), 0, source->width() - 1);
      const auto rgba = display_rgba8_at(*source, source_x, source_y);
      const auto over_white = [&rgba](int channel) {
        return (rgba[static_cast<std::size_t>(channel)] * rgba[3] + 255 * (255 - rgba[3]) + 127) / 255;
      };
      fitted.setPixelColor(x, y, QColor(over_white(0), over_white(1), over_white(2)));
    }
  }
  return fitted;
}

// Preset tiles: the thumbnail box on top, the name below in up to two wrapped lines.
constexpr int kBlackWhiteCellPadding = 4;
constexpr int kBlackWhiteNameLines = 2;
constexpr int kBlackWhitePresetColumns = 5;

// The narrowest tile (never narrower than the thumbnail box plus padding) in which every name
// fits on at most two lines without breaking a word, so no name is ever elided in any
// language. Long single words widen the tile instead of being cut.
int black_white_preset_cell_width(const QFontMetrics& metrics, const QStringList& names) {
  const auto text_width_for = [&metrics](const QString& name, int width) {
    const auto box = metrics.boundingRect(QRect(0, 0, width, 100000), Qt::AlignHCenter | Qt::TextWordWrap, name);
    return box.width() <= width && box.height() <= kBlackWhiteNameLines * metrics.lineSpacing();
  };
  int width = kBlackWhiteThumbnailSize.width() + 2 * kBlackWhiteCellPadding;
  for (; width < 400; width += 2) {
    const auto text_width = width - 2 * kBlackWhiteCellPadding;
    if (std::all_of(names.begin(), names.end(),
                    [&](const QString& name) { return text_width_for(name, text_width); })) {
      break;
    }
  }
  return width;
}

QSize black_white_preset_cell_size(const QFontMetrics& metrics, int width) {
  return QSize(width, kBlackWhiteCellPadding + kBlackWhiteThumbnailSize.height() + kBlackWhiteCellPadding +
                          kBlackWhiteNameLines * metrics.lineSpacing() + kBlackWhiteCellPadding);
}

// One preset in the strip: a checkable tile that paints its thumbnail (centred in the fixed
// thumbnail box, whatever the image's aspect) and its whole name. Selection, hover and text
// use theme roles; the thumbnail is content.
class BlackWhitePresetTile final : public QAbstractButton {
public:
  BlackWhitePresetTile(const QString& preset_id, const QString& name, QSize cell, QWidget* parent)
      : QAbstractButton(parent), preset_id_(preset_id), cell_(cell) {
    setText(name);
    setToolTip(name);
    setAccessibleName(name);
    setCheckable(true);
    setFixedSize(cell);
    setFocusPolicy(Qt::StrongFocus);
    setProperty("patchy.presetId", preset_id);
  }

  [[nodiscard]] const QString& preset_id() const noexcept { return preset_id_; }

  void set_thumbnail(QImage image) {
    thumbnail_ = std::move(image);
    update();
  }

  [[nodiscard]] QSize sizeHint() const override { return cell_; }

  // The name wraps under the thumbnail, so the height its text needs at a width is what
  // tells whether it fits (the text-size sweep reads it).
  [[nodiscard]] bool hasHeightForWidth() const override { return true; }
  [[nodiscard]] int heightForWidth(int width) const override {
    const auto text_box = fontMetrics().boundingRect(QRect(0, 0, width - 2 * kBlackWhiteCellPadding, 100000),
                                                     Qt::AlignHCenter | Qt::TextWordWrap, text());
    return kBlackWhiteCellPadding + kBlackWhiteThumbnailSize.height() + kBlackWhiteCellPadding + text_box.height() +
           kBlackWhiteCellPadding;
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (isChecked()) {
      painter.setPen(QPen(theme().list_selection_border, 1.0));
      painter.setBrush(theme().list_selection_bg);
      painter.drawRoundedRect(box, 4.0, 4.0);
    } else if (underMouse() || hasFocus()) {
      painter.setPen(hasFocus() ? QPen(theme().accent, 1.0) : Qt::NoPen);
      painter.setBrush(theme().button_hover_bg);
      painter.drawRoundedRect(box, 4.0, 4.0);
    }
    const QRect icon_box((width() - kBlackWhiteThumbnailSize.width()) / 2, kBlackWhiteCellPadding,
                         kBlackWhiteThumbnailSize.width(), kBlackWhiteThumbnailSize.height());
    if (!thumbnail_.isNull()) {
      painter.drawImage(QPoint(icon_box.left() + (icon_box.width() - thumbnail_.width()) / 2,
                               icon_box.top() + (icon_box.height() - thumbnail_.height()) / 2),
                        thumbnail_);
    }
    const QRect text_rect(kBlackWhiteCellPadding, icon_box.bottom() + 1 + kBlackWhiteCellPadding,
                          width() - 2 * kBlackWhiteCellPadding,
                          height() - icon_box.bottom() - 1 - 2 * kBlackWhiteCellPadding);
    painter.setPen(isChecked() ? theme().list_selection_text : theme().text_primary);
    painter.drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text());
  }

  void enterEvent(QEnterEvent* event) override {
    QAbstractButton::enterEvent(event);
    update();
  }

  void leaveEvent(QEvent* event) override {
    QAbstractButton::leaveEvent(event);
    update();
  }

private:
  QString preset_id_;
  QSize cell_;
  QImage thumbnail_;
};

// The tint's Hue and Saturation tracks: a horizontal slider whose track shows what it picks
// (a hue rainbow, or gray to the current hue at full saturation) as content colors; the frame
// and handle use theme roles.
class BlackWhiteRampSlider final : public QWidget {
public:
  enum class Ramp { Hue, Saturation };

  BlackWhiteRampSlider(Ramp ramp, int maximum, QWidget* parent)
      : QWidget(parent), ramp_(ramp), maximum_(maximum) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(120);
    setFixedHeight(20);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_value(int value) {
    value = std::clamp(value, 0, maximum_);
    if (value != value_) {
      value_ = value;
      update();
    }
  }
  [[nodiscard]] int value() const noexcept { return value_; }

  // The saturation track's hue.
  void set_hue(int hue) {
    if (hue != hue_) {
      hue_ = hue;
      update();
    }
  }

  void set_changed(std::function<void(int)> changed) { changed_ = std::move(changed); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) {
      painter.setOpacity(0.35);
    }
    const auto track = track_rect();
    QLinearGradient gradient(track.topLeft(), track.topRight());
    if (ramp_ == Ramp::Hue) {
      for (int step = 0; step <= 6; ++step) {
        const auto color = black_white_tint_color(step * 60 % 360, 100);
        gradient.setColorAt(step / 6.0, QColor(color.red, color.green, color.blue));
      }
    } else {
      const auto gray = black_white_tint_color(hue_, 0);
      const auto full = black_white_tint_color(hue_, 100);
      gradient.setColorAt(0.0, QColor(gray.red, gray.green, gray.blue));
      gradient.setColorAt(1.0, QColor(full.red, full.green, full.blue));
    }
    painter.setPen(QPen(theme().slider_groove_border, 1.0));
    painter.setBrush(gradient);
    painter.drawRoundedRect(track, 3.0, 3.0);
    const auto x = x_for_value(value_);
    const QRectF handle(x - 4.0, 1.5, 8.0, height() - 3.0);
    painter.setPen(QPen(hasFocus() ? theme().accent : theme().slider_handle_border, 1.0));
    painter.setBrush(theme().slider_handle_bg);
    painter.drawRoundedRect(handle, 2.0, 2.0);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      set_from_x(event->position().x());
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      set_from_x(event->position().x());
    }
  }

  void keyPressEvent(QKeyEvent* event) override {
    int step = 0;
    switch (event->key()) {
      case Qt::Key_Left:
      case Qt::Key_Down: step = -1; break;
      case Qt::Key_Right:
      case Qt::Key_Up: step = 1; break;
      case Qt::Key_PageDown: step = -10; break;
      case Qt::Key_PageUp: step = 10; break;
      default: QWidget::keyPressEvent(event); return;
    }
    emit_value(std::clamp(value_ + step, 0, maximum_));
  }

private:
  [[nodiscard]] QRectF track_rect() const {
    return QRectF(5.5, 4.5, std::max(1.0, width() - 11.0), height() - 9.0);
  }

  [[nodiscard]] double x_for_value(int value) const {
    const auto track = track_rect();
    return track.left() + track.width() * value / std::max(1, maximum_);
  }

  void set_from_x(double x) {
    const auto track = track_rect();
    const auto fraction = std::clamp((x - track.left()) / std::max(1.0, track.width()), 0.0, 1.0);
    emit_value(static_cast<int>(std::lround(fraction * maximum_)));
  }

  void emit_value(int value) {
    set_value(value);
    if (changed_) {
      changed_(value_);
    }
  }

  Ramp ramp_;
  int maximum_;
  int value_{0};
  int hue_{0};
  std::function<void(int)> changed_;
};

QImage black_white_thumbnail(const QImage& source, const BlackWhiteSettings& settings) {
  QImage result(source.size(), QImage::Format_RGB888);
  for (int y = 0; y < source.height(); ++y) {
    const auto* in = source.constScanLine(y);
    auto* out = result.scanLine(y);
    for (int x = 0; x < source.width(); ++x, in += 3, out += 3) {
      const auto value = black_white_transfer(settings, in[0], in[1], in[2], true);
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(value[static_cast<std::size_t>(channel)]), 0L, 255L));
      }
    }
  }
  return result;
}

}  // namespace

std::optional<BlackWhiteSettings> request_black_white_settings(
    QWidget* parent, std::function<void(bool, const BlackWhiteSettings&)> preview_changed,
    BlackWhiteSettings initial, const PixelBuffer* source) {
  // The six weights are slider rows; the tint lives in this shared state.
  auto state = std::make_shared<BlackWhiteSettings>(clamp_black_white(initial));
  const auto build_settings = [state](const std::vector<QSpinBox*>& spins) {
    auto settings = *state;
    std::array<int*, 6> weights{&settings.reds,  &settings.yellows, &settings.greens,
                                &settings.cyans, &settings.blues,   &settings.magentas};
    for (std::size_t index = 0; index < weights.size() && index < spins.size(); ++index) {
      *weights[index] = spins[index]->value();
    }
    return clamp_black_white(settings);
  };
  // Read once when the dialog opens, from a downsample: nothing here scales with the image
  // on a slider move. `source` is not kept.
  const auto thumbnail_source = black_white_thumbnail_source(source, kBlackWhiteThumbnailSize);
  std::optional<std::array<double, 6>> influence;
  if (source != nullptr && !source->empty()) {
    influence = black_white_weight_influence(*source);
  }
  const auto percent = QStringLiteral(" %");
  const auto& start = *state;
  return request_adjustment_settings_dialog<BlackWhiteSettings>(
      parent, QStringLiteral("patchyBlackWhiteDialog"), QObject::tr("Black & White"),
      QStringLiteral("blackWhitePreviewCheck"),
      {{QObject::tr("Reds"), QStringLiteral("blackWhiteReds"), -200, 300, start.reds, percent},
       {QObject::tr("Yellows"), QStringLiteral("blackWhiteYellows"), -200, 300, start.yellows, percent},
       {QObject::tr("Greens"), QStringLiteral("blackWhiteGreens"), -200, 300, start.greens, percent},
       {QObject::tr("Cyans"), QStringLiteral("blackWhiteCyans"), -200, 300, start.cyans, percent},
       {QObject::tr("Blues"), QStringLiteral("blackWhiteBlues"), -200, 300, start.blues, percent},
       {QObject::tr("Magentas"), QStringLiteral("blackWhiteMagentas"), -200, 300, start.magentas, percent}},
      build_settings, std::move(preview_changed), {},
      [state, build_settings, thumbnail_source, influence](
          QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>& spins,
          const std::function<void()>& flush_preview) {
        // Each row: the hue and the gray it becomes, then (with an image) how much of the
        // image that slider acts on.
        const std::array<QString, 6> hue_names{QObject::tr("Reds"),  QObject::tr("Yellows"), QObject::tr("Greens"),
                                               QObject::tr("Cyans"), QObject::tr("Blues"),   QObject::tr("Magentas")};
        const std::array<QString, 6> object_keys{QStringLiteral("Reds"),  QStringLiteral("Yellows"),
                                                 QStringLiteral("Greens"), QStringLiteral("Cyans"),
                                                 QStringLiteral("Blues"), QStringLiteral("Magentas")};
        double largest_share = 0.0;
        if (influence.has_value()) {
          largest_share = *std::max_element(influence->begin(), influence->end());
        }
        auto chips = std::make_shared<std::vector<BlackWhiteHueChip*>>();
        for (std::size_t index = 0; index < spins.size() && index < kBlackWhiteSliderHues.size(); ++index) {
          auto* row = spins[index]->parentWidget();
          auto* row_layout = row != nullptr ? qobject_cast<QHBoxLayout*>(row->layout()) : nullptr;
          if (row_layout == nullptr) {
            continue;
          }
          auto* chip = new BlackWhiteHueChip(kBlackWhiteSliderHues[index], row);
          chip->setObjectName(QStringLiteral("blackWhite") + object_keys[index] + QStringLiteral("Chip"));
          chip->setToolTip(QObject::tr("%1 and the gray they become").arg(hue_names[index]));
          row_layout->insertWidget(0, chip);
          chips->push_back(chip);
          if (influence.has_value()) {
            const auto share = (*influence)[index];
            auto* meter = new BlackWhiteInfluenceMeter(kBlackWhiteSliderHues[index], share, largest_share, row);
            meter->setObjectName(QStringLiteral("blackWhite") + object_keys[index] + QStringLiteral("Influence"));
            const auto text = QObject::tr("This slider acts on %1% of the image's color")
                                  .arg(static_cast<int>(std::lround(share * 100.0)));
            meter->setToolTip(text);
            meter->setAccessibleName(text);
            row_layout->addWidget(meter);
          }
        }

        // Tint: the checkbox and swatch, then Hue and Saturation, which edit the same color.
        auto* tint_row = new QWidget(&dialog);
        auto* tint_layout = new QHBoxLayout(tint_row);
        tint_layout->setContentsMargins(0, 0, 0, 0);
        auto* tint = new QCheckBox(QObject::tr("Tint"), tint_row);
        tint->setObjectName(QStringLiteral("blackWhiteTintCheck"));
        tint->setChecked(state->tint);
        auto* swatch = new QPushButton(tint_row);
        swatch->setObjectName(QStringLiteral("blackWhiteTintColorButton"));
        swatch->setAccessibleName(QObject::tr("Tint color"));
        tint_layout->addWidget(tint);
        tint_layout->addWidget(swatch);
        tint_layout->addStretch(1);
        form->addRow(QString(), tint_row);
        const auto add_ramp_row = [&dialog, form](const QString& label, BlackWhiteRampSlider::Ramp ramp, int maximum,
                                                  const QString& object_prefix, const QString& suffix) {
          auto* row = new QWidget(&dialog);
          auto* layout = new QHBoxLayout(row);
          layout->setContentsMargins(0, 0, 0, 0);
          auto* slider = new BlackWhiteRampSlider(ramp, maximum, row);
          slider->setObjectName(object_prefix + QStringLiteral("Slider"));
          slider->setAccessibleName(label);
          auto* spin = new QSpinBox(row);
          spin->setObjectName(object_prefix + QStringLiteral("Spin"));
          spin->setRange(0, maximum);
          spin->setSuffix(suffix);
          spin->setAccessibleName(label);
          configure_dialog_spinbox(spin);
          layout->addWidget(slider, 1);
          layout->addWidget(spin);
          form->addRow(label, row);
          return std::pair{slider, spin};
        };
        const auto hue_row = add_ramp_row(QObject::tr("Hue"), BlackWhiteRampSlider::Ramp::Hue, 359,
                                          QStringLiteral("blackWhiteTintHue"), QString(QChar(0x00B0)));
        const auto saturation_row =
            add_ramp_row(QObject::tr("Saturation"), BlackWhiteRampSlider::Ramp::Saturation, 100,
                         QStringLiteral("blackWhiteTintSaturation"), QStringLiteral(" %"));
        auto* hue_slider = hue_row.first;
        auto* hue_spin = hue_row.second;
        auto* saturation_slider = saturation_row.first;
        auto* saturation_spin = saturation_row.second;
        const std::array<QWidget*, 5> tint_controls{swatch, hue_slider, hue_spin, saturation_slider, saturation_spin};
        const auto show_swatch = [swatch, state] {
          const auto color = state->tint_color;
          set_themed_style(*swatch, color_button_style(QColor(color.red, color.green, color.blue)));
        };
        // Everything Tint shows, from the state: used after a preset or a swatch pick. The
        // sliders follow the color; while they are dragged the color follows them instead,
        // so a slider never moves under the pointer.
        const auto sync_tint_controls = [state, tint, tint_controls, hue_slider, hue_spin, saturation_slider,
                                         saturation_spin, show_swatch, form] {
          const QSignalBlocker block_tint(tint);
          const QSignalBlocker block_hue(hue_spin);
          const QSignalBlocker block_saturation(saturation_spin);
          tint->setChecked(state->tint);
          for (auto* control : tint_controls) {
            control->setEnabled(state->tint);
          }
          for (auto* field : {static_cast<QWidget*>(hue_spin->parentWidget()),
                              static_cast<QWidget*>(saturation_spin->parentWidget())}) {
            if (auto* label = form->labelForField(field); label != nullptr) {
              label->setEnabled(state->tint);
            }
          }
          const auto hue_saturation = black_white_tint_hue_saturation(state->tint_color, hue_spin->value());
          hue_spin->setValue(hue_saturation.hue);
          saturation_spin->setValue(hue_saturation.saturation);
          hue_slider->set_value(hue_saturation.hue);
          saturation_slider->set_value(hue_saturation.saturation);
          saturation_slider->set_hue(hue_saturation.hue);
          show_swatch();
        };
        sync_tint_controls();

        // Built-in presets with thumbnails of the image, in two titled sections that scroll:
        // Custom (whatever the sliders hold) and the neutral presets, then the tinted ones.
        auto* strip = new QScrollArea(&dialog);
        strip->setObjectName(QStringLiteral("blackWhitePresetStrip"));
        strip->setAccessibleName(QObject::tr("Black & White presets"));
        strip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        strip->setWidgetResizable(true);
        auto* content = new QWidget(strip);
        content->setObjectName(QStringLiteral("blackWhitePresetContent"));
        auto* content_layout = new QVBoxLayout(content);
        content_layout->setContentsMargins(4, 2, 4, 4);
        content_layout->setSpacing(2);
        QStringList names{QObject::tr("Custom")};
        for (const auto& preset : builtin_black_white_presets()) {
          names << QCoreApplication::translate("QObject", preset.name);
        }
        content->ensurePolished();
        const QFontMetrics metrics(content->font());
        const auto cell = black_white_preset_cell_size(metrics, black_white_preset_cell_width(metrics, names));
        auto* group = new QButtonGroup(&dialog);
        group->setExclusive(true);
        auto tiles = std::make_shared<std::vector<BlackWhitePresetTile*>>();
        QLabel* first_header = nullptr;
        const auto add_section = [&](const QString& title, const QString& object_name, bool tinted) {
          auto* header = new QLabel(title, content);
          header->setObjectName(object_name);
          auto header_font = header->font();
          header_font.setBold(true);
          header->setFont(header_font);
          content_layout->addWidget(header);
          if (first_header == nullptr) {
            first_header = header;
          }
          auto* grid = new QGridLayout();
          grid->setContentsMargins(0, 0, 0, 4);
          grid->setHorizontalSpacing(2);
          grid->setVerticalSpacing(2);
          int index = 0;
          const auto add_tile = [&](const QString& id, const QString& name) {
            auto* tile = new BlackWhitePresetTile(id, name, cell, content);
            tile->setObjectName(QStringLiteral("blackWhitePreset_") + (id.isEmpty() ? QStringLiteral("custom") : id));
            group->addButton(tile);
            grid->addWidget(tile, index / kBlackWhitePresetColumns, index % kBlackWhitePresetColumns);
            tiles->push_back(tile);
            ++index;
          };
          if (!tinted) {
            add_tile(QString(), names.front());
          }
          for (const auto& preset : builtin_black_white_presets()) {
            if (preset.tint == tinted) {
              add_tile(QString::fromLatin1(preset.id), QCoreApplication::translate("QObject", preset.name));
            }
          }
          grid->setColumnStretch(kBlackWhitePresetColumns, 1);
          content_layout->addLayout(grid);
        };
        add_section(QObject::tr("Neutral"), QStringLiteral("blackWhitePresetNeutralHeader"), false);
        add_section(QObject::tr("Tinted"), QStringLiteral("blackWhitePresetTintedHeader"), true);
        // The user's own presets (#528 follow-up): a third section, shown once there are any.
        auto* user_header = new QLabel(QObject::tr("My Presets"), content);
        user_header->setObjectName(QStringLiteral("blackWhitePresetUserHeader"));
        {
          auto header_font = user_header->font();
          header_font.setBold(true);
          user_header->setFont(header_font);
        }
        content_layout->addWidget(user_header);
        auto* user_grid = new QGridLayout();
        user_grid->setContentsMargins(0, 0, 0, 4);
        user_grid->setHorizontalSpacing(2);
        user_grid->setVerticalSpacing(2);
        user_grid->setColumnStretch(kBlackWhitePresetColumns, 1);
        content_layout->addLayout(user_grid);
        auto user_presets = std::make_shared<std::vector<std::pair<QString, BlackWhiteSettings>>>();
        content_layout->addStretch(1);
        tiles->front()->setToolTip(QObject::tr("The current slider settings"));
        strip->setWidget(content);
        // A header and two rows of tiles in view; the rest scrolls.
        const auto frame = 2 * strip->frameWidth();
        const auto header_height = first_header->sizeHint().height() + content_layout->spacing();
        strip->setFixedHeight(frame + 2 + header_height + 2 * cell.height() + 2 + 4);
        strip->setMinimumWidth(frame + 8 + kBlackWhitePresetColumns * (cell.width() + 2) +
                               strip->verticalScrollBar()->sizeHint().width());
        // Preset thumbnails never change: each renders its own weights and tint.
        for (auto* tile : *tiles) {
          for (const auto& preset : builtin_black_white_presets()) {
            if (tile->preset_id() == QLatin1String(preset.id)) {
              tile->set_thumbnail(black_white_thumbnail(thumbnail_source, black_white_with_preset(*state, preset)));
            }
          }
        }
        // The strip with the preset buttons in a column on its right.
        auto* strip_row = new QWidget(&dialog);
        strip_row->setObjectName(QStringLiteral("blackWhitePresetRow"));
        auto* strip_row_layout = new QHBoxLayout(strip_row);
        strip_row_layout->setContentsMargins(0, 0, 0, 0);
        strip_row_layout->setSpacing(6);
        strip_row_layout->addWidget(strip, 1);
        if (auto* layout = qobject_cast<QVBoxLayout*>(dialog.layout()); layout != nullptr) {
          layout->insertWidget(0, strip_row);
          if (influence.has_value()) {
            auto* note = new QLabel(QObject::tr("Bars: how much of this image each slider acts on."), &dialog);
            note->setObjectName(QStringLiteral("blackWhiteInfluenceNote"));
            note->setWordWrap(true);
            layout->insertWidget(1, note);
          }
        }

        const auto current = [build_settings, spins] { return build_settings(spins); };
        auto applying_preset = std::make_shared<bool>(false);
        auto preset_buttons = std::make_shared<AdjustmentPresetButtons*>(nullptr);
        const auto refresh = [current, chips, tiles, thumbnail_source, applying_preset, user_presets,
                              preset_buttons] {
          const auto settings = current();
          for (std::size_t index = 0; index < chips->size(); ++index) {
            (*chips)[index]->set_gray(black_white_gray_for_hue(settings, kBlackWhiteSliderHues[index]));
          }
          tiles->front()->set_thumbnail(black_white_thumbnail(thumbnail_source, settings));
          if (*applying_preset) {
            return;
          }
          // A user preset matches on its weights and Tint, and on the tint color when tinted.
          const auto user_matches = [&settings](BlackWhiteSettings saved) {
            if (!saved.tint) {
              saved.tint_color = settings.tint_color;
            }
            return saved == settings;
          };
          QString checked_user;
          for (auto* tile : *tiles) {
            if (tile->isChecked() && tile->preset_id().startsWith(QStringLiteral("user:"))) {
              checked_user = tile->preset_id().mid(5);
            }
          }
          QString first_user;
          bool keep_checked = false;
          for (const auto& [name, saved] : *user_presets) {
            if (user_matches(saved)) {
              keep_checked = keep_checked || name == checked_user;
              if (first_user.isEmpty()) {
                first_user = name;
              }
            }
          }
          const auto* match = matching_black_white_preset(settings);
          auto wanted = keep_checked      ? QStringLiteral("user:") + checked_user
                        : match != nullptr ? QString::fromLatin1(match->id)
                        : first_user.isEmpty() ? QString()
                                               : QStringLiteral("user:") + first_user;
          for (auto* tile : *tiles) {
            if (tile->preset_id() == wanted) {
              tile->setChecked(true);
              break;
            }
          }
          if (*preset_buttons != nullptr) {
            (*preset_buttons)->refresh();
          }
        };
        refresh();
        for (auto* spin : spins) {
          QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [refresh](int) { refresh(); });
        }
        for (auto* tile : *tiles) {
          QObject::connect(tile, &QAbstractButton::clicked, &dialog,
                           [tile, spins, state, applying_preset, sync_tint_controls, refresh, flush_preview] {
                             for (const auto& preset : builtin_black_white_presets()) {
                               if (tile->preset_id() != QLatin1String(preset.id)) {
                                 continue;
                               }
                               *applying_preset = true;
                               for (std::size_t index = 0; index < spins.size() && index < preset.weights.size();
                                    ++index) {
                                 spins[index]->setValue(preset.weights[index]);
                               }
                               const auto applied = black_white_with_preset(*state, preset);
                               state->tint = applied.tint;
                               state->tint_color = applied.tint_color;
                               sync_tint_controls();
                               *applying_preset = false;
                               refresh();
                               flush_preview();
                             }
                           });
        }

        QObject::connect(tint, &QCheckBox::toggled, &dialog,
                         [state, sync_tint_controls, flush_preview, refresh](bool checked) {
                           state->tint = checked;
                           sync_tint_controls();
                           refresh();
                           flush_preview();
                         });
        const auto hue_saturation_changed = [state, hue_spin, saturation_spin, hue_slider, saturation_slider,
                                             show_swatch, refresh, flush_preview] {
          hue_slider->set_value(hue_spin->value());
          saturation_slider->set_value(saturation_spin->value());
          saturation_slider->set_hue(hue_spin->value());
          state->tint_color = black_white_tint_color(hue_spin->value(), saturation_spin->value());
          show_swatch();
          refresh();
          flush_preview();
        };
        for (auto* spin : {hue_spin, saturation_spin}) {
          QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                           [hue_saturation_changed](int) { hue_saturation_changed(); });
        }
        hue_slider->set_changed([hue_spin](int value) { hue_spin->setValue(value); });
        saturation_slider->set_changed([saturation_spin](int value) { saturation_spin->setValue(value); });
        QObject::connect(swatch, &QPushButton::clicked, &dialog,
                         [&dialog, swatch, state, sync_tint_controls, flush_preview, refresh] {
          // Picks preview live; Cancel puts the original color back.
          const QPointer<QPushButton> alive(swatch);
          const auto original = state->tint_color;
          const auto apply = [alive, state, sync_tint_controls, flush_preview, refresh](QColor picked) {
            if (alive == nullptr) {
              return;
            }
            state->tint_color = RgbColor{static_cast<std::uint8_t>(picked.red()),
                                         static_cast<std::uint8_t>(picked.green()),
                                         static_cast<std::uint8_t>(picked.blue())};
            sync_tint_controls();
            refresh();
            flush_preview();
          };
          const auto picked = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue),
                                                   QObject::tr("Tint Color"), apply);
          apply(picked.value_or(QColor(original.red, original.green, original.blue)));
        });

        // User presets: tiles in their own section, and Save, Delete, Import and Export
        // (.blw) beside the strip.
        const auto apply_settings = [spins, state, applying_preset, sync_tint_controls, refresh,
                                     flush_preview](const BlackWhiteSettings& settings) {
          const std::array<int, 6> weights{settings.reds,  settings.yellows, settings.greens,
                                           settings.cyans, settings.blues,   settings.magentas};
          *applying_preset = true;
          for (std::size_t index = 0; index < spins.size() && index < weights.size(); ++index) {
            spins[index]->setValue(weights[index]);
          }
          state->tint = settings.tint;
          state->tint_color = settings.tint_color;
          sync_tint_controls();
          *applying_preset = false;
          refresh();
          flush_preview();
        };
        auto user_tiles = std::make_shared<std::vector<BlackWhitePresetTile*>>();
        const auto rebuild_user_tiles = [&dialog, content, user_header, user_grid, group, tiles, user_tiles,
                                         user_presets, cell, thumbnail_source, apply_settings] {
          for (auto* tile : *user_tiles) {
            group->removeButton(tile);
            tiles->erase(std::remove(tiles->begin(), tiles->end(), tile), tiles->end());
            delete tile;
          }
          user_tiles->clear();
          user_presets->clear();
          for (const auto& user : user_adjustment_presets(AdjustmentPresetKind::BlackWhite)) {
            try {
              user_presets->emplace_back(user.name, blw::read_file(to_filesystem_path(user.path)));
            } catch (const std::exception&) {
            }
          }
          int index = 0;
          for (const auto& [name, saved] : *user_presets) {
            auto* tile = new BlackWhitePresetTile(QStringLiteral("user:") + name, name, cell, content);
            tile->setObjectName(QStringLiteral("blackWhiteUserPreset_") + name);
            tile->set_thumbnail(black_white_thumbnail(thumbnail_source, saved));
            group->addButton(tile);
            user_grid->addWidget(tile, index / kBlackWhitePresetColumns, index % kBlackWhitePresetColumns);
            tiles->push_back(tile);
            user_tiles->push_back(tile);
            const auto settings = saved;
            QObject::connect(tile, &QAbstractButton::clicked, &dialog,
                             [apply_settings, settings] { apply_settings(settings); });
            ++index;
          }
          user_header->setVisible(!user_presets->empty());
        };
        rebuild_user_tiles();
        AdjustmentPresetButtons::Hooks hooks;
        hooks.write_current = [current](const QString& path) { blw::write_file(to_filesystem_path(path), current()); };
        hooks.check_file = [](const QString& path) { (void)blw::read_file(to_filesystem_path(path)); };
        hooks.selected_user_preset = [tiles]() -> std::optional<QString> {
          for (auto* tile : *tiles) {
            if (tile->isChecked() && tile->preset_id().startsWith(QStringLiteral("user:"))) {
              return tile->preset_id().mid(5);
            }
          }
          return std::nullopt;
        };
        hooks.user_presets_changed = [strip, rebuild_user_tiles, user_presets, user_tiles, current, apply_settings,
                                      refresh](const QString& name) {
          rebuild_user_tiles();
          if (!name.isEmpty()) {
            for (std::size_t index = 0; index < user_presets->size(); ++index) {
              if ((*user_presets)[index].first != name) {
                continue;
              }
              if ((*user_presets)[index].second != current()) {
                apply_settings((*user_presets)[index].second);
              }
              // Once the new tile has its place in the strip's layout.
              QTimer::singleShot(0, strip, [strip, tile = QPointer<QWidget>((*user_tiles)[index])] {
                if (tile != nullptr) {
                  strip->ensureWidgetVisible(tile);
                }
              });
              break;
            }
          }
          refresh();
        };
        auto* buttons = new AdjustmentPresetButtons(AdjustmentPresetKind::BlackWhite, QStringLiteral("blackWhite"),
                                                    AdjustmentPresetButtons::Arrangement::Column, hooks, strip_row);
        *preset_buttons = buttons;
        strip_row_layout->addWidget(buttons);
        QWidget* previous = tiles->back();
        for (QWidget* next : {static_cast<QWidget*>(buttons->save_button()), static_cast<QWidget*>(buttons->delete_button()),
                              static_cast<QWidget*>(buttons->import_button()),
                              static_cast<QWidget*>(buttons->export_button())}) {
          QWidget::setTabOrder(previous, next);
          previous = next;
        }
        refresh();
      });
}

std::optional<GradientMapSettings> request_gradient_map_settings(
    QWidget* parent, std::function<void(bool, const GradientMapSettings&)> preview_changed,
    GradientMapSettings initial, GradientLibrary* library, RgbColor foreground, RgbColor background) {
  // The editor's callbacks hold references into its context, so everything it points at
  // lives here for the whole dialog. A gradient map has no batch edits.
  struct Host {
    std::vector<std::string> explicit_fields;
    std::function<void()> reset_batch_capture;
    std::shared_ptr<AppearanceEdits<LayerStyleSettings>> batch_edits =
        std::make_shared<AppearanceEdits<LayerStyleSettings>>();
    std::unique_ptr<layer_style_detail::LayerStyleGradientContext> context;
    std::unique_ptr<layer_style_detail::LayerStyleGradientControls> controls;
  };
  auto host = std::make_shared<Host>();
  const auto build_settings = [host, initial](const std::vector<QSpinBox*>&) {
    GradientMapSettings settings = initial;
    if (host->controls != nullptr) {
      const auto value = host->controls->value();
      auto& gradient = settings.gradient;
      static_cast<GradientDefinition&>(gradient) = static_cast<const GradientDefinition&>(value);
      gradient.form = GradientDefinitionForm::Solid;
      gradient.reverse = value.reverse;
      gradient.dither = value.dither;
      gradient.interpolation = value.interpolation;
    }
    return settings;
  };
  return request_adjustment_settings_dialog<GradientMapSettings>(
      parent, QStringLiteral("patchyGradientMapDialog"), QObject::tr("Gradient Map"),
      QStringLiteral("gradientMapPreviewCheck"), {}, build_settings, std::move(preview_changed), {},
      [host, initial, library, foreground, background](QDialog& dialog, QFormLayout* form,
                                                       const std::vector<QSpinBox*>&,
                                                       const std::function<void()>& flush_preview) {
        host->context = std::make_unique<layer_style_detail::LayerStyleGradientContext>(
            layer_style_detail::LayerStyleGradientContext{dialog, library, foreground, background, false,
                                                          host->batch_edits, host->explicit_fields,
                                                          host->reset_batch_capture,
                                                          [](bool) { return std::string(); }});
        host->controls = layer_style_detail::make_gradient_controls(
            *host->context, form, &dialog, QStringLiteral("gradientMap"), initial.gradient,
            QObject::tr("Gradient Stop Color"));
        auto& controls = *host->controls;
        // A gradient map uses the colors alone: no placement, no opacity, solid only.
        const auto hide_row = [form](QWidget* widget) {
          for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
            int row = -1;
            QFormLayout::ItemRole role{};
            form->getWidgetPosition(current, &row, &role);
            if (row >= 0) {
              form->setRowVisible(row, false);
              return;
            }
          }
        };
        for (QWidget* widget : std::initializer_list<QWidget*>{
                 controls.form_combo, controls.style_combo, controls.angle, controls.scale, controls.offset_x,
                 controls.offset_y, controls.reset_alignment, controls.noise_controls}) {
          if (widget != nullptr) {
            hide_row(widget);
          }
        }
        for (QWidget* widget : std::initializer_list<QWidget*>{controls.align, controls.stop_opacity_label,
                                                               controls.stop_opacity}) {
          if (widget != nullptr) {
            widget->hide();
          }
        }
        if (controls.editor != nullptr) {
          controls.editor->set_opacity_track_enabled(false);
        }
        controls.changed = [flush_preview](bool) { flush_preview(); };
      });
}

std::optional<BrightnessContrastSettings> request_brightness_contrast_settings(
    QWidget* parent, std::function<void(bool, const BrightnessContrastSettings&)> preview_changed,
    BrightnessContrastSettings initial) {
  initial = clamp_brightness_contrast(initial);
  auto legacy_check = std::make_shared<QCheckBox*>(nullptr);

  // Photoshop's slider ranges differ per algorithm: modern takes brightness
  // -150..150 and contrast -50..100, legacy -100..100 for both.
  const auto rows_for = [](const BrightnessContrastSettings& value) {
    const auto brightness_range = value.use_legacy ? kBrightnessContrastLegacyRange : kModernBrightnessRange;
    const auto contrast_low = value.use_legacy ? -kBrightnessContrastLegacyRange : kModernContrastMin;
    const auto contrast_high = value.use_legacy ? kBrightnessContrastLegacyRange : kModernContrastMax;
    return std::vector<SliderRowSpec>{{QObject::tr("Brightness"), QStringLiteral("brightnessContrastBrightness"),
                                       -brightness_range, brightness_range, value.brightness, {}},
                                      {QObject::tr("Contrast"), QStringLiteral("brightnessContrastContrast"),
                                       contrast_low, contrast_high, value.contrast, {}}};
  };

  const auto build_settings = [legacy_check](const std::vector<QSpinBox*>& spins) {
    BrightnessContrastSettings settings{spins[0]->value(), spins[1]->value(),
                                        *legacy_check != nullptr && (*legacy_check)->isChecked()};
    return clamp_brightness_contrast(settings);
  };

  const auto retarget = [rows_for](QDialog& dialog, const BrightnessContrastSettings& value,
                                   const std::vector<QSpinBox*>& spins) {
    const auto rows = rows_for(value);
    for (std::size_t index = 0; index < spins.size() && index < rows.size(); ++index) {
      auto* slider = dialog.findChild<QSlider*>(rows[index].object_prefix + QStringLiteral("Slider"));
      if (slider != nullptr) {
        const QSignalBlocker block_slider(slider);
        slider->setRange(rows[index].minimum, rows[index].maximum);
        slider->setValue(rows[index].value);
      }
      const QSignalBlocker block_spin(spins[index]);
      spins[index]->setRange(rows[index].minimum, rows[index].maximum);
      spins[index]->setValue(rows[index].value);
    }
  };

  // Object names deliberately differ from the destructive catalog dialog's
  // filterBrightness/filterContrast so tests can target each unambiguously.
  return request_adjustment_settings_dialog<BrightnessContrastSettings>(
      parent, QStringLiteral("patchyBrightnessContrastDialog"), QObject::tr("Brightness/Contrast"),
      QStringLiteral("brightnessContrastPreviewCheck"), rows_for(initial), build_settings,
      std::move(preview_changed), {},
      [initial, legacy_check, retarget](QDialog& dialog, QFormLayout* form, const std::vector<QSpinBox*>& spins,
                                        const std::function<void()>& flush_preview) {
        auto* check = new QCheckBox(QObject::tr("Use Legacy"), &dialog);
        check->setObjectName(QStringLiteral("brightnessContrastUseLegacyCheck"));
        check->setChecked(initial.use_legacy);
        *legacy_check = check;
        form->addRow(QString(), check);
        QObject::connect(check, &QCheckBox::toggled, &dialog,
                         [&dialog, spins, retarget, flush_preview](bool use_legacy) {
                           // Photoshop clamps the sliders into the incoming
                           // mode's range when the algorithm is switched.
                           const auto clamped = clamp_brightness_contrast(
                               BrightnessContrastSettings{spins[0]->value(), spins[1]->value(), use_legacy});
                           retarget(dialog, clamped, spins);
                           flush_preview();
                         });
      });
}

}  // namespace patchy::ui
