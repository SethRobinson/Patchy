#include "ui/visual_filter_gallery_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/filter_gallery_controls.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kFilterIdRole = Qt::UserRole + 1;
constexpr int kThumbnailReadyRole = Qt::UserRole + 2;
constexpr int kFilterCategoryRole = Qt::UserRole + 3;
constexpr int kMaximumProxyDimension = 640;
constexpr int kMaximumThumbnailDimension = 180;

constexpr std::array<FilterCategory, 8> kGalleryCategoryOrder = {
    FilterCategory::PhotoLooks, FilterCategory::Blur,
    FilterCategory::Sharpen,    FilterCategory::Distort,
    FilterCategory::Noise,      FilterCategory::Pixelate,
    FilterCategory::Stylize,    FilterCategory::Render,
};

constexpr auto kGalleryFavoritesKey = "filters/gallery/favorites";
constexpr auto kGalleryCategoryKey = "filters/gallery/category";
constexpr auto kGalleryLastFilterKey = "filters/gallery/lastFilterId";
constexpr auto kGalleryLivePreviewKey =
    "filters/gallery/liveCanvasPreview";
constexpr auto kGallerySizeKey = "filters/gallery/size";

[[nodiscard]] QString gallery_category_token(FilterCategory category) {
  switch (category) {
    case FilterCategory::PhotoLooks:
      return QStringLiteral("photo_looks");
    case FilterCategory::Blur:
      return QStringLiteral("blur");
    case FilterCategory::Sharpen:
      return QStringLiteral("sharpen");
    case FilterCategory::Distort:
      return QStringLiteral("distort");
    case FilterCategory::Noise:
      return QStringLiteral("noise");
    case FilterCategory::Pixelate:
      return QStringLiteral("pixelate");
    case FilterCategory::Stylize:
      return QStringLiteral("stylize");
    case FilterCategory::Render:
      return QStringLiteral("render");
    case FilterCategory::Uncategorized:
      return QStringLiteral("uncategorized");
    case FilterCategory::Adjustment:
      return QStringLiteral("adjustment");
  }
  return QStringLiteral("uncategorized");
}

[[nodiscard]] std::vector<const FilterDefinition*> gallery_filters(
    const FilterRegistry& registry) {
  std::vector<const FilterDefinition*> filters;
  for (const auto category : kGalleryCategoryOrder) {
    for (const auto& filter : registry.filters()) {
      if (!filter.catalog.adjustment_only && filter.catalog.execute &&
          filter.catalog.category == category) {
        filters.push_back(&filter);
      }
    }
  }
  return filters;
}

[[nodiscard]] QIcon favorite_icon(bool filled) {
  constexpr int size = 20;
  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPolygonF star;
  constexpr double pi = 3.14159265358979323846;
  const QPointF center(size / 2.0, size / 2.0);
  for (int point = 0; point < 10; ++point) {
    const auto radius = point % 2 == 0 ? 8.0 : 3.6;
    const auto angle = -pi / 2.0 + static_cast<double>(point) * pi / 5.0;
    star << center + QPointF(std::cos(angle) * radius,
                             std::sin(angle) * radius);
  }
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(filled ? QColor(255, 202, 58) : QColor(205, 207, 211),
                      1.4));
  painter.setBrush(filled ? QColor(255, 202, 58) : Qt::NoBrush);
  painter.drawPolygon(star);
  return QIcon(QPixmap::fromImage(image));
}

struct GalleryProxy {
  PixelBuffer original;
  QRegion selection;
  bool selection_restricted{false};
  double spatial_scale{1.0};
};

struct GalleryProxyRender {
  QImage image;
  Rect bounds{};
};

struct GalleryProxyPreviewState {
  struct Work {
    std::uint64_t generation{0};
    FilterInvocation invocation;
  };

  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::function<void(Work)> start;
  std::function<void(GalleryProxyRender, std::string)> apply;
  std::function<void(QString)> fail;
};

void enqueue_gallery_proxy_preview(
    const std::shared_ptr<GalleryProxyPreviewState>& state,
    FilterInvocation invocation) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation =
      state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  GalleryProxyPreviewState::Work work{generation, std::move(invocation)};
  if (state->in_flight) {
    state->pending = std::move(work);
    return;
  }
  state->start(std::move(work));
}

void cancel_gallery_proxy_preview(
    const std::shared_ptr<GalleryProxyPreviewState>& state) {
  if (state == nullptr || state->closed) {
    return;
  }
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
}

void close_gallery_proxy_preview(
    const std::shared_ptr<GalleryProxyPreviewState>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
  state->start = {};
  state->apply = {};
  state->fail = {};
}

