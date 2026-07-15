#include "ui/main_window_shared.hpp"

#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_filter_effects.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/brush_presets.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_list_widget.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QRegion>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

namespace patchy::ui {

bool layer_tree_contains_smart_filters(const Layer& layer) {
  if (layer.smart_filter_stack() != nullptr) {
    return true;
  }
  return std::any_of(layer.children().begin(), layer.children().end(),
                     layer_tree_contains_smart_filters);
}

bool layer_tree_contains_smart_object(const Layer& layer) {
  if (layer_is_smart_object(layer)) {
    return true;
  }
  return std::any_of(layer.children().begin(), layer.children().end(),
                     layer_tree_contains_smart_object);
}

bool document_contains_smart_objects(const Document& document) {
  return std::any_of(document.layers().begin(), document.layers().end(),
                     layer_tree_contains_smart_object);
}

QString localized_adjustment_display_name(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return QObject::tr("Levels");
    case AdjustmentKind::Curves:
      return QObject::tr("Curves");
    case AdjustmentKind::HueSaturation:
      return QObject::tr("Hue/Saturation");
    case AdjustmentKind::ColorBalance:
      return QObject::tr("Color Balance");
  }
  return QObject::tr("Adjustment");
}

void clear_layer_psd_style_source(Layer& layer) {
  auto& blocks = layer.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    // 'lmfx' must go too: a stale multi-instance block left beside a
    // regenerated lfx2 would win in Photoshop and hide the edit.
    return block.key == "lfx2" || block.key == "lrFX" || block.key == "plFX" ||
           block.key == "lmfx";
  });
}

double text_size_ppi(const Document& document) noexcept {
  const auto ppi = document.print_settings().horizontal_ppi;
  return std::isfinite(ppi) && ppi > 0.0 ? std::clamp(ppi, 1.0, 9999.0) : 300.0;
}

double text_pixels_to_points(int pixels, const Document& document) noexcept {
  return std::max(0.01, static_cast<double>(std::max(1, pixels)) * 72.0 / text_size_ppi(document));
}

int text_points_to_pixels(double points, const Document& document) noexcept {
  if (!std::isfinite(points)) {
    return 1;
  }
  return std::max(1, static_cast<int>(std::lround(std::max(0.01, points) * text_size_ppi(document) / 72.0)));
}

void set_layer_pixels_preserving_origin(Layer& layer, PixelBuffer pixels, Rect original_bounds) {
  const auto x = original_bounds.x;
  const auto y = original_bounds.y;
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(Rect{x, y, layer.pixels().width(), layer.pixels().height()});
}

void set_layer_pixels_with_bounds(Layer& layer, PixelBuffer pixels, Rect new_bounds) {
  const auto x = new_bounds.x;
  const auto y = new_bounds.y;
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(Rect{x, y, layer.pixels().width(), layer.pixels().height()});
}

PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect) {
  PixelBuffer mask_pixels(selection_rect.width(), selection_rect.height(), PixelFormat::gray8());
  mask_pixels.clear(0);
  if (selection_rect.isEmpty()) {
    return mask_pixels;
  }

  if (!canvas.selection_has_partial_alpha()) {
    const auto selected = canvas.selected_document_region().intersected(QRegion(selection_rect));
    const QRect local_bounds(0, 0, selection_rect.width(), selection_rect.height());
    for (const auto& rect : selected) {
      const auto local =
          QRect(rect.x() - selection_rect.x(), rect.y() - selection_rect.y(), rect.width(), rect.height())
              .intersected(local_bounds);
      if (local.isEmpty()) {
        continue;
      }
      for (int y = local.top(); y <= local.bottom(); ++y) {
        auto row = mask_pixels.row(y);
        std::fill(row.begin() + local.left(), row.begin() + local.left() + local.width(),
                  static_cast<std::uint8_t>(255));
      }
    }
    return mask_pixels;
  }

  for (int y = 0; y < selection_rect.height(); ++y) {
    for (int x = 0; x < selection_rect.width(); ++x) {
      const QPoint document_point(selection_rect.x() + x, selection_rect.y() + y);
      *mask_pixels.pixel(x, y) = canvas.selection_alpha_at(document_point);
    }
  }
  return mask_pixels;
}

int proportional_brush_step(int size, int direction, bool coarse) {
  const double f = coarse ? 0.30 : 0.10;
  const int min_step = coarse ? 2 : 1;
  const double basis = direction > 0 ? size * f : size * f / (1.0 + f);
  return std::max(min_step, static_cast<int>(std::lround(basis)));
}

