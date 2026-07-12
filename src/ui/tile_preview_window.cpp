#include "ui/tile_preview_window.hpp"

#include "core/document.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kTimerIntervalMs = 150;
constexpr std::int64_t kLiveUpdatePixelCap = 1'000'000;
const QString kWindowSizeSettingsKey = QStringLiteral("ui/tilePreviewWindowSize");

std::uint64_t hash_combine(std::uint64_t hash, std::uint64_t value) noexcept {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

std::uint64_t hash_layers(std::uint64_t hash, const std::vector<Layer>& layers) noexcept {
  for (const auto& layer : layers) {
    hash = hash_combine(hash, layer.render_revision());
    hash = hash_combine(hash, layer.visible() ? 1U : 0U);
    if (!layer.children().empty()) {
      hash = hash_layers(hash, layer.children());
    }
  }
  return hash_combine(hash, layers.size());
}

}  // namespace

// Paints the composite tiled across the whole viewport with nearest-neighbor scaling
// (pixel-art WYSIWYG). Left-drag pans (the tiling wraps, so no pan can scroll off the
// content); double-click recenters; the mouse wheel zooms about the cursor. "Fit" zoom
// means 3x3 tiles fit the viewport.
class TileViewWidget : public QWidget {
public:
  explicit TileViewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(180, 180);
    setCursor(Qt::OpenHandCursor);
    set_pan(QPoint());
    set_zoom(0);
  }

  void set_composite(QImage composite) {
    composite_ = std::move(composite);
    update();
  }

  void set_zoom(int percent_or_fit) {
    zoom_ = percent_or_fit;  // 0 = fit
    setProperty("zoomPercent", zoom_);  // test-visible state, like panOffset
    update();
  }

  // Reports wheel-zoom percents so the window can mirror them in the zoom combo.
  void set_zoom_changed_callback(std::function<void(int)> callback) {
    zoom_changed_ = std::move(callback);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(42, 44, 48));
    if (composite_.isNull()) {
      return;
    }
    const auto scale = effective_scale();
    const auto draw_w = std::max(1, static_cast<int>(composite_.width() * scale));
    const auto draw_h = std::max(1, static_cast<int>(composite_.height() * scale));
    // Anchor the grid at the unpanned 3x3 center tile, then wrap to the first tile origin
    // at or left of/above the viewport so tiles always cover the whole widget.
    const auto anchor_x = (width() - draw_w * 3) / 2 + draw_w + pan_.x();
    const auto anchor_y = (height() - draw_h * 3) / 2 + draw_h + pan_.y();
    const auto wrap_start = [](int anchor, int step) {
      const int rem = anchor % step;
      return rem > 0 ? rem - step : rem;  // in (-step, 0]
    };
    const int start_x = wrap_start(anchor_x, draw_w);
    const int start_y = wrap_start(anchor_y, draw_h);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int y = start_y; y < height(); y += draw_h) {
      for (int x = start_x; x < width(); x += draw_w) {
        painter.drawImage(QRect(x, y, draw_w, draw_h), composite_);
      }
    }
    // A faint outline around the tile under the viewport center so the wrap boundaries are
    // findable wherever the view is panned.
    const int outline_x = start_x + ((width() / 2 - start_x) / draw_w) * draw_w;
    const int outline_y = start_y + ((height() / 2 - start_y) / draw_h) * draw_h;
    painter.setPen(QColor(255, 255, 255, 60));
    painter.drawRect(QRect(outline_x, outline_y, draw_w - 1, draw_h - 1));
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = true;
      drag_position_ = event->pos();
      setCursor(Qt::ClosedHandCursor);
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_) {
      set_pan(pan_ + (event->pos() - drag_position_));
      drag_position_ = event->pos();
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
      setCursor(Qt::OpenHandCursor);
    }
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      set_pan(QPoint());
    }
  }

  void wheelEvent(QWheelEvent* event) override {
    const auto delta = event->angleDelta().y();
    if (delta == 0 || composite_.isNull()) {
      event->ignore();
      return;
    }
    zoom_at(event->position(), delta > 0);
    event->accept();
  }

