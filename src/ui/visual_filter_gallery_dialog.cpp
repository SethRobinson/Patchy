#include "ui/visual_filter_gallery_dialog.hpp"

#include "filters/smart_filter_recipe_mapping.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/filter_gallery_controls.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/filter_overlay_sync.hpp"
#include "ui/filter_parameter_panel.hpp"
#include "ui/filter_preview_proxy.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
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
constexpr int kRecipeEntryIdRole = Qt::UserRole + 4;
constexpr int kThumbnailExactRole = Qt::UserRole + 5;
constexpr int kSmartFilterBadgeRole = Qt::UserRole + 6;
constexpr int kMaximumThumbnailDimension = 180;

constexpr std::array<FilterCategory, 9> kGalleryCategoryOrder = {
    FilterCategory::PhotoLooks, FilterCategory::Blur,
    FilterCategory::Sharpen,    FilterCategory::Distort,
    FilterCategory::Noise,      FilterCategory::Pixelate,
    FilterCategory::Stylize,    FilterCategory::Render,
    FilterCategory::Artistic,
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
    case FilterCategory::Artistic:
      return QStringLiteral("artistic");
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

struct GalleryRecipeEntry {
  std::uint64_t ui_id{0};
  FilterRecipeEntry value;
};

struct GalleryThumbnailRenderState {
  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
};

constexpr QColor kSmartFilterBadgeFill{0x14, 0x73, 0xe6};
constexpr QColor kSmartFilterBadgeBorder{0x9c, 0xcf, 0xff};

[[nodiscard]] QIcon thumbnail_icon(const QImage& source,
                                   bool smart_filter_badge = false) {
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
  if (smart_filter_badge) {
    // Deliberately unlocalized glyph, like the favorite star; the row tooltip
    // carries the localized explanation.
    const QRect chip(width - 27, height - 18, 24, 15);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kSmartFilterBadgeBorder, 1.0));
    painter.setBrush(kSmartFilterBadgeFill);
    painter.drawRoundedRect(chip, 3.0, 3.0);
    auto badge_font = painter.font();
    badge_font.setPointSizeF(8.0);
    badge_font.setBold(true);
    painter.setFont(badge_font);
    painter.setPen(Qt::white);
    painter.drawText(chip, Qt::AlignCenter, QStringLiteral("SF"));
  }
  painter.end();
  return QIcon(QPixmap::fromImage(thumbnail));
}