QString default_startup_brush_preset_id() {
  return QStringLiteral("round");
}

void apply_brush_preset(CanvasWidget& canvas, const BrushPreset& preset) {
  canvas.set_brush_build_up(preset.build_up);
  canvas.set_brush_size(preset.size);
  canvas.set_brush_opacity(preset.opacity);
  canvas.set_brush_flow(preset.flow);
  canvas.set_brush_softness(preset.softness);
}

QString translate_source(const QObject* object, const char* property_name) {
  const auto source = object->property(property_name).toString();
  if (source.isEmpty()) {
    return {};
  }
  auto context = object->property(kTranslationContextProperty).toString();
  if (context.isEmpty()) {
    context = QStringLiteral("patchy::ui::MainWindow");
  }
  const auto context_bytes = context.toUtf8();
  const auto source_bytes = source.toUtf8();
  return QCoreApplication::translate(context_bytes.constData(), source_bytes.constData());
}

void bind_translated_text(QObject* object, const char* source, const char* context) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationTextProperty, QString::fromLatin1(source));
}

void bind_translated_tooltip(QObject* object, const char* source, const char* context) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationToolTipProperty, QString::fromLatin1(source));
}

void apply_bound_translation(QObject* object) {
  if (object == nullptr) {
    return;
  }

  if (object->property(kTranslationTextProperty).isValid()) {
    const auto text = translate_source(object, kTranslationTextProperty);
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setText(text);
    } else if (auto* menu = qobject_cast<QMenu*>(object); menu != nullptr) {
      menu->setTitle(text);
    } else if (auto* dock = qobject_cast<QDockWidget*>(object); dock != nullptr) {
      dock->setWindowTitle(text);
    } else if (auto* toolbar = qobject_cast<QToolBar*>(object); toolbar != nullptr) {
      toolbar->setWindowTitle(text);
    } else if (auto* label = qobject_cast<QLabel*>(object); label != nullptr) {
      label->setText(text);
    } else if (auto* button = qobject_cast<QAbstractButton*>(object); button != nullptr) {
      button->setText(text);
    } else if (auto* group = qobject_cast<QGroupBox*>(object); group != nullptr) {
      group->setTitle(text);
    }
  }

  if (object->property(kTranslationToolTipProperty).isValid()) {
    const auto tooltip = translate_source(object, kTranslationToolTipProperty);
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setToolTip(tooltip);
    } else if (auto* widget = qobject_cast<QWidget*>(object); widget != nullptr) {
      widget->setToolTip(tooltip);
    }
  }

  if (object->property(kTranslationStatusTipProperty).isValid()) {
    if (auto* action = qobject_cast<QAction*>(object); action != nullptr) {
      action->setStatusTip(translate_source(object, kTranslationStatusTipProperty));
    }
  }
}

void bind_action_text(QAction* action, const char* source) {
  bind_translated_text(action, source);
  apply_bound_translation(action);
}

void bind_widget_text(QObject* object, const char* source) {
  bind_translated_text(object, source);
  apply_bound_translation(object);
}

void bind_tooltip(QObject* object, const char* source) {
  bind_translated_tooltip(object, source);
  apply_bound_translation(object);
}

QString tool_name(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QObject::tr("Move");
    case CanvasTool::Marquee:
      return QObject::tr("Marquee");
    case CanvasTool::EllipticalMarquee:
      return QObject::tr("Elliptical Marquee");
    case CanvasTool::Lasso:
      return QObject::tr("Lasso");
    case CanvasTool::MagneticLasso:
      return QObject::tr("Magnetic Lasso");
    case CanvasTool::MagicWand:
      return QObject::tr("Magic Wand");
    case CanvasTool::QuickSelect:
      return QObject::tr("Quick Select");
    case CanvasTool::Brush:
      return QObject::tr("Brush");
    case CanvasTool::Clone:
      return QObject::tr("Clone Stamp");
    case CanvasTool::Healing:
      return QObject::tr("Healing Brush");
    case CanvasTool::Smudge:
      return QObject::tr("Smudge");
    case CanvasTool::Dodge:
      return QObject::tr("Dodge");
    case CanvasTool::Burn:
      return QObject::tr("Burn");
    case CanvasTool::Sponge:
      return QObject::tr("Sponge");
    case CanvasTool::BlurBrush:
      return QObject::tr("Blur");
    case CanvasTool::SharpenBrush:
      return QObject::tr("Sharpen");
    case CanvasTool::Eraser:
      return QObject::tr("Eraser");
    case CanvasTool::Gradient:
      return QObject::tr("Gradient");
    case CanvasTool::Line:
      return QObject::tr("Line");
    case CanvasTool::Rectangle:
      return QObject::tr("Rectangle");
    case CanvasTool::Ellipse:
      return QObject::tr("Ellipse");
    case CanvasTool::Fill:
      return QObject::tr("Fill");
    case CanvasTool::Eyedropper:
      return QObject::tr("Eyedropper");
    case CanvasTool::Text:
      return QObject::tr("Type");
    case CanvasTool::Pan:
      return QObject::tr("Pan");
    case CanvasTool::Zoom:
      return QObject::tr("Zoom");
  }
  return QObject::tr("Tool");
}