private:
  void set_pan(QPoint pan) {
    pan_ = pan;
    setProperty("panOffset", pan_);  // test-visible state, like paletteConvertPreview's zoom
    update();
  }

  [[nodiscard]] double effective_scale() const {
    double scale = zoom_ > 0 ? zoom_ / 100.0
                             : std::min(static_cast<double>(width()) / (composite_.width() * 3.0),
                                        static_cast<double>(height()) / (composite_.height() * 3.0));
    return std::max(scale, 0.01);
  }

  void zoom_at(QPointF pos, bool zoom_in) {
    const auto old_scale = effective_scale();
    const auto old_percent = old_scale * 100.0;
    auto percent = static_cast<int>(std::lround(zoom_in ? old_percent * 1.25 : old_percent / 1.25));
    // Integer percents: force at least one point of motion so low zooms never stick.
    if (zoom_in) {
      percent = std::max(percent, static_cast<int>(old_percent) + 1);
    } else {
      percent = std::min(percent, static_cast<int>(std::ceil(old_percent)) - 1);
    }
    // Floor zoom-out at ~24 tiles across the viewport: paintEvent draws one drawImage
    // per tile, and 1px tiles of a small composite would mean hundreds of thousands.
    const auto min_percent = std::clamp(
        static_cast<int>(std::ceil(100.0 * std::max(width() / (24.0 * composite_.width()),
                                                    height() / (24.0 * composite_.height())))),
        1, 1600);
    percent = std::clamp(percent, min_percent, 1600);
    const auto new_scale = percent / 100.0;
    // Keep the composite point under the cursor stationary. The painted grid steps in
    // integer draw sizes (paintEvent truncates), so anchor in tile fractions of those.
    const auto old_draw_w = std::max(1, static_cast<int>(composite_.width() * old_scale));
    const auto old_draw_h = std::max(1, static_cast<int>(composite_.height() * old_scale));
    const auto new_draw_w = std::max(1, static_cast<int>(composite_.width() * new_scale));
    const auto new_draw_h = std::max(1, static_cast<int>(composite_.height() * new_scale));
    const auto tile_x = (pos.x() - ((width() - old_draw_w * 3) / 2 + old_draw_w + pan_.x())) / old_draw_w;
    const auto tile_y = (pos.y() - ((height() - old_draw_h * 3) / 2 + old_draw_h + pan_.y())) / old_draw_h;
    set_pan(QPoint(
        static_cast<int>(std::lround(pos.x() - tile_x * new_draw_w - (width() - new_draw_w * 3) / 2 - new_draw_w)),
        static_cast<int>(std::lround(pos.y() - tile_y * new_draw_h - (height() - new_draw_h * 3) / 2 - new_draw_h))));
    set_zoom(percent);
    if (zoom_changed_) {
      zoom_changed_(percent);
    }
  }

  QImage composite_;
  int zoom_{0};
  QPoint pan_;
  QPoint drag_position_;
  bool dragging_{false};
  std::function<void(int)> zoom_changed_;
};

