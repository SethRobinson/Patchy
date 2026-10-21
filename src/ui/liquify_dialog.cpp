#include "ui/liquify_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/modifier_names.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/theme_qss.hpp"
#include "ui/localization.hpp"
#include "ui/main_window_shared.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kMaximumProxyEdge = 720;
// Rate: stationary dabs per second at Rate 100 (Rate 80, the default, gives 48).
constexpr double kMaximumDabsPerSecond = 60.0;

// The tools that keep working while the brush is held still.
bool works_while_held(LiquifyTool tool) {
  return tool == LiquifyTool::TwirlClockwise || tool == LiquifyTool::TwirlCounterClockwise ||
         tool == LiquifyTool::Pucker || tool == LiquifyTool::Bloat || tool == LiquifyTool::Reconstruct;
}

// Undo steps the workspace keeps: one mesh each (about 55 KB at the 720 px proxy).
constexpr std::size_t kHistoryLimit = 100;

// "Warp (W)": a control's tooltip naming the key that reaches it.
QString with_key_hint(const QString& text, const QKeySequence& keys) {
  return QStringLiteral("%1 (%2)").arg(text, keys.toString(QKeySequence::NativeText));
}

QImage proxy_selection_mask(const QRegion& selection, Rect bounds,
                            QSize original_size, QSize proxy_size) {
  if (selection.isEmpty()) {
    return {};  // No selection means the whole image is editable.
  }
  QImage mask(proxy_size, QImage::Format_Grayscale8);
  mask.fill(0);
  // Preserve the preview's endpoint-aligned, floor-rounded sampling, including
  // one-pixel axes. Map region spans to these samples once, never search the
  // entire region for each pixel of every brush preview.
  const auto sample_coordinates = [](int origin, int original, int proxy) {
    std::vector<int> coordinates(static_cast<std::size_t>(proxy));
    for (int i = 0; i < proxy; ++i) {
      coordinates[static_cast<std::size_t>(i)] = origin + static_cast<int>(
          static_cast<std::int64_t>(i) * std::max(0, original - 1) / std::max(1, proxy - 1));
    }
    return coordinates;
  };
  const auto xs = sample_coordinates(bounds.x, original_size.width(), proxy_size.width());
  const auto ys = sample_coordinates(bounds.y, original_size.height(), proxy_size.height());
  for (const auto& rect : selection.intersected(to_qrect(bounds))) {
    const auto left = std::lower_bound(xs.begin(), xs.end(), rect.left()) - xs.begin();
    const auto right = std::upper_bound(xs.begin(), xs.end(), rect.right()) - xs.begin();
    const auto top = std::lower_bound(ys.begin(), ys.end(), rect.top()) - ys.begin();
    const auto bottom = std::upper_bound(ys.begin(), ys.end(), rect.bottom()) - ys.begin();
    for (auto y = top; y < bottom; ++y) {
      auto* row = mask.scanLine(static_cast<int>(y));
      std::fill(row + left, row + right, static_cast<uchar>(255));
    }
  }
  return mask;
}