[[nodiscard]] std::uint8_t clamp_byte(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

// Bounded premultiplied bilinear resampling. It reads the immutable source in
// place and allocates only the at-most-640px proxy, even for very large layers.
[[nodiscard]] PixelBuffer make_proxy_pixels(const PixelBuffer& source,
                                            int width, int height) {
  if (source.width() == width && source.height() == height) {
    return source;
  }
  PixelBuffer result(width, height, source.format());
  const auto channels = source.format().channels;
  const auto scale_x = static_cast<double>(source.width()) / width;
  const auto scale_y = static_cast<double>(source.height()) / height;
  for (int y = 0; y < height; ++y) {
    const auto sy = (y + 0.5) * scale_y - 0.5;
    const auto y0 = std::clamp(static_cast<int>(std::floor(sy)), 0,
                               source.height() - 1);
    const auto y1 = std::min(y0 + 1, source.height() - 1);
    const auto fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
    for (int x = 0; x < width; ++x) {
      const auto sx = (x + 0.5) * scale_x - 0.5;
      const auto x0 = std::clamp(static_cast<int>(std::floor(sx)), 0,
                                 source.width() - 1);
      const auto x1 = std::min(x0 + 1, source.width() - 1);
      const auto fx = std::clamp(sx - std::floor(sx), 0.0, 1.0);
      const std::array<const std::uint8_t*, 4> samples = {
          source.pixel(x0, y0), source.pixel(x1, y0),
          source.pixel(x0, y1), source.pixel(x1, y1)};
      const std::array<double, 4> weights = {
          (1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
          (1.0 - fx) * fy, fx * fy};
      auto* destination = result.pixel(x, y);
      double alpha = 0.0;
      std::array<double, 3> premultiplied{};
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample_alpha =
            channels >= 4 ? samples[index][3] / 255.0 : 1.0;
        alpha += weights[index] * sample_alpha;
        for (int channel = 0; channel < 3; ++channel) {
          premultiplied[static_cast<std::size_t>(channel)] +=
              weights[index] * samples[index][channel] * sample_alpha;
        }
      }
      for (int channel = 0; channel < 3; ++channel) {
        destination[channel] =
            alpha > 0.0
                ? clamp_byte(premultiplied[static_cast<std::size_t>(channel)] /
                             alpha)
                : 0;
      }
      if (channels >= 4) {
        destination[3] = clamp_byte(alpha * 255.0);
      }
    }
  }
  return result;
}

[[nodiscard]] GalleryProxy make_gallery_proxy(const PixelBuffer& source,
                                               Rect bounds,
                                               const QRegion& selection,
                                               int maximum_dimension) {
  GalleryProxy proxy;
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 ||
      source.format().channels < 3) {
    return proxy;
  }
  const auto largest = std::max(source.width(), source.height());
  proxy.spatial_scale =
      largest > maximum_dimension
          ? static_cast<double>(maximum_dimension) / largest
          : 1.0;
  const auto width = std::max(
      1, static_cast<int>(std::lround(source.width() * proxy.spatial_scale)));
  const auto height = std::max(
      1, static_cast<int>(std::lround(source.height() * proxy.spatial_scale)));
  proxy.original = make_proxy_pixels(source, width, height);
  proxy.selection_restricted = !selection.isEmpty();
  if (!proxy.selection_restricted) {
    return proxy;
  }

  const QRect source_bounds(bounds.x, bounds.y, source.width(), source.height());
  const auto selected = selection.intersected(QRegion(source_bounds));
  const auto scale_x = static_cast<double>(width) / source.width();
  const auto scale_y = static_cast<double>(height) / source.height();
  for (const auto& rect : selected) {
    const auto local_left = rect.left() - bounds.x;
    const auto local_top = rect.top() - bounds.y;
    const auto local_right = rect.right() + 1 - bounds.x;
    const auto local_bottom = rect.bottom() + 1 - bounds.y;
    const auto left = static_cast<int>(std::floor(local_left * scale_x));
    const auto top = static_cast<int>(std::floor(local_top * scale_y));
    const auto right = static_cast<int>(std::ceil(local_right * scale_x));
    const auto bottom = static_cast<int>(std::ceil(local_bottom * scale_y));
    proxy.selection += QRect(left, top, std::max(1, right - left),
                             std::max(1, bottom - top));
  }
  proxy.selection &= QRegion(QRect(0, 0, width, height));
  return proxy;
}

[[nodiscard]] QImage image_from_pixels(const PixelBuffer& pixels) {
  if (pixels.empty()) {
    return {};
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto channels = pixels.format().channels;
  for (int y = 0; y < pixels.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* source = pixels.pixel(x, y);
      row[x * 4 + 0] = source[0];
      row[x * 4 + 1] = source[1];
      row[x * 4 + 2] = source[2];
      row[x * 4 + 3] = channels >= 4 ? source[3] : 255;
    }
  }
  return image;
}

[[nodiscard]] QImage align_original_to_bounds(const QImage& original,
                                              Rect result_bounds) {
  if (original.isNull() || result_bounds.width <= 0 ||
      result_bounds.height <= 0) {
    return original;
  }
  QImage aligned(result_bounds.width, result_bounds.height,
                 QImage::Format_RGBA8888);
  aligned.fill(Qt::transparent);
  QPainter painter(&aligned);
  painter.drawImage(-result_bounds.x, -result_bounds.y, original);
  return aligned;
}

[[nodiscard]] GalleryProxyRender render_proxy(
    const GalleryProxy& proxy, const FilterRegistry& registry,
    const FilterInvocation& invocation,
    const FilterProgress* progress = nullptr) {
  const auto local_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  if (proxy.original.empty() ||
      (proxy.selection_restricted && proxy.selection.isEmpty())) {
    return {image_from_pixels(proxy.original), local_bounds};
  }
  const auto scaled = registry.scale(invocation, proxy.spatial_scale);
  if (!scaled.has_value()) {
    return {image_from_pixels(proxy.original), local_bounds};
  }
  const FilterPreviewSettings settings{true, *scaled};
  auto result_bounds = local_bounds;
  auto pixels = build_filter_preview_pixels(proxy.original, proxy.selection,
                                            local_bounds, registry, settings,
                                            progress, &result_bounds);
  return {image_from_pixels(pixels), result_bounds};
}

[[nodiscard]] QIcon thumbnail_icon(const QImage& source) {
  constexpr int width = 128;
  constexpr int height = 78;
  QImage thumbnail(width, height, QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&thumbnail);
  constexpr int tile = 10;
  for (int y = 0; y < height; y += tile) {
    for (int x = 0; x < width; x += tile) {
      const auto light = ((x / tile) + (y / tile)) % 2 == 0;
      painter.fillRect(QRect(x, y, tile, tile),
                       light ? QColor(218, 220, 224)
                             : QColor(184, 187, 192));
    }
  }
  if (!source.isNull()) {
    const auto scaled = source.scaled(width, height, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
    painter.drawImage((width - scaled.width()) / 2,
                      (height - scaled.height()) / 2, scaled);
  }
  painter.end();
  return QIcon(QPixmap::fromImage(thumbnail));
}

[[nodiscard]] double numeric_value(const FilterParameterValue& value,
                                   double fallback) {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) {
    return static_cast<double>(*integer);
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return *number;
  }
  return fallback;
}

}  // namespace