void set_text_smoothing_combo_value(QComboBox* combo, int value) {
  if (combo == nullptr) {
    return;
  }
  value = std::clamp(value, 0, 16);
  const auto index = combo->findData(value);
  const auto fallback_index = combo->findData(kDefaultTextAntiAlias);
  const QSignalBlocker blocker(combo);
  combo->setCurrentIndex(index >= 0 ? index : std::max(0, fallback_index));
}

namespace {

bool ui_profile_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_UI_PROFILE");
  return enabled;
}

}  // namespace

void log_ui_profile(std::string_view stage, double elapsed_ms, std::string_view detail) {
  if (!ui_profile_enabled()) {
    return;
  }
  std::cerr << "PATCHY_UI_PROFILE stage=" << stage << " elapsed_ms=" << elapsed_ms;
  if (!detail.empty()) {
    std::cerr << " detail=\"" << detail << "\"";
  }
  std::cerr << '\n';
}

void restyle_layer_rows(QListWidget* list) {
  if (list == nullptr) {
    return;
  }
  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    auto* row_widget = list->itemWidget(item);
    if (row_widget == nullptr) {
      continue;
    }
    const auto is_group = item->data(kLayerIsGroupRole).toBool();
    const auto background = item->isSelected() ? QStringLiteral("#3c4651")
                            : is_group        ? QStringLiteral("#292d31")
                                              : QStringLiteral("#242628");
    const auto divider = item->isSelected() ? QStringLiteral("#4f91ca") : QStringLiteral("#303338");
    row_widget->setStyleSheet(QStringLiteral(
                                  "QWidget#layerRowWidget { background: %1; border-bottom: 1px solid %2; }")
                                  .arg(background, divider));
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      auto font = name->font();
      font.setBold(item == list->currentItem() || is_group);
      name->setFont(font);
    }
  }
}

bool smart_filter_mask_document_editing_supported(
    std::int32_t document_width, std::int32_t document_height) noexcept {
  const auto document_pixels =
      document_width > 0 && document_height > 0
          ? static_cast<std::uint64_t>(document_width) *
                static_cast<std::uint64_t>(document_height)
          : 0;
  return document_pixels > 0 &&
         document_pixels <= psd::kMaximumEditableSmartFilterMaskPixels;
}

std::optional<PixelBuffer> materialize_smart_filter_mask(
    const SmartFilterMask& mask, std::int32_t document_width,
    std::int32_t document_height) {
  if (!smart_filter_mask_document_editing_supported(document_width,
                                                     document_height) ||
      mask.pixels.empty() ||
      mask.pixels.format() != PixelFormat::gray8() ||
      mask.bounds.width != mask.pixels.width() ||
      mask.bounds.height != mask.pixels.height()) {
    return std::nullopt;
  }
  PixelBuffer pixels(document_width, document_height, PixelFormat::gray8());
  pixels.clear(mask.extend_with_white ? 255U : mask.default_color);
  const auto left = std::max<std::int64_t>(0, mask.bounds.x);
  const auto top = std::max<std::int64_t>(0, mask.bounds.y);
  const auto right = std::min<std::int64_t>(
      document_width, static_cast<std::int64_t>(mask.bounds.x) +
                          mask.bounds.width);
  const auto bottom = std::min<std::int64_t>(
      document_height, static_cast<std::int64_t>(mask.bounds.y) +
                           mask.bounds.height);
  for (auto y = top; y < bottom; ++y) {
    for (auto x = left; x < right; ++x) {
      *pixels.pixel(static_cast<std::int32_t>(x),
                    static_cast<std::int32_t>(y)) =
          *mask.pixels.pixel(
              static_cast<std::int32_t>(x - mask.bounds.x),
              static_cast<std::int32_t>(y - mask.bounds.y));
    }
  }
  return pixels;
}