class LiquifyPreviewWidget final : public QWidget {
public:
  LiquifyPreviewWidget(PixelBuffer source, QImage original_image,
                       Rect original_bounds, QRegion selection,
                       int original_width, int original_height,
                       QWidget* parent = nullptr)
      : QWidget(parent), source_(std::move(source)),
        selection_mask_(proxy_selection_mask(selection, original_bounds,
                                             QSize(original_width, original_height),
                                             QSize(source_.width(), source_.height()))),
        original_width_(original_width),
        mesh_(source_.width(), source_.height()) {
    setObjectName(QStringLiteral("liquifyPreview"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(500, 380);
    rate_timer_.setObjectName(QStringLiteral("liquifyRateTimer"));
    connect(&rate_timer_, &QTimer::timeout, this, [this] { apply_held_dab(); });
    if (!selection_mask_.isNull()) {
      // Unselected pixels always show the original: convert it to the preview's format
      // once (the same per-pixel conversion as before), then copy rows per render.
      unselected_image_ = QImage(original_image.size(), QImage::Format_RGBA8888);
      for (int y = 0; y < original_image.height(); ++y) {
        for (int x = 0; x < original_image.width(); ++x) {
          unselected_image_.setPixelColor(x, y, original_image.pixelColor(x, y));
        }
      }
    }
    render_preview();
  }

  [[nodiscard]] const LiquifyMesh& mesh() const noexcept { return mesh_; }

  void set_tool(LiquifyTool tool) { tool_ = tool; }
  void set_size(int size) {
    size_ = size;
    update();
  }
  void set_pressure(int pressure) { pressure_ = pressure; }
  void set_rate(int rate) {
    rate_ = rate;
    if (rate_timer_.isActive()) {
      rate_timer_.start(rate_interval_ms());
    }
  }
  void set_stylus_pressure(bool enabled) { stylus_pressure_ = enabled; }
  void set_density(int density) { density_ = density; }
  void set_show_mask(bool show) {
    show_mask_ = show;
    invalidate_preview();
  }
  // Restore All is an undo step of its own; it does nothing on an untouched mesh.
  void restore_all() {
    if (mesh_.is_identity() && !mesh_.has_frozen_nodes()) {
      return;
    }
    push_history(mesh_);
    mesh_.reset();
    invalidate_preview();
  }

  // The workspace's own history (GitHub #296): every stroke, Freeze and Thaw included,
  // and Restore All is one step. The document's undo stack sees only Apply.
  void set_history_changed(std::function<void()> callback) { history_changed_ = std::move(callback); }
  [[nodiscard]] bool can_undo() const noexcept { return !undo_history_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_history_.empty(); }
  void undo() { step_history(undo_history_, redo_history_); }
  void redo() { step_history(redo_history_, undo_history_); }

protected:
  void paintEvent(QPaintEvent*) override {
    // Brush moves only update the mesh; the proxy renders here, once per painted
    // frame, so a burst of pointer events (high-rate mice, tablets, compositors that
    // deliver every motion event) never queues one render per event (GitHub #296).
    if (preview_stale_) {
      render_preview();
    }
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 35, 35));
    const auto target = displayed_rect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          target.width() < preview_image_.width());
    painter.drawImage(target, preview_image_);
    painter.setPen(QPen(QColor(105, 105, 105), 1));
    painter.drawRect(target.adjusted(0, 0, -1, -1));

    if (cursor_inside_ && target.contains(cursor_position_)) {
      const double radius = cursor_radius();
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(QColor(0, 0, 0, 190), 3));
      painter.drawEllipse(cursor_position_, radius, radius);
      painter.setPen(QPen(QColor(255, 255, 255, 220), 1));
      painter.drawEllipse(cursor_position_, radius, radius);
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      return;
    }
    const auto point = image_point(event->position());
    if (!point.has_value()) {
      return;
    }
    dragging_ = true;
    last_point_ = *point;
    push_history(mesh_);
    held_modifiers_ = event->modifiers();
    moved_since_held_dab_ = false;
    apply_gesture(*point, *point, event->modifiers());
    if (works_while_held(tool_)) {
      rate_timer_.start(rate_interval_ms());
    }
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const auto previous_cursor = cursor_rect();
    cursor_position_ = event->position();
    cursor_inside_ = true;
    if (dragging_) {
      const auto point = image_point(event->position());
      if (point.has_value()) {
        apply_gesture(last_point_, *point, event->modifiers());
        last_point_ = *point;
        held_modifiers_ = event->modifiers();
        moved_since_held_dab_ = true;
      }
    }
    if (!preview_stale_) {
      // Hovering repaints only the brush outline's old and new places.
      update(previous_cursor);
      update(cursor_rect());
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
      rate_timer_.stop();
      event->accept();
    }
  }

  // A pen's pressure scales Pressure for the mouse events Qt synthesizes from it, so the
  // tablet event itself is left unaccepted.
  void tabletEvent(QTabletEvent* event) override {
    if (event->type() == QEvent::TabletRelease) {
      pen_down_ = false;
    } else if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove) {
      pen_down_ = event->type() == QEvent::TabletPress || event->buttons() != Qt::NoButton;
      pen_pressure_ = event->pressure();
    }
    event->ignore();
  }

  void leaveEvent(QEvent*) override {
    cursor_inside_ = false;
    dragging_ = false;
    rate_timer_.stop();
    update();
  }

