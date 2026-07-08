#include "ui/tile_preview_window.hpp"

#include "core/document.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kTimerIntervalMs = 150;
constexpr std::int64_t kLiveUpdatePixelCap = 1'000'000;

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

// Paints the composite tiled 3x3 with nearest-neighbor scaling (pixel-art WYSIWYG).
class TileViewWidget : public QWidget {
public:
  explicit TileViewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(180, 180);
  }

  void set_composite(QImage composite) {
    composite_ = std::move(composite);
    update();
  }

  void set_zoom(int percent_or_fit) {
    zoom_ = percent_or_fit;  // 0 = fit
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(42, 44, 48));
    if (composite_.isNull()) {
      return;
    }
    const auto tile_w = composite_.width();
    const auto tile_h = composite_.height();
    double scale = zoom_ > 0 ? zoom_ / 100.0
                             : std::min(static_cast<double>(width()) / (tile_w * 3.0),
                                        static_cast<double>(height()) / (tile_h * 3.0));
    scale = std::max(scale, 0.01);
    const auto draw_w = std::max(1, static_cast<int>(tile_w * scale));
    const auto draw_h = std::max(1, static_cast<int>(tile_h * scale));
    const auto origin_x = (width() - draw_w * 3) / 2;
    const auto origin_y = (height() - draw_h * 3) / 2;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int ty = 0; ty < 3; ++ty) {
      for (int tx = 0; tx < 3; ++tx) {
        painter.drawImage(QRect(origin_x + tx * draw_w, origin_y + ty * draw_h, draw_w, draw_h), composite_);
      }
    }
    // A faint outline around the center tile so the wrap boundaries are findable.
    painter.setPen(QColor(255, 255, 255, 60));
    painter.drawRect(QRect(origin_x + draw_w, origin_y + draw_h, draw_w - 1, draw_h - 1));
  }

private:
  QImage composite_;
  int zoom_{0};
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
  content->addWidget(view_, 1);

  connect(zoom_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { view_->set_zoom(zoom_combo_->currentData().toInt()); });
  connect(refresh_button_, &QPushButton::clicked, this, [this] { refresh_now(); });
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
  timer_.setInterval(kTimerIntervalMs);
  resize(420, 460);
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

void TilePreviewWindow::closeEvent(QCloseEvent* event) {
  timer_.stop();
  emit preview_closed();
  QDialog::closeEvent(event);
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

}  // namespace patchy::ui
