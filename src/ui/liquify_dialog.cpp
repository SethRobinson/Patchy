#include "ui/liquify_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kMaximumProxyEdge = 720;

class LiquifyPreviewWidget final : public QWidget {
public:
  LiquifyPreviewWidget(PixelBuffer source, QImage original_image,
                       Rect original_bounds, QRegion selection,
                       int original_width, int original_height,
                       QWidget* parent = nullptr)
      : QWidget(parent), source_(std::move(source)),
        original_image_(std::move(original_image)),
        bounds_(original_bounds), selection_(std::move(selection)),
        original_width_(original_width), original_height_(original_height),
        mesh_(source_.width(), source_.height()) {
    setObjectName(QStringLiteral("liquifyPreview"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(500, 380);
    render_preview();
  }

  [[nodiscard]] const LiquifyMesh& mesh() const noexcept { return mesh_; }

  void set_tool(LiquifyTool tool) { tool_ = tool; }
  void set_size(int size) {
    size_ = size;
    update();
  }
  void set_pressure(int pressure) { pressure_ = pressure; }
  void set_density(int density) { density_ = density; }
  void set_show_mask(bool show) {
    show_mask_ = show;
    render_preview();
  }
  void restore_all() {
    mesh_.reset();
    render_preview();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 35, 35));
    const auto target = displayed_rect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          target.width() < preview_image_.width());
    painter.drawImage(target, preview_image_);
    painter.setPen(QPen(QColor(105, 105, 105), 1));
    painter.drawRect(target.adjusted(0, 0, -1, -1));

    if (cursor_inside_ && target.contains(cursor_position_)) {
      const double scale = target.width() / std::max(1, source_.width());
      const double proxy_size =
          size_ * static_cast<double>(source_.width()) /
          std::max(1, original_width_);
      const double radius = std::max(2.0, proxy_size * scale * 0.5);
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
    apply_gesture(*point, *point, event->modifiers());
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    cursor_position_ = event->position();
    cursor_inside_ = true;
    if (dragging_) {
      const auto point = image_point(event->position());
      if (point.has_value()) {
        apply_gesture(last_point_, *point, event->modifiers());
        last_point_ = *point;
      }
    }
    update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
      event->accept();
    }
  }

  void leaveEvent(QEvent*) override {
    cursor_inside_ = false;
    dragging_ = false;
    update();
  }

private:
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
    mesh_.apply_stroke(gesture_tool, from.x(), from.y(), to.x(), to.y(),
                       std::max(1.0, proxy_size), pressure_, density_);
    render_preview();
  }

  void render_preview() {
    const auto rendered = mesh_.render(source_);
    if (!rendered.has_value()) {
      return;
    }
    preview_image_ = qimage_from_pixel_buffer(*rendered);
    const bool selected_only = !selection_.isEmpty();
    for (int y = 0; y < preview_image_.height(); ++y) {
      for (int x = 0; x < preview_image_.width(); ++x) {
        const int document_x = bounds_.x + std::clamp(
            static_cast<int>((static_cast<std::int64_t>(x) *
                              std::max(0, original_width_ - 1)) /
                             std::max(1, preview_image_.width() - 1)),
            0, std::max(0, original_width_ - 1));
        const int document_y = bounds_.y + std::clamp(
            static_cast<int>((static_cast<std::int64_t>(y) *
                              std::max(0, original_height_ - 1)) /
                             std::max(1, preview_image_.height() - 1)),
            0, std::max(0, original_height_ - 1));
        if (selected_only &&
            !selection_.contains(QPoint(document_x, document_y))) {
          preview_image_.setPixelColor(x, y, original_image_.pixelColor(x, y));
          continue;
        }
        if (show_mask_) {
          const double frozen = mesh_.freeze_strength_at(x, y);
          if (frozen > 0.0) {
            const auto color = preview_image_.pixelColor(x, y);
            preview_image_.setPixelColor(
                x, y,
                QColor(static_cast<int>(color.red() * (1.0 - 0.45 * frozen)),
                       static_cast<int>(color.green() * (1.0 - 0.2 * frozen)),
                       static_cast<int>(color.blue() * (1.0 - 0.05 * frozen) +
                                        255.0 * 0.45 * frozen),
                       color.alpha()));
          }
        }
      }
    }
    update();
  }

  PixelBuffer source_;
  QImage original_image_;
  Rect bounds_{};
  QRegion selection_;
  int original_width_{0};
  int original_height_{0};
  LiquifyMesh mesh_;
  LiquifyTool tool_{LiquifyTool::ForwardWarp};
  QImage preview_image_;
  QPointF cursor_position_;
  QPointF last_point_;
  int size_{150};
  int pressure_{50};
  int density_{50};
  bool show_mask_{true};
  bool cursor_inside_{false};
  bool dragging_{false};
};