private:
  [[nodiscard]] double cursor_radius() const {
    const double scale = displayed_rect().width() / std::max(1, source_.width());
    const double proxy_size = size_ * static_cast<double>(source_.width()) / std::max(1, original_width_);
    return std::max(2.0, proxy_size * scale * 0.5);
  }

  // The brush outline's paint area: the circle, its 3 px dark pen and a margin.
  [[nodiscard]] QRect cursor_rect() const {
    const double reach = cursor_radius() + 4.0;
    return QRectF(cursor_position_ - QPointF(reach, reach), QSizeF(2.0 * reach, 2.0 * reach))
        .toAlignedRect();
  }

  void invalidate_preview() {
    preview_stale_ = true;
    update();
  }

  [[nodiscard]] int rate_interval_ms() const {
    const double per_second = std::max(1.0, kMaximumDabsPerSecond * rate_ / 100.0);
    return std::clamp(static_cast<int>(std::lround(1000.0 / per_second)), 16, 1000);
  }

  // Rate: while the button is held without moving, the stationary tools add a dab per
  // tick at the last point; a move in between already applied the brush.
  void apply_held_dab() {
    if (!dragging_) {
      rate_timer_.stop();
      return;
    }
    if (!moved_since_held_dab_) {
      apply_gesture(last_point_, last_point_, held_modifiers_);
    }
    moved_since_held_dab_ = false;
  }

  // Records `before`, the mesh as it was, as one undo step; a new step drops the redo steps.
  void push_history(LiquifyMesh before) {
    undo_history_.push_back(std::move(before));
    if (undo_history_.size() > kHistoryLimit) {
      undo_history_.pop_front();
    }
    redo_history_.clear();
    if (history_changed_) {
      history_changed_();
    }
  }

  void step_history(std::deque<LiquifyMesh>& from, std::deque<LiquifyMesh>& to) {
    if (dragging_ || from.empty()) {
      return;
    }
    to.push_back(std::move(mesh_));
    mesh_ = std::move(from.back());
    from.pop_back();
    if (history_changed_) {
      history_changed_();
    }
    invalidate_preview();
  }

  [[nodiscard]] QRectF displayed_rect() const {
    if (preview_image_.isNull()) {
      return {};
    }
    const QRectF available = rect().adjusted(12, 12, -12, -12);
    const double scale = std::min(available.width() / preview_image_.width(),
                                  available.height() / preview_image_.height());
    const QSizeF size(preview_image_.width() * scale,
                      preview_image_.height() * scale);
    return QRectF(QPointF((width() - size.width()) * 0.5,
                          (height() - size.height()) * 0.5),
                  size);
  }

  [[nodiscard]] std::optional<QPointF> image_point(QPointF widget_point) const {
    const auto target = displayed_rect();
    if (!target.contains(widget_point) || target.width() <= 0.0 ||
        target.height() <= 0.0) {
      return std::nullopt;
    }
    return QPointF((widget_point.x() - target.left()) * source_.width() /
                       target.width(),
                   (widget_point.y() - target.top()) * source_.height() /
                       target.height());
  }

  void apply_gesture(QPointF from, QPointF to,
                     Qt::KeyboardModifiers modifiers) {
    const double proxy_size =
        size_ * static_cast<double>(source_.width()) /
        std::max(1, original_width_);
    const auto gesture_tool =
        tool_ == LiquifyTool::TwirlClockwise &&
                modifiers.testFlag(Qt::AltModifier)
            ? LiquifyTool::TwirlCounterClockwise
            : tool_;
    const double pen = stylus_pressure_ && pen_down_ ? std::clamp(pen_pressure_, 0.0, 1.0) : 1.0;
    mesh_.apply_stroke(gesture_tool, from.x(), from.y(), to.x(), to.y(),
                       std::max(1.0, proxy_size), pressure_ * pen, density_);
    invalidate_preview();
  }

  void render_preview() {
    preview_stale_ = false;
    const auto rendered = mesh_.render(source_);
    if (!rendered.has_value()) {
      return;
    }
    preview_image_ = qimage_from_pixel_buffer(*rendered);
    const bool tint = show_mask_ && mesh_.has_frozen_nodes();
    if (selection_mask_.isNull() && !tint) {
      return;
    }
    // RGBA8888 rows hold R, G, B, A bytes: edit them in place rather than through
    // per-pixel QColor round trips.
    for (int y = 0; y < preview_image_.height(); ++y) {
      const auto* selected = selection_mask_.isNull() ? nullptr : selection_mask_.constScanLine(y);
      const auto* original = selected == nullptr ? nullptr : unselected_image_.constScanLine(y);
      auto* row = preview_image_.scanLine(y);
      for (int x = 0; x < preview_image_.width(); ++x) {
        auto* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
        if (selected != nullptr && selected[x] == 0U) {
          std::copy_n(original + static_cast<std::ptrdiff_t>(x) * 4, 4, pixel);
          continue;
        }
        if (tint) {
          const double frozen = mesh_.freeze_strength_at(x, y);
          if (frozen > 0.0) {
            // The blue overlay; bright blues clamp at 255.
            pixel[0] = static_cast<uchar>(static_cast<int>(pixel[0] * (1.0 - 0.45 * frozen)));
            pixel[1] = static_cast<uchar>(static_cast<int>(pixel[1] * (1.0 - 0.2 * frozen)));
            pixel[2] = static_cast<uchar>(
                std::min(255, static_cast<int>(pixel[2] * (1.0 - 0.05 * frozen) + 255.0 * 0.45 * frozen)));
          }
        }
      }
    }
  }

  PixelBuffer source_;
  QImage unselected_image_;
  QImage selection_mask_;
  int original_width_{0};
  LiquifyMesh mesh_;
  std::deque<LiquifyMesh> undo_history_;
  std::deque<LiquifyMesh> redo_history_;
  std::function<void()> history_changed_;
  LiquifyTool tool_{LiquifyTool::ForwardWarp};
  QImage preview_image_;
  QPointF cursor_position_;
  QPointF last_point_;
  int size_{150};
  int pressure_{50};
  int density_{50};
  int rate_{80};
  QTimer rate_timer_;
  Qt::KeyboardModifiers held_modifiers_;
  bool moved_since_held_dab_{false};
  bool stylus_pressure_{true};
  bool pen_down_{false};
  double pen_pressure_{1.0};
  bool show_mask_{true};
  bool preview_stale_{false};
  bool cursor_inside_{false};
  bool dragging_{false};
};