VisualFilterGalleryResult request_visual_filter_gallery(
    QWidget* parent, const PixelBuffer& immutable_original, Rect bounds,
    const QRegion& selection, const FilterRegistry& registry,
    RgbColor foreground, RgbColor background,
    VisualFilterGalleryPreviewCallback preview_changed) {
  const auto proxy =
      make_gallery_proxy(immutable_original, bounds, selection,
                         kMaximumProxyDimension);
  const auto thumbnail_proxy =
      make_gallery_proxy(immutable_original, bounds, selection,
                         kMaximumThumbnailDimension);
  const auto original_image = image_from_pixels(proxy.original);
  const auto original_thumbnail = image_from_pixels(thumbnail_proxy.original);
  const auto available_filters = gallery_filters(registry);

  auto settings = app_settings();
  std::set<std::string, std::less<>> favorite_ids;
  QStringList cleaned_favorites;
  const auto saved_favorites =
      settings.value(QLatin1String(kGalleryFavoritesKey)).toStringList();
  for (const auto& saved_id : saved_favorites) {
    const auto id = saved_id.toStdString();
    const auto valid = std::any_of(
        available_filters.begin(), available_filters.end(),
        [&](const FilterDefinition* filter) { return filter->identifier == id; });
    if (valid && favorite_ids.insert(id).second) {
      cleaned_favorites.push_back(saved_id);
    }
  }
  if (cleaned_favorites != saved_favorites) {
    settings.setValue(QLatin1String(kGalleryFavoritesKey), cleaned_favorites);
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("filterGalleryDialog"));
  dialog.setWindowTitle(QObject::tr("Visual Filters & Looks"));
  dialog.setMinimumSize(880, 560);
  const auto saved_size =
      settings.value(QLatin1String(kGallerySizeKey)).toSize();
  if (saved_size.width() >= 880 && saved_size.width() <= 3200 &&
      saved_size.height() >= 560 && saved_size.height() <= 2400) {
    dialog.resize(saved_size);
  } else {
    dialog.resize(1120, 720);
  }
  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(8);

  auto* content = new QHBoxLayout();
  content->setSpacing(10);
  root->addLayout(content, 1);

  auto* looks_column = new QVBoxLayout();
  auto* looks_heading = new QLabel(QObject::tr("Filters"), &dialog);
  looks_column->addWidget(looks_heading);
  auto* search = new QLineEdit(&dialog);
  search->setObjectName(QStringLiteral("filterGallerySearchEdit"));
  search->setPlaceholderText(QObject::tr("Search filters"));
  search->setClearButtonEnabled(true);
  looks_column->addWidget(search);
  auto* category = new QComboBox(&dialog);
  category->setObjectName(QStringLiteral("filterGalleryCategoryCombo"));
  category->addItem(QObject::tr("All"), QStringLiteral("all"));
  category->addItem(QObject::tr("Favorites"), QStringLiteral("favorites"));
  for (const auto gallery_category : kGalleryCategoryOrder) {
    const auto present = std::any_of(
        available_filters.begin(), available_filters.end(),
        [gallery_category](const FilterDefinition* filter) {
          return filter->catalog.category == gallery_category;
        });
    if (!present) {
      continue;
    }
    category->addItem(filter_category_display_name(gallery_category),
                      gallery_category_token(gallery_category));
  }
  looks_column->addWidget(category);
  auto* looks = new QListWidget(&dialog);
  looks->setObjectName(QStringLiteral("filterGalleryLooksList"));
  looks->setIconSize(QSize(128, 78));
  looks->setMinimumWidth(260);
  looks->setMaximumWidth(280);
  looks->setSpacing(3);
  looks_column->addWidget(looks, 1);
  auto* empty_label = new QLabel(QObject::tr("No filters match this view."),
                                 &dialog);
  empty_label->setObjectName(QStringLiteral("filterGalleryEmptyLabel"));
  empty_label->setWordWrap(true);
  empty_label->hide();
  looks_column->addWidget(empty_label);
  content->addLayout(looks_column);

  auto* center = new QVBoxLayout();
  auto* preview = new ZoomableImagePreview(&dialog);
  preview->setObjectName(QStringLiteral("filterGalleryPreview"));
  preview->setProperty("filterGallerySpatialOverlay", true);
  preview->setProperty("filterGalleryRenderedFilterId", QString());
  preview->set_image(original_image);
  center->addWidget(preview, 1);

  auto* preview_controls = new QHBoxLayout();
  preview_controls->setSpacing(4);
  auto* before = new QPushButton(QObject::tr("Before"), &dialog);
  before->setObjectName(QStringLiteral("filterGalleryBeforeButton"));
  before->setToolTip(
      QObject::tr("Hold to compare with the unadjusted image"));
  preview_controls->addWidget(before);
  auto* canvas_preview =
      new QCheckBox(QObject::tr("Live Canvas Preview"), &dialog);
  canvas_preview->setObjectName(
      QStringLiteral("filterGalleryCanvasPreviewCheck"));
  canvas_preview->setChecked(
      settings.value(QLatin1String(kGalleryLivePreviewKey), true).toBool());
  preview_controls->addWidget(canvas_preview);
  preview_controls->addStretch(1);

  const auto make_zoom_button = [&dialog](const char* object_name,
                                          const QString& text,
                                          const QString& tooltip) {
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* zoom_fit = make_zoom_button(
      "filterGalleryZoomFit", QObject::tr("Fit"),
      QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button(
      "filterGalleryZoom100", QStringLiteral("100%"),
      QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("filterGalleryZoomOut", QStringLiteral("-"),
                                    QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("filterGalleryZoomIn", QStringLiteral("+"),
                                   QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("filterGalleryZoomLabel"));
  zoom_label->setMinimumWidth(78);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  preview_controls->addWidget(zoom_fit);
  preview_controls->addWidget(zoom_100);
  preview_controls->addWidget(zoom_out);
  preview_controls->addWidget(zoom_in);
  preview_controls->addWidget(zoom_label);
  center->addLayout(preview_controls);
  content->addLayout(center, 1);

  auto* parameters = new QWidget(&dialog);
  parameters->setObjectName(QStringLiteral("filterGalleryParameters"));
  parameters->setFixedWidth(280);
  auto* parameter_layout = new QVBoxLayout(parameters);
  parameter_layout->setContentsMargins(10, 8, 10, 8);
  auto* parameter_header = new QHBoxLayout();
  auto* parameter_heading = new QLabel(QObject::tr("Settings"), parameters);
  parameter_header->addWidget(parameter_heading);
  parameter_header->addStretch(1);
  auto* favorite = new QToolButton(parameters);
  favorite->setObjectName(QStringLiteral("filterGalleryFavoriteButton"));
  favorite->setCheckable(true);
  favorite->setAutoRaise(true);
  favorite->setAccessibleName(QObject::tr("Favorite filter"));
  favorite->setToolTip(QObject::tr("Add or remove this filter from Favorites"));
  parameter_header->addWidget(favorite);
  parameter_layout->addLayout(parameter_header);
  auto* parameter_form_host = new QWidget(parameters);
  parameter_form_host->setObjectName(
      QStringLiteral("filterGalleryParameterEditor"));
  parameter_layout->addWidget(parameter_form_host);
  parameter_layout->addStretch(1);
  content->addWidget(parameters);

  auto* footer = new QHBoxLayout();
  auto* status = new QLabel(QObject::tr("Ready"), &dialog);
  status->setObjectName(QStringLiteral("filterGalleryStatusLabel"));
  footer->addWidget(status, 1);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
          QDialogButtonBox::Reset,
      &dialog);
  buttons->setObjectName(QStringLiteral("filterGalleryButtonBox"));
  if (auto* apply = buttons->button(QDialogButtonBox::Ok)) {
    apply->setText(QObject::tr("Apply"));
  }
  footer->addWidget(buttons);
  root->addLayout(footer);

  std::map<std::string, FilterInvocation, std::less<>> invocations;
  std::map<std::string, QListWidgetItem*, std::less<>> filter_items;
  auto* original_item = new QListWidgetItem(QObject::tr("Original"), looks);
  original_item->setData(kFilterIdRole, QString());
  original_item->setIcon(thumbnail_icon(original_thumbnail));
  original_item->setData(kThumbnailReadyRole, true);
  for (const auto* definition : available_filters) {
    const auto& id = definition->identifier;
    invocations.emplace(
        id, registry.default_invocation(id, foreground, background));
    auto* item = new QListWidgetItem(filter_display_name(*definition), looks);
    item->setData(kFilterIdRole, QString::fromStdString(id));
    item->setData(kThumbnailReadyRole, false);
    item->setData(kFilterCategoryRole,
                  gallery_category_token(definition->catalog.category));
    filter_items.emplace(id, item);
  }
  looks->setCurrentRow(0);

  const auto current_id = [looks] {
    const auto* item = looks->currentItem();
    return item == nullptr ? std::string{}
                           : item->data(kFilterIdRole).toString().toStdString();
  };
  const auto current_invocation = [&]() -> std::optional<FilterInvocation> {
    const auto id = current_id();
    if (id.empty()) {
      return std::nullopt;
    }
    const auto found = invocations.find(id);
    return found == invocations.end()
               ? std::optional<FilterInvocation>{}
               : std::optional<FilterInvocation>{found->second};
  };
  std::function<void()> schedule_thumbnails;
  const auto update_favorite_button = [&] {
    const auto id = current_id();
    const auto is_filter = !id.empty() && filter_items.contains(id);
    const auto is_favorite =
        is_filter && favorite_ids.find(id) != favorite_ids.end();
    const QSignalBlocker blocker(favorite);
    favorite->setEnabled(is_filter);
    favorite->setChecked(is_favorite);
    favorite->setIcon(favorite_icon(is_favorite));
  };
  const auto save_favorites = [&] {
    QStringList ids;
    for (const auto* definition : available_filters) {
      if (favorite_ids.contains(definition->identifier)) {
        ids.push_back(QString::fromStdString(definition->identifier));
      }
    }
    settings.setValue(QLatin1String(kGalleryFavoritesKey), ids);
    settings.sync();
  };
  const auto save_dialog_state = [&] {
    settings.setValue(QLatin1String(kGalleryFavoritesKey), [&] {
      QStringList ids;
      for (const auto* definition : available_filters) {
        if (favorite_ids.contains(definition->identifier)) {
          ids.push_back(QString::fromStdString(definition->identifier));
        }
      }
      return ids;
    }());
    settings.setValue(QLatin1String(kGalleryCategoryKey),
                      category->currentData().toString());
    settings.setValue(QLatin1String(kGalleryLastFilterKey),
                      QString::fromStdString(current_id()));
    settings.setValue(QLatin1String(kGalleryLivePreviewKey),
                      canvas_preview->isChecked());
    settings.setValue(QLatin1String(kGallerySizeKey), dialog.size());
    settings.sync();
  };
  std::function<void()> apply_list_filter;
  apply_list_filter = [&] {
    const auto view = category->currentData().toString();
    const auto query = search->text().trimmed();
    int visible_filters = 0;
    for (const auto* definition : available_filters) {
      const auto found = filter_items.find(definition->identifier);
      if (found == filter_items.end()) {
        continue;
      }
      const auto category_name =
          filter_category_display_name(definition->catalog.category);
      const auto matches_query =
          query.isEmpty() ||
          filter_display_name(*definition).contains(query,
                                                    Qt::CaseInsensitive) ||
          QString::fromStdString(definition->display_name)
              .contains(query, Qt::CaseInsensitive) ||
          category_name.contains(query, Qt::CaseInsensitive) ||
          gallery_category_token(definition->catalog.category)
              .replace(QLatin1Char('_'), QLatin1Char(' '))
              .contains(query, Qt::CaseInsensitive);
      const auto matches_view =
          view == QStringLiteral("all") ||
          (view == QStringLiteral("favorites") &&
           favorite_ids.contains(definition->identifier)) ||
          view == gallery_category_token(definition->catalog.category);
      const auto visible = matches_query && matches_view;
      found->second->setHidden(!visible);
      visible_filters += visible ? 1 : 0;
    }
    original_item->setHidden(false);
    if (looks->currentItem() == nullptr || looks->currentItem()->isHidden()) {
      looks->setCurrentItem(original_item);
    }
    empty_label->setVisible(visible_filters == 0);
    update_favorite_button();
    if (schedule_thumbnails) {
      schedule_thumbnails();
    }
  };
  const auto emit_canvas_preview = [&] {
    if (preview_changed) {
      preview_changed(VisualFilterGalleryPreview{
          canvas_preview->isChecked(), current_invocation()});
    }
  };

  auto current_proxy_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  std::string rendered_proxy_filter_id;
  std::function<void()> refresh_spatial_overlay;
  auto proxy_preview_state = std::make_shared<GalleryProxyPreviewState>();
  auto proxy_registry = std::make_shared<const FilterRegistry>(registry);
  proxy_preview_state->apply =
      [&](GalleryProxyRender rendered, std::string filter_id) {
        preview->set_image(rendered.image);
        preview->setProperty(
            "filterGalleryRenderedFilterId",
            QString::fromStdString(filter_id));
        current_proxy_bounds = rendered.bounds;
        rendered_proxy_filter_id = filter_id;
        if (refresh_spatial_overlay) {
          refresh_spatial_overlay();
        }
        if (const auto item = filter_items.find(filter_id);
            item != filter_items.end()) {
          item->second->setIcon(thumbnail_icon(rendered.image));
          item->second->setData(kThumbnailReadyRole, true);
        }
        status->setText(QObject::tr("Ready"));
      };
  proxy_preview_state->fail =
      [status](QString) { status->setText(QObject::tr("Ready")); };
  proxy_preview_state->start =
      [proxy_preview_state, proxy_registry, proxy](
          GalleryProxyPreviewState::Work work) {
        proxy_preview_state->in_flight = true;
        auto rendered = std::make_shared<GalleryProxyRender>();
        auto error = std::make_shared<QString>();
        auto cancelled = std::make_shared<bool>(false);
        auto* app = QCoreApplication::instance();
        const auto generation = work.generation;
        const auto filter_id = work.invocation.filter_id;
        std::thread([app, proxy_preview_state, proxy_registry, proxy,
                     invocation = std::move(work.invocation), generation,
                     filter_id, rendered, error, cancelled] {
          FilterProgress progress{
              [proxy_preview_state, generation](int, int,
                                                FilterProgressStage) {
                return proxy_preview_state->generation.load(
                           std::memory_order_acquire) == generation;
              }};
          try {
            *rendered =
                render_proxy(proxy, *proxy_registry, invocation, &progress);
          } catch (const FilterCancelled&) {
            *cancelled = true;
          } catch (const std::exception& caught) {
            *error = QString::fromUtf8(caught.what());
          }
          if (app == nullptr) {
            return;
          }
          QMetaObject::invokeMethod(
              app,
              [proxy_preview_state, generation, filter_id, rendered, error,
               cancelled]() mutable {
                proxy_preview_state->in_flight = false;
                const auto is_latest =
                    generation == proxy_preview_state->generation.load(
                                      std::memory_order_acquire);
                if (!proxy_preview_state->closed && is_latest &&
                    !*cancelled) {
                  if (error->isEmpty() && proxy_preview_state->apply) {
                    proxy_preview_state->apply(std::move(*rendered),
                                               filter_id);
                  } else if (!error->isEmpty() &&
                             proxy_preview_state->fail) {
                    proxy_preview_state->fail(*error);
                  }
                }
                if (!proxy_preview_state->closed &&
                    proxy_preview_state->pending.has_value() &&
                    proxy_preview_state->start) {
                  auto next =
                      std::move(*proxy_preview_state->pending);
                  proxy_preview_state->pending.reset();
                  proxy_preview_state->start(std::move(next));
                }
              },
              Qt::QueuedConnection);
        }).detach();
      };
  auto* central_timer = new QTimer(&dialog);
  central_timer->setSingleShot(true);
  central_timer->setInterval(35);
  const auto render_current = [&] {
    const auto invocation = current_invocation();
    if (!invocation.has_value()) {
      cancel_gallery_proxy_preview(proxy_preview_state);
      preview->set_image(original_image);
      preview->setProperty("filterGalleryRenderedFilterId", QString());
      current_proxy_bounds =
          Rect::from_size(proxy.original.width(), proxy.original.height());
      rendered_proxy_filter_id.clear();
      if (refresh_spatial_overlay) {
        refresh_spatial_overlay();
      }
      status->setText(QObject::tr("Ready"));
      return;
    }
    status->setText(QObject::tr("Rendering preview..."));
    enqueue_gallery_proxy_preview(proxy_preview_state, *invocation);
  };
  QObject::connect(central_timer, &QTimer::timeout, &dialog, render_current);
  const auto schedule_render =
      [central_timer, status, proxy_preview_state] {
    cancel_gallery_proxy_preview(proxy_preview_state);
    status->setText(QObject::tr("Rendering preview..."));
    central_timer->start();
  };

  const auto base_dialog_style = dialog.styleSheet();
  const auto spinbox_style = dialog_spinbox_button_style();
  std::function<void()> rebuild_parameter_editor;
  rebuild_parameter_editor = [&] {
    delete parameter_form_host->layout();
    const auto children = parameter_form_host->findChildren<QWidget*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (auto* child : children) {
      delete child;
    }
    auto* form = new QFormLayout(parameter_form_host);
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);

    const auto id = current_id();
    const auto invocation_it = invocations.find(id);
    const auto* definition = registry.find(id);
    if (id.empty() || invocation_it == invocations.end() ||
        definition == nullptr) {
      auto* hint = new QLabel(
          QObject::tr("Choose a filter to adjust its settings."),
          parameter_form_host);
      hint->setWordWrap(true);
      form->addRow(hint);
    } else {
      const auto spec = filter_dialog_spec_for(*definition);
      const auto commit_value =
          [&, id](const std::string& key, FilterParameterValue value) {
            if (const auto found = invocations.find(id);
                found != invocations.end()) {
              found->second.parameters[key] = std::move(value);
            }
            if (refresh_spatial_overlay) {
              refresh_spatial_overlay();
            }
            schedule_render();
            emit_canvas_preview();
          };
      for (const auto& control : spec.controls) {
        const auto parameter =
            invocation_it->second.parameters.find(control.parameter_key);
        const auto& current_value =
            parameter != invocation_it->second.parameters.end()
                ? parameter->second
                : control.default_value;
        if (control.kind == FilterParameterKind::Boolean) {
          auto* check = new QCheckBox(parameter_form_host);
          check->setObjectName(control.object_name + QStringLiteral("Check"));
          const auto* checked = std::get_if<bool>(&current_value);
          check->setChecked(checked != nullptr && *checked);
          form->addRow(control.label, check);
          QObject::connect(
              check, &QCheckBox::toggled, &dialog,
              [commit_value, key = control.parameter_key](bool value) {
                commit_value(key, value);
              });
          continue;
        }
        if (control.kind == FilterParameterKind::Option) {
          auto* combo = new QComboBox(parameter_form_host);
          combo->setObjectName(control.object_name + QStringLiteral("Combo"));
          for (const auto& option : control.options) {
            combo->addItem(
                QCoreApplication::translate("QObject",
                                            option.display_name.c_str()),
                QString::fromStdString(option.value));
          }
          const auto* selected = std::get_if<std::string>(&current_value);
          const auto index =
              selected == nullptr
                  ? -1
                  : combo->findData(QString::fromStdString(*selected));
          combo->setCurrentIndex(index >= 0 ? index
                                             : (combo->count() > 0 ? 0 : -1));
          form->addRow(control.label, combo);
          QObject::connect(
              combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
              [combo, commit_value, key = control.parameter_key](int) {
                commit_value(
                    key, combo->currentData().toString().toStdString());
              });
          continue;
        }

        auto* row_host = new QWidget(parameter_form_host);
        auto* row = new QHBoxLayout(row_host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* slider = new QSlider(Qt::Horizontal, row_host);
        slider->setObjectName(control.object_name + QStringLiteral("Slider"));
        row->addWidget(slider, 1);
        if (control.kind == FilterParameterKind::Double) {
          const auto minimum = control.typed_minimum.value_or(0.0);
          const auto maximum = control.typed_maximum.value_or(100.0);
          const auto step = std::max(0.000001, control.step.value_or(0.01));
          const auto ticks = std::clamp(
              static_cast<int>(std::lround((maximum - minimum) / step)), 1,
              1'000'000);
          auto* spin = new QDoubleSpinBox(row_host);
          spin->setObjectName(control.object_name + QStringLiteral("Spin"));
          spin->setRange(minimum, maximum);
          spin->setSingleStep(step);
          int decimals = 0;
          for (auto probe = step;
               decimals < 6 &&
               std::abs(probe - std::round(probe)) > 0.0000001;
               probe *= 10.0) {
            ++decimals;
          }
          spin->setDecimals(decimals);
          if (!control.suffix.isEmpty()) {
            spin->setSuffix(control.suffix);
          }
          configure_dialog_spinbox(spin, 84);
          spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
          slider->setRange(0, ticks);
          const auto value = std::clamp(
              numeric_value(current_value,
                            numeric_value(control.default_value, minimum)),
              minimum, maximum);
          spin->setValue(value);
          slider->setValue(std::clamp(
              static_cast<int>(std::lround((value - minimum) / step)), 0,
              ticks));
          QObject::connect(
              slider, &QSlider::valueChanged, spin,
              [spin, minimum, maximum, step](int tick) {
                spin->setValue(std::clamp(
                    minimum + static_cast<double>(tick) * step, minimum,
                    maximum));
              });
          QObject::connect(
              spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
              [slider, minimum, step, ticks](double changed) {
                slider->setValue(std::clamp(
                    static_cast<int>(
                        std::lround((changed - minimum) / step)),
                    0, ticks));
              });
          QObject::connect(
              spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
              [commit_value, key = control.parameter_key](double changed) {
                commit_value(key, changed);
              });
          row->addWidget(spin);
        } else {
          auto* spin = new QSpinBox(row_host);
          spin->setObjectName(control.object_name + QStringLiteral("Spin"));
          spin->setRange(control.minimum, control.maximum);
          if (!control.suffix.isEmpty()) {
            spin->setSuffix(control.suffix);
          }
          configure_dialog_spinbox(spin, 78);
          spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
          slider->setRange(control.minimum, control.maximum);
          const auto value = std::clamp(
              static_cast<int>(std::lround(
                  numeric_value(current_value, control.value))),
              control.minimum, control.maximum);
          slider->setValue(value);
          spin->setValue(value);
          QObject::connect(slider, &QSlider::valueChanged, spin,
                           &QSpinBox::setValue);
          QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged),
                           slider, &QSlider::setValue);
          QObject::connect(
              spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
              [commit_value, key = control.parameter_key](int changed) {
                commit_value(key, static_cast<std::int64_t>(changed));
              });
          row->addWidget(spin);
        }
        form->addRow(control.label, row_host);
      }

      const auto angle_control = std::find_if(
          spec.controls.begin(), spec.controls.end(),
          [](const FilterControlSpec& control) {
            return control.presentation ==
                   FilterParameterPresentation::Angle;
          });
      if (angle_control != spec.controls.end()) {
        auto* dial = new FilterAngleDial(parameter_form_host);
        dial->setObjectName(QStringLiteral("filterAngleDial"));
        dial->set_range(angle_control->minimum, angle_control->maximum);
        dial->set_default_angle(angle_control->value);
        auto* spin = parameter_form_host->findChild<QSpinBox*>(
            angle_control->object_name + QStringLiteral("Spin"));
        dial->set_angle(spin != nullptr ? spin->value()
                                        : angle_control->value);
        dial->set_angle_changed_callback(
            [dial, spin](int degrees, bool) {
              if (spin != nullptr) {
                spin->setValue(degrees);
                dial->set_angle(spin->value());
              }
            });
        if (spin != nullptr) {
          QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), dial,
                           [dial](int degrees) {
                             dial->set_angle(degrees);
                           });
        }
        form->addRow(dial);
      }

      const auto control_with_presentation =
          [&](FilterParameterPresentation presentation)
          -> std::optional<FilterControlSpec> {
        const auto found = std::find_if(
            spec.controls.begin(), spec.controls.end(),
            [presentation](const FilterControlSpec& control) {
              return control.presentation == presentation;
            });
        return found == spec.controls.end()
                   ? std::optional<FilterControlSpec>{}
                   : std::optional<FilterControlSpec>{*found};
      };
      const auto wave_amplitude = control_with_presentation(
          FilterParameterPresentation::WaveAmplitude);
      const auto wave_wavelength = control_with_presentation(
          FilterParameterPresentation::WaveWavelength);
      const auto wave_phase = control_with_presentation(
          FilterParameterPresentation::WavePhase);
      if (wave_amplitude.has_value() && wave_wavelength.has_value() &&
          wave_phase.has_value()) {
        auto* waveform = new FilterWaveformControl(parameter_form_host);
        waveform->setObjectName(QStringLiteral("filterWaveformControl"));
        waveform->set_ranges(
            FilterWaveformValues{wave_amplitude->minimum,
                                 wave_wavelength->minimum,
                                 wave_phase->minimum},
            FilterWaveformValues{wave_amplitude->maximum,
                                 wave_wavelength->maximum,
                                 wave_phase->maximum});
        waveform->set_default_values(FilterWaveformValues{
            wave_amplitude->value, wave_wavelength->value,
            wave_phase->value});
        auto* amplitude_spin = parameter_form_host->findChild<QSpinBox*>(
            wave_amplitude->object_name + QStringLiteral("Spin"));
        auto* wavelength_spin = parameter_form_host->findChild<QSpinBox*>(
            wave_wavelength->object_name + QStringLiteral("Spin"));
        auto* phase_spin = parameter_form_host->findChild<QSpinBox*>(
            wave_phase->object_name + QStringLiteral("Spin"));
        const auto read_wave_values =
            [amplitude_spin, wavelength_spin, phase_spin,
             defaults = FilterWaveformValues{wave_amplitude->value,
                                             wave_wavelength->value,
                                             wave_phase->value}] {
              return FilterWaveformValues{
                  amplitude_spin != nullptr ? amplitude_spin->value()
                                            : defaults.amplitude,
                  wavelength_spin != nullptr ? wavelength_spin->value()
                                             : defaults.wavelength,
                  phase_spin != nullptr ? phase_spin->value()
                                        : defaults.phase};
            };
        waveform->set_values(read_wave_values());
        waveform->set_values_changed_callback(
            [&, id, waveform, amplitude_spin, wavelength_spin, phase_spin,
             amplitude = *wave_amplitude, wavelength = *wave_wavelength,
             phase = *wave_phase](FilterWaveformValues values, bool) {
              const auto sync = [&](QSpinBox* spin,
                                    const FilterControlSpec& control,
                                    int value) {
                if (spin != nullptr) {
                  const QSignalBlocker spin_blocker(spin);
                  spin->setValue(value);
                }
                if (auto* slider = parameter_form_host->findChild<QSlider*>(
                        control.object_name + QStringLiteral("Slider"));
                    slider != nullptr) {
                  const QSignalBlocker slider_blocker(slider);
                  slider->setValue(value);
                }
                if (const auto found = invocations.find(id);
                    found != invocations.end()) {
                  found->second.parameters[control.parameter_key] =
                      static_cast<std::int64_t>(value);
                }
              };
              sync(amplitude_spin, amplitude, values.amplitude);
              sync(wavelength_spin, wavelength, values.wavelength);
              sync(phase_spin, phase, values.phase);
              waveform->set_values(values);
              schedule_render();
              emit_canvas_preview();
            });
        const auto refresh_waveform = [waveform, read_wave_values](int) {
          waveform->set_values(read_wave_values());
        };
        if (amplitude_spin != nullptr) {
          QObject::connect(amplitude_spin,
                           qOverload<int>(&QSpinBox::valueChanged), waveform,
                           refresh_waveform);
        }
        if (wavelength_spin != nullptr) {
          QObject::connect(wavelength_spin,
                           qOverload<int>(&QSpinBox::valueChanged), waveform,
                           refresh_waveform);
        }
        if (phase_spin != nullptr) {
          QObject::connect(phase_spin,
                           qOverload<int>(&QSpinBox::valueChanged), waveform,
                           refresh_waveform);
        }
        form->addRow(waveform);
      }
    }
    // QStyleSheetStyle does not reliably lay out spin-box subcontrols that were
    // created after the parent stylesheet. Reapply one stable stylesheet after
    // each editor rebuild; never append to the current value.
    dialog.setStyleSheet(QString());
    dialog.setStyleSheet(base_dialog_style + spinbox_style);
    if (refresh_spatial_overlay) {
      refresh_spatial_overlay();
    }
  };

  refresh_spatial_overlay = [&] {
    const auto id = current_id();
    const auto invocation = invocations.find(id);
    const auto* definition = registry.find(id);
    if (id.empty() || invocation == invocations.end() ||
        definition == nullptr || rendered_proxy_filter_id != id) {
      preview->set_center_radius_overlay(std::nullopt);
      return;
    }
    const auto spec = filter_dialog_spec_for(*definition);
    const auto find_control = [&](FilterParameterPresentation presentation)
        -> const FilterControlSpec* {
      const auto found = std::find_if(
          spec.controls.begin(), spec.controls.end(),
          [presentation](const FilterControlSpec& control) {
            return control.presentation == presentation;
          });
      return found == spec.controls.end() ? nullptr : &*found;
    };
    const auto* center_x =
        find_control(FilterParameterPresentation::CenterXPercent);
    const auto* center_y =
        find_control(FilterParameterPresentation::CenterYPercent);
    if (center_x == nullptr || center_y == nullptr) {
      preview->set_center_radius_overlay(std::nullopt);
      return;
    }
    const auto value_for = [&](const FilterControlSpec& control) {
      const auto found =
          invocation->second.parameters.find(control.parameter_key);
      return numeric_value(found != invocation->second.parameters.end()
                               ? found->second
                               : control.default_value,
                           control.value);
    };
    const auto mapped_center = [](double percent, int source_extent,
                                  int result_origin, int result_extent) {
      if (source_extent <= 1 || result_extent <= 1) {
        return 0.5;
      }
      const auto source_coordinate =
          static_cast<double>(source_extent - 1) * percent / 100.0;
      return std::clamp(
          (source_coordinate - static_cast<double>(result_origin)) /
              static_cast<double>(result_extent - 1),
          0.0, 1.0);
    };
    NormalizedCenterRadiusOverlay overlay;
    overlay.center = QPointF(
        mapped_center(value_for(*center_x), proxy.original.width(),
                      current_proxy_bounds.x, current_proxy_bounds.width),
        mapped_center(value_for(*center_y), proxy.original.height(),
                      current_proxy_bounds.y, current_proxy_bounds.height));
    if (const auto* radius = find_control(
            FilterParameterPresentation::EffectRadiusPercent);
        radius != nullptr) {
      const auto source_shorter =
          std::max(1, std::min(proxy.original.width(), proxy.original.height()));
      const auto result_shorter = std::max(
          1, std::min(current_proxy_bounds.width, current_proxy_bounds.height));
      overlay.radius = std::clamp(
          value_for(*radius) / 100.0 *
              static_cast<double>(source_shorter) / result_shorter,
          0.01, 1.0);
    }
    preview->set_center_radius_overlay(overlay);
  };
  preview->set_center_radius_changed_callback(
      [&](NormalizedCenterRadiusOverlay overlay, bool gesture_finished) {
        const auto id = current_id();
        auto invocation = invocations.find(id);
        const auto* definition = registry.find(id);
        if (id.empty() || invocation == invocations.end() ||
            definition == nullptr) {
          return;
        }
        const auto spec = filter_dialog_spec_for(*definition);
        const auto find_control = [&](FilterParameterPresentation presentation)
            -> const FilterControlSpec* {
          const auto found = std::find_if(
              spec.controls.begin(), spec.controls.end(),
              [presentation](const FilterControlSpec& control) {
                return control.presentation == presentation;
              });
          return found == spec.controls.end() ? nullptr : &*found;
        };
        const auto sync_double = [&](const FilterControlSpec* control,
                                     double requested) {
          if (control == nullptr) {
            return;
          }
          const auto minimum = control->typed_minimum.value_or(0.0);
          const auto maximum = control->typed_maximum.value_or(100.0);
          const auto step =
              std::max(0.000001, control->step.value_or(0.01));
          auto value = std::clamp(
              minimum + std::round((requested - minimum) / step) * step,
              minimum, maximum);
          if (auto* spin = parameter_form_host->findChild<QDoubleSpinBox*>(
                  control->object_name + QStringLiteral("Spin"));
              spin != nullptr) {
            const QSignalBlocker blocker(spin);
            spin->setValue(value);
            value = spin->value();
          }
          invocation->second.parameters[control->parameter_key] = value;
          if (auto* slider = parameter_form_host->findChild<QSlider*>(
                  control->object_name + QStringLiteral("Slider"));
              slider != nullptr) {
            const QSignalBlocker blocker(slider);
            slider->setValue(static_cast<int>(
                std::lround((value - minimum) / step)));
          }
        };
        const auto sync_integer = [&](const FilterControlSpec* control,
                                      double requested) {
          if (control == nullptr) {
            return;
          }
          const auto value = std::clamp(
              static_cast<int>(std::lround(requested)), control->minimum,
              control->maximum);
          invocation->second.parameters[control->parameter_key] =
              static_cast<std::int64_t>(value);
          if (auto* spin = parameter_form_host->findChild<QSpinBox*>(
                  control->object_name + QStringLiteral("Spin"));
              spin != nullptr) {
            const QSignalBlocker blocker(spin);
            spin->setValue(value);
          }
          if (auto* slider = parameter_form_host->findChild<QSlider*>(
                  control->object_name + QStringLiteral("Slider"));
              slider != nullptr) {
            const QSignalBlocker blocker(slider);
            slider->setValue(value);
          }
        };
        const auto source_percent = [](double normalized, int source_extent,
                                       int result_origin,
                                       int result_extent) {
          if (source_extent <= 1 || result_extent <= 1) {
            return 50.0;
          }
          const auto source_coordinate =
              static_cast<double>(result_origin) +
              normalized * static_cast<double>(result_extent - 1);
          return std::clamp(
              source_coordinate /
                  static_cast<double>(source_extent - 1) *
                  100.0,
              0.0, 100.0);
        };
        sync_double(
            find_control(FilterParameterPresentation::CenterXPercent),
            source_percent(overlay.center.x(), proxy.original.width(),
                           current_proxy_bounds.x,
                           current_proxy_bounds.width));
        sync_double(
            find_control(FilterParameterPresentation::CenterYPercent),
            source_percent(overlay.center.y(), proxy.original.height(),
                           current_proxy_bounds.y,
                           current_proxy_bounds.height));
        if (overlay.radius.has_value()) {
          const auto source_shorter = std::max(
              1, std::min(proxy.original.width(), proxy.original.height()));
          const auto result_shorter = std::max(
              1,
              std::min(current_proxy_bounds.width, current_proxy_bounds.height));
          sync_integer(
              find_control(
                  FilterParameterPresentation::EffectRadiusPercent),
              *overlay.radius * static_cast<double>(result_shorter) /
                  source_shorter * 100.0);
        }
        refresh_spatial_overlay();
        if (gesture_finished) {
          schedule_render();
        } else {
          central_timer->stop();
          cancel_gallery_proxy_preview(proxy_preview_state);
        }
        emit_canvas_preview();
      });

  preview->set_zoom_changed_callback([preview, zoom_label] {
    const auto percent = preview->property("previewZoomPercent").toInt();
    zoom_label->setText(preview->fit_mode()
                            ? QObject::tr("Fit (%1%)").arg(percent)
                            : QStringLiteral("%1%").arg(percent));
  });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_step(1); });
  QObject::connect(before, &QPushButton::pressed, &dialog, [&] {
    central_timer->stop();
    cancel_gallery_proxy_preview(proxy_preview_state);
    preview->set_image(
        align_original_to_bounds(original_image, current_proxy_bounds));
    preview->set_center_radius_overlay(std::nullopt);
    status->setText(QObject::tr("Before"));
  });
  QObject::connect(before, &QPushButton::released, &dialog, schedule_render);
  QObject::connect(canvas_preview, &QCheckBox::toggled, &dialog,
                   [&](bool) { emit_canvas_preview(); });
  QObject::connect(looks, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem*, QListWidgetItem*) {
                     update_favorite_button();
                     rebuild_parameter_editor();
                     schedule_render();
                     emit_canvas_preview();
                   });
  QObject::connect(search, &QLineEdit::textChanged, &dialog,
                   [&](const QString&) { apply_list_filter(); });
  QObject::connect(category, qOverload<int>(&QComboBox::currentIndexChanged),
                   &dialog, [&](int) { apply_list_filter(); });
  QObject::connect(favorite, &QToolButton::toggled, &dialog, [&](bool checked) {
    const auto id = current_id();
    if (id.empty() || !filter_items.contains(id)) {
      return;
    }
    if (checked) {
      favorite_ids.insert(id);
    } else {
      favorite_ids.erase(id);
    }
    favorite->setIcon(favorite_icon(checked));
    save_favorites();
    apply_list_filter();
  });

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset)) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      const auto id = current_id();
      if (!id.empty()) {
        invocations[id] =
            registry.default_invocation(id, foreground, background);
      }
      rebuild_parameter_editor();
      schedule_render();
      emit_canvas_preview();
    });
  }

  // One proxy render per event-loop turn keeps opening the dialog immediate and
  // never lets thumbnail work block interaction for a whole batch.
  auto* thumbnail_timer = new QTimer(&dialog);
  thumbnail_timer->setInterval(1);
  QObject::connect(thumbnail_timer, &QTimer::timeout, &dialog, [&] {
    for (int row = 1; row < looks->count(); ++row) {
      auto* item = looks->item(row);
      if (item->isHidden() || item->data(kThumbnailReadyRole).toBool()) {
        continue;
      }
      const auto id = item->data(kFilterIdRole).toString().toStdString();
      if (const auto found = invocations.find(id);
          found != invocations.end()) {
        item->setIcon(thumbnail_icon(
            render_proxy(thumbnail_proxy, registry, found->second).image));
        item->setData(kThumbnailReadyRole, true);
      }
      return;
    }
    thumbnail_timer->stop();
  });
  schedule_thumbnails = [thumbnail_timer] {
    if (!thumbnail_timer->isActive()) {
      thumbnail_timer->start();
    }
  };

  rebuild_parameter_editor();
  const auto saved_category =
      settings.value(QLatin1String(kGalleryCategoryKey),
                     QStringLiteral("all"))
          .toString();
  const auto saved_category_index = category->findData(saved_category);
  category->setCurrentIndex(saved_category_index >= 0 ? saved_category_index
                                                       : 0);
  apply_list_filter();
  const auto saved_filter_id =
      settings.value(QLatin1String(kGalleryLastFilterKey)).toString();
  if (!saved_filter_id.isEmpty()) {
    const auto found =
        filter_items.find(saved_filter_id.toStdString());
    if (found != filter_items.end() && !found->second->isHidden()) {
      looks->setCurrentItem(found->second);
    }
  }
  schedule_thumbnails();
  QTimer::singleShot(0, &dialog, [&] {
    if (dialog.isVisible()) {
      emit_canvas_preview();
    }
  });

  const auto dialog_result = run_non_modal_dialog(dialog);
  close_gallery_proxy_preview(proxy_preview_state);
  save_dialog_state();
  if (dialog_result != QDialog::Accepted) {
    return {};
  }
  const auto invocation = current_invocation();
  if (!invocation.has_value()) {
    return {VisualFilterGalleryOutcome::Original, std::nullopt};
  }
  return {VisualFilterGalleryOutcome::Filter, invocation};
}

}  // namespace patchy::ui