void update_layer_target_styles(QListWidget* list, std::optional<LayerId> active_layer,
                                CanvasWidget::LayerEditTarget edit_target) {
  if (list == nullptr) {
    return;
  }

  auto set_target_active = [](QWidget* widget, bool active) {
    if (widget == nullptr) {
      return;
    }
    widget->setProperty("layerTargetActive", active);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  };

  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    auto* row_widget = list->itemWidget(item);
    if (item == nullptr || row_widget == nullptr) {
      continue;
    }
    const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    const auto row_active = active_layer.has_value() && *active_layer == layer_id;
    set_target_active(row_widget->findChild<QWidget*>(QStringLiteral("layerContentThumbnail")),
                      row_active && edit_target == CanvasWidget::LayerEditTarget::Content);
    set_target_active(row_widget->findChild<QWidget*>(QStringLiteral("layerMaskThumbnail")),
                      row_active && edit_target == CanvasWidget::LayerEditTarget::Mask);
    set_target_active(
        row_widget->findChild<QWidget*>(
            QStringLiteral("layerSmartFilterMaskThumbnail")),
        row_active &&
            edit_target == CanvasWidget::LayerEditTarget::SmartFilterMask);
  }
}

bool layer_has_rasterizable_content(const Layer& layer) {
  return layer.kind() == LayerKind::Pixel || layer.kind() == LayerKind::Text || layer_is_text(layer);
}

bool layer_can_rasterize(const Layer& layer) {
  return layer.kind() == LayerKind::Text || layer_is_text(layer) || layer_is_smart_object(layer);
}

bool layer_can_rasterize_layer_style(const Layer& layer) {
  return layer_has_rasterizable_content(layer) && !layer.layer_style().empty();
}

QString default_file_dialog_directory() {
  auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (path.isEmpty()) {
    path = QDir::homePath();
  }
  return path;
}

QString last_save_directory() {
  auto settings = app_settings();
  const auto path = settings.value(QStringLiteral("lastSaveDirectory")).toString();
  if (!path.isEmpty()) {
    const QFileInfo info(path);
    if (info.isDir()) {
      return info.absoluteFilePath();
    }
  }
  return default_file_dialog_directory();
}

void remember_save_directory_for_path(const QString& path) {
  const QFileInfo info(path);
  const auto directory = info.absoluteDir();
  if (!directory.exists()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("lastSaveDirectory"), directory.absolutePath());
}

QString file_dialog_initial_path(const QString& existing_path, const QString& filename) {
  if (!existing_path.isEmpty()) {
    return existing_path;
  }
  return QDir(last_save_directory()).filePath(filename);
}

namespace {

bool smart_filter_record_is_cloneable(const SmartFilterEffectsRecord& record) {
  return !record.original_placed_uuid.empty() && record.raw_storage != nullptr &&
         record.raw_body_offset <= record.raw_storage->size() &&
         record.raw_body_length <= record.raw_storage->size() - record.raw_body_offset;
}

const SmartFilterEffectsRecord* transferred_smart_filter_record(
    const std::vector<SmartFilterEffectsRecord>& records, std::string_view placed_uuid) {
  const SmartFilterEffectsRecord* found = nullptr;
  for (const auto& record : records) {
    if (record.placed_uuid != placed_uuid) {
      continue;
    }
    if (found != nullptr) {
      return nullptr;
    }
    found = &record;
  }
  return found;
}

}  // namespace

class PhotoshopLayerIdAllocator {
public:
  explicit PhotoshopLayerIdAllocator(const std::vector<Layer>& layers) {
    const auto collect = [this](const auto& self,
                                const std::vector<Layer>& siblings) -> void {
      for (const auto& layer : siblings) {
        if (const auto id = patchy::photoshop_layer_id(layer); id.has_value()) {
          used_.insert(*id);
        }
        self(self, layer.children());
      }
    };
    collect(collect, layers);
    if (!used_.empty() &&
        *used_.rbegin() != std::numeric_limits<std::uint32_t>::max()) {
      next_ = *used_.rbegin() + 1U;
    }
  }

  [[nodiscard]] std::optional<std::uint32_t> allocate() {
    // A run of occupied candidates cannot be longer than used_.size(). Trying
    // one more value therefore either finds a hole or proves the id space full.
    const auto attempts = used_.size() + 1U;
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
      const auto candidate = next_;
      next_ = candidate == std::numeric_limits<std::uint32_t>::max()
                  ? 1U
                  : candidate + 1U;
      if (used_.insert(candidate).second) {
        return candidate;
      }
    }
    return std::nullopt;
  }