struct ToolEntry {
  const char* name;
  const char* object_name;
  LiquifyTool tool;
  Qt::Key key;  // Photoshop's Liquify letters
};

constexpr std::array<ToolEntry, 8> kTools{{
    {QT_TRANSLATE_NOOP("QObject", "Warp"), "liquifyWarpTool",
     LiquifyTool::ForwardWarp, Qt::Key_W},
    {QT_TRANSLATE_NOOP("QObject", "Reconstruct"),
     "liquifyReconstructTool", LiquifyTool::Reconstruct, Qt::Key_R},
    {QT_TRANSLATE_NOOP("QObject", "Smooth"), "liquifySmoothTool",
     LiquifyTool::Smooth, Qt::Key_E},
    {QT_TRANSLATE_NOOP("QObject", "Twirl"), "liquifyTwirlTool",
     LiquifyTool::TwirlClockwise, Qt::Key_C},
    {QT_TRANSLATE_NOOP("QObject", "Pucker"), "liquifyPuckerTool",
     LiquifyTool::Pucker, Qt::Key_S},
    {QT_TRANSLATE_NOOP("QObject", "Bloat"), "liquifyBloatTool",
     LiquifyTool::Bloat, Qt::Key_B},
    {QT_TRANSLATE_NOOP("QObject", "Freeze"), "liquifyFreezeTool",
     LiquifyTool::FreezeMask, Qt::Key_F},
    {QT_TRANSLATE_NOOP("QObject", "Thaw"), "liquifyThawTool",
     LiquifyTool::ThawMask, Qt::Key_D},
}};

}  // namespace