TilePreviewWindow::TilePreviewWindow(std::function<const Document*()> document_provider, QWidget* parent)
    : QDialog(parent), provider_(std::move(document_provider)) {
  setObjectName(QStringLiteral("tilePreviewWindow"));
  setWindowFlag(Qt::Tool, true);
  auto* root = new QVBoxLayout(this);
  auto* content = install_dark_dialog_chrome(*this, root, tr("Seamless Tile Preview"));
  content->setSpacing(8);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  zoom_combo_ = new QComboBox(this);
  zoom_combo_->setObjectName(QStringLiteral("tilePreviewZoomCombo"));
  zoom_combo_->addItem(tr("Fit"), 0);
  zoom_combo_->addItem(QStringLiteral("100%"), 100);
  zoom_combo_->addItem(QStringLiteral("200%"), 200);
  zoom_combo_->addItem(QStringLiteral("400%"), 400);
  controls->addWidget(zoom_combo_);
  refresh_button_ = new QPushButton(tr("Refresh"), this);
  refresh_button_->setObjectName(QStringLiteral("tilePreviewRefreshButton"));
  controls->addWidget(refresh_button_);
  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("tilePreviewStatusLabel"));
  controls->addWidget(status_label_, 1);
  content->addLayout(controls);

  view_ = new TileViewWidget(this);
  view_->setObjectName(QStringLiteral("tilePreviewView"));
  view_->setToolTip(tr("Drag to pan. Mouse wheel zooms. Double-click to recenter."));
  content->addWidget(view_, 1);
  view_->set_zoom_changed_callback([this](int percent) {
    // Mirror wheel zooms in the combo. The blocker matters when clearing the index:
    // index -1 has invalid currentData(), which toInt()s to 0 and would snap to Fit.
    const QSignalBlocker blocker(zoom_combo_);
    if (const auto index = zoom_combo_->findData(percent); index >= 0) {
      zoom_combo_->setCurrentIndex(index);
    } else {
      zoom_combo_->setCurrentIndex(-1);
      zoom_combo_->setPlaceholderText(QStringLiteral("%1%").arg(percent));
    }
  });

  // Frameless chrome has no native resize border; the corner grip is the resize handle.
  size_grip_ = new VisibleSizeGrip(this);
  size_grip_->setObjectName(QStringLiteral("tilePreviewSizeGrip"));

  connect(zoom_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { view_->set_zoom(zoom_combo_->currentData().toInt()); });
  connect(refresh_button_, &QPushButton::clicked, this, [this] { refresh_now(); });
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
  timer_.setInterval(kTimerIntervalMs);
  resize(420, 460);
  if (const auto saved = app_settings().value(kWindowSizeSettingsKey).toSize();
      saved.isValid() && !saved.isEmpty()) {
    resize(saved.expandedTo(minimumSizeHint()));
  }
}

void TilePreviewWindow::done(int result) {
  timer_.stop();
  app_settings().setValue(kWindowSizeSettingsKey, size());
  QDialog::done(result);  // hides the dialog; must happen before closeEvent re-checks visibility
  emit preview_closed();  // unchecks the View menu toggle, whose handler close()s us for real
}

std::uint64_t TilePreviewWindow::document_probe(const Document* document) const {
  if (document == nullptr) {
    return 0;
  }
  // Const reads only: Layer's MUTABLE accessors bump revisions on access, which would make
  // this probe invalidate every revision-keyed cache (see AGENTS.md).
  std::uint64_t hash = 0x811c9dc5ULL;
  hash = hash_combine(hash, reinterpret_cast<std::uintptr_t>(document));
  hash = hash_combine(hash, static_cast<std::uint64_t>(document->width()));
  hash = hash_combine(hash, static_cast<std::uint64_t>(document->height()));
  hash = hash_layers(hash, document->layers());
  if (document->palette_editing().has_value()) {
    hash = hash_combine(hash, document->palette_editing()->palette_revision);
  }
  return hash;
}

void TilePreviewWindow::refresh_now() {
  const auto* document = provider_ != nullptr ? provider_() : nullptr;
  if (document == nullptr || document->width() <= 0 || document->height() <= 0) {
    view_->set_composite(QImage());
    status_label_->setText(tr("No document"));
    last_probe_ = 0;
    return;
  }
  view_->set_composite(qimage_from_document(*document, true));
  const std::int64_t pixels = static_cast<std::int64_t>(document->width()) * document->height();
  status_label_->setText(pixels > kLiveUpdatePixelCap
                             ? tr("Large document: use Refresh to update")
                             : tr("%1 x %2, live").arg(document->width()).arg(document->height()));
  last_probe_ = document_probe(document);
}

void TilePreviewWindow::tick() {
  const auto* document = provider_ != nullptr ? provider_() : nullptr;
  if (document != nullptr) {
    const std::int64_t pixels = static_cast<std::int64_t>(document->width()) * document->height();
    if (pixels > kLiveUpdatePixelCap) {
      return;  // manual Refresh only past the cap
    }
  }
  const auto probe = document_probe(document);
  if (probe != last_probe_) {
    refresh_now();
  }
}

void TilePreviewWindow::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  refresh_now();
  timer_.start();
}

void TilePreviewWindow::hideEvent(QHideEvent* event) {
  timer_.stop();
  QDialog::hideEvent(event);
}

void TilePreviewWindow::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  if (size_grip_ != nullptr) {
    size_grip_->move(width() - size_grip_->width(), height() - size_grip_->height());
    size_grip_->raise();
  }
}

}  // namespace patchy::ui