[[nodiscard]] QIcon warning_icon() {
  constexpr int size = 16;
  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  QPolygonF triangle;
  triangle << QPointF(8.0, 1.5) << QPointF(15.0, 14.5) << QPointF(1.0, 14.5);
  painter.setPen(QPen(QColor(120, 90, 20), 1.0));
  painter.setBrush(QColor(255, 202, 58));
  painter.drawPolygon(triangle);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(60, 45, 10));
  painter.drawRect(QRectF(7.2, 5.5, 1.6, 5.0));
  painter.drawEllipse(QRectF(7.2, 11.5, 1.6, 1.6));
  painter.end();
  return QIcon(QPixmap::fromImage(image));
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
    VisualFilterGalleryPreviewCallback preview_changed,
    FilterLookLibrary* look_library,
    VisualFilterGalleryExactRecipeRenderer exact_renderer,
    VisualFilterGalleryExactPreviewCallback exact_preview_ready,
    GalleryTargetContext target_context) {
  const Rect exact_source_bounds{bounds.x, bounds.y,
                                 immutable_original.width(),
                                 immutable_original.height()};
  const auto proxy =
      make_filter_preview_proxy(immutable_original, bounds, selection,
                         kFilterProxyMaximumDimension);
  const auto thumbnail_proxy =
      make_filter_preview_proxy(immutable_original, bounds, selection,
                         kMaximumThumbnailDimension);
  const auto original_image = image_from_pixels(proxy.original);
  const auto original_thumbnail = image_from_pixels(thumbnail_proxy.original);
  const auto available_filters = gallery_filters(registry);
  auto owned_look_library =
      look_library == nullptr ? std::make_unique<FilterLookLibrary>() : nullptr;
  auto& saved_look_library =
      look_library != nullptr ? *look_library : *owned_look_library;

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

  const auto smart_object_target =
      target_context.kind == GalleryTargetKind::SmartObject;
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("filterGalleryDialog"));
  dialog.setWindowTitle(QObject::tr("Filter Gallery"));
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
  // Leave enough room for the longer spatial-control labels without
  // collapsing their sliders to a handle-sized sliver.
  parameters->setFixedWidth(340);
  auto* parameter_layout = new QVBoxLayout(parameters);
  parameter_layout->setContentsMargins(10, 8, 10, 8);

  auto* saved_looks_heading =
      new QLabel(QObject::tr("Saved Looks"), parameters);
  saved_looks_heading->setObjectName(
      QStringLiteral("filterGallerySavedLooks"));
  parameter_layout->addWidget(saved_looks_heading);
  auto* saved_looks_combo = new QComboBox(parameters);
  saved_looks_combo->setObjectName(
      QStringLiteral("filterGallerySavedLooksCombo"));
  parameter_layout->addWidget(saved_looks_combo);
  auto* saved_look_actions = new QHBoxLayout();
  saved_look_actions->setContentsMargins(0, 0, 0, 4);
  saved_look_actions->setSpacing(5);
  auto* save_look = new QPushButton(QObject::tr("Save Look..."), parameters);
  save_look->setObjectName(QStringLiteral("filterGallerySaveLookButton"));
  auto* rename_look =
      new QPushButton(QObject::tr("Rename..."), parameters);
  rename_look->setObjectName(
      QStringLiteral("filterGalleryRenameLookButton"));
  auto* delete_look = new QPushButton(QObject::tr("Delete"), parameters);
  delete_look->setObjectName(
      QStringLiteral("filterGalleryDeleteLookButton"));
  saved_look_actions->addWidget(save_look);
  saved_look_actions->addWidget(rename_look);
  saved_look_actions->addWidget(delete_look);
  parameter_layout->addLayout(saved_look_actions);

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
  auto* parameter_form_host = new FilterParameterPanel(parameters);
  parameter_form_host->setObjectName(
      QStringLiteral("filterGalleryParameterEditor"));
  parameter_layout->addWidget(parameter_form_host);

  auto* applied_effects = new QWidget(parameters);
  applied_effects->setObjectName(
      QStringLiteral("filterGalleryAppliedEffects"));
  auto* applied_layout = new QVBoxLayout(applied_effects);
  applied_layout->setContentsMargins(0, 6, 0, 0);
  applied_layout->setSpacing(5);
  auto* applied_heading =
      new QLabel(QObject::tr("Applied Effects"), applied_effects);
  applied_layout->addWidget(applied_heading);
  auto* applied_list = new QListWidget(applied_effects);
  applied_list->setObjectName(
      QStringLiteral("filterGalleryAppliedEffectsList"));
  applied_list->setSelectionMode(QAbstractItemView::SingleSelection);
  applied_list->setDragEnabled(true);
  applied_list->setAcceptDrops(true);
  applied_list->setDropIndicatorShown(true);
  applied_list->setDragDropOverwriteMode(false);
  applied_list->setDefaultDropAction(Qt::MoveAction);
  applied_list->setDragDropMode(QAbstractItemView::InternalMove);
  applied_list->setMinimumHeight(110);
  // The application stylesheet hides QListWidget indicators globally because
  // the Layers panel paints its own visibility controls. This list uses native
  // item check states, so restore a compact, readable indicator locally.
  applied_list->setStyleSheet(QStringLiteral(R"(
    QListWidget#filterGalleryAppliedEffectsList::item {
      min-height: 30px;
      padding: 0 4px;
    }
    QListWidget#filterGalleryAppliedEffectsList::indicator {
      width: 13px;
      height: 13px;
      min-width: 13px;
      min-height: 13px;
      max-width: 13px;
      max-height: 13px;
      margin-left: 4px;
      margin-right: 5px;
      background: #4a4a4a;
      border: 1px solid #8a8a8a;
    }
    QListWidget#filterGalleryAppliedEffectsList::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
      image: url(:/patchy/icons/checkmark.svg);
    }
  )"));
  applied_layout->addWidget(applied_list, 1);
  auto* applied_actions = new QHBoxLayout();
  applied_actions->setContentsMargins(0, 0, 0, 0);
  applied_actions->setSpacing(6);
  auto* duplicate_effect =
      new QPushButton(QObject::tr("Duplicate"), applied_effects);
  duplicate_effect->setObjectName(
      QStringLiteral("filterGalleryDuplicateEffectButton"));
  duplicate_effect->setToolTip(
      QObject::tr("Duplicate the selected effect"));
  applied_actions->addWidget(duplicate_effect);
  auto* remove_effect =
      new QPushButton(QObject::tr("Remove"), applied_effects);
  remove_effect->setObjectName(
      QStringLiteral("filterGalleryRemoveEffectButton"));
  remove_effect->setToolTip(QObject::tr("Remove the selected effect"));
  applied_actions->addWidget(remove_effect);
  applied_layout->addLayout(applied_actions);
  parameter_layout->addWidget(applied_effects, 1);
  content->addWidget(parameters);

  auto* outcome_label = new QLabel(&dialog);
  outcome_label->setObjectName(QStringLiteral("filterGalleryOutcomeLabel"));
  outcome_label->setWordWrap(true);
  if (!smart_object_target) {
    outcome_label->setText(
        QObject::tr("Applies permanently to this layer. To keep effects "
                    "editable, use Filter > Convert for Smart Filters "
                    "first."));
  }
  root->addWidget(outcome_label);

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

  std::map<std::string, FilterInvocation, std::less<>> catalog_invocations;
  std::map<std::string, QListWidgetItem*, std::less<>> filter_items;
  std::vector<GalleryRecipeEntry> recipe_entries;
  std::uint64_t next_recipe_entry_id = 1;
  std::optional<std::uint64_t> active_recipe_entry_id;
  bool synchronizing_catalog = false;
  bool rebuilding_applied_list = false;
  auto* original_item = new QListWidgetItem(QObject::tr("Original"), looks);
  original_item->setData(kFilterIdRole, QString());
  original_item->setIcon(thumbnail_icon(original_thumbnail));
  original_item->setData(kThumbnailReadyRole, true);
  original_item->setData(kThumbnailExactRole, false);
  for (const auto* definition : available_filters) {
    const auto& id = definition->identifier;
    catalog_invocations.emplace(
        id, registry.default_invocation(id, foreground, background));
    auto* item = new QListWidgetItem(filter_display_name(*definition), looks);
    item->setData(kFilterIdRole, QString::fromStdString(id));
    item->setData(kThumbnailReadyRole, false);
    item->setData(kThumbnailExactRole, false);
    item->setData(kFilterCategoryRole,
                  gallery_category_token(definition->catalog.category));
    const auto badged = native_smart_filter_kind_for(id).has_value();
    item->setData(kSmartFilterBadgeRole, badged);
    item->setIcon(thumbnail_icon(QImage(), badged));
    if (badged) {
      item->setToolTip(
          smart_object_target
              ? QObject::tr("Applies to this Smart Object as an editable "
                            "Smart Filter.")
              : QObject::tr(
                    "This filter can run as an editable Smart Filter. Use "
                    "Filter > Convert for Smart Filters on this layer to "
                    "keep it editable."));
    } else {
      item->setToolTip(
          smart_object_target
              ? QObject::tr("This filter has no Smart Filter mapping. "
                            "Applying it will rasterize the Smart Object.")
              : QObject::tr("Applies permanently to the layer pixels."));
    }
    filter_items.emplace(id, item);
  }
  looks->setCurrentRow(0);

  const auto selected_catalog_id = [looks] {
    const auto* item = looks->currentItem();
    return item == nullptr ? std::string{}
                           : item->data(kFilterIdRole).toString().toStdString();
  };
  const auto find_recipe_entry = [&](std::uint64_t ui_id)
      -> GalleryRecipeEntry* {
    const auto found = std::find_if(
        recipe_entries.begin(), recipe_entries.end(),
        [ui_id](const GalleryRecipeEntry& entry) {
          return entry.ui_id == ui_id;
        });
    return found == recipe_entries.end() ? nullptr : &*found;
  };
  const auto active_recipe_entry = [&]() -> GalleryRecipeEntry* {
    return active_recipe_entry_id.has_value()
               ? find_recipe_entry(*active_recipe_entry_id)
               : nullptr;
  };
  const auto active_filter_id = [&] {
    const auto* entry = active_recipe_entry();
    return entry == nullptr ? std::string{}
                            : entry->value.invocation.filter_id;
  };
  const auto current_recipe = [&]() -> std::optional<FilterRecipe> {
    if (recipe_entries.empty()) {
      return std::nullopt;
    }
    FilterRecipe recipe;
    recipe.entries.reserve(recipe_entries.size());
    for (const auto& entry : recipe_entries) {
      recipe.entries.push_back(entry.value);
    }
    return recipe;
  };
  bool loading_saved_look = false;
  std::function<void()> refresh_saved_look_controls;
  std::function<void(const QString&)> rebuild_saved_looks;
  const auto selected_saved_look_id = [saved_looks_combo] {
    return saved_looks_combo->currentData().toString();
  };
  const auto mark_recipe_custom = [&] {
    if (loading_saved_look) {
      return;
    }
    const QSignalBlocker blocker(saved_looks_combo);
    saved_looks_combo->setCurrentIndex(0);
    if (refresh_saved_look_controls) {
      refresh_saved_look_controls();
    }
  };
  refresh_saved_look_controls = [&] {
    const auto recipe = current_recipe();
    save_look->setEnabled(recipe.has_value() && !recipe->entries.empty() &&
                          registry.supports(*recipe));
    const auto id = selected_saved_look_id();
    const auto has_saved_look =
        !id.isEmpty() && saved_look_library.find_entry(id) != nullptr;
    rename_look->setEnabled(has_saved_look);
    delete_look->setEnabled(has_saved_look);
  };
  rebuild_saved_looks = [&](const QString& preferred_id) {
    const QSignalBlocker blocker(saved_looks_combo);
    saved_looks_combo->clear();
    saved_looks_combo->addItem(QObject::tr("Custom"), QString());
    int preferred_index = 0;
    for (const auto& entry : saved_look_library.entries()) {
      const auto supported = !entry.recipe.entries.empty() &&
                             registry.supports(entry.recipe);
      const auto index = saved_looks_combo->count();
      saved_looks_combo->addItem(entry.name, entry.id);
      saved_looks_combo->setItemData(
          index,
          supported
              ? QObject::tr("Apply this saved Look")
              : QObject::tr(
                    "This Look uses filters or settings that this version of "
                    "Patchy cannot apply."),
          Qt::ToolTipRole);
      if (auto* model =
              qobject_cast<QStandardItemModel*>(saved_looks_combo->model());
          model != nullptr && model->item(index) != nullptr) {
        model->item(index)->setEnabled(supported);
      }
      if (supported && entry.id == preferred_id) {
        preferred_index = index;
      }
    }
    saved_looks_combo->setCurrentIndex(preferred_index);
    refresh_saved_look_controls();
  };
  rebuild_saved_looks({});

  // The badge and the per-entry marks come from the same single decision
  // point as Apply (smart_filter_recipe_mapping); the whole-recipe outcome
  // prefers the caller's predicate because caller-side constraints (the
  // 64-entry native stack cap) are not visible to the mapper.
  const auto entry_blocks_smart_mapping =
      [&](const FilterRecipeEntry& entry) {
        FilterRecipe one_entry_recipe;
        one_entry_recipe.entries.push_back(entry);
        return !smart_filter_entries_from_recipe(one_entry_recipe, registry)
                    .has_value();
      };
  const auto recipe_maps_to_smart_stack = [&](const FilterRecipe& recipe) {
    if (target_context.recipe_maps_to_smart_stack) {
      return target_context.recipe_maps_to_smart_stack(recipe);
    }
    return smart_filter_entries_from_recipe(recipe, registry).has_value();
  };
  std::function<void()> refresh_smart_filter_hints;
  refresh_smart_filter_hints = [&] {
    if (!smart_object_target) {
      return;
    }
    const auto recipe = current_recipe();
    // An empty recipe is Original: nothing gets baked, so keep the positive
    // text instead of the raw mapper's empty-recipe rejection.
    const auto mappable =
        !recipe.has_value() || recipe_maps_to_smart_stack(*recipe);
    outcome_label->setText(
        mappable
            ? QObject::tr("Applies as editable Smart Filters.")
            : QObject::tr("Applying will rasterize the Smart Object (some "
                          "effects have no Smart Filter mapping)."));
    // setIcon/setToolTip emit itemChanged; keep the recipe handlers out.
    const auto was_rebuilding = rebuilding_applied_list;
    rebuilding_applied_list = true;
    const QSignalBlocker blocker(applied_list);
    for (int row = 0; row < applied_list->count(); ++row) {
      auto* item = applied_list->item(row);
      const auto* entry = find_recipe_entry(
          item->data(kRecipeEntryIdRole).toULongLong());
      const auto blocks =
          entry != nullptr && entry_blocks_smart_mapping(entry->value);
      item->setIcon(blocks ? warning_icon() : QIcon());
      item->setToolTip(
          blocks ? QObject::tr("This effect cannot be applied as a Smart "
                               "Filter. Applying the stack will rasterize "
                               "the Smart Object.")
                 : QString());
    }
    rebuilding_applied_list = was_rebuilding;
  };

  std::function<void()> rebuild_applied_list;
  rebuild_applied_list = [&] {
    rebuilding_applied_list = true;
    const QSignalBlocker blocker(applied_list);
    applied_list->clear();
    QListWidgetItem* selected_item = nullptr;
    for (auto entry = recipe_entries.rbegin();
         entry != recipe_entries.rend(); ++entry) {
      const auto* definition = registry.find(entry->value.invocation.filter_id);
      auto* item = new QListWidgetItem(
          definition != nullptr
              ? filter_display_name(*definition)
              : QString::fromStdString(entry->value.invocation.filter_id),
          applied_list);
      item->setData(kRecipeEntryIdRole,
                    QVariant::fromValue<qulonglong>(entry->ui_id));
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                     Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
      item->setCheckState(entry->value.enabled ? Qt::Checked
                                               : Qt::Unchecked);
      if (active_recipe_entry_id == entry->ui_id) {
        selected_item = item;
      }
    }
    if (selected_item == nullptr && applied_list->count() > 0) {
      selected_item = applied_list->item(0);
      active_recipe_entry_id =
          selected_item->data(kRecipeEntryIdRole).toULongLong();
    }
    applied_list->setCurrentItem(selected_item);
    const auto has_active = active_recipe_entry() != nullptr;
    duplicate_effect->setEnabled(has_active);
    remove_effect->setEnabled(has_active);
    rebuilding_applied_list = false;
    refresh_smart_filter_hints();
  };
  std::function<void()> schedule_thumbnails;
  const auto update_favorite_button = [&] {
    auto id = active_filter_id();
    if (id.empty()) {
      id = selected_catalog_id();
    }
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
    auto last_filter_id = active_filter_id();
    if (last_filter_id.empty()) {
      last_filter_id = selected_catalog_id();
    }
    settings.setValue(QLatin1String(kGalleryLastFilterKey),
                      QString::fromStdString(last_filter_id));
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
      const QSignalBlocker blocker(looks);
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
          canvas_preview->isChecked(), current_recipe()});
    }
  };
  std::optional<VisualFilterGalleryExactPreview> accepted_exact_preview;
  const auto emit_accepted_exact_preview = [&] {
    if (exact_preview_ready && accepted_exact_preview.has_value()) {
      accepted_exact_preview->canvas_enabled = canvas_preview->isChecked();
      exact_preview_ready(*accepted_exact_preview);
    }
  };

  auto current_proxy_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  std::map<std::uint64_t, Rect> rendered_entry_input_bounds;
  std::function<void()> refresh_spatial_overlay;
  auto proxy_preview_state = std::make_shared<FilterProxyPreviewState>();
  auto proxy_registry = std::make_shared<const FilterRegistry>(registry);
  proxy_preview_state->apply =
      [&](FilterProxyRender rendered, FilterRecipe rendered_recipe,
          std::vector<std::uint64_t> entry_ids, std::uint64_t,
          std::string filter_id) {
        preview->set_image(rendered.image);
        preview->setProperty(
            "filterGalleryRenderedFilterId",
            QString::fromStdString(filter_id));
        const auto used_exact = rendered.exact_result != nullptr;
        preview->setProperty("filterGalleryExactPreview", used_exact);
        if (used_exact) {
          accepted_exact_preview = VisualFilterGalleryExactPreview{
              canvas_preview->isChecked(), std::move(rendered_recipe),
              rendered.exact_result};
        } else {
          accepted_exact_preview.reset();
        }
        current_proxy_bounds = rendered.bounds;
        rendered_entry_input_bounds.clear();
        const auto traced_count = std::min(
            entry_ids.size(), rendered.entry_input_bounds.size());
        for (std::size_t index = 0; index < traced_count; ++index) {
          rendered_entry_input_bounds.emplace(
              entry_ids[index], rendered.entry_input_bounds[index]);
        }
        if (refresh_spatial_overlay) {
          refresh_spatial_overlay();
        }
        status->setText(QObject::tr("Ready"));
        emit_accepted_exact_preview();
      };
  proxy_preview_state->fail =
      [status](QString) { status->setText(QObject::tr("Ready")); };
  proxy_preview_state->start = make_filter_proxy_render_start(
      proxy_preview_state, proxy_registry, proxy, exact_source_bounds,
      exact_renderer);
  auto* central_timer = new QTimer(&dialog);
  central_timer->setSingleShot(true);
  central_timer->setInterval(35);
  const auto render_current = [&] {
    const auto recipe = current_recipe();
    const auto* active = active_recipe_entry();
    if (!recipe.has_value() || active == nullptr) {
      cancel_filter_proxy_preview(proxy_preview_state);
      accepted_exact_preview.reset();
      preview->set_image(original_image);
      preview->setProperty("filterGalleryRenderedFilterId", QString());
      preview->setProperty("filterGalleryExactPreview", false);
      current_proxy_bounds =
          Rect::from_size(proxy.original.width(), proxy.original.height());
      rendered_entry_input_bounds.clear();
      if (refresh_spatial_overlay) {
        refresh_spatial_overlay();
      }
      status->setText(QObject::tr("Ready"));
      return;
    }
    status->setText(QObject::tr("Rendering preview..."));
    std::vector<std::uint64_t> entry_ids;
    entry_ids.reserve(recipe_entries.size());
    for (const auto& entry : recipe_entries) {
      entry_ids.push_back(entry.ui_id);
    }
    enqueue_filter_proxy_preview(
        proxy_preview_state, *recipe, std::move(entry_ids), active->ui_id,
        active->value.invocation.filter_id);
  };
  QObject::connect(central_timer, &QTimer::timeout, &dialog, render_current);
  const auto schedule_render =
      [central_timer, status, proxy_preview_state,
       &accepted_exact_preview] {
    cancel_filter_proxy_preview(proxy_preview_state);
    accepted_exact_preview.reset();
    status->setText(QObject::tr("Rendering preview..."));
    central_timer->start();
  };

  const auto base_dialog_style = dialog.styleSheet();
  const auto spinbox_style = dialog_spinbox_button_style();
  std::function<void()> rebuild_parameter_editor;
  rebuild_parameter_editor = [&] {
    const auto* active = active_recipe_entry();
    const auto id = active != nullptr
                        ? active->value.invocation.filter_id
                        : std::string{};
    const auto active_id = active != nullptr ? active->ui_id : 0;
    const auto* definition = registry.find(id);
    FilterParameterPanelOptions panel_options;
    panel_options.plus_minus_spin_buttons = true;
    panel_options.double_spin_width = 84;
    panel_options.integer_spin_uses_typed_range = false;
    panel_options.slider_row_spacing = 6;
    panel_options.form_margins = QMargins(0, 4, 0, 0);
    panel_options.form_horizontal_spacing = 8;
    panel_options.form_vertical_spacing = 8;
    panel_options.build_companions = true;
    if (active == nullptr || definition == nullptr) {
      parameter_form_host->clear(
          QObject::tr("Choose a filter to adjust its settings."),
          panel_options);
    } else {
      parameter_form_host->rebuild(filter_dialog_spec_for(*definition),
                                   active->value.invocation, panel_options);
      parameter_form_host->set_values_changed_callback(
          [&, active_id](const FilterParameterPanel::ValueChanges& changes) {
            if (auto* entry = find_recipe_entry(active_id);
                entry != nullptr) {
              for (const auto& [key, value] : changes) {
                entry->value.invocation.parameters[key] = value;
              }
              catalog_invocations[entry->value.invocation.filter_id] =
                  entry->value.invocation;
              if (const auto item =
                      filter_items.find(entry->value.invocation.filter_id);
                  item != filter_items.end()) {
                item->second->setData(kThumbnailReadyRole, false);
                item->second->setData(kThumbnailExactRole, false);
              }
            }
            if (schedule_thumbnails) {
              schedule_thumbnails();
            }
            mark_recipe_custom();
            refresh_smart_filter_hints();
            if (refresh_spatial_overlay) {
              refresh_spatial_overlay();
            }
            schedule_render();
            emit_canvas_preview();
          });
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
    const auto* entry = active_recipe_entry();
    const auto id = entry != nullptr
                        ? entry->value.invocation.filter_id
                        : std::string{};
    const auto* definition = registry.find(id);
    const auto input_bounds =
        entry != nullptr ? rendered_entry_input_bounds.find(entry->ui_id)
                         : rendered_entry_input_bounds.end();
    if (entry == nullptr || definition == nullptr ||
        input_bounds == rendered_entry_input_bounds.end()) {
      preview->set_center_radius_overlay(std::nullopt);
      preview->set_tilt_shift_overlay(std::nullopt);
      return;
    }
    const auto spec = filter_dialog_spec_for(*definition);
    const auto value_for = [&](const FilterControlSpec& control) {
      const auto found =
          entry->value.invocation.parameters.find(control.parameter_key);
      return numeric_value(found != entry->value.invocation.parameters.end()
                               ? found->second
                               : control.default_value,
                           control.value);
    };
    const auto state = overlay_state_for(
        spec, value_for,
        FilterOverlayGeometry{input_bounds->second, current_proxy_bounds});
    preview->set_tilt_shift_overlay(state.tilt_shift);
    preview->set_center_radius_overlay(state.center_radius);
  };
  preview->set_center_radius_changed_callback(
      [&](NormalizedCenterRadiusOverlay overlay, bool gesture_finished) {
        auto* entry = active_recipe_entry();
        const auto id = entry != nullptr
                            ? entry->value.invocation.filter_id
                            : std::string{};
        const auto* definition = registry.find(id);
        const auto input_bounds =
            entry != nullptr ? rendered_entry_input_bounds.find(entry->ui_id)
                             : rendered_entry_input_bounds.end();
        if (entry == nullptr || definition == nullptr ||
            input_bounds == rendered_entry_input_bounds.end()) {
          return;
        }
        const auto spec = filter_dialog_spec_for(*definition);
        const FilterOverlayGeometry geometry{input_bounds->second,
                                             current_proxy_bounds};
        const auto sync_role = [&](FilterParameterPresentation role,
                                   double requested) {
          if (const auto* control = overlay_find_control(spec, role);
              control != nullptr) {
            entry->value.invocation.parameters[control->parameter_key] =
                parameter_form_host->sync_control(*control, requested);
          }
        };
        sync_role(FilterParameterPresentation::CenterXPercent,
                  overlay_source_percent(
                      overlay.center.x(), geometry.input_bounds.x,
                      geometry.input_bounds.width, geometry.result_bounds.x,
                      geometry.result_bounds.width));
        sync_role(FilterParameterPresentation::CenterYPercent,
                  overlay_source_percent(
                      overlay.center.y(), geometry.input_bounds.y,
                      geometry.input_bounds.height, geometry.result_bounds.y,
                      geometry.result_bounds.height));
        if (overlay.radius.has_value()) {
          sync_role(FilterParameterPresentation::EffectRadiusPercent,
                    overlay_width_percent_from_normalized(*overlay.radius,
                                                          geometry));
        }
        catalog_invocations[id] = entry->value.invocation;
        if (const auto item = filter_items.find(id);
            item != filter_items.end()) {
          item->second->setData(kThumbnailReadyRole, false);
          item->second->setData(kThumbnailExactRole, false);
        }
        if (schedule_thumbnails) {
          schedule_thumbnails();
        }
        mark_recipe_custom();
        refresh_smart_filter_hints();
        refresh_spatial_overlay();
        if (gesture_finished) {
          schedule_render();
        } else {
          central_timer->stop();
          cancel_filter_proxy_preview(proxy_preview_state);
          accepted_exact_preview.reset();
        }
        emit_canvas_preview();
      });
  preview->set_tilt_shift_changed_callback(
      [&](NormalizedTiltShiftOverlay overlay, bool gesture_finished) {
        auto* entry = active_recipe_entry();
        const auto id = entry != nullptr
                            ? entry->value.invocation.filter_id
                            : std::string{};
        const auto* definition = registry.find(id);
        const auto input_bounds =
            entry != nullptr ? rendered_entry_input_bounds.find(entry->ui_id)
                             : rendered_entry_input_bounds.end();
        if (entry == nullptr || definition == nullptr ||
            input_bounds == rendered_entry_input_bounds.end()) {
          return;
        }
        const auto spec = filter_dialog_spec_for(*definition);
        const FilterOverlayGeometry geometry{input_bounds->second,
                                             current_proxy_bounds};
        const auto sync_role = [&](FilterParameterPresentation role,
                                   double requested) {
          if (const auto* control = overlay_find_control(spec, role);
              control != nullptr) {
            entry->value.invocation.parameters[control->parameter_key] =
                parameter_form_host->sync_control(*control, requested);
          }
        };
        sync_role(FilterParameterPresentation::CenterXPercent,
                  overlay_source_percent(
                      overlay.center.x(), geometry.input_bounds.x,
                      geometry.input_bounds.width, geometry.result_bounds.x,
                      geometry.result_bounds.width));
        sync_role(FilterParameterPresentation::CenterYPercent,
                  overlay_source_percent(
                      overlay.center.y(), geometry.input_bounds.y,
                      geometry.input_bounds.height, geometry.result_bounds.y,
                      geometry.result_bounds.height));
        sync_role(FilterParameterPresentation::Angle, overlay.angle_degrees);
        sync_role(FilterParameterPresentation::TiltFocusHalfWidthPercent,
                  overlay_width_percent_from_normalized(
                      overlay.focus_half_width, geometry));
        sync_role(FilterParameterPresentation::TiltTransitionWidthPercent,
                  overlay_width_percent_from_normalized(
                      overlay.transition_width, geometry));

        catalog_invocations[id] = entry->value.invocation;
        if (const auto item = filter_items.find(id);
            item != filter_items.end()) {
          item->second->setData(kThumbnailReadyRole, false);
          item->second->setData(kThumbnailExactRole, false);
        }
        mark_recipe_custom();
        refresh_smart_filter_hints();
        refresh_spatial_overlay();
        if (gesture_finished) {
          if (schedule_thumbnails) {
            schedule_thumbnails();
          }
          schedule_render();
          emit_canvas_preview();
        } else {
          central_timer->stop();
          cancel_filter_proxy_preview(proxy_preview_state);
          accepted_exact_preview.reset();
        }
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
    cancel_filter_proxy_preview(proxy_preview_state);
    accepted_exact_preview.reset();
    preview->set_image(
        align_original_to_bounds(original_image, current_proxy_bounds));
    preview->set_center_radius_overlay(std::nullopt);
    preview->set_tilt_shift_overlay(std::nullopt);
    status->setText(QObject::tr("Before"));
  });
  QObject::connect(before, &QPushButton::released, &dialog, schedule_render);
  QObject::connect(canvas_preview, &QCheckBox::toggled, &dialog,
                   [&](bool) {
                     emit_canvas_preview();
                     emit_accepted_exact_preview();
                   });

  const auto sync_catalog_to_active = [&] {
    synchronizing_catalog = true;
    {
      const QSignalBlocker blocker(looks);
      auto* wanted = original_item;
      const auto id = active_filter_id();
      if (!id.empty()) {
        if (const auto found = filter_items.find(id);
            found != filter_items.end()) {
          wanted = found->second;
        }
      }
      looks->setCurrentItem(wanted);
    }
    synchronizing_catalog = false;
    update_favorite_button();
  };
  const auto invalidate_spatial_trace = [&] {
    rendered_entry_input_bounds.clear();
    preview->set_center_radius_overlay(std::nullopt);
    preview->set_tilt_shift_overlay(std::nullopt);
  };
  const auto refresh_recipe_ui = [&](bool sync_catalog,
                                     bool recipe_changed = true) {
    invalidate_spatial_trace();
    if (recipe_changed) {
      mark_recipe_custom();
    }
    rebuild_applied_list();
    if (sync_catalog) {
      sync_catalog_to_active();
    } else {
      update_favorite_button();
    }
    rebuild_parameter_editor();
    schedule_render();
    emit_canvas_preview();
  };
  QObject::connect(looks, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem*, QListWidgetItem*) {
                     if (synchronizing_catalog) {
                       return;
                     }
                     const auto id = selected_catalog_id();
                     if (id.empty()) {
                       recipe_entries.clear();
                       active_recipe_entry_id.reset();
                     } else {
                       const auto invocation = catalog_invocations.find(id);
                       if (invocation == catalog_invocations.end()) {
                         return;
                       }
                       if (auto* entry = active_recipe_entry();
                           entry != nullptr) {
                         entry->value.invocation = invocation->second;
                       } else {
                         const auto ui_id = next_recipe_entry_id++;
                         recipe_entries.push_back(GalleryRecipeEntry{
                             ui_id, FilterRecipeEntry{invocation->second}});
                         active_recipe_entry_id = ui_id;
                       }
                      }
                      refresh_recipe_ui(false);
                    });
  QObject::connect(looks, &QListWidget::itemClicked, &dialog,
                   [&](QListWidgetItem* item) {
                     if (synchronizing_catalog || item == nullptr ||
                         !item->data(kFilterIdRole).toString().isEmpty() ||
                         recipe_entries.empty()) {
                       return;
                     }
                     // Filtering can select Original under a signal blocker so
                     // the active recipe survives a catalog-only view change.
                     // A later explicit click on that already-current row must
                     // still perform Original's clear action.
                     recipe_entries.clear();
                     active_recipe_entry_id.reset();
                     refresh_recipe_ui(false);
                   });
  QObject::connect(applied_list, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem* current, QListWidgetItem*) {
                     if (rebuilding_applied_list || current == nullptr) {
                       return;
                     }
                     active_recipe_entry_id =
                         current->data(kRecipeEntryIdRole).toULongLong();
                     sync_catalog_to_active();
                     rebuild_parameter_editor();
                     if (refresh_spatial_overlay) {
                       refresh_spatial_overlay();
                     }
                   });
  QObject::connect(applied_list, &QListWidget::itemChanged, &dialog,
                   [&](QListWidgetItem* item) {
                     if (rebuilding_applied_list || item == nullptr) {
                       return;
                     }
                     const auto ui_id =
                         item->data(kRecipeEntryIdRole).toULongLong();
                      if (auto* entry = find_recipe_entry(ui_id);
                          entry != nullptr) {
                        invalidate_spatial_trace();
                        entry->value.enabled =
                            item->checkState() == Qt::Checked;
                        mark_recipe_custom();
                        refresh_smart_filter_hints();
                        schedule_render();
                       emit_canvas_preview();
                     }
                   });
  QObject::connect(applied_list->model(), &QAbstractItemModel::rowsMoved,
                   &dialog, [&] {
                     if (rebuilding_applied_list) {
                       return;
                     }
                     invalidate_spatial_trace();
                     QTimer::singleShot(0, &dialog, [&] {
                       if (rebuilding_applied_list) {
                         return;
                       }
                       std::map<std::uint64_t, FilterRecipeEntry> entries_by_id;
                       for (const auto& entry : recipe_entries) {
                         entries_by_id.emplace(entry.ui_id, entry.value);
                       }
                       std::vector<GalleryRecipeEntry> reordered;
                       reordered.reserve(recipe_entries.size());
                       for (int row = applied_list->count() - 1; row >= 0;
                            --row) {
                         const auto ui_id = applied_list->item(row)
                                                ->data(kRecipeEntryIdRole)
                                                .toULongLong();
                         if (const auto found = entries_by_id.find(ui_id);
                             found != entries_by_id.end()) {
                           reordered.push_back(
                               GalleryRecipeEntry{ui_id, found->second});
                         }
                       }
                       if (reordered.size() != recipe_entries.size()) {
                         rebuild_applied_list();
                         return;
                        }
                        recipe_entries = std::move(reordered);
                        mark_recipe_custom();
                        schedule_render();
                       emit_canvas_preview();
                     });
                   });
  QObject::connect(duplicate_effect, &QPushButton::clicked, &dialog, [&] {
    if (!active_recipe_entry_id.has_value()) {
      return;
    }
    const auto found = std::find_if(
        recipe_entries.begin(), recipe_entries.end(),
        [&](const GalleryRecipeEntry& entry) {
          return entry.ui_id == *active_recipe_entry_id;
        });
    if (found == recipe_entries.end()) {
      return;
    }
    const auto insertion =
        static_cast<std::size_t>(found - recipe_entries.begin()) + 1U;
    const auto ui_id = next_recipe_entry_id++;
    const auto duplicate = GalleryRecipeEntry{ui_id, found->value};
    recipe_entries.insert(recipe_entries.begin() +
                              static_cast<std::ptrdiff_t>(insertion),
                          duplicate);
    active_recipe_entry_id = ui_id;
    refresh_recipe_ui(true);
  });
  QObject::connect(remove_effect, &QPushButton::clicked, &dialog, [&] {
    if (!active_recipe_entry_id.has_value()) {
      return;
    }
    const auto found = std::find_if(
        recipe_entries.begin(), recipe_entries.end(),
        [&](const GalleryRecipeEntry& entry) {
          return entry.ui_id == *active_recipe_entry_id;
        });
    if (found == recipe_entries.end()) {
      return;
    }
    const auto old_index =
        static_cast<std::size_t>(found - recipe_entries.begin());
    const auto old_visual_row =
        recipe_entries.size() - 1U - old_index;
    recipe_entries.erase(found);
    if (recipe_entries.empty()) {
      active_recipe_entry_id.reset();
    } else {
      const auto new_visual_row =
          std::min(old_visual_row, recipe_entries.size() - 1U);
      const auto new_index =
          recipe_entries.size() - 1U - new_visual_row;
      active_recipe_entry_id = recipe_entries[new_index].ui_id;
    }
    refresh_recipe_ui(true);
  });
  QObject::connect(search, &QLineEdit::textChanged, &dialog,
                   [&](const QString&) { apply_list_filter(); });
  QObject::connect(category, qOverload<int>(&QComboBox::currentIndexChanged),
                   &dialog, [&](int) { apply_list_filter(); });
  QObject::connect(favorite, &QToolButton::toggled, &dialog, [&](bool checked) {
    auto id = active_filter_id();
    if (id.empty()) {
      id = selected_catalog_id();
    }
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

  const auto request_look_name = [&](const QString& title,
                                     const QString& initial,
                                     const QString& accept_text)
      -> std::optional<QString> {
    QInputDialog input(&dialog);
    input.setObjectName(QStringLiteral("filterGalleryLookNameDialog"));
    input.setWindowTitle(title);
    input.setLabelText(QObject::tr("Look name:"));
    input.setInputMode(QInputDialog::TextInput);
    input.setTextValue(initial);
    input.setOkButtonText(accept_text);
    input.resize(420, input.sizeHint().height());
    if (exec_dialog(input) != QDialog::Accepted) {
      return std::nullopt;
    }
    return input.textValue().trimmed();
  };
  const auto show_look_failure = [&](const QString& title,
                                     FilterLookLibraryError error,
                                     const QString& operation) {
    QString message;
    if (error == FilterLookLibraryError::InvalidName) {
      message = QObject::tr("Enter a name for the Look.");
    } else if (error == FilterLookLibraryError::InvalidRecipe) {
      message = QObject::tr("This Look cannot be saved.");
    } else if (error == FilterLookLibraryError::NotFound) {
      message = QObject::tr("The selected Look no longer exists.");
    } else {
      message = QObject::tr(
                    "Could not %1 the Look. Check that the Looks folder is "
                    "writable.")
                    .arg(operation);
    }
    (void)show_warning_message(
        &dialog, title, message, QMessageBox::Ok, QMessageBox::Ok,
        QStringLiteral("filterGalleryLookErrorMessageBox"));
  };
  QObject::connect(saved_looks_combo,
                   qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                   [&](int) {
                     if (loading_saved_look) {
                       return;
                     }
                     const auto id = selected_saved_look_id();
                     if (id.isEmpty()) {
                       refresh_saved_look_controls();
                       return;
                     }
                     const auto* entry = saved_look_library.find_entry(id);
                     if (entry == nullptr || entry->recipe.entries.empty() ||
                         !registry.supports(entry->recipe)) {
                       const QSignalBlocker blocker(saved_looks_combo);
                       saved_looks_combo->setCurrentIndex(0);
                       status->setText(QObject::tr("Unsupported Look"));
                       refresh_saved_look_controls();
                       return;
                     }
                     loading_saved_look = true;
                     recipe_entries.clear();
                     recipe_entries.reserve(entry->recipe.entries.size());
                     for (const auto& recipe_entry : entry->recipe.entries) {
                       recipe_entries.push_back(GalleryRecipeEntry{
                           next_recipe_entry_id++, recipe_entry});
                     }
                     active_recipe_entry_id = recipe_entries.empty()
                                                  ? std::optional<std::uint64_t>{}
                                                  : std::optional<std::uint64_t>{
                                                        recipe_entries.back().ui_id};
                     refresh_recipe_ui(true, false);
                     loading_saved_look = false;
                     refresh_saved_look_controls();
                   });
  QObject::connect(save_look, &QPushButton::clicked, &dialog, [&] {
    const auto recipe = current_recipe();
    if (!recipe.has_value() || recipe->entries.empty() ||
        !registry.supports(*recipe)) {
      return;
    }
    const auto name = request_look_name(QObject::tr("Save Look"), {},
                                        QObject::tr("Save"));
    if (!name.has_value()) {
      return;
    }
    FilterLookLibraryError error = FilterLookLibraryError::None;
    const auto id = saved_look_library.add_look(*name, *recipe, &error);
    if (id.isEmpty()) {
      show_look_failure(QObject::tr("Save Look"), error,
                        QObject::tr("save"));
      return;
    }
    rebuild_saved_looks(id);
  });
  QObject::connect(rename_look, &QPushButton::clicked, &dialog, [&] {
    const auto id = selected_saved_look_id();
    const auto* entry = saved_look_library.find_entry(id);
    if (entry == nullptr) {
      return;
    }
    const auto name = request_look_name(QObject::tr("Rename Look"),
                                        entry->name,
                                        QObject::tr("Rename"));
    if (!name.has_value()) {
      return;
    }
    FilterLookLibraryError error = FilterLookLibraryError::None;
    if (!saved_look_library.rename_look(id, *name, &error)) {
      show_look_failure(QObject::tr("Rename Look"), error,
                        QObject::tr("rename"));
      return;
    }
    rebuild_saved_looks(id);
  });
  QObject::connect(delete_look, &QPushButton::clicked, &dialog, [&] {
    const auto id = selected_saved_look_id();
    const auto* entry = saved_look_library.find_entry(id);
    if (entry == nullptr) {
      return;
    }
    const auto answer = show_warning_message(
        &dialog, QObject::tr("Delete Look"),
        QObject::tr("Delete Look \"%1\"?").arg(entry->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
        QStringLiteral("filterGalleryDeleteLookMessageBox"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    FilterLookLibraryError error = FilterLookLibraryError::None;
    if (!saved_look_library.remove_look(id, &error)) {
      show_look_failure(QObject::tr("Delete Look"), error,
                        QObject::tr("delete"));
      return;
    }
    rebuild_saved_looks({});
  });

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset)) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      if (auto* entry = active_recipe_entry(); entry != nullptr) {
        const auto id = entry->value.invocation.filter_id;
        entry->value.invocation =
            registry.default_invocation(id, foreground, background);
        catalog_invocations[id] = entry->value.invocation;
        if (const auto item = filter_items.find(id);
            item != filter_items.end()) {
          item->second->setData(kThumbnailReadyRole, false);
          item->second->setData(kThumbnailExactRole, false);
        }
        if (schedule_thumbnails) {
          schedule_thumbnails();
        }
        mark_recipe_custom();
        refresh_smart_filter_hints();
      }
      rebuild_parameter_editor();
      schedule_render();
      emit_canvas_preview();
    });
  }

  // Thumbnails are generated lazily, one worker job at a time. Exact renderers
  // may need the full-resolution source, so neither they nor the catalog
  // fallback are allowed to block the UI event loop.
  auto* thumbnail_timer = new QTimer(&dialog);
  thumbnail_timer->setInterval(1);
  auto thumbnail_render_state =
      std::make_shared<GalleryThumbnailRenderState>();
  QObject::connect(thumbnail_timer, &QTimer::timeout, &dialog, [&, proxy_registry] {
    if (thumbnail_render_state->in_flight) {
      thumbnail_timer->stop();
      return;
    }
    for (int row = 1; row < looks->count(); ++row) {
      auto* item = looks->item(row);
      if (item->isHidden() || item->data(kThumbnailReadyRole).toBool()) {
        continue;
      }
      const auto id = item->data(kFilterIdRole).toString().toStdString();
      if (const auto found = catalog_invocations.find(id);
          found != catalog_invocations.end()) {
        thumbnail_timer->stop();
        thumbnail_render_state->in_flight = true;
        const auto generation = thumbnail_render_state->generation.load(
            std::memory_order_acquire);
        const auto invocation = found->second;
        FilterRecipe recipe;
        recipe.entries.push_back(FilterRecipeEntry{invocation});
        auto rendered = std::make_shared<FilterProxyRender>();
        auto error = std::make_shared<QString>();
        auto cancelled = std::make_shared<bool>(false);
        auto* app = QCoreApplication::instance();
        run_tracked_background_worker([app, thumbnail_render_state, proxy_registry,
                     thumbnail_proxy, exact_source_bounds, exact_renderer,
                     recipe = std::move(recipe), invocation, id, generation,
                     rendered, error, cancelled, thumbnail_timer,
                     &filter_items]() mutable {
          FilterProgress progress{
              [thumbnail_render_state, generation](int, int,
                                                   FilterProgressStage) {
                return thumbnail_render_state->generation.load(
                           std::memory_order_acquire) == generation;
              }};
          try {
            std::optional<FilterRenderResult> exact;
            if (exact_renderer) {
              exact = exact_renderer(recipe, &progress);
            }
            if (exact.has_value()) {
              auto shared_exact = std::make_shared<const FilterRenderResult>(
                  std::move(*exact));
              *rendered = exact_render_to_proxy(
                  std::move(shared_exact), exact_source_bounds,
                  thumbnail_proxy);
            } else {
              if (progress.update &&
                  !progress.update(0, 1, FilterProgressStage::Filtering)) {
                throw FilterCancelled();
              }
              *rendered = render_filter_proxy(thumbnail_proxy, *proxy_registry,
                                       invocation, &progress);
            }
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
              [thumbnail_render_state, generation, id, rendered, error,
               cancelled, thumbnail_timer, &filter_items]() mutable {
                thumbnail_render_state->in_flight = false;
                const auto is_latest =
                    generation == thumbnail_render_state->generation.load(
                                      std::memory_order_acquire);
                if (!thumbnail_render_state->closed && is_latest &&
                    !*cancelled) {
                  if (const auto found = filter_items.find(id);
                      found != filter_items.end()) {
                    if (error->isEmpty()) {
                      found->second->setIcon(thumbnail_icon(
                          rendered->image,
                          native_smart_filter_kind_for(id).has_value()));
                      found->second->setData(
                          kThumbnailExactRole,
                          rendered->exact_result != nullptr);
                    }
                    // A failed exact renderer must not create a tight retry
                    // loop; changing this invocation invalidates it again.
                    found->second->setData(kThumbnailReadyRole, true);
                  }
                }
                if (!thumbnail_render_state->closed) {
                  thumbnail_timer->start();
                }
              },
              Qt::QueuedConnection);
        });
      }
      return;
    }
    thumbnail_timer->stop();
  });
  schedule_thumbnails = [thumbnail_timer, thumbnail_render_state] {
    thumbnail_render_state->generation.fetch_add(1,
                                                 std::memory_order_acq_rel);
    if (!thumbnail_render_state->in_flight &&
        !thumbnail_timer->isActive()) {
      thumbnail_timer->start();
    }
  };

  rebuild_applied_list();
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
  close_filter_proxy_preview(proxy_preview_state);
  thumbnail_render_state->closed = true;
  thumbnail_render_state->generation.fetch_add(1,
                                               std::memory_order_acq_rel);
  thumbnail_timer->stop();
  save_dialog_state();
  if (dialog_result != QDialog::Accepted) {
    return {};
  }
  const auto recipe = current_recipe();
  if (!recipe.has_value()) {
    return {VisualFilterGalleryOutcome::Original, std::nullopt};
  }
  return {VisualFilterGalleryOutcome::Filter, recipe};
}

}  // namespace patchy::ui