std::optional<LiquifyMesh> request_liquify(QWidget* parent,
                                            const PixelBuffer& source,
                                            Rect bounds,
                                            const QRegion& selection,
                                            double document_ppi) {
  auto source_image = qimage_from_pixel_buffer(source);
  if (source_image.isNull()) {
    return std::nullopt;
  }
  auto proxy_size = source_image.size();
  if (std::max(proxy_size.width(), proxy_size.height()) >
      kMaximumProxyEdge) {
    proxy_size.scale(QSize(kMaximumProxyEdge, kMaximumProxyEdge),
                     Qt::KeepAspectRatio);
    // KeepAspectRatio rounds the short edge of a very thin layer (a 1 px rule wider than
    // 720 px) down to 0; a null proxy would throw from the mesh constructor inside the
    // menu slot and take the process down.
    proxy_size = proxy_size.expandedTo(QSize(1, 1));
  }
  auto proxy_image = source_image.scaled(proxy_size, Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
  auto proxy_pixels = pixels_from_image_rgba(proxy_image);

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("liquifyDialog"));
  dialog.setWindowTitle(QObject::tr("Liquify"));
  dialog.resize(1000, 680);

  auto* root = new QVBoxLayout(&dialog);
  auto* content = new QHBoxLayout();
  root->addLayout(content, 1);

  auto* tools = new QWidget(&dialog);
  tools->setObjectName(QStringLiteral("liquifyToolColumn"));
  auto* tools_layout = new QVBoxLayout(tools);
  tools_layout->setContentsMargins(0, 0, 0, 0);
  tools_layout->setSpacing(4);
  auto* tool_group = new QButtonGroup(&dialog);
  tool_group->setExclusive(true);
  int tool_width = 96;
  for (const auto& entry : kTools) {
    tool_width = std::max(
        tool_width,
        tools->fontMetrics().horizontalAdvance(translate_data_text(entry.name)) + 24);
  }
  tools->setFixedWidth(tool_width);
  std::vector<QToolButton*> tool_buttons;
  for (std::size_t index = 0; index < kTools.size(); ++index) {
    const auto& entry = kTools[index];
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(entry.object_name));
    button->setText(translate_data_text(entry.name));
    const auto tip = entry.tool == LiquifyTool::TwirlClockwise
                         ? resolve_modifier_names(QObject::tr("Twirl clockwise; hold %ALT% to reverse"))
                         : translate_data_text(entry.name);
    button->setToolTip(with_key_hint(tip, QKeySequence(entry.key)));
    tool_buttons.push_back(button);
    button->setCheckable(true);
    button->setFixedWidth(tool_width);
    tool_group->addButton(button, static_cast<int>(index));
    tools_layout->addWidget(button);
    if (index == 0) {
      button->setChecked(true);
    }
  }
  tools_layout->addStretch(1);
  content->addWidget(tools);

  auto* preview = new LiquifyPreviewWidget(
      std::move(proxy_pixels), proxy_image, bounds, selection, source.width(),
      source.height(), &dialog);
  content->addWidget(preview, 1);

  auto* controls = new QVBoxLayout();
  controls->setContentsMargins(8, 0, 0, 0);
  auto* heading = new QLabel(QObject::tr("Brush Controls"), &dialog);
  QFont heading_font = heading->font();
  heading_font.setBold(true);
  heading->setFont(heading_font);
  controls->addWidget(heading);
  auto* form = new QFormLayout();
  const int default_size =
      std::clamp(std::min(source.width(), source.height()) / 4, 50, 300);
  auto* size = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Size:"),
      QStringLiteral("liquifySizeSlider"),
      QStringLiteral("liquifySizeSpin"), 5, 2000, default_size,
      SpinUnit::Pixels,
      [ppi = sanitized_document_ppi(document_ppi)] { return UnitConversionContext{ppi, 0.0}; }, 80,
      /*row_spacing=*/-1, /*step_buttons=*/false, SliderCurve::FineLowEnd);
  auto* pressure = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Pressure:"),
      QStringLiteral("liquifyPressureSlider"),
      QStringLiteral("liquifyPressureSpin"), 1, 100, 50,
      percent_suffix(), 72);
  auto* density = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Density:"),
      QStringLiteral("liquifyDensitySlider"),
      QStringLiteral("liquifyDensitySpin"), 1, 100, 50,
      percent_suffix(), 72);
  auto* rate = add_dialog_slider_spin_row(form, &dialog, QObject::tr("Rate:"), QStringLiteral("liquifyRateSlider"),
                                          QStringLiteral("liquifyRateSpin"), 1, 100, 80, QString(), 72);
  rate->setToolTip(QObject::tr("How fast the brush keeps working while it is held still"));
  controls->addLayout(form);
  auto* stylus = new QCheckBox(QObject::tr("Stylus Pressure"), &dialog);
  stylus->setObjectName(QStringLiteral("liquifyStylusPressureCheck"));
  stylus->setChecked(true);
  controls->addWidget(stylus);

  auto* show_mask = new QCheckBox(QObject::tr("Show Freeze Mask"), &dialog);
  show_mask->setObjectName(QStringLiteral("liquifyShowMaskCheck"));
  show_mask->setChecked(true);
  controls->addWidget(show_mask);
  auto* restore = new QPushButton(QObject::tr("Restore All"), &dialog);
  restore->setObjectName(QStringLiteral("liquifyRestoreAllButton"));
  controls->addWidget(restore);
  auto* history_row = new QHBoxLayout();
  auto* undo = new QPushButton(QObject::tr("Undo"), &dialog);
  undo->setObjectName(QStringLiteral("liquifyUndoButton"));
  undo->setToolTip(with_key_hint(undo->text(), QKeySequence(QKeySequence::Undo)));
  auto* redo = new QPushButton(QObject::tr("Redo"), &dialog);
  redo->setObjectName(QStringLiteral("liquifyRedoButton"));
  redo->setToolTip(with_key_hint(redo->text(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)));
  history_row->addWidget(undo);
  history_row->addWidget(redo);
  controls->addLayout(history_row);
  auto* note = new QLabel(
      QObject::tr("Liquify edits pixels directly. Rasterize a Smart Object before using it."),
      &dialog);
  note->setObjectName(QStringLiteral("liquifyRasterNote"));
  note->setWordWrap(true);
  note->setMaximumWidth(190);
  controls->addWidget(note);
  controls->addStretch(1);
  content->addLayout(controls);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("liquifyButtonBox"));
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  QObject::connect(tool_group, &QButtonGroup::idClicked, &dialog,
                   [preview](int id) {
                     if (id >= 0 && id < static_cast<int>(kTools.size())) {
                       preview->set_tool(kTools[static_cast<std::size_t>(id)].tool);
                     }
                   });
  QObject::connect(size, &QSpinBox::valueChanged, &dialog,
                   [preview](int value) { preview->set_size(value); });
  QObject::connect(pressure, &QSpinBox::valueChanged, &dialog,
                   [preview](int value) { preview->set_pressure(value); });
  QObject::connect(density, &QSpinBox::valueChanged, &dialog,
                   [preview](int value) { preview->set_density(value); });
  QObject::connect(rate, &QSpinBox::valueChanged, &dialog, [preview](int value) { preview->set_rate(value); });
  QObject::connect(stylus, &QCheckBox::toggled, &dialog,
                   [preview](bool checked) { preview->set_stylus_pressure(checked); });
  QObject::connect(show_mask, &QCheckBox::toggled, &dialog,
                   [preview](bool checked) { preview->set_show_mask(checked); });
  QObject::connect(restore, &QPushButton::clicked, &dialog,
                   [preview] { preview->restore_all(); });
  const auto refresh_history = [preview, undo, redo] {
    undo->setEnabled(preview->can_undo());
    redo->setEnabled(preview->can_redo());
  };
  preview->set_history_changed(refresh_history);
  refresh_history();
  QObject::connect(undo, &QPushButton::clicked, &dialog, [preview] { preview->undo(); });
  QObject::connect(redo, &QPushButton::clicked, &dialog, [preview] { preview->redo(); });
  // Window shortcuts on the modal dialog: the main window's application shortcuts are
  // blocked while it is open, so these keys never reach the document.
  const auto add_shortcut = [&dialog](const QKeySequence& keys, auto&& slot) {
    auto* shortcut = new QShortcut(keys, &dialog);
    QObject::connect(shortcut, &QShortcut::activated, &dialog, std::forward<decltype(slot)>(slot));
  };
  add_shortcut(QKeySequence(QKeySequence::Undo), [preview] { preview->undo(); });
  for (const auto& keys : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)}) {
    add_shortcut(keys, [preview] { preview->redo(); });
  }
  for (std::size_t index = 0; index < kTools.size(); ++index) {
    add_shortcut(QKeySequence(kTools[index].key), [button = tool_buttons[index]] { button->click(); });
  }
  // [ and ] step the brush size like the canvas brush keys (Shift for a larger step).
  const auto add_size_step = [&add_shortcut, size](const QKeySequence& keys, int direction, bool coarse) {
    add_shortcut(keys, [size, direction, coarse] {
      const int value = size->value();
      size->setValue(std::clamp(value + direction * proportional_brush_step(value, direction, coarse),
                                size->minimum(), size->maximum()));
    });
  };
  add_size_step(QKeySequence(Qt::Key_BracketLeft), -1, false);
  add_size_step(QKeySequence(Qt::Key_BracketRight), 1, false);
  add_size_step(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), -1, true);
  add_size_step(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), 1, true);
  preview->set_size(default_size);

  append_themed_style(dialog, dialog_spinbox_button_style());
  remember_dialog_position(dialog);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return preview->mesh();
}

}  // namespace patchy::ui