private:
  std::set<std::uint32_t> used_;
  std::uint32_t next_{1U};
};

bool smart_filter_records_available_for_clone(
    const Layer& source, const SmartFilterEffectsStore& store,
    const std::vector<SmartFilterEffectsRecord>* transferred_records) {
  if (source.smart_filter_stack() != nullptr) {
    const auto placed_uuid = smart_object_placed_uuid(source);
    const auto* record = transferred_records != nullptr
                             ? transferred_smart_filter_record(*transferred_records, placed_uuid)
                             : store.find_unique(placed_uuid);
    if (record == nullptr || !smart_filter_record_is_cloneable(*record)) {
      return false;
    }
  }
  return std::all_of(source.children().begin(), source.children().end(),
                     [&](const Layer& child) {
                       return smart_filter_records_available_for_clone(
                           child, store, transferred_records);
                     });
}

std::optional<Layer> clone_layer_tree_with_document_ids(
    Document& document, const Layer& source,
    const std::vector<SmartFilterEffectsRecord>* transferred_records,
    PhotoshopLayerIdAllocator* native_layer_ids) {
  std::optional<PhotoshopLayerIdAllocator> owned_native_layer_ids;
  if (native_layer_ids == nullptr) {
    owned_native_layer_ids.emplace(document.layers());
    native_layer_ids = &*owned_native_layer_ids;
  }
  auto cloned = source.clone_with_id(document.allocate_layer_id());
  cloned.children().clear();
  if (patchy::photoshop_layer_id(source).has_value()) {
    const auto new_native_layer_id = native_layer_ids->allocate();
    if (!new_native_layer_id.has_value()) {
      return std::nullopt;
    }
    patchy::set_photoshop_layer_id(cloned, *new_native_layer_id);
  }

  if (layer_is_smart_object(source)) {
    const auto old_placed_uuid = smart_object_placed_uuid(source);
    if (!old_placed_uuid.empty()) {
      const auto new_placed_uuid = generate_smart_object_uuid();
      if (source.smart_filter_stack() != nullptr) {
        bool cache_cloned = false;
        if (transferred_records != nullptr) {
          if (const auto* record = transferred_smart_filter_record(
                  *transferred_records, old_placed_uuid);
              record != nullptr) {
            cache_cloned = document.metadata().smart_filter_effects.adopt(
                *record, new_placed_uuid);
          }
        } else {
          cache_cloned = document.metadata().smart_filter_effects.clone_rekey(
              old_placed_uuid, new_placed_uuid);
        }
        if (!cache_cloned) {
          return std::nullopt;
        }
      }
      cloned.metadata()[kLayerMetadataSmartObjectPlaced] = new_placed_uuid;
      mark_layer_smart_object_block_dirty(cloned);
    }
  }

  for (const auto& child : source.children()) {
    auto child_clone = clone_layer_tree_with_document_ids(document, child,
                                                          transferred_records,
                                                          native_layer_ids);
    if (!child_clone.has_value()) {
      return std::nullopt;
    }
    cloned.add_child(std::move(*child_clone));
  }
  return cloned;
}
// Promoted from main_window.cpp's anonymous namespace: used by the
// document-dialog TU and by the layer/selection commands that stayed in
// main_window.cpp.
PixelBuffer make_solid_pixels(std::int32_t width, std::int32_t height, QColor color, PixelFormat format) {
  PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

std::optional<QString> request_text_input(QWidget* parent, const QString& object_name, const QString& title,
                                          const QString& label, const QString& initial) {
  QInputDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  dialog.setLabelText(label);
  dialog.setInputMode(QInputDialog::TextInput);
  dialog.setTextEchoMode(QLineEdit::Normal);
  dialog.setTextValue(initial);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog.textValue();
}

std::optional<int> request_integer_input(QWidget* parent, const QString& object_name, const QString& title,
                                         const QString& label, int value, int minimum, int maximum, int step) {
  QDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* spin = new QSpinBox(&dialog);
  spin->setObjectName(QStringLiteral("integerInputSpin"));
  spin->setRange(minimum, maximum);
  spin->setSingleStep(std::max(1, step));
  spin->setValue(std::clamp(value, minimum, maximum));
  configure_dialog_spinbox(spin);
  form->addRow(label, spin);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  spin->selectAll();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return spin->value();
}

}  // namespace patchy::ui