struct ToolEntry {
  const char* name;
  const char* object_name;
  LiquifyTool tool;
};

constexpr std::array<ToolEntry, 8> kTools{{
    {QT_TRANSLATE_NOOP("QObject", "Warp"), "liquifyWarpTool",
     LiquifyTool::ForwardWarp},
    {QT_TRANSLATE_NOOP("QObject", "Reconstruct"),
     "liquifyReconstructTool", LiquifyTool::Reconstruct},
    {QT_TRANSLATE_NOOP("QObject", "Smooth"), "liquifySmoothTool",
     LiquifyTool::Smooth},
    {QT_TRANSLATE_NOOP("QObject", "Twirl"), "liquifyTwirlTool",
     LiquifyTool::TwirlClockwise},
    {QT_TRANSLATE_NOOP("QObject", "Pucker"), "liquifyPuckerTool",
     LiquifyTool::Pucker},
    {QT_TRANSLATE_NOOP("QObject", "Bloat"), "liquifyBloatTool",
     LiquifyTool::Bloat},
    {QT_TRANSLATE_NOOP("QObject", "Freeze"), "liquifyFreezeTool",
     LiquifyTool::FreezeMask},
    {QT_TRANSLATE_NOOP("QObject", "Thaw"), "liquifyThawTool",
     LiquifyTool::ThawMask},
}};

}  // namespace

std::optional<LiquifyMesh> request_liquify(QWidget* parent,
                                            const PixelBuffer& source,
                                            Rect bounds,
                                            const QRegion& selection) {
  auto source_image = qimage_from_pixel_buffer(source);
  if (source_image.isNull()) {
    return std::nullopt;
  }
  auto proxy_size = source_image.size();
  if (std::max(proxy_size.width(), proxy_size.height()) >
      kMaximumProxyEdge) {
    proxy_size.scale(QSize(kMaximumProxyEdge, kMaximumProxyEdge),
                     Qt::KeepAspectRatio);
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
        tools->fontMetrics().horizontalAdvance(QObject::tr(entry.name)) + 24);
  }
  tools->setFixedWidth(tool_width);
  for (std::size_t index = 0; index < kTools.size(); ++index) {
    const auto& entry = kTools[index];
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(entry.object_name));
    button->setText(QObject::tr(entry.name));
    button->setToolTip(QObject::tr(entry.name));
    if (entry.tool == LiquifyTool::TwirlClockwise) {
      button->setToolTip(
          QObject::tr("Twirl clockwise; hold Alt or Option to reverse"));
    }
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
      QStringLiteral(" px"), 80);
  auto* pressure = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Pressure:"),
      QStringLiteral("liquifyPressureSlider"),
      QStringLiteral("liquifyPressureSpin"), 1, 100, 50,
      QStringLiteral(" %"), 72);
  auto* density = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Density:"),
      QStringLiteral("liquifyDensitySlider"),
      QStringLiteral("liquifyDensitySpin"), 1, 100, 50,
      QStringLiteral(" %"), 72);
  controls->addLayout(form);

  auto* show_mask = new QCheckBox(QObject::tr("Show Freeze Mask"), &dialog);
  show_mask->setObjectName(QStringLiteral("liquifyShowMaskCheck"));
  show_mask->setChecked(true);
  controls->addWidget(show_mask);
  auto* restore = new QPushButton(QObject::tr("Restore All"), &dialog);
  restore->setObjectName(QStringLiteral("liquifyRestoreAllButton"));
  controls->addWidget(restore);
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
  QObject::connect(show_mask, &QCheckBox::toggled, &dialog,
                   [preview](bool checked) { preview->set_show_mask(checked); });
  QObject::connect(restore, &QPushButton::clicked, &dialog,
                   [preview] { preview->restore_all(); });
  preview->set_size(default_size);

  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  remember_dialog_position(dialog);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return preview->mesh();
}

}  // namespace patchy::ui
