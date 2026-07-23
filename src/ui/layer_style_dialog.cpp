#include "ui/layer_style_dialog.hpp"

#include "core/contour_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/coalesced_preview_emitter.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <type_traits>
#include <utility>

namespace patchy::ui {

namespace {

// Forwards a left double-click on a passive color patch to its page's Choose
// Color button, so the swatch itself opens the picker (Photoshop behavior).
// Parented to the watched widget; the button is tracked by QPointer in case it
// dies first.
class DoubleClickClicksButton : public QObject {
 public:
  DoubleClickClicksButton(QAbstractButton* button, QWidget* watched)
      : QObject(watched), button_(button) {
    watched->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && button_ != nullptr) {
      button_->click();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QPointer<QAbstractButton> button_;
};

class GradientCanvasRepositionFilter final : public QObject {
public:
  GradientCanvasRepositionFilter(CanvasWidget *canvas, Rect layer_bounds,
                                 std::function<bool()> enabled,
                                 std::function<void(float, float)> moved,
                                 QObject *parent)
      : QObject(parent), canvas_(canvas), layer_bounds_(layer_bounds),
        enabled_(std::move(enabled)), moved_(std::move(moved)) {
    canvas_->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched != canvas_)
      return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      if (mouse->button() != Qt::LeftButton || !enabled_())
        return false;
      dragging_ = true;
      last_position_ = mouse->position();
      canvas_->grabMouse();
      return true;
    }
    if (event->type() == QEvent::MouseMove && dragging_) {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      const auto delta = mouse->position() - last_position_;
      last_position_ = mouse->position();
      const auto origin = canvas_->widget_position_for_document_point(
          QPoint(layer_bounds_.x, layer_bounds_.y));
      const auto opposite = canvas_->widget_position_for_document_point(
          QPoint(layer_bounds_.x + std::max(1, layer_bounds_.width),
                 layer_bounds_.y + std::max(1, layer_bounds_.height)));
      const auto pixels_per_document_x = std::max(
          0.0001, std::abs(opposite.x() - origin.x()) /
                      static_cast<double>(std::max(1, layer_bounds_.width)));
      const auto pixels_per_document_y = std::max(
          0.0001, std::abs(opposite.y() - origin.y()) /
                      static_cast<double>(std::max(1, layer_bounds_.height)));
      moved_(static_cast<float>(delta.x() / pixels_per_document_x * 100.0 /
                                std::max(1, layer_bounds_.width)),
             static_cast<float>(delta.y() / pixels_per_document_y * 100.0 /
                                std::max(1, layer_bounds_.height)));
      return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && dragging_) {
      dragging_ = false;
      canvas_->releaseMouse();
      return true;
    }
    return false;
  }

private:
  CanvasWidget *canvas_{};
  Rect layer_bounds_{};
  std::function<bool()> enabled_;
  std::function<void(float, float)> moved_;
  bool dragging_{false};
  QPointF last_position_{};
};

LayerStyleGradient default_layer_style_gradient() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops.push_back(GradientColorStop{0.0F, RgbColor{255, 255, 255}});
  gradient.color_stops.push_back(GradientColorStop{1.0F, RgbColor{32, 32, 32}});
  gradient.alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
  gradient.alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  return gradient;
}

LayerDropShadow default_drop_shadow() {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.spread = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerInnerShadow default_inner_shadow() {
  LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.choke = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerOuterGlow default_outer_glow() {
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Normal;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.spread = 0.0F;
  glow.size = 5.0F;
  return glow;
}

LayerInnerGlow default_inner_glow() {
  LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.choke = 0.0F;
  glow.size = 5.0F;
  glow.source = LayerInnerGlowSource::Edge;
  return glow;
}

LayerColorOverlay default_color_overlay() {
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = BlendMode::Normal;
  overlay.color = RgbColor{255, 0, 0};
  overlay.opacity = 1.0F;
  return overlay;
}

LayerGradientFill default_gradient_fill() {
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient = default_layer_style_gradient();
  return fill;
}

LayerStroke default_stroke() {
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = BlendMode::Normal;
  stroke.color = RgbColor{255, 0, 0};
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = LayerStrokePosition::Outside;
  return stroke;
}

LayerBevelEmboss default_bevel_emboss() {
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = BlendMode::Screen;
  bevel.highlight_color = RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = BlendMode::Multiply;
  bevel.shadow_color = RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  return bevel;
}

LayerSatin default_satin() {
  LayerSatin satin;
  satin.enabled = true;
  return satin;
}

LayerPatternOverlay default_pattern_overlay() {
  LayerPatternOverlay overlay;
  overlay.enabled = true;
  const auto presets = builtin_pattern_presets();
  if (!presets.empty()) {
    overlay.pattern_id = presets.front().id;
    overlay.pattern_name = presets.front().english_name;
  }
  return overlay;
}

// Thumbnail of a pattern tile for picker items (small tiles repeat 2x2 so they
// still read at icon size).
QIcon pattern_preset_icon(const PixelBuffer& tile, int extent = 24) {
  if (tile.empty() || tile.format().channels < 4) {
    return {};
  }
  QImage image(tile.width(), tile.height(), QImage::Format_RGBA8888);
  for (std::int32_t y = 0; y < tile.height(); ++y) {
    std::memcpy(image.scanLine(y), tile.pixel(0, y), static_cast<std::size_t>(tile.width()) * 4U);
  }
  QImage canvas_image(extent, extent, QImage::Format_RGBA8888);
  canvas_image.fill(Qt::transparent);
  QPainter painter(&canvas_image);
  const auto repeats = tile.width() < extent || tile.height() < extent ? 2 : 1;
  const auto cell_w = extent / repeats;
  const auto cell_h = extent / repeats;
  for (int ry = 0; ry < repeats; ++ry) {
    for (int rx = 0; rx < repeats; ++rx) {
      painter.drawImage(QRect(rx * cell_w, ry * cell_h, cell_w, cell_h), image);
    }
  }
  painter.end();
  return QIcon(QPixmap::fromImage(canvas_image));
}

[[nodiscard]] bool pattern_tiles_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.format() == rhs.format() && lhs.data().size() == rhs.data().size() &&
         std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

// Thumbnail of a contour curve: the engine LUT drawn as a polyline on a chip.
QIcon contour_preset_icon(const StyleContour& contour) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 24;
  QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
  image.fill(QColor(43, 43, 43));
  QPainter painter(&image);
  painter.setPen(QColor(90, 90, 90));
  painter.drawRect(0, 0, kWidth - 1, kHeight - 1);
  const auto lut = build_style_contour_lut(contour);
  QPolygonF line;
  for (int x = 0; x < kWidth; ++x) {
    const auto t = static_cast<float>(x) / static_cast<float>(kWidth - 1);
    const auto value = sample_style_contour_lut(lut, t, true);
    line << QPointF(1.0 + x * (kWidth - 3.0) / (kWidth - 1.0),
                    (kHeight - 2.0) - value * (kHeight - 4.0) + 1.0);
  }
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(220, 226, 235), 1.2));
  painter.drawPolyline(line);
  painter.end();
  return QIcon(QPixmap::fromImage(image));
}

constexpr int kPatternIdRole = Qt::UserRole;
constexpr int kPatternPersistNameRole = Qt::UserRole + 1;
constexpr int kPatternPickerIndexRole = Qt::UserRole + 2;
constexpr int kPatternPickerIsLeafRole = Qt::UserRole + 3;
constexpr const char *kContourCustomId = "custom";

GradientDefinition resolved_gradient_definition(GradientDefinition definition,
                                                RgbColor foreground,
                                                RgbColor background) {
  for (auto &stop : definition.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground)
      stop.color = foreground;
    if (stop.kind == GradientColorStop::Kind::Background)
      stop.color = background;
    stop.kind = GradientColorStop::Kind::User;
  }
  return definition;
}

void show_gradient_preset_popup(
    QPushButton *anchor, GradientLibrary &library,
    std::function<void(const GradientLibraryEntry &)> use_gradient,
    std::function<void()> manage_gradients) {
  auto *popup = new QFrame(anchor, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(anchor->objectName() + QStringLiteral("Popup"));
  popup->setFrameShape(QFrame::StyledPanel);
  auto *layout = new QVBoxLayout(popup);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  auto *tree = new QTreeWidget(popup);
  tree->setObjectName(anchor->objectName() + QStringLiteral("Tree"));
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(true);
  tree->setIconSize(QSize(112, 28));
  QHash<QString, QTreeWidgetItem *> folders;
  for (const auto &entry : library.entries()) {
    QTreeWidgetItem *parent = nullptr;
    QString path;
    for (const auto &part :
         entry.folder.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      if (!path.isEmpty())
        path += QLatin1Char('/');
      path += part;
      auto *folder = folders.value(path, nullptr);
      if (folder == nullptr) {
        folder = parent != nullptr
                     ? new QTreeWidgetItem(parent,
                                           {gradient_folder_display_name(part)})
                     : new QTreeWidgetItem(
                           tree, {gradient_folder_display_name(part)});
        folder->setIcon(0, anchor->style()->standardIcon(QStyle::SP_DirIcon));
        folders.insert(path, folder);
      }
      parent = folder;
    }
    auto *item = parent != nullptr
                     ? new QTreeWidgetItem(
                           parent, {gradient_library_entry_display_name(entry)})
                     : new QTreeWidgetItem(
                           tree, {gradient_library_entry_display_name(entry)});
    item->setIcon(0, QIcon(entry.thumbnail));
    item->setData(0, Qt::UserRole, entry.storage_id);
  }
  tree->expandAll();
  layout->addWidget(tree, 1);
  auto *manage = new QPushButton(QObject::tr("Manage Gradients..."), popup);
  manage->setObjectName(anchor->objectName() + QStringLiteral("ManageButton"));
  layout->addWidget(manage);
  const auto select = [popup, &library, use_gradient](QTreeWidgetItem *item) {
    const auto id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) {
      item->setExpanded(!item->isExpanded());
      return;
    }
    if (const auto *entry = library.find_entry(id); entry != nullptr)
      use_gradient(*entry);
    popup->close();
  };
  QObject::connect(tree, &QTreeWidget::itemClicked, popup,
                   [select](QTreeWidgetItem *item, int) { select(item); });
  QObject::connect(tree, &QTreeWidget::itemActivated, popup,
                   [select](QTreeWidgetItem *item, int) { select(item); });
  QObject::connect(manage, &QPushButton::clicked, popup,
                   [popup, manage_gradients] {
                     popup->close();
                     manage_gradients();
                   });
  popup->resize(std::max(anchor->width(), 380), 440);
  // Clamped/flipped like the pattern and brush-tip popups; the old plain move
  // could run this popup off the bottom or right screen edge.
  position_popup_below(*anchor, *popup);
  popup->show();
  tree->setFocus(Qt::PopupFocusReason);
}

// QComboBox does not present hierarchical models as an expandable tree on all
// Qt styles. Keep its flat model for the selected-value display and existing
// combo-box behavior, but replace the drop-down with a folder-aware tree.
class PatternPickerCombo final : public QComboBox {
public:
  explicit PatternPickerCombo(QWidget *parent = nullptr) : QComboBox(parent) {}

  void clear_patterns() {
    entries_.clear();
    QComboBox::clear();
  }

  void add_pattern(const QIcon &icon, const QString &flat_label,
                   const QString &display_name, const QString &pattern_id,
                   const QString &persist_name, const QString &folder = {}) {
    QComboBox::addItem(icon, flat_label, pattern_id);
    setItemData(count() - 1, persist_name, kPatternPersistNameRole);
    entries_.push_back({display_name, folder});
  }

  void add_pattern(const QString &flat_label, const QString &display_name,
                   const QString &pattern_id, const QString &persist_name,
                   const QString &folder = {}) {
    add_pattern({}, flat_label, display_name, pattern_id, persist_name, folder);
  }

  void showPopup() override {
    if (!isEnabled() || entries_.empty()) {
      return;
    }
    hidePopup();

    auto *popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName(objectName() + QStringLiteral("Popup"));
    popup->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    auto *tree = new QTreeWidget(popup);
    tree->setObjectName(objectName() + QStringLiteral("Tree"));
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setIconSize(iconSize());
    tree->setUniformRowHeights(true);
    layout->addWidget(tree);

    QHash<QString, QTreeWidgetItem *> folders;
    const auto selected_folder =
        currentIndex() >= 0 &&
                currentIndex() < static_cast<int>(entries_.size())
            ? entries_[static_cast<std::size_t>(currentIndex())].folder
            : QString{};
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
      const auto &entry = entries_[static_cast<std::size_t>(index)];
      QTreeWidgetItem *parent_item = nullptr;
      if (!entry.folder.isEmpty()) {
        parent_item = folders.value(entry.folder, nullptr);
        if (parent_item == nullptr) {
          parent_item = new QTreeWidgetItem(tree, QStringList{entry.folder});
          parent_item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
          folders.insert(entry.folder, parent_item);
          if (!folder_expansion_.contains(entry.folder)) {
            folder_expansion_.insert(entry.folder,
                                     entry.folder == selected_folder);
          }
          parent_item->setExpanded(folder_expansion_.value(entry.folder));
        }
      }

      auto *item =
          parent_item != nullptr
              ? new QTreeWidgetItem(parent_item,
                                    QStringList{entry.display_name})
              : new QTreeWidgetItem(tree, QStringList{entry.display_name});
      item->setIcon(0, itemIcon(index));
      item->setData(0, kPatternPickerIndexRole, index);
      item->setData(0, kPatternPickerIsLeafRole, true);
      if (index == currentIndex()) {
        tree->setCurrentItem(item);
      }
    }

    QObject::connect(tree, &QTreeWidget::itemExpanded, popup,
                     [this](QTreeWidgetItem *item) {
                       if (item->childCount() > 0) {
                         folder_expansion_.insert(item->text(0), true);
                       }
                     });
    QObject::connect(tree, &QTreeWidget::itemCollapsed, popup,
                     [this](QTreeWidgetItem *item) {
                       if (item->childCount() > 0) {
                         folder_expansion_.insert(item->text(0), false);
                       }
                     });
    const auto select_pattern = [this, popup](QTreeWidgetItem *item) {
      const auto index = item->data(0, kPatternPickerIndexRole).toInt();
      setCurrentIndex(index);
      emit activated(index);
      popup->close();
    };
    QObject::connect(tree, &QTreeWidget::itemClicked, popup,
                     [select_pattern](QTreeWidgetItem *item, int) {
                       if (!item->data(0, kPatternPickerIsLeafRole).toBool()) {
                         item->setExpanded(!item->isExpanded());
                         return;
                       }
                       select_pattern(item);
                     });
    QObject::connect(tree, &QTreeWidget::itemActivated, popup,
                     [select_pattern](QTreeWidgetItem *item, int) {
                       if (item->data(0, kPatternPickerIsLeafRole).toBool()) {
                         select_pattern(item);
                       }
                     });

    popup_ = popup;
    auto visible_rows = tree->topLevelItemCount();
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      const auto *item = tree->topLevelItem(index);
      if (item->isExpanded()) {
        visible_rows += item->childCount();
      }
    }
    const auto popup_width = std::max(width(), 320);
    // A short folder must not collapse the picker into a cramped two-row menu.
    // Keep enough vertical room to browse and compare thumbnails; longer
    // libraries remain bounded and scroll inside the tree.
    const auto popup_height = std::clamp(visible_rows * 30 + 4, 280, 420);
    popup->resize(popup_width, popup_height);

    position_popup_below(*this, *popup);
    popup->show();
    tree->setFocus(Qt::PopupFocusReason);
    if (tree->currentItem() != nullptr) {
      tree->scrollToItem(tree->currentItem());
    }
  }

  void hidePopup() override {
    if (popup_ != nullptr) {
      popup_->close();
      popup_.clear();
    }
  }

private:
  struct Entry {
    QString display_name;
    QString folder;
  };

  std::vector<Entry> entries_;
  QHash<QString, bool> folder_expansion_;
  QPointer<QFrame> popup_;
};

enum class LayerStyleCategoryPage {
  Blending = 0,
  BevelEmboss,
  Stroke,
  InnerShadow,
  InnerGlow,
  Satin,
  ColorOverlay,
  GradientOverlay,
  OuterGlow,
  DropShadow,
  PatternOverlay,
  BevelContour,
  BevelTexture,
  // Appended: enum values are QStackedWidget indices, so the Styles page is
  // created last even though its category row is listed first.
  Styles
};

// The Styles row shares LayerStyleEffectKind::None with Blending Options; this
// distinct effect-index marker lets rebuild_category_list re-select it.
constexpr int kStylesCategoryIndex = -2;

enum class LayerStyleEffectKind {
  None = 0,
  BevelEmboss,
  Stroke,
  InnerShadow,
  InnerGlow,
  Satin,
  ColorOverlay,
  GradientOverlay,
  OuterGlow,
  DropShadow,
  PatternOverlay,
  BevelContour,
  BevelTexture
};

constexpr int kLayerStylePageRole = Qt::UserRole + 1;
constexpr int kLayerStyleEffectKindRole = Qt::UserRole + 2;
constexpr int kLayerStyleEffectIndexRole = Qt::UserRole + 3;

// Grows the vector with defaults until `index` exists (the former per-effect
// ensure_* lambda family, shared by the enabled-state and save paths).
template <typename Effect>
Effect& ensure_effect(std::vector<Effect>& effects, int index, Effect (*make_default)()) {
  while (index >= 0 && effects.size() <= static_cast<std::size_t>(index)) {
    effects.push_back(make_default());
  }
  return effects[static_cast<std::size_t>(std::max(0, index))];
}

// Uniform access to the stackable per-effect vectors (Stroke, the shadows and
// glows, Satin, the overlays), keyed by LayerStyleEffectKind: count drives the
// remove-instance button, add_instance backs "+", remove_instance backs "-".
// Bevel & Emboss and its sub-options are not stackable and have no entry.
struct LayerStyleEffectVectorOps {
  std::size_t (*count)(const LayerStyle& source);
  // Inserts a copy of source_index (or a default when empty), enabled, after
  // the source row; returns the inserted index.
  int (*add_instance)(LayerStyle& target, int source_index);
  // Erases index when present; returns the index to select next.
  int (*remove_instance)(LayerStyle& target, int index);
};

template <auto Member, auto MakeDefault>
constexpr LayerStyleEffectVectorOps effect_vector_ops() {
  return {
      [](const LayerStyle& source) { return (source.*Member).size(); },
      [](LayerStyle& target, int source_index) {
        auto& vector = target.*Member;
        auto value = MakeDefault();
        if (!vector.empty()) {
          value = vector[static_cast<std::size_t>(
              std::clamp(source_index, 0, static_cast<int>(vector.size()) - 1))];
        }
        value.enabled = true;
        const auto insert_index =
            vector.empty() ? 0 : std::clamp(source_index + 1, 0, static_cast<int>(vector.size()));
        vector.insert(vector.begin() + insert_index, std::move(value));
        return insert_index;
      },
      [](LayerStyle& target, int index) {
        auto& vector = target.*Member;
        if (index >= 0 && vector.size() > static_cast<std::size_t>(index)) {
          vector.erase(vector.begin() + index);
        }
        return vector.empty() ? 0 : std::min(index, static_cast<int>(vector.size()) - 1);
      },
  };
}

const LayerStyleEffectVectorOps* effect_vector_ops_for_kind(LayerStyleEffectKind kind) {
  switch (kind) {
    case LayerStyleEffectKind::Stroke: {
      static constexpr auto ops = effect_vector_ops<&LayerStyle::strokes, default_stroke>();
      return &ops;
    }
    case LayerStyleEffectKind::InnerShadow: {
      static constexpr auto ops =
          effect_vector_ops<&LayerStyle::inner_shadows, default_inner_shadow>();
      return &ops;
    }
    case LayerStyleEffectKind::InnerGlow: {
      static constexpr auto ops = effect_vector_ops<&LayerStyle::inner_glows, default_inner_glow>();
      return &ops;
    }
    case LayerStyleEffectKind::Satin: {
      static constexpr auto ops = effect_vector_ops<&LayerStyle::satins, default_satin>();
      return &ops;
    }
    case LayerStyleEffectKind::ColorOverlay: {
      static constexpr auto ops =
          effect_vector_ops<&LayerStyle::color_overlays, default_color_overlay>();
      return &ops;
    }
    case LayerStyleEffectKind::GradientOverlay: {
      static constexpr auto ops =
          effect_vector_ops<&LayerStyle::gradient_fills, default_gradient_fill>();
      return &ops;
    }
    case LayerStyleEffectKind::PatternOverlay: {
      static constexpr auto ops =
          effect_vector_ops<&LayerStyle::pattern_overlays, default_pattern_overlay>();
      return &ops;
    }
    case LayerStyleEffectKind::OuterGlow: {
      static constexpr auto ops = effect_vector_ops<&LayerStyle::outer_glows, default_outer_glow>();
      return &ops;
    }
    case LayerStyleEffectKind::DropShadow: {
      static constexpr auto ops =
          effect_vector_ops<&LayerStyle::drop_shadows, default_drop_shadow>();
      return &ops;
    }
    case LayerStyleEffectKind::BevelEmboss:
    case LayerStyleEffectKind::BevelContour:
    case LayerStyleEffectKind::BevelTexture:
    case LayerStyleEffectKind::None:
      break;
  }
  return nullptr;
}

// Shared host state for the callback-driven gradient-stop editor used by both
// Gradient Overlay and gradient Strokes. The editor widget intentionally never
// owns or mutates its stop vectors; these working vectors remain stable during
// a drag, and value() sorts only the copies written back to LayerStyle.
struct LayerStyleGradientControls {
  GradientStopsEditorWidget* editor{nullptr};
  QSpinBox* stop_location{nullptr};
  QLabel* stop_color_label{nullptr};
  QLineEdit* stop_hex{nullptr};
  QPushButton* stop_swatch{nullptr};
  QLabel* stop_opacity_label{nullptr};
  QSpinBox* stop_opacity{nullptr};
  QLabel* stop_midpoint_label{nullptr};
  QSpinBox* stop_midpoint{nullptr};
  QPushButton* add_stop{nullptr};
  QPushButton* remove_stop{nullptr};
  QComboBox *form_combo{nullptr};
  QSpinBox *smoothness{nullptr};
  QWidget *solid_controls{nullptr};
  QWidget *selected_stop_controls{nullptr};
  QWidget *midpoint_controls{nullptr};
  QWidget *stop_button_controls{nullptr};
  QWidget *noise_controls{nullptr};
  QSpinBox *noise_seed{nullptr};
  QSpinBox *noise_roughness{nullptr};
  QCheckBox*noise_transparency{nullptr};
  QCheckBox *noise_restrict{nullptr};
  QComboBox *noise_color_model{nullptr};
  std::array<QSpinBox *, 4> noise_minimum{};
  std::array<QSpinBox *, 4> noise_maximum{};
  QCheckBox * reverse{nullptr};
  QCheckBox *dither{nullptr};
  QCheckBox *align{nullptr};
  QComboBox*interpolation{nullptr};
  QComboBox * style_combo{nullptr};
  QSpinBox* angle{nullptr};
  QSpinBox* scale{nullptr};
  QSpinBox *offset_x{nullptr};
  QSpinBox *offset_y{nullptr};
  QPushButton *reset_alignment{nullptr};
  LayerStyleGradient template_gradient;
  std::vector<GradientColorStop> color_stops;
  std::vector<GradientAlphaStop> alpha_stops;
  int selected_color_stop{0};
  int selected_alpha_stop{-1};
  bool loading{false};
  std::function<void()> update_previews;
  std::function<void(bool)> changed;
  std::function<void(const LayerStyleGradient&)> load;

  [[nodiscard]] LayerStyleGradient value() const {
    LayerStyleGradient result = template_gradient;
    result.form =
        static_cast<GradientDefinitionForm>(form_combo->currentData().toInt());
    result.smoothness =
        static_cast<std::uint16_t>(smoothness->value() * 4096 / 100);
    result.noise.seed = static_cast<std::uint32_t>(noise_seed->value());
    result.noise.roughness =
        static_cast<std::uint16_t>(noise_roughness->value() * 4096 / 100);
    result.noise.add_transparency = noise_transparency->isChecked();
    result.noise.restrict_colors = noise_restrict->isChecked();
    result.noise.color_model = static_cast<GradientNoiseColorModel>(
        noise_color_model->currentData().toInt());
    for (std::size_t channel = 0; channel < result.noise.minimum.size();
         ++channel) {
      result.noise.minimum[channel] =
          static_cast<std::uint16_t>(noise_minimum[channel]->value());
      result.noise.maximum[channel] =
          static_cast<std::uint16_t>(noise_maximum[channel]->value());
    }
    result.type = static_cast<LayerStyleGradientType>(style_combo->currentData().toInt());
    result.reverse = reverse->isChecked();
    result.dither = dither->isChecked();
    result.align_with_layer = align->isChecked();
    result.interpolation = static_cast<GradientInterpolationMethod>(
        interpolation->currentData().toInt());
    result.angle_degrees = static_cast<float>(angle->value());
    result.scale = static_cast<float>(scale->value()) / 100.0F;
    result.offset_x_percent = static_cast<float>(offset_x->value());
    result.offset_y_percent = static_cast<float>(offset_y->value());
    result.color_stops = color_stops;
    if (result.color_stops.empty()) {
      result.color_stops = default_layer_style_gradient().color_stops;
    }
    std::stable_sort(result.color_stops.begin(), result.color_stops.end(),
                     [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    result.alpha_stops = alpha_stops;
    if (result.alpha_stops.empty()) {
      result.alpha_stops = default_layer_style_gradient().alpha_stops;
    }
    std::stable_sort(result.alpha_stops.begin(), result.alpha_stops.end(),
                     [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    return result;
  }
};

QListWidgetItem* add_layer_style_category(QListWidget* list, const QString& text, bool checkable, bool checked,
                                           LayerStyleCategoryPage page,
                                           LayerStyleEffectKind kind = LayerStyleEffectKind::None,
                                           int effect_index = -1) {
  auto* item = new QListWidgetItem(text, list);
  item->setData(kLayerStylePageRole, static_cast<int>(page));
  item->setData(kLayerStyleEffectKindRole, static_cast<int>(kind));
  item->setData(kLayerStyleEffectIndexRole, effect_index);
  if (checkable) {
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  return item;
}

void update_color_preview_label(QWidget* widget, int red, int green, int blue) {
  widget->setStyleSheet(QStringLiteral("%1 { background: rgb(%2, %3, %4); border: 1px solid #9aa4b2; }")
                           .arg(widget->metaObject()->className())
                           .arg(red)
                           .arg(green)
                           .arg(blue));
}

// One effect's R/G/B slider rows plus its color-preview swatch and the control
// that opens the picker (the "Color RGB" block formerly duplicated per effect
// page; picker == preview except Color Overlay's separate button + label).
struct RgbColorRowWidgets {
  QSpinBox* red{nullptr};
  QSpinBox* green{nullptr};
  QSpinBox* blue{nullptr};
  QWidget* preview{nullptr};
  QAbstractButton* picker{nullptr};
  QWidget* row_container{nullptr};

  [[nodiscard]] RgbColor value() const {
    return RgbColor{static_cast<std::uint8_t>(red->value()), static_cast<std::uint8_t>(green->value()),
                    static_cast<std::uint8_t>(blue->value())};
  }
  void set_value(RgbColor color) const {
    red->setValue(color.red);
    green->setValue(color.green);
    blue->setValue(color.blue);
  }
  void update_preview() const {
    update_color_preview_label(preview, red->value(), green->value(), blue->value());
  }
};

}  // namespace

std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer, std::function<void(const LayerStyleSettings&)> preview_changed,
    PatternStore* document_patterns, PatternLibrary* pattern_library, StyleLibrary* style_library,
    std::function<void(const QString& name, const PixelBuffer& tile)> open_pattern_as_image,
    GradientLibrary *gradient_library, RgbColor foreground,
    RgbColor background) {
  const LayerStyleSettings original_settings{
      static_cast<int>(std::round(layer.opacity() * 100.0F)), layer.blend_mode(), layer.layer_style(),
      layer.blend_if(), false};
  auto style = layer.layer_style();
  auto blend_if = layer.blend_if();
  const auto blend_if_payload_status = layer.blend_if_payload_status();
  bool replace_unsupported_blend_if = false;
  auto shadow = style.drop_shadows.empty() ? default_drop_shadow() : style.drop_shadows.front();
  auto inner_shadow = style.inner_shadows.empty() ? default_inner_shadow() : style.inner_shadows.front();
  auto outer_glow = style.outer_glows.empty() ? default_outer_glow() : style.outer_glows.front();
  auto inner_glow = style.inner_glows.empty() ? default_inner_glow() : style.inner_glows.front();
  auto color_overlay = style.color_overlays.empty() ? default_color_overlay() : style.color_overlays.front();
  auto gradient = style.gradient_fills.empty() ? default_gradient_fill() : style.gradient_fills.front();
  auto stroke = style.strokes.empty() ? default_stroke() : style.strokes.front();
  auto bevel = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
  auto satin = style.satins.empty() ? default_satin() : style.satins.front();
  auto pattern_overlay =
      style.pattern_overlays.empty() ? default_pattern_overlay() : style.pattern_overlays.front();
  // Manager selections whose Photoshop id collides with different embedded
  // pixels receive a document-local id. Keep those resources alive across
  // Preview-off callbacks, where MainWindow restores the original store.
  std::vector<PatternResource> transient_manager_patterns;
  if (style.pattern_overlays.empty() && pattern_library != nullptr &&
      pattern_library->find_entry_by_pattern_id(QString::fromStdString(pattern_overlay.pattern_id)) == nullptr) {
    if (!pattern_library->entries().empty()) {
      const auto& first = pattern_library->entries().front();
      pattern_overlay.pattern_id = first.id.toStdString();
      pattern_overlay.pattern_name = first.name.toStdString();
    } else if (document_patterns != nullptr && !document_patterns->patterns.empty()) {
      pattern_overlay.pattern_id = document_patterns->patterns.front().id;
      pattern_overlay.pattern_name = document_patterns->patterns.front().name;
    } else {
      pattern_overlay.pattern_id.clear();
      pattern_overlay.pattern_name.clear();
    }
  }
  // Imported non-preset curves survive picker round trips: each picker keeps
  // the original custom curve and only a deliberate preset pick replaces it.
  StyleContour custom_gloss_contour = bevel.gloss_contour;
  StyleContour custom_bevel_sub_contour = bevel.contour.contour;
  if (gradient.gradient.color_stops.empty()) {
    gradient.gradient = default_layer_style_gradient();
  }
  if (stroke.uses_gradient && stroke.gradient.color_stops.empty()) {
    stroke.gradient = default_layer_style_gradient();
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyLayerStyleDialog"));
  dialog.resize(840, 680);
  auto* root = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Layer Style"),
                                          DialogChromeCloseMode::Accept);

  auto* name_row = new QHBoxLayout();
  name_row->addWidget(new QLabel(QObject::tr("Name:"), &dialog));
  auto* name = new QLineEdit(QString::fromStdString(layer.name()), &dialog);
  name->setObjectName(QStringLiteral("layerStyleLayerNameEdit"));
  name->setReadOnly(true);
  name_row->addWidget(name, 1);
  root->addLayout(name_row);

  // A pattern reference resolves if its tile is embedded in the document or is
  // one of Patchy's built-in presets (materialized into the document on apply).
  const auto pattern_reference_resolves = [document_patterns, pattern_library](const std::string& id) {
    if (id.empty()) {
      return false;
    }
    if (document_patterns != nullptr && document_patterns->find(id) != nullptr) {
      return true;
    }
    if (pattern_library != nullptr &&
        pattern_library->find_entry_by_pattern_id(QString::fromStdString(id)) != nullptr) {
      return true;
    }
    return is_bundled_pattern_id(id);
  };
  std::vector<std::string> missing_pattern_names;
  const auto note_missing_pattern = [&](const std::string& id, const std::string& effect_name, bool enabled) {
    if (!enabled || id.empty() || pattern_reference_resolves(id)) {
      return;
    }
    auto label = effect_name.empty() ? id : effect_name;
    if (std::find(missing_pattern_names.begin(), missing_pattern_names.end(), label) ==
        missing_pattern_names.end()) {
      missing_pattern_names.push_back(std::move(label));
    }
  };
  for (const auto& effect : style.pattern_overlays) {
    note_missing_pattern(effect.pattern_id, effect.pattern_name, effect.enabled);
  }
  // Bevel textures with unresolvable patterns are presented unchecked (the
  // Photoshop semantics), so they raise no banner here; re-checking the row
  // surfaces the live missing-pattern warning below.
  QLabel* missing_pattern_warning = nullptr;
  if (!missing_pattern_names.empty()) {
    QStringList names;
    for (const auto& missing : missing_pattern_names) {
      names << QString::fromStdString(missing);
    }
    auto* warning = new QLabel(&dialog);
    missing_pattern_warning = warning;
    warning->setObjectName(QStringLiteral("layerStyleMissingPatternWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setText(
        QObject::tr("Pattern \"%1\" is not embedded in this document, so the "
                    "effect that references it cannot "
                    "render until you choose another pattern.")
            .arg(names.join(QStringLiteral("\", \""))));
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleMissingPatternWarning { background: #4a3a1f; border: "
        "1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }
  const bool has_unsupported_satin_contour =
      std::any_of(style.satins.begin(), style.satins.end(), [](const LayerSatin& effect) {
        return effect.unsupported_contour_options;
      });
  if (has_unsupported_satin_contour) {
    auto* warning = new QLabel(
        QObject::tr("Photoshop Satin custom contours and contour anti-aliasing "
                    "are preserved until you edit layer "
                    "styles. Patchy previews and saves edited Satin with the "
                    "non-anti-aliased Linear contour."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleSatinContourWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleSatinContourWarning { background: #4a3a1f; border: "
        "1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }
  if (layer.kind() == LayerKind::Group) {
    auto* warning = new QLabel(
        QObject::tr("Layer effects on groups are preserved for PSD "
                               "round-trip but are not rendered yet. Satin "
                               "controls remain editable, but Preview cannot "
                               "show the group result."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleGroupEffectsWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleGroupEffectsWarning { background: #4a3a1f; border: "
        "1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }

  QLabel* blend_if_unsupported_warning = nullptr;
  QPushButton* replace_blend_if_button = nullptr;
  if (blend_if_payload_status == BlendIfPayloadStatus::Unsupported) {
    auto* warning_row = new QWidget(&dialog);
    warning_row->setObjectName(QStringLiteral("layerStyleBlendIfUnsupportedWarningRow"));
    auto* warning_layout = new QHBoxLayout(warning_row);
    warning_layout->setContentsMargins(8, 6, 8, 6);
    warning_layout->setSpacing(8);
    blend_if_unsupported_warning = new QLabel(
        QObject::tr("This layer contains Photoshop Blend If data for an "
                    "unsupported color mode or payload shape. "
                    "Patchy preserves it unchanged and does not preview it "
                    "unless you replace it."),
        warning_row);
    blend_if_unsupported_warning->setObjectName(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    blend_if_unsupported_warning->setWordWrap(true);
    blend_if_unsupported_warning->setProperty("warningBanner", true);
    warning_layout->addWidget(blend_if_unsupported_warning, 1);
    replace_blend_if_button = new QPushButton(QObject::tr("Replace with Editable Defaults"), warning_row);
    replace_blend_if_button->setObjectName(QStringLiteral("layerStyleBlendIfReplaceButton"));
    warning_layout->addWidget(replace_blend_if_button, 0, Qt::AlignVCenter);
    warning_row->setStyleSheet(QStringLiteral("QWidget#layerStyleBlendIfUnsupportedWarningRow { "
                       "background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; }"));
    root->addWidget(warning_row);
  }

  const auto boundary_blend_if = decode_layer_blend_if(layer.raw_psd_group_boundary_blending_ranges());
  if (!layer.raw_psd_group_boundary_blending_ranges().empty() &&
      (boundary_blend_if.status == BlendIfPayloadStatus::Unsupported ||
       !blend_if_is_identity(boundary_blend_if.settings))) {
    auto* warning = new QLabel(
        QObject::tr("This folder's closing PSD record contains "
                               "separate Blend If data. Patchy preserves that "
                               "boundary data unchanged; the controls below "
                               "edit only the visible folder record."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleBlendIfBoundaryWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleBlendIfBoundaryWarning { background: #4a3a1f; "
        "border: 1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }

  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  auto* categories = new QListWidget(&dialog);
  categories->setObjectName(QStringLiteral("layerStyleCategoryList"));
  categories->setMinimumWidth(210);
  categories->setMaximumWidth(230);
  auto* category_pane = new QWidget(&dialog);
  auto* category_layout = new QVBoxLayout(category_pane);
  category_layout->setContentsMargins(0, 0, 0, 0);
  category_layout->setSpacing(6);
  category_layout->addWidget(categories, 1);
  auto* category_actions = new QHBoxLayout();
  category_actions->setContentsMargins(0, 0, 0, 0);
  category_actions->setSpacing(4);
  category_actions->addStretch(1);
  auto* remove_selected_instance = new QPushButton(category_pane);
  remove_selected_instance->setObjectName(QStringLiteral("layerStyleRemoveSelectedInstanceButton"));
  remove_selected_instance->setIcon(dialog.style()->standardIcon(QStyle::SP_TrashIcon));
  remove_selected_instance->setFixedSize(26, 24);
  remove_selected_instance->setToolTip(QObject::tr("Remove Selected Instance"));
  category_actions->addWidget(remove_selected_instance);
  category_layout->addLayout(category_actions);
  body->addWidget(category_pane);

  auto* controls = new QStackedWidget(&dialog);
  controls->setObjectName(QStringLiteral("layerStyleOptionsStack"));
  body->addWidget(controls, 1);
  auto make_page = [controls](const QString& object_name) {
    auto* page = new QWidget(controls);
    page->setObjectName(object_name);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    controls->addWidget(page);
    return layout;
  };
  auto slider_object_name = [](QString spin_object_name) {
    if (spin_object_name.endsWith(QStringLiteral("Spin"))) {
      spin_object_name.chop(4);
    }
    return spin_object_name + QStringLiteral("Slider");
  };
  auto add_slider_spin_row = [&slider_object_name](QFormLayout* form, QWidget* parent, const QString& label,
                                                   const QString& spin_object_name, int minimum, int maximum,
                                                   int value, const QString& suffix = {}, int spin_width = 72) {
    return add_dialog_slider_spin_row(form, parent, label, slider_object_name(spin_object_name),
                                      spin_object_name, minimum, maximum, value, suffix, spin_width,
                                      /*row_spacing=*/8);
  };
  auto add_color_slider_row = [&slider_object_name](QVBoxLayout* layout, QWidget* parent, const QString& label,
                                                    const QString& spin_object_name, std::uint8_t value) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* channel_label = new QLabel(label, row);
    channel_label->setFixedWidth(18);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(slider_object_name(spin_object_name));
    slider->setRange(0, 255);
    slider->setValue(value);
    auto* spin = new QSpinBox(row);
    spin->setObjectName(spin_object_name);
    spin->setRange(0, 255);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 54);
    row_layout->addWidget(channel_label);
    row_layout->addWidget(slider, 1);
    row_layout->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    layout->addWidget(row);
    return spin;
  };
  // The "Color RGB" form row: R/G/B slider rows plus a clickable preview swatch
  // button below them. Widgets follow the shared naming scheme
  // (<prefix>RedSpin/<prefix>GreenSpin/<prefix>BlueSpin/<prefix>ColorPreview).
  auto make_color_rows = [&add_color_slider_row](QFormLayout* form, QWidget* group,
                                                 const QString& object_prefix, RgbColor initial) {
    RgbColorRowWidgets rows;
    auto* color_row = new QWidget(group);
    auto* color_layout = new QVBoxLayout(color_row);
    color_layout->setContentsMargins(0, 0, 0, 0);
    color_layout->setSpacing(4);
    rows.row_container = color_row;
    rows.red = add_color_slider_row(color_layout, color_row, QObject::tr("R"),
                                    object_prefix + QStringLiteral("RedSpin"), initial.red);
    rows.green = add_color_slider_row(color_layout, color_row, QObject::tr("G"),
                                      object_prefix + QStringLiteral("GreenSpin"), initial.green);
    rows.blue = add_color_slider_row(color_layout, color_row, QObject::tr("B"),
                                     object_prefix + QStringLiteral("BlueSpin"), initial.blue);
    auto* preview_row = new QWidget(color_row);
    auto* preview_layout = new QHBoxLayout(preview_row);
    preview_layout->setContentsMargins(26, 0, 0, 0);
    preview_layout->setSpacing(8);
    auto* preview = new QPushButton(group);
    preview->setObjectName(object_prefix + QStringLiteral("ColorPreview"));
    preview->setFixedSize(28, 22);
    preview->setToolTip(QObject::tr("Choose Color..."));
    preview_layout->addWidget(preview);
    preview_layout->addStretch(1);
    color_layout->addWidget(preview_row);
    rows.preview = preview;
    rows.picker = preview;
    rows.update_preview();
    form->addRow(QObject::tr("Color RGB"), color_row);
    return rows;
  };
  auto make_gradient_controls = [&](QFormLayout* form, QWidget* parent_widget, const QString& object_prefix,
                                    const LayerStyleGradient& initial_gradient,
                                    const QString& color_picker_title, bool offer_shape_burst = false) {
    auto controls_state = std::make_unique<LayerStyleGradientControls>();
    auto* state = controls_state.get();

    auto *preset_button = new QPushButton(QObject::tr("Preset..."),parent_widget);
    preset_button->setObjectName(object_prefix + QStringLiteral("PresetButton"));
    preset_button->setEnabled(gradient_library != nullptr);
    form->addRow(QObject::tr("Preset"), preset_button);
    if (gradient_library != nullptr) {
      const auto apply_preset = [state, foreground, background](
                                    const GradientLibraryEntry &entry) {
        auto value = state->value();
        static_cast<GradientDefinition &>(value) = resolved_gradient_definition(
            entry.definition, foreground, background);
        state->load(value);
        if (state->changed)
          state->changed(true);
      };QObject::connect(preset_button, &QPushButton::clicked, &dialog,
                       [&, state, preset_button, apply_preset] {
                         show_gradient_preset_popup(
                             preset_button, *gradient_library, apply_preset,
                             [&, state, apply_preset] {
                               const auto selected = request_gradient_manager(
                                   &dialog, *gradient_library, {},
                                   state->value());
                               if (const auto *entry =
                                       gradient_library->find_entry(selected);
                                   entry != nullptr) {
                                 apply_preset(*entry);
                               }
                             });
                       });
    }

    state->form_combo = new QComboBox(parent_widget);
    state->form_combo->setObjectName(object_prefix + QStringLiteral("FormCombo"));
    state->form_combo->addItem(QObject::tr("Solid"),
                               static_cast<int>(GradientDefinitionForm::Solid));
    state->form_combo->addItem(QObject::tr("Noise"),
                               static_cast<int>(GradientDefinitionForm::Noise));
    form->addRow(QObject::tr("Gradient Type"), state->form_combo);

    state->solid_controls = new QWidget(parent_widget);
    auto *solid_layout = new QVBoxLayout(state->solid_controls);
    solid_layout->setContentsMargins(0, 0, 0, 0);
    solid_layout->setSpacing(6);
    state->editor = new GradientStopsEditorWidget(state->solid_controls);
    state->editor->setObjectName(object_prefix + QStringLiteral("StopsEditor"));
    state->editor->set_opacity_track_enabled(true);
    solid_layout->addWidget(state->editor);
    form->addRow(QObject::tr("Gradient"), state->solid_controls);

    auto *selected_row = new QWidget(parent_widget);
    state->selected_stop_controls = selected_row;selected_row->setObjectName(object_prefix + QStringLiteral("SelectedStopRow"));
    auto *selected_layout = new QHBoxLayout(selected_row);
    selected_layout->setContentsMargins(0, 0, 0, 0);
    selected_layout->setSpacing(8);
    auto *location_label = new QLabel(QObject::tr("Location"), selected_row);
    state->stop_location = new QSpinBox(selected_row);
    state->stop_location->setObjectName(object_prefix + QStringLiteral("StopLocationSpin"));
    state->stop_location->setRange(0, 100);
    state->stop_location->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_location, 64);
    state->stop_color_label = new QLabel(QObject::tr("Color"), selected_row);
    state->stop_hex = new QLineEdit(selected_row);
    state->stop_hex->setObjectName(object_prefix + QStringLiteral("StopHexEdit"));state->stop_hex->setFixedWidth(72);
    state->stop_hex->setMaxLength(7);state->stop_swatch = new QPushButton( selected_row);
    state->stop_swatch->setObjectName(object_prefix + QStringLiteral("StopSwatchButton"));
    state->stop_swatch->setFixedSize(34, 24);
    state->stop_swatch->setToolTip(QObject::tr("Choose Color..."));
    state->stop_opacity_label = new QLabel(QObject::tr("Opacity"), selected_row);
    state->stop_opacity = new QSpinBox(selected_row);
    state->stop_opacity->setObjectName(object_prefix + QStringLiteral("StopOpacitySpin"));
    state->stop_opacity->setRange(0, 100);
    state->stop_opacity->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_opacity, 64);
    selected_layout->addWidget(location_label);
    selected_layout->addWidget(state->stop_location);
    selected_layout->addWidget(state->stop_color_label);
    selected_layout->addWidget(state->stop_hex);
    selected_layout->addWidget(state->stop_swatch);
    selected_layout->addWidget(state->stop_opacity_label);
    selected_layout->addWidget(state->stop_opacity);
    selected_layout->addStretch(1);
    form->addRow(QString(), selected_row);

    auto *midpoint_row = new QWidget(parent_widget);
    state->midpoint_controls = midpoint_row;
    auto *midpoint_layout = new QHBoxLayout(midpoint_row);
    midpoint_layout->setContentsMargins(0, 0, 0, 0);
    midpoint_layout->setSpacing(8);
    state->stop_midpoint_label =
        new QLabel(QObject::tr("Midpoint"), midpoint_row);
    state->stop_midpoint = new QSpinBox(midpoint_row);
    state->stop_midpoint->setObjectName(object_prefix + QStringLiteral("StopMidpointSpin"));
    state->stop_midpoint->setRange(5, 95);
    state->stop_midpoint->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_midpoint, 64);
    midpoint_layout->addWidget(state->stop_midpoint_label);
    midpoint_layout->addWidget(state->stop_midpoint);
    midpoint_layout->addStretch(1);
    form->addRow(QString(), midpoint_row);

    auto *stop_buttons = new QWidget(parent_widget);
    state->stop_button_controls = stop_buttons;
    auto *
    stop_button_layout = new QHBoxLayout(stop_buttons);
    stop_button_layout->setContentsMargins(0, 0, 0, 0);
    stop_button_layout->setSpacing(6);state->add_stop = new QPushButton(QObject::tr("Add Stop"), stop_buttons);

    state->add_stop->setObjectName(object_prefix + QStringLiteral("AddStopButton"));
    state->remove_stop = new QPushButton(QObject::tr("Remove Stop"), stop_buttons);
    state->remove_stop->setObjectName(object_prefix + QStringLiteral("RemoveStopButton"));
    stop_button_layout->addWidget(state->add_stop);
    stop_button_layout->addWidget(state->remove_stop);
    stop_button_layout->addStretch(1);
    form->addRow(QString(), stop_buttons);

    state->smoothness = add_slider_spin_row(
        form, parent_widget,QObject::tr("Smoothness"),
                                       object_prefix + QStringLiteral("SmoothnessSpin"), 0, 100,
                                       static_cast<int>(std::lround(initial_gradient.smoothness * 100.0 / 4096.0)),
        QStringLiteral("%"));

    state->noise_controls = new QWidget( parent_widget);
    auto *noise_form = new QFormLayout(state->noise_controls);
    noise_form->setContentsMargins(0, 0, 0, 0);
    state->noise_seed = new QSpinBox(state->noise_controls);
    state->noise_seed->setObjectName(
                                       object_prefix + QStringLiteral("NoiseSeedSpin"));
    state->noise_seed->setRange(0, std::numeric_limits<int>::max());
    configure_dialog_spinbox(state->noise_seed);
    noise_form->addRow(QObject::tr("Seed"), state->noise_seed);
    state->noise_roughness = new QSpinBox(state->noise_controls);
    state->noise_roughness->setObjectName(object_prefix +
                                          QStringLiteral("NoiseRoughnessSpin"));
    state->noise_roughness->setRange(0, 100);
    state->noise_roughness->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->noise_roughness);
    noise_form->addRow(QObject::tr("Roughness"), state->noise_roughness);
    state->noise_color_model = new QComboBox(state->noise_controls);
    state->noise_color_model->setObjectName(
        object_prefix + QStringLiteral("NoiseColorModelCombo"));
    state->noise_color_model->addItem(
        QStringLiteral("RGB"), static_cast<int>(GradientNoiseColorModel::RGB));
    state->noise_color_model->addItem(
        QStringLiteral("HSB"), static_cast<int>(GradientNoiseColorModel::HSB));
    state->noise_color_model->addItem(
        QStringLiteral("Lab"), static_cast<int>(GradientNoiseColorModel::Lab));
    noise_form->addRow(QObject::tr("Color Model"), state->noise_color_model);
    state->noise_transparency =
        new QCheckBox(QObject::tr("Add Transparency"), state->noise_controls);
    state->noise_transparency->setObjectName(
        object_prefix + QStringLiteral("NoiseTransparencyCheck"));
    noise_form->addRow(QString(), state->noise_transparency);
    state->noise_restrict =
        new QCheckBox(QObject::tr("Restrict Colors"), state->noise_controls);
    state->noise_restrict->setObjectName(
        object_prefix + QStringLiteral("NoiseRestrictColorsCheck"));
    noise_form->addRow(QString(), state->noise_restrict);
    for (std::size_t channel = 0; channel < state->noise_minimum.size();
         ++channel) {
      auto *range_row = new QWidget(state->noise_controls);
      auto *range_layout = new QHBoxLayout(range_row);
      range_layout->setContentsMargins(0, 0, 0, 0);
      range_layout->setSpacing(6);
      state->noise_minimum[channel] = new QSpinBox(range_row);
      state->noise_minimum[channel]->setObjectName(
          object_prefix +
          QStringLiteral("NoiseChannel%1MinimumSpin").arg(channel + 1U));
      state->noise_minimum[channel]->setRange(0, 100);
      state->noise_minimum[channel]->setSuffix(QStringLiteral("%"));
      configure_dialog_spinbox(state->noise_minimum[channel], 64);
      state->noise_maximum[channel] = new QSpinBox(range_row);
      state->noise_maximum[channel]->setObjectName(
          object_prefix +
          QStringLiteral("NoiseChannel%1MaximumSpin").arg(channel + 1U));
      state->noise_maximum[channel]->setRange(0, 100);
      state->noise_maximum[channel]->setSuffix(QStringLiteral("%"));
      configure_dialog_spinbox(state->noise_maximum[channel], 64);
      range_layout->addWidget(state->noise_minimum[channel]);
      range_layout->addWidget(new QLabel(QObject::tr("to"), range_row));
      range_layout->addWidget(state->noise_maximum[channel]);
      range_layout->addStretch(1);
      noise_form->addRow(QObject::tr("Channel %1 Range").arg(channel + 1U),
                         range_row);
    }
    form->addRow(QString(), state->noise_controls);

    state->reverse = new QCheckBox(QObject::tr("Reverse"), parent_widget);
    state->reverse->setObjectName(object_prefix +
                                  QStringLiteral("ReverseCheck"));
    form->addRow(QString(), state->reverse);
    state->dither = new QCheckBox(QObject::tr("Dither"), parent_widget);
    state->dither->setObjectName(object_prefix + QStringLiteral("DitherCheck"));
    form->addRow(QString(), state->dither);
    state->align =
        new QCheckBox(QObject::tr("Align with Layer"), parent_widget);
    state->align->setObjectName(object_prefix + QStringLiteral("AlignCheck"));
    form->addRow(QString(), state->align);
    state->interpolation = new QComboBox(parent_widget);
    state->interpolation->setObjectName(object_prefix +
                                        QStringLiteral("InterpolationCombo"));
    state->interpolation->addItem(
        QObject::tr("Classic"),
        static_cast<int>(GradientInterpolationMethod::Classic));
    state->interpolation->addItem(
        QObject::tr("Perceptual"),
        static_cast<int>(GradientInterpolationMethod::Perceptual));
    state->interpolation->addItem(
        QObject::tr("Linear", "gradient interpolation"),
        static_cast<int>(GradientInterpolationMethod::Linear));
    form->addRow(QObject::tr("Method"), state->interpolation);
    state->style_combo = new QComboBox(parent_widget);
    state->style_combo->setObjectName(object_prefix +
                                      QStringLiteral("StyleCombo"));
    state->style_combo->addItem(
        QObject::tr("Linear"),
        static_cast<int>(LayerStyleGradientType::Linear));
    state->style_combo->addItem(
        QObject::tr("Radial"),
        static_cast<int>(LayerStyleGradientType::Radial));
    state->style_combo->addItem(
        QObject::tr("Angle", "gradient style"),
        static_cast<int>(LayerStyleGradientType::Angle));
    state->style_combo->addItem(
        QObject::tr("Reflected"),
        static_cast<int>(LayerStyleGradientType::Reflected));
    state->style_combo->addItem(
        QObject::tr("Diamond"),
        static_cast<int>(LayerStyleGradientType::Diamond));
    if (offer_shape_burst) {
      // Photoshop offers Shape Burst for the Stroke effect only.
      state->style_combo->addItem(
          QObject::tr("Shape Burst"),
          static_cast<int>(LayerStyleGradientType::ShapeBurst));
    }
    form->addRow(QObject::tr("Style"), state->style_combo);
    state->angle = add_slider_spin_row(
        form, parent_widget, QObject::tr("Angle"),
        object_prefix + QStringLiteral("AngleSpin"), -180, 180,
        static_cast<int>(std::round(initial_gradient.angle_degrees)));
    state->scale = add_slider_spin_row(
        form, parent_widget, QObject::tr("Scale"),
        object_prefix + QStringLiteral("ScaleSpin"), 1, 1000,
                                       static_cast<int>(std::round(initial_gradient.scale * 100.0F)),
                                       QStringLiteral("%"));
    state->offset_x = add_slider_spin_row(
        form, parent_widget, QObject::tr("Horizontal Offset"),
        object_prefix + QStringLiteral("OffsetXSpin"), -100, 100,
        static_cast<int>(std::round(initial_gradient.offset_x_percent)),
        QStringLiteral("%"));
    state->offset_y = add_slider_spin_row(
        form, parent_widget, QObject::tr("Vertical Offset"),
        object_prefix + QStringLiteral("OffsetYSpin"), -100, 100,
        static_cast<int>(std::round(initial_gradient.offset_y_percent)),
        QStringLiteral("%"));
    state->reset_alignment =
        new QPushButton(QObject::tr("Reset Alignment"), parent_widget);
    state->reset_alignment->setObjectName(
        object_prefix + QStringLiteral("ResetAlignmentButton"));
    form->addRow(QString(), state->reset_alignment);

    state->update_previews = [state] {
      if (state->color_stops.empty()) {
        state->color_stops = default_layer_style_gradient().color_stops;
      }
      if (state->alpha_stops.empty()) {
        state->alpha_stops = default_layer_style_gradient().alpha_stops;
      }
      if (state->selected_alpha_stop >= 0) {
        state->selected_alpha_stop =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->selected_color_stop = -1;
      } else {
        state->selected_color_stop =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
      }

      std::vector<GradientStop> editor_stops;
      editor_stops.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        editor_stops.push_back(
            GradientStop{stop.location, EditColor{stop.color.red, stop.color.green, stop.color.blue, 255}});
      }
      state->editor->set_stops(std::move(editor_stops));
      std::vector<float> color_midpoints;
      color_midpoints.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        color_midpoints.push_back(stop.midpoint);
      }
      state->editor->set_color_midpoints(std::move(color_midpoints));
      state->editor->set_opacity_stops(state->alpha_stops);
      state->editor->set_current_row(state->selected_color_stop);
      state->editor->set_current_opacity_row(state->selected_alpha_stop);

      const bool color_selected = state->selected_color_stop >= 0;
      state->stop_color_label->setVisible(color_selected);
      state->stop_hex->setVisible(color_selected);
      state->stop_swatch->setVisible(color_selected);
      state->stop_opacity_label->setVisible(!color_selected);
      state->stop_opacity->setVisible(!color_selected);

      const QSignalBlocker location_blocker(state->stop_location);
      const QSignalBlocker hex_blocker(state->stop_hex);
      const QSignalBlocker opacity_blocker(state->stop_opacity);
      const QSignalBlocker midpoint_blocker(state->stop_midpoint);
      auto has_previous_stop = [](const auto& stops, int row) {
        std::vector<int> rows(stops.size());
        std::iota(rows.begin(), rows.end(), 0);
        std::stable_sort(rows.begin(), rows.end(), [&](int lhs, int rhs) {
          return stops[static_cast<std::size_t>(lhs)].location <
                 stops[static_cast<std::size_t>(rhs)].location;
        });
        const auto found = std::find(rows.begin(), rows.end(), row);
        if (found == rows.end() || found == rows.begin()) {
          return false;
        }
        const auto previous = *(found - 1);
        return stops[static_cast<std::size_t>(previous)].location <
               stops[static_cast<std::size_t>(row)].location;
      };
      bool midpoint_available = false;
      if (color_selected) {
        const auto& stop = state->color_stops[static_cast<std::size_t>(state->selected_color_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_hex->setText(
            QColor(stop.color.red, stop.color.green, stop.color.blue).name(QColor::HexRgb).toUpper());
        update_color_preview_label(state->stop_swatch, stop.color.red, stop.color.green, stop.color.blue);
        state->remove_stop->setEnabled(state->color_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->color_stops, state->selected_color_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      } else {
        const auto& stop = state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_opacity->setValue(
            static_cast<int>(std::lround(std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0F)));
        state->remove_stop->setEnabled(state->alpha_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->alpha_stops, state->selected_alpha_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      }
      state->stop_midpoint_label->setEnabled(midpoint_available);
      state->stop_midpoint->setEnabled(midpoint_available);
    };
    state->load = [state](const LayerStyleGradient& value) {
      state->loading = true;
      state->template_gradient = value;
      state->color_stops = value.color_stops;
      state->alpha_stops = value.alpha_stops;
      state->selected_color_stop = 0;
      state->selected_alpha_stop = -1;
      state->reverse->setChecked(value.reverse);
      state->dither->setChecked(value.dither);
      state->align->setChecked(value.align_with_layer);
      state->form_combo->setCurrentIndex(std::max(
          0, state->form_combo->findData(static_cast<int>(value.form))));
      state->smoothness->setValue(
          static_cast<int>(std::lround(value.smoothness * 100.0 / 4096.0)));
      state->noise_seed->setValue(static_cast<int>(std::min<std::uint32_t>(
          value.noise.seed, std::numeric_limits<int>::max())));
      state->noise_roughness->setValue(static_cast<int>(
          std::lround(value.noise.roughness * 100.0 / 4096.0)));
      state->noise_transparency->setChecked(value.noise.add_transparency);
      state->noise_restrict->setChecked(value.noise.restrict_colors);
      state->noise_color_model->setCurrentIndex(
          std::max(0, state->noise_color_model->findData(
                          static_cast<int>(value.noise.color_model))));
      for (std::size_t channel = 0; channel < state->noise_minimum.size();
           ++channel) {
        state->noise_minimum[channel]->setValue(value.noise.minimum[channel]);
        state->noise_maximum[channel]->setValue(value.noise.maximum[channel]);
      }
      state->interpolation->setCurrentIndex(
          std::max(0, state->interpolation->findData(
                          static_cast<int>(value.interpolation))));
      state->style_combo->setCurrentIndex(
          std::max(0, state->style_combo->findData(static_cast<int>(value.type))));
      state->angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
      state->scale->setValue(static_cast<int>(std::round(value.scale * 100.0F)));
      state->offset_x->setValue(
          static_cast<int>(std::round(value.offset_x_percent)));
      state->offset_y->setValue(
          static_cast<int>(std::round(value.offset_y_percent)));
      state->solid_controls->setVisible(value.form ==
                                        GradientDefinitionForm::Solid);
      state->selected_stop_controls->setVisible(value.form ==
                                                GradientDefinitionForm::Solid);
      state->midpoint_controls->setVisible(value.form ==
                                           GradientDefinitionForm::Solid);
      state->stop_button_controls->setVisible(value.form ==
                                              GradientDefinitionForm::Solid);
      state->smoothness->parentWidget()->setVisible(
          value.form == GradientDefinitionForm::Solid);
      state->noise_controls->setVisible(value.form ==
                                        GradientDefinitionForm::Noise);
      state->update_previews();
      state->loading = false;
    };

    auto notify_changed = [state](bool immediate) {
      if (!state->loading && state->changed) {
        state->changed(immediate);
      }
    };
    state->editor->stop_selected = [state](int row) {
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
    };
    state->editor->opacity_stop_selected = [state](int row) {
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
    };
    state->editor->stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->color_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->stop_color_picked = [state, notify_changed](int row, QColor color) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size()) || !color.isValid()) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      notify_changed(true);
    };
    state->editor->stop_add_requested = [state, notify_changed](GradientStop stop) {
      state->color_stops.push_back(GradientColorStop{std::clamp(stop.location, 0.0F, 1.0F),
                                                     RgbColor{stop.color.r, stop.color.g, stop.color.b}});
      state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_color_stop;
    };
    state->editor->opacity_stop_add_requested = [state, notify_changed](GradientAlphaStop stop) {
      state->alpha_stops.push_back(
          GradientAlphaStop{std::clamp(stop.location, 0.0F, 1.0F), std::clamp(stop.opacity, 0.0F, 1.0F)});
      state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_alpha_stop;
    };
    state->editor->stop_delete_requested = [state, notify_changed](int row) {
      if (state->color_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops.erase(state->color_stops.begin() + row);
      state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    state->editor->opacity_stop_delete_requested = [state, notify_changed](int row) {
      if (state->alpha_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops.erase(state->alpha_stops.begin() + row);
      state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    QObject::connect(state->stop_location, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading) {
                         return;
                       }
                       const auto location = static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].location = location;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].location = location;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_midpoint, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || !state->stop_midpoint->isEnabled()) {
                         return;
                       }
                       const auto midpoint = static_cast<float>(std::clamp(value, 5, 95)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].midpoint = midpoint;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].midpoint = midpoint;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || state->selected_alpha_stop < 0 ||
                           state->selected_alpha_stop >= static_cast<int>(state->alpha_stops.size())) {
                         return;
                       }
                       state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].opacity =
                           static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_hex, &QLineEdit::editingFinished, &dialog, [state, notify_changed] {
      if (state->loading || state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      auto text = state->stop_hex->text().trimmed();
      if (!text.startsWith(QLatin1Char('#')) && text.size() == 6) {
        text.prepend(QLatin1Char('#'));
      }
      const QColor color(text);
      if (!color.isValid()) {
        state->update_previews();
        return;
      }
      state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      notify_changed(true);
    });
    auto choose_stop_color = [&, state, notify_changed, color_picker_title] {
      if (state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      const auto row = state->selected_color_stop;
      const auto original = state->color_stops[static_cast<std::size_t>(row)].color;
      auto apply_color = [state, row](QColor color) {
        if (!color.isValid() || row >= static_cast<int>(state->color_stops.size())) {
          return;
        }
        state->color_stops[static_cast<std::size_t>(row)].color =
            RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                     static_cast<std::uint8_t>(color.blue())};
        state->update_previews();
      };
      const auto chosen = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue),
                                               color_picker_title, [&](QColor color) {
                                                 apply_color(color);
                                                 notify_changed(false);
                                               });
      if (!chosen.has_value()) {
        apply_color(QColor(original.red, original.green, original.blue));
        notify_changed(true);
        return;
      }
      apply_color(*chosen);
      notify_changed(true);
    };
    QObject::connect(state->stop_swatch, &QPushButton::clicked, &dialog, choose_stop_color);
    state->editor->choose_stop_color_requested = [state, choose_stop_color](int row) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      choose_stop_color();
    };
    QObject::connect(state->add_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        auto stop = GradientAlphaStop{0.5F, 1.0F};
        if (!state->alpha_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1));
          stop = state->alpha_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->alpha_stops.push_back(stop);
        state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
        state->selected_color_stop = -1;
      } else {
        auto stop = GradientColorStop{0.5F, RgbColor{255, 255, 255}};
        if (!state->color_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1));
          stop = state->color_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->color_stops.push_back(stop);
        state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
        state->selected_alpha_stop = -1;
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->remove_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        if (state->alpha_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->alpha_stops.erase(state->alpha_stops.begin() + row);
        state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      } else {
        if (state->color_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
        state->color_stops.erase(state->color_stops.begin() + row);
        state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->reverse, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->dither, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->align, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->form_combo, &QComboBox::currentIndexChanged,
                     &dialog, [state, notify_changed](int) {
                       const auto solid =
                           state->form_combo->currentData().toInt() ==
                           static_cast<int>(GradientDefinitionForm::Solid);
                       state->solid_controls->setVisible(solid);
                       state->selected_stop_controls->setVisible(solid);
                       state->midpoint_controls->setVisible(solid);
                       state->stop_button_controls->setVisible(solid);
                       state->smoothness->parentWidget()->setVisible(solid);
                       state->noise_controls->setVisible(!solid); notify_changed(true); });
    QObject::connect(state->interpolation, &QComboBox::currentIndexChanged, &dialog,
                     [notify_changed](int) { notify_changed(true); });
    QObject::connect(state->style_combo, &QComboBox::currentIndexChanged,
                     &dialog, [notify_changed](int) { notify_changed(true); });
    QObject::connect(state->angle, qOverload<int>(&QSpinBox::valueChanged),
                     &dialog, [notify_changed](int) { notify_changed(false); });
    QObject::connect(state->scale, qOverload<int>(&QSpinBox::valueChanged),
                     &dialog, [notify_changed](int) { notify_changed(false); });
    for (auto *spin :
         {state->smoothness, state->noise_seed, state->noise_roughness,
          state->offset_x, state->offset_y}) {
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [notify_changed](int) { notify_changed(false); });
    }
    QObject::connect(state->noise_transparency, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->noise_restrict, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->noise_color_model, &QComboBox::currentIndexChanged,
                     &dialog, [notify_changed](int) { notify_changed(true); });
    for (std::size_t channel = 0; channel < state->noise_minimum.size();
         ++channel) {
      QObject::connect(state->noise_minimum[channel],
                       qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [state, notify_changed, channel](int value) {
                         if (value > state->noise_maximum[channel]->value())
                           state->noise_maximum[channel]->setValue(value);
                         notify_changed(false);
                       });
      QObject::connect(state->noise_maximum[channel],
                       qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [state, notify_changed, channel](int value) {
                         if (value < state->noise_minimum[channel]->value())
                           state->noise_minimum[channel]->setValue(value);
                         notify_changed(false);
                       });
    }
    QObject::connect(state->reset_alignment, &QPushButton::clicked, &dialog,
                     [state, notify_changed] {
                       state->offset_x->setValue(0);
                       state->offset_y->setValue(0);
                       notify_changed(true); });

    state->load(initial_gradient);
    return controls_state;
  };

  auto* blending_layout = make_page(QStringLiteral("layerStyleBlendingPage"));
  auto* bevel_layout = make_page(QStringLiteral("layerStyleBevelEmbossPage"));
  auto* stroke_layout = make_page(QStringLiteral("layerStyleStrokePage"));
  auto* inner_shadow_layout = make_page(QStringLiteral("layerStyleInnerShadowPage"));
  auto* inner_glow_layout = make_page(QStringLiteral("layerStyleInnerGlowPage"));
  auto* satin_layout = make_page(QStringLiteral("layerStyleSatinPage"));
  auto* color_overlay_layout = make_page(QStringLiteral("layerStyleColorOverlayPage"));
  auto* gradient_layout = make_page(QStringLiteral("layerStyleGradientOverlayPage"));
  auto* outer_glow_layout = make_page(QStringLiteral("layerStyleOuterGlowPage"));
  auto* shadow_layout = make_page(QStringLiteral("layerStyleDropShadowPage"));
  auto* pattern_overlay_layout = make_page(QStringLiteral("layerStylePatternOverlayPage"));
  auto* bevel_contour_layout = make_page(QStringLiteral("layerStyleBevelContourPage"));
  auto* bevel_texture_layout = make_page(QStringLiteral("layerStyleBevelTexturePage"));
  auto* styles_layout = make_page(QStringLiteral("layerStyleStylesPage"));

  // --- Styles page: the preset browser -------------------------------------
  auto* styles_hint = new QLabel(
      QObject::tr("Click a preset to replace the current effects. "
                             "Right-click a folder or style "
                  "to export it as a Photoshop .asl file."),
      &dialog);
  styles_hint->setObjectName(QStringLiteral("layerStyleStylesHint"));
  styles_hint->setWordWrap(true);
  styles_layout->addWidget(styles_hint);
  auto* styles_browser = new StyleBrowserWidget(style_library, &dialog);
  styles_browser->setObjectName(QStringLiteral("layerStyleStylesBrowser"));
  styles_browser->set_icon_extent(48);
  styles_browser->set_show_no_style_entry(true);
  styles_layout->addWidget(styles_browser, 1);
  auto* styles_buttons = new QHBoxLayout();
  auto* new_style_button = new QPushButton(QObject::tr("New Style…"), &dialog);
  new_style_button->setObjectName(QStringLiteral("layerStyleNewStyleButton"));
  new_style_button->setToolTip(
      QObject::tr("Save the current effects (and optionally blending options) "
                  "as a preset"));
  auto* manage_styles_button = new QPushButton(QObject::tr("Manage Styles…"), &dialog);
  manage_styles_button->setObjectName(QStringLiteral("layerStyleManageStylesButton"));
  styles_buttons->addWidget(new_style_button);
  styles_buttons->addWidget(manage_styles_button);
  styles_buttons->addStretch(1);
  styles_layout->addLayout(styles_buttons);
  new_style_button->setEnabled(style_library != nullptr);
  manage_styles_button->setEnabled(style_library != nullptr);
  styles_browser->reload();

  // Pattern pickers list document-embedded resources first, then the persistent
  // user library. A direct dialog caller that supplies no library retains the
  // built-in fallback used before the Pattern Manager existed.
  const auto pattern_entry_display_name = [](const PatternLibraryEntry& entry) {
    if (const auto* preset = find_builtin_pattern_preset(entry.id.toStdString());
        preset != nullptr && entry.name == QString::fromLatin1(preset->english_name)) {
      return QObject::tr(preset->english_name);
    }
    return entry.name;
  };
  const auto populate_pattern_combo = [&](PatternPickerCombo* combo, const std::string& current_id,
                                          const std::string& current_name) {
    const QSignalBlocker blocker(combo);
    combo->clear_patterns();
    if (document_patterns != nullptr) {
      for (const auto& resource : document_patterns->patterns) {
        const auto name = QString::fromStdString(resource.name);
        const auto display = name.isEmpty() ? QString::fromStdString(resource.id) : name;
        combo->add_pattern(pattern_preset_icon(resource.tile), display, display,
                           QString::fromStdString(resource.id), name);
      }
    }
    if (pattern_library != nullptr) {
      for (const auto& entry : pattern_library->entries()) {
        if (document_patterns != nullptr && document_patterns->find(entry.id.toStdString()) != nullptr) {
          continue;
        }
        const auto display = pattern_entry_display_name(entry);
        const auto label = entry.folder.isEmpty() ? display
                                                   : QObject::tr("%1 / %2").arg(entry.folder, display);
        combo->add_pattern(QIcon(entry.thumbnail), label, display, entry.id, entry.name,
                           entry.folder);
      }
    } else {
      for (const auto& preset : builtin_pattern_presets()) {
        if (document_patterns != nullptr && document_patterns->find(preset.id) != nullptr) {
          continue;
        }
        const auto display = QObject::tr(preset.english_name);
        combo->add_pattern(pattern_preset_icon(generate_builtin_pattern_tile(preset.id)), display,
                           display, QString::fromLatin1(preset.id),
                           QString::fromLatin1(preset.english_name), default_patterns_folder_name());
      }
    }
    const auto current = QString::fromStdString(current_id);
    auto index = combo->findData(current, kPatternIdRole);
    if (index < 0 && !current.isEmpty()) {
      // Unresolvable reference: keep it selectable so opening and accepting the
      // dialog never silently rewrites the file's pattern id.
      const auto display = current_name.empty() ? current_id : current_name;
      const auto missing = QObject::tr("%1 (missing)").arg(QString::fromStdString(display));
      combo->add_pattern(missing, missing, current, QString::fromStdString(current_name));
      index = combo->count() - 1;
    }
    combo->setCurrentIndex(index >= 0 ? index : (combo->count() > 0 ? 0 : -1));
  };
  auto make_pattern_combo = [&](QWidget* combo_parent, const QString& object_name,
                                const std::string& current_id, const std::string& current_name) {
    auto* combo = new PatternPickerCombo(combo_parent);
    combo->setObjectName(object_name);
    combo->setIconSize(QSize(24, 24));
    populate_pattern_combo(combo, current_id, current_name);
    return combo;
  };
  auto pattern_combo_id = [](const QComboBox* combo) {
    return combo->currentData(kPatternIdRole).toString().toStdString();
  };
  auto pattern_combo_persist_name = [](const QComboBox* combo) {
    return combo->currentData(kPatternPersistNameRole).toString().toStdString();
  };
  auto set_pattern_combo_id = [](QComboBox* combo, const std::string& id) {
    const auto index = combo->findData(QString::fromStdString(id), kPatternIdRole);
    combo->setCurrentIndex(index >= 0 ? index : 0);
  };

  // Contour pickers: the built-in shape roster plus a leading "Custom" entry
  // whenever the current curve matches no preset (imported Photoshop curves).
  // The host-retained custom curve is only replaced by a deliberate preset
  // pick.
  auto make_contour_combo = [&](QWidget* combo_parent, const QString& object_name,
                                const StyleContour& current, const StyleContour& custom_value) {
    auto* combo = new QComboBox(combo_parent);
    combo->setObjectName(object_name);
    combo->setIconSize(QSize(32, 24));
    const auto* matching = find_builtin_contour_preset(current);
    if (matching == nullptr) {
      combo->addItem(contour_preset_icon(custom_value), QObject::tr("Custom"),
                     QString::fromLatin1(kContourCustomId));
    }
    for (const auto& preset : builtin_contour_presets()) {
      combo->addItem(contour_preset_icon(preset.contour), QObject::tr(preset.english_name),
                     QString::fromLatin1(preset.id));
    }
    if (matching != nullptr) {
      combo->setCurrentIndex(
          std::max(0, combo->findData(QString::fromLatin1(matching->id), kPatternIdRole)));
    } else {
      combo->setCurrentIndex(0);
    }
    return combo;
  };
  auto contour_combo_value = [](const QComboBox* combo, const StyleContour& custom_value) {
    const auto id = combo->currentData().toString();
    if (id == QLatin1String(kContourCustomId)) {
      return custom_value;
    }
    if (const auto* preset = find_builtin_contour_preset(id.toStdString()); preset != nullptr) {
      return preset->contour;
    }
    return custom_value;
  };
  auto set_contour_combo_value = [](QComboBox* combo, const StyleContour& value) {
    const auto* matching = find_builtin_contour_preset(value);
    if (matching != nullptr) {
      const auto index = combo->findData(QString::fromLatin1(matching->id));
      if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
      }
    }
    const auto custom_index = combo->findData(QString::fromLatin1(kContourCustomId));
    combo->setCurrentIndex(std::max(0, custom_index));
  };

  auto* blending_group = new QGroupBox(QObject::tr("Blending Options"), controls);
  auto* blending_form = new QFormLayout(blending_group);
  auto* blend = new QComboBox(blending_group);
  blend->setObjectName(QStringLiteral("layerStyleBlendModeCombo"));
  add_blend_mode_items(blend);
  blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(layer.blend_mode()))));
  blending_form->addRow(QObject::tr("Blend Mode"), blend);
  auto* opacity = add_slider_spin_row(blending_form, blending_group, QObject::tr("Opacity"),
                                      QStringLiteral("layerStyleOpacitySpin"), 0, 100,
                                      static_cast<int>(std::round(layer.opacity() * 100.0F)), QStringLiteral("%"));
  auto* fill_opacity = add_slider_spin_row(
      blending_form, blending_group, QObject::tr("Fill Opacity"),
      QStringLiteral("layerStyleFillOpacitySpin"), 0, 100,
      static_cast<int>(std::round(layer.fill_opacity() * 100.0F)), QStringLiteral("%"));
  fill_opacity->setEnabled(layer.kind() != LayerKind::Group);
  auto* show_effects = new QCheckBox(QObject::tr("Show Effects"), blending_group);
  show_effects->setObjectName(QStringLiteral("layerStyleShowEffectsCheck"));
  show_effects->setChecked(style.effects_visible);
  blending_form->addRow(QString(), show_effects);
  auto* mask_hides_effects = new QCheckBox(QObject::tr("Layer Mask Hides Effects"), blending_group);
  mask_hides_effects->setObjectName(QStringLiteral("layerStyleMaskHidesEffectsCheck"));
  mask_hides_effects->setChecked(style.layer_mask_hides_effects);
  mask_hides_effects->setToolTip(
      QObject::tr("Clip drop shadows, glows, and strokes with the layer mask "
                  "instead of only shaping them"));
  blending_form->addRow(QString(), mask_hides_effects);
  blending_layout->addWidget(blending_group);

  struct BlendIfRowWidgets {
    struct SpinControl {
      QWidget* container{nullptr};
      QSpinBox* spin{nullptr};
      QPushButton* decrease{nullptr};
      QPushButton* increase{nullptr};

      void sync_buttons() const {
        decrease->setEnabled(spin->value() > spin->minimum());
        increase->setEnabled(spin->value() < spin->maximum());
      }
    };

    BlendIfRangeEditorWidget* editor{nullptr};
    SpinControl black_low;
    SpinControl black_high;
    SpinControl white_low;
    SpinControl white_high;
  };

  auto* blend_if_group = new QGroupBox(QObject::tr("Blend If"), controls);
  blend_if_group->setObjectName(QStringLiteral("layerStyleBlendIfGroup"));
  auto* blend_if_layout = new QVBoxLayout(blend_if_group);
  blend_if_layout->setSpacing(4);
  auto* blend_if_channel_row = new QHBoxLayout();
  auto* blend_if_channel_label = new QLabel(QObject::tr("Blend If:"), blend_if_group);
  auto* blend_if_channel = new QComboBox(blend_if_group);
  blend_if_channel->setObjectName(QStringLiteral("layerStyleBlendIfChannelCombo"));
  blend_if_channel->addItem(QObject::tr("Gray"), static_cast<int>(BlendIfChannel::Gray));
  blend_if_channel->addItem(QObject::tr("Red"), static_cast<int>(BlendIfChannel::Red));
  blend_if_channel->addItem(QObject::tr("Green"), static_cast<int>(BlendIfChannel::Green));
  blend_if_channel->addItem(QObject::tr("Blue"), static_cast<int>(BlendIfChannel::Blue));
  blend_if_channel_label->setBuddy(blend_if_channel);
  blend_if_channel_row->addWidget(blend_if_channel_label);
  blend_if_channel_row->addWidget(blend_if_channel, 1);
  auto* reset_blend_if_channel = new QPushButton(QObject::tr("Reset Channel"), blend_if_group);
  reset_blend_if_channel->setObjectName(QStringLiteral("layerStyleBlendIfResetChannelButton"));
  blend_if_channel_row->addWidget(reset_blend_if_channel);
  blend_if_layout->addLayout(blend_if_channel_row);

  const auto blend_if_value_style = QStringLiteral(R"(
    QSpinBox {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
      color: #f0f0f0;
      padding: 0 5px;
    }
    QSpinBox:disabled {
      background: #2c2c2c;
      color: #767676;
    }
  )");
  const auto blend_if_step_button_style = QStringLiteral(R"(
    QPushButton {
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 2px;
      padding: 0;
    }
    QPushButton:hover { background: #4a4a4a; border-color: #696969; }
    QPushButton:pressed { background: #2f75bd; border-color: #6bb3ff; }
    QPushButton:disabled { background: #2e2e2e; border-top-color: #444444; }
  )");

  auto make_blend_if_spin = [&](QWidget* parent_widget, const QString& object_name,
                                const QString& accessible_name) {
    BlendIfRowWidgets::SpinControl control;
    control.container = new QWidget(parent_widget);
    auto* layout = new QHBoxLayout(control.container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* spin = new QSpinBox(control.container);
    spin->setObjectName(object_name);
    spin->setRange(0, 255);
    spin->setAccessibleName(accessible_name);
    configure_dialog_spinbox(spin, 48);
    spin->setFixedWidth(48);
    spin->setStyleSheet(blend_if_value_style);

    auto* decrease = new QPushButton(QStringLiteral("-"), control.container);
    decrease->setObjectName(object_name + QStringLiteral("DecreaseButton"));
    decrease->setAccessibleName(QObject::tr("Decrease %1").arg(accessible_name));
    decrease->setToolTip(decrease->accessibleName());
    decrease->setAutoRepeat(true);
    configure_compact_symbol_button(decrease);
    decrease->setStyleSheet(blend_if_step_button_style);

    auto* increase = new QPushButton(QStringLiteral("+"), control.container);
    increase->setObjectName(object_name + QStringLiteral("IncreaseButton"));
    increase->setAccessibleName(QObject::tr("Increase %1").arg(accessible_name));
    increase->setToolTip(increase->accessibleName());
    increase->setAutoRepeat(true);
    configure_compact_symbol_button(increase);
    increase->setStyleSheet(blend_if_step_button_style);

    layout->addWidget(spin);
    layout->addWidget(decrease);
    layout->addWidget(increase);

    control.spin = spin;
    control.decrease = decrease;
    control.increase = increase;
    QObject::connect(decrease, &QPushButton::clicked, spin, &QSpinBox::stepDown);
    QObject::connect(increase, &QPushButton::clicked, spin, &QSpinBox::stepUp);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), control.container,
                     [decrease, increase, spin](int) {
                       decrease->setEnabled(spin->value() > spin->minimum());
                       increase->setEnabled(spin->value() < spin->maximum());
                     });
    control.sync_buttons();
    return control;
  };

  auto make_blend_if_row = [&](const QString& title, const QString& object_prefix,
                               const QString& editor_object_name) {
    BlendIfRowWidgets row;
    auto* title_label = new QLabel(title, blend_if_group);
    title_label->setStyleSheet(QStringLiteral("font-weight: 600;"));
    blend_if_layout->addWidget(title_label);
    row.editor = new BlendIfRangeEditorWidget(blend_if_group);
    row.editor->setObjectName(editor_object_name);
    row.editor->set_accessibility_text(
        QObject::tr("%1 Blend If range").arg(title),
        QObject::tr("Use Page Up or Page Down to select a handle, arrow keys "
                    "to move it, and Alt/Option-drag to "
                    "split a joined handle."));
    blend_if_layout->addWidget(row.editor);

    auto* values = new QHBoxLayout();
    values->setSpacing(5);
    auto* black_label = new QLabel(QObject::tr("Black"), blend_if_group);
    values->addWidget(black_label);
    row.black_low = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("BlackLowSpin"),
        QObject::tr("%1 black transition start").arg(title));
    row.black_high = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("BlackHighSpin"),
        QObject::tr("%1 black transition end").arg(title));
    black_label->setBuddy(row.black_low.spin);
    values->addWidget(row.black_low.container);
    values->addWidget(new QLabel(QObject::tr("to"), blend_if_group));
    values->addWidget(row.black_high.container);
    values->addStretch(1);
    auto* white_label = new QLabel(QObject::tr("White"), blend_if_group);
    values->addWidget(white_label);
    row.white_low = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("WhiteLowSpin"),
        QObject::tr("%1 white transition start").arg(title));
    row.white_high = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("WhiteHighSpin"),
        QObject::tr("%1 white transition end").arg(title));
    white_label->setBuddy(row.white_low.spin);
    values->addWidget(row.white_low.container);
    values->addWidget(new QLabel(QObject::tr("to"), blend_if_group));
    values->addWidget(row.white_high.container);
    blend_if_layout->addLayout(values);
    return row;
  };

  auto blend_if_this = make_blend_if_row(QObject::tr("This Layer"), QStringLiteral("layerStyleBlendIfThis"),
                                         QStringLiteral("layerStyleBlendIfThisEditor"));
  auto blend_if_underlying =
      make_blend_if_row(QObject::tr("Underlying Layer"), QStringLiteral("layerStyleBlendIfUnderlying"),
                        QStringLiteral("layerStyleBlendIfUnderlyingEditor"));
  blend_if_group->setEnabled(blend_if_payload_status != BlendIfPayloadStatus::Unsupported);
  blending_layout->addWidget(blend_if_group);

  bool loading_blend_if_controls = false;
  const auto current_blend_if_channel_index = [&] {
    return static_cast<std::size_t>(std::clamp(blend_if_channel->currentData().toInt(), 0, 3));
  };
  const auto set_blend_if_row = [&](BlendIfRowWidgets& row, BlendIfThresholds thresholds) {
    const QSignalBlocker black_low_blocker(row.black_low.spin);
    const QSignalBlocker black_high_blocker(row.black_high.spin);
    const QSignalBlocker white_low_blocker(row.white_low.spin);
    const QSignalBlocker white_high_blocker(row.white_high.spin);
    for (auto* spin : {row.black_low.spin, row.black_high.spin, row.white_low.spin, row.white_high.spin}) {
      spin->setRange(0, 255);
    }
    row.black_low.spin->setValue(thresholds.black_low);
    row.black_high.spin->setValue(thresholds.black_high);
    row.white_low.spin->setValue(thresholds.white_low);
    row.white_high.spin->setValue(thresholds.white_high);
    row.black_low.spin->setRange(0, thresholds.black_high);
    row.black_high.spin->setRange(thresholds.black_low, thresholds.white_low);
    row.white_low.spin->setRange(thresholds.black_high, thresholds.white_high);
    row.white_high.spin->setRange(thresholds.white_low, 255);
    row.black_low.sync_buttons();
    row.black_high.sync_buttons();
    row.white_low.sync_buttons();
    row.white_high.sync_buttons();
    row.editor->set_thresholds(thresholds);
  };
  const auto load_blend_if_controls = [&] {
    loading_blend_if_controls = true;
    const auto channel_index = current_blend_if_channel_index();
    const auto channel = static_cast<BlendIfChannel>(channel_index);
    blend_if_this.editor->set_ramp_channel(channel);
    blend_if_underlying.editor->set_ramp_channel(channel);
    set_blend_if_row(blend_if_this, blend_if.channels[channel_index].this_layer);
    set_blend_if_row(blend_if_underlying, blend_if.channels[channel_index].underlying_layer);
    loading_blend_if_controls = false;
  };
  load_blend_if_controls();

  // Preview is transient dialog state, independent of Photoshop's persisted
  // master effects switch (Show Effects above). It stays visible on every page.
  auto* preview_check = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview_check->setObjectName(QStringLiteral("layerStylePreviewCheck"));
  preview_check->setChecked(true);
  blending_layout->addStretch(1);

  auto* bevel_group = new QGroupBox(QObject::tr("Bevel & Emboss"), controls);
  auto* bevel_form = new QFormLayout(bevel_group);
  auto* bevel_style = new QComboBox(bevel_group);
  bevel_style->setObjectName(QStringLiteral("layerStyleBevelStyleCombo"));
  bevel_style->addItem(QObject::tr("Inner Bevel"), static_cast<int>(BevelEmbossStyleKind::InnerBevel));
  bevel_style->addItem(QObject::tr("Outer Bevel"), static_cast<int>(BevelEmbossStyleKind::OuterBevel));
  bevel_style->addItem(QObject::tr("Emboss"), static_cast<int>(BevelEmbossStyleKind::Emboss));
  bevel_style->addItem(QObject::tr("Pillow Emboss"), static_cast<int>(BevelEmbossStyleKind::PillowEmboss));
  bevel_style->addItem(QObject::tr("Stroke Emboss"), static_cast<int>(BevelEmbossStyleKind::StrokeEmboss));
  bevel_style->setCurrentIndex(std::max(0, bevel_style->findData(static_cast<int>(bevel.style))));
  bevel_form->addRow(QObject::tr("Style"), bevel_style);
  auto* bevel_technique = new QComboBox(bevel_group);
  bevel_technique->setObjectName(QStringLiteral("layerStyleBevelTechniqueCombo"));
  bevel_technique->addItem(QObject::tr("Smooth"), static_cast<int>(BevelTechnique::Smooth));
  bevel_technique->addItem(QObject::tr("Chisel Hard"), static_cast<int>(BevelTechnique::ChiselHard));
  bevel_technique->addItem(QObject::tr("Chisel Soft"), static_cast<int>(BevelTechnique::ChiselSoft));
  bevel_technique->setCurrentIndex(
      std::max(0, bevel_technique->findData(static_cast<int>(bevel.technique))));
  bevel_form->addRow(QObject::tr("Technique"), bevel_technique);
  auto* bevel_size = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleBevelSizeSpin"), 1, 250,
                                         static_cast<int>(std::round(bevel.size)));
  auto* bevel_soften = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Soften"),
                                           QStringLiteral("layerStyleBevelSoftenSpin"), 0, 16,
                                           static_cast<int>(std::round(bevel.soften)));
  auto* bevel_depth = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Depth"),
                                          QStringLiteral("layerStyleBevelDepthSpin"), 1, 1000,
                                          static_cast<int>(std::round(bevel.depth * 100.0F)), QStringLiteral("%"));
  auto* bevel_angle = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Angle"),
                                          QStringLiteral("layerStyleBevelAngleSpin"), -180, 180,
                                          static_cast<int>(std::round(bevel.angle_degrees)));
  auto* bevel_altitude = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Altitude"),
                                             QStringLiteral("layerStyleBevelAltitudeSpin"), 0, 90,
                                             static_cast<int>(std::round(bevel.altitude_degrees)));
  auto* bevel_direction = new QComboBox(bevel_group);
  bevel_direction->setObjectName(QStringLiteral("layerStyleBevelDirectionCombo"));
  bevel_direction->addItem(QObject::tr("Up"), true);
  bevel_direction->addItem(QObject::tr("Down"), false);
  bevel_direction->setCurrentIndex(bevel.direction_up ? 0 : 1);
  bevel_form->addRow(QObject::tr("Direction"), bevel_direction);
  auto* bevel_gloss_row = new QWidget(bevel_group);
  auto* bevel_gloss_layout = new QHBoxLayout(bevel_gloss_row);
  bevel_gloss_layout->setContentsMargins(0, 0, 0, 0);
  bevel_gloss_layout->setSpacing(8);
  auto* bevel_gloss_contour = make_contour_combo(bevel_gloss_row,
                                                 QStringLiteral("layerStyleBevelGlossContourCombo"),
                                                 bevel.gloss_contour, custom_gloss_contour);
  bevel_gloss_layout->addWidget(bevel_gloss_contour, 1);
  auto* bevel_gloss_anti_aliased = new QCheckBox(QObject::tr("Anti-aliased"), bevel_gloss_row);
  bevel_gloss_anti_aliased->setObjectName(QStringLiteral("layerStyleBevelGlossAntiAliasedCheck"));
  bevel_gloss_anti_aliased->setChecked(bevel.gloss_anti_aliased);
  bevel_gloss_layout->addWidget(bevel_gloss_anti_aliased);
  bevel_form->addRow(QObject::tr("Gloss Contour"), bevel_gloss_row);

  auto bevel_highlight_color = bevel.highlight_color;
  auto* bevel_highlight_group = new QGroupBox(QObject::tr("Highlight"), bevel_group);
  auto* bevel_highlight_form = new QFormLayout(bevel_highlight_group);
  auto* bevel_highlight_blend = new QComboBox(bevel_highlight_group);
  bevel_highlight_blend->setObjectName(QStringLiteral("layerStyleBevelHighlightBlendModeCombo"));
  add_blend_mode_items(bevel_highlight_blend);
  bevel_highlight_blend->setCurrentIndex(
      std::max(0, bevel_highlight_blend->findData(static_cast<int>(bevel.highlight_blend_mode))));
  bevel_highlight_form->addRow(QObject::tr("Blend Mode"), bevel_highlight_blend);
  auto* bevel_highlight_opacity =
      add_slider_spin_row(bevel_highlight_form, bevel_highlight_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelHighlightOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.highlight_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_highlight_color_button = new QPushButton(bevel_highlight_group);
  bevel_highlight_color_button->setObjectName(QStringLiteral("layerStyleBevelHighlightColorButton"));
  bevel_highlight_color_button->setFixedSize(34, 24);
  bevel_highlight_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_highlight_form->addRow(QObject::tr("Color"), bevel_highlight_color_button);
  bevel_form->addRow(bevel_highlight_group);

  auto bevel_shadow_color = bevel.shadow_color;
  auto* bevel_shadow_group = new QGroupBox(QObject::tr("Shadow"), bevel_group);
  auto* bevel_shadow_form = new QFormLayout(bevel_shadow_group);
  auto* bevel_shadow_blend = new QComboBox(bevel_shadow_group);
  bevel_shadow_blend->setObjectName(QStringLiteral("layerStyleBevelShadowBlendModeCombo"));
  add_blend_mode_items(bevel_shadow_blend);
  bevel_shadow_blend->setCurrentIndex(
      std::max(0, bevel_shadow_blend->findData(static_cast<int>(bevel.shadow_blend_mode))));
  bevel_shadow_form->addRow(QObject::tr("Blend Mode"), bevel_shadow_blend);
  auto* bevel_shadow_opacity =
      add_slider_spin_row(bevel_shadow_form, bevel_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.shadow_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_shadow_color_button = new QPushButton(bevel_shadow_group);
  bevel_shadow_color_button->setObjectName(QStringLiteral("layerStyleBevelShadowColorButton"));
  bevel_shadow_color_button->setFixedSize(34, 24);
  bevel_shadow_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_shadow_form->addRow(QObject::tr("Color"), bevel_shadow_color_button);
  bevel_form->addRow(bevel_shadow_group);
  auto update_bevel_color_previews = [&] {
    update_color_preview_label(bevel_highlight_color_button, bevel_highlight_color.red,
                               bevel_highlight_color.green, bevel_highlight_color.blue);
    update_color_preview_label(bevel_shadow_color_button, bevel_shadow_color.red,
                               bevel_shadow_color.green, bevel_shadow_color.blue);
  };
  update_bevel_color_previews();
  bevel_layout->addWidget(bevel_group);
  bevel_layout->addStretch(1);

  auto* bevel_contour_group = new QGroupBox(QObject::tr("Contour"), controls);
  auto* bevel_contour_form = new QFormLayout(bevel_contour_group);
  auto* bevel_contour_row = new QWidget(bevel_contour_group);
  auto* bevel_contour_row_layout = new QHBoxLayout(bevel_contour_row);
  bevel_contour_row_layout->setContentsMargins(0, 0, 0, 0);
  bevel_contour_row_layout->setSpacing(8);
  auto* bevel_contour_combo = make_contour_combo(bevel_contour_row,
                                                 QStringLiteral("layerStyleBevelContourCombo"),
                                                 bevel.contour.contour, custom_bevel_sub_contour);
  bevel_contour_row_layout->addWidget(bevel_contour_combo, 1);
  auto* bevel_contour_anti_aliased = new QCheckBox(QObject::tr("Anti-aliased"), bevel_contour_row);
  bevel_contour_anti_aliased->setObjectName(QStringLiteral("layerStyleBevelContourAntiAliasedCheck"));
  bevel_contour_anti_aliased->setChecked(bevel.contour.anti_aliased);
  bevel_contour_row_layout->addWidget(bevel_contour_anti_aliased);
  bevel_contour_form->addRow(QObject::tr("Contour"), bevel_contour_row);
  auto* bevel_contour_range = add_slider_spin_row(
      bevel_contour_form, bevel_contour_group, QObject::tr("Range"),
      QStringLiteral("layerStyleBevelContourRangeSpin"), 1, 100,
      static_cast<int>(std::round(bevel.contour.range * 100.0F)), QStringLiteral("%"));
  bevel_contour_layout->addWidget(bevel_contour_group);
  bevel_contour_layout->addStretch(1);

  auto* bevel_texture_group = new QGroupBox(QObject::tr("Texture"), controls);
  auto* bevel_texture_form = new QFormLayout(bevel_texture_group);
  auto* bevel_texture_pattern_row = new QWidget(bevel_texture_group);
  auto* bevel_texture_pattern_layout = new QHBoxLayout(bevel_texture_pattern_row);
  bevel_texture_pattern_layout->setContentsMargins(0, 0, 0, 0);
  bevel_texture_pattern_layout->setSpacing(8);
  auto* bevel_texture_pattern = make_pattern_combo(bevel_texture_pattern_row,
                                                   QStringLiteral("layerStyleBevelTexturePatternCombo"),
                                                   bevel.texture.pattern_id, bevel.texture.pattern_name);
  bevel_texture_pattern_layout->addWidget(bevel_texture_pattern, 1);
  auto* bevel_texture_manage = new QPushButton(QObject::tr("Manage…"), bevel_texture_pattern_row);
  bevel_texture_manage->setObjectName(QStringLiteral("layerStyleBevelTextureManageButton"));
  bevel_texture_manage->setToolTip(QObject::tr("Open Pattern Manager"));
  bevel_texture_pattern_layout->addWidget(bevel_texture_manage);
  bevel_texture_form->addRow(QObject::tr("Pattern"), bevel_texture_pattern_row);
  auto* bevel_texture_scale = add_slider_spin_row(
      bevel_texture_form, bevel_texture_group, QObject::tr("Scale"),
      QStringLiteral("layerStyleBevelTextureScaleSpin"), 1, 1000,
      static_cast<int>(std::round(bevel.texture.scale * 100.0F)), QStringLiteral("%"));
  auto* bevel_texture_depth = add_slider_spin_row(
      bevel_texture_form, bevel_texture_group, QObject::tr("Depth"),
      QStringLiteral("layerStyleBevelTextureDepthSpin"), -1000, 1000,
      static_cast<int>(std::round(bevel.texture.depth * 100.0F)), QStringLiteral("%"));
  auto* bevel_texture_invert = new QCheckBox(QObject::tr("Invert"), bevel_texture_group);
  bevel_texture_invert->setObjectName(QStringLiteral("layerStyleBevelTextureInvertCheck"));
  bevel_texture_invert->setChecked(bevel.texture.invert);
  bevel_texture_form->addRow(QString(), bevel_texture_invert);
  auto* bevel_texture_link = new QCheckBox(QObject::tr("Link with Layer"), bevel_texture_group);
  bevel_texture_link->setObjectName(QStringLiteral("layerStyleBevelTextureLinkCheck"));
  bevel_texture_link->setChecked(bevel.texture.link_with_layer);
  bevel_texture_link->setToolTip(
      QObject::tr("Anchor the pattern to the layer so it follows when the layer moves"));
  bevel_texture_form->addRow(QString(), bevel_texture_link);
  auto* bevel_texture_snap = new QPushButton(QObject::tr("Snap to Origin"), bevel_texture_group);
  bevel_texture_snap->setObjectName(QStringLiteral("layerStyleBevelTextureSnapOriginButton"));
  bevel_texture_form->addRow(QString(), bevel_texture_snap);
  bevel_texture_layout->addWidget(bevel_texture_group);
  bevel_texture_layout->addStretch(1);

  auto* pattern_overlay_group = new QGroupBox(QObject::tr("Pattern Overlay"), controls);
  auto* pattern_overlay_form = new QFormLayout(pattern_overlay_group);
  auto* pattern_overlay_blend = new QComboBox(pattern_overlay_group);
  pattern_overlay_blend->setObjectName(QStringLiteral("layerStylePatternOverlayBlendModeCombo"));
  add_blend_mode_items(pattern_overlay_blend);
  pattern_overlay_blend->setCurrentIndex(
      std::max(0, pattern_overlay_blend->findData(static_cast<int>(pattern_overlay.blend_mode))));
  pattern_overlay_form->addRow(QObject::tr("Blend Mode"), pattern_overlay_blend);
  auto* pattern_overlay_opacity = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Opacity"),
      QStringLiteral("layerStylePatternOverlayOpacitySpin"), 0, 100,
      static_cast<int>(std::round(pattern_overlay.opacity * 100.0F)), QStringLiteral("%"));
  auto* pattern_overlay_pattern_row = new QWidget(pattern_overlay_group);
  auto* pattern_overlay_pattern_layout = new QHBoxLayout(pattern_overlay_pattern_row);
  pattern_overlay_pattern_layout->setContentsMargins(0, 0, 0, 0);
  pattern_overlay_pattern_layout->setSpacing(8);
  auto* pattern_overlay_pattern = make_pattern_combo(
      pattern_overlay_pattern_row, QStringLiteral("layerStylePatternOverlayPatternCombo"),
      pattern_overlay.pattern_id, pattern_overlay.pattern_name);
  pattern_overlay_pattern_layout->addWidget(pattern_overlay_pattern, 1);
  auto* pattern_overlay_manage = new QPushButton(QObject::tr("Manage…"), pattern_overlay_pattern_row);
  pattern_overlay_manage->setObjectName(QStringLiteral("layerStylePatternOverlayManageButton"));
  pattern_overlay_manage->setToolTip(QObject::tr("Open Pattern Manager"));
  pattern_overlay_pattern_layout->addWidget(pattern_overlay_manage);
  pattern_overlay_form->addRow(QObject::tr("Pattern"), pattern_overlay_pattern_row);
  auto* pattern_overlay_angle = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Angle"),
      QStringLiteral("layerStylePatternOverlayAngleSpin"), -180, 180,
      static_cast<int>(std::round(pattern_overlay.angle_degrees)));
  auto* pattern_overlay_scale = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Scale"),
      QStringLiteral("layerStylePatternOverlayScaleSpin"), 1, 1000,
      static_cast<int>(std::round(pattern_overlay.scale * 100.0F)), QStringLiteral("%"));
  auto* pattern_overlay_link = new QCheckBox(QObject::tr("Link with Layer"), pattern_overlay_group);
  pattern_overlay_link->setObjectName(QStringLiteral("layerStylePatternOverlayLinkCheck"));
  pattern_overlay_link->setChecked(pattern_overlay.link_with_layer);
  pattern_overlay_link->setToolTip(
      QObject::tr("Anchor the pattern to the layer so it follows when the layer moves"));
  pattern_overlay_form->addRow(QString(), pattern_overlay_link);
  auto* pattern_overlay_snap = new QPushButton(QObject::tr("Snap to Origin"), pattern_overlay_group);
  pattern_overlay_snap->setObjectName(QStringLiteral("layerStylePatternOverlaySnapOriginButton"));
  pattern_overlay_form->addRow(QString(), pattern_overlay_snap);
  pattern_overlay_layout->addWidget(pattern_overlay_group);
  pattern_overlay_layout->addStretch(1);

  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), controls);
  auto* stroke_form = new QFormLayout(stroke_group);
  auto* stroke_size = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleStrokeSizeSpin"), 1, 250,
                                          static_cast<int>(std::round(stroke.size)));
  auto* stroke_position = new QComboBox(stroke_group);
  stroke_position->setObjectName(QStringLiteral("layerStyleStrokePositionCombo"));
  stroke_position->addItem(QObject::tr("Outside"), static_cast<int>(LayerStrokePosition::Outside));
  stroke_position->addItem(QObject::tr("Inside"), static_cast<int>(LayerStrokePosition::Inside));
  stroke_position->addItem(QObject::tr("Center"), static_cast<int>(LayerStrokePosition::Center));
  stroke_position->setCurrentIndex(std::max(0, stroke_position->findData(static_cast<int>(stroke.position))));
  stroke_form->addRow(QObject::tr("Position"), stroke_position);
  auto* stroke_blend = new QComboBox(stroke_group);
  stroke_blend->setObjectName(QStringLiteral("layerStyleStrokeBlendModeCombo"));
  add_blend_mode_items(stroke_blend);
  stroke_blend->setCurrentIndex(std::max(0, stroke_blend->findData(static_cast<int>(stroke.blend_mode))));
  stroke_form->addRow(QObject::tr("Blend Mode"), stroke_blend);
  auto* stroke_opacity = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleStrokeOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(stroke.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* stroke_fill = new QComboBox(stroke_group);
  stroke_fill->setObjectName(QStringLiteral("layerStyleStrokeFillCombo"));
  stroke_fill->addItem(QObject::tr("Color"), false);
  stroke_fill->addItem(QObject::tr("Gradient"), true);
  stroke_fill->setCurrentIndex(stroke.uses_gradient ? 1 : 0);
  stroke_form->addRow(QObject::tr("Fill"), stroke_fill);
  const auto stroke_rows =
      make_color_rows(stroke_form, stroke_group, QStringLiteral("layerStyleStroke"), stroke.color);
  auto* stroke_red = stroke_rows.red;
  auto* stroke_green = stroke_rows.green;
  auto* stroke_blue = stroke_rows.blue;
  auto* stroke_gradient_group = new QGroupBox(QObject::tr("Gradient"), stroke_group);
  stroke_gradient_group->setObjectName(QStringLiteral("layerStyleStrokeGradientGroup"));
  auto* stroke_gradient_form = new QFormLayout(stroke_gradient_group);
  auto stroke_gradient_controls =
      make_gradient_controls(stroke_gradient_form, stroke_gradient_group,
                             QStringLiteral("layerStyleStrokeGradient"), stroke.gradient,
                             QObject::tr("Choose Gradient Stop Color"), /*offer_shape_burst=*/true);
  stroke_form->addRow(stroke_gradient_group);
  auto update_stroke_fill_visibility = [&] {
    const bool uses_gradient = stroke_fill->currentData().toBool();
    stroke_form->setRowVisible(stroke_rows.row_container, !uses_gradient);
    stroke_form->setRowVisible(stroke_gradient_group, uses_gradient);
  };
  update_stroke_fill_visibility();
  stroke_layout->addWidget(stroke_group);
  stroke_layout->addStretch(1);

  auto* inner_shadow_group = new QGroupBox(QObject::tr("Inner Shadow"), controls);
  auto* inner_shadow_form = new QFormLayout(inner_shadow_group);
  auto* inner_shadow_blend = new QComboBox(inner_shadow_group);
  inner_shadow_blend->setObjectName(QStringLiteral("layerStyleInnerShadowBlendModeCombo"));
  add_blend_mode_items(inner_shadow_blend);
  inner_shadow_blend->setCurrentIndex(
      std::max(0, inner_shadow_blend->findData(static_cast<int>(inner_shadow.blend_mode))));
  inner_shadow_form->addRow(QObject::tr("Blend Mode"), inner_shadow_blend);
  auto* inner_shadow_opacity =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_shadow_angle =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleInnerShadowAngleSpin"), -180, 180,
                          static_cast<int>(std::round(inner_shadow.angle_degrees)));
  auto* inner_shadow_distance =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Distance"),
                          QStringLiteral("layerStyleInnerShadowDistanceSpin"), 0, 1000,
                          static_cast<int>(std::round(inner_shadow.distance)));
  auto* inner_shadow_size = add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Size"),
                                                QStringLiteral("layerStyleInnerShadowSizeSpin"), 0, 1000,
                                                static_cast<int>(std::round(inner_shadow.size)));
  auto* inner_shadow_choke =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerShadowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.choke)), QStringLiteral("%"));
  const auto inner_shadow_rows = make_color_rows(
      inner_shadow_form, inner_shadow_group, QStringLiteral("layerStyleInnerShadow"), inner_shadow.color);
  auto* inner_shadow_red = inner_shadow_rows.red;
  auto* inner_shadow_green = inner_shadow_rows.green;
  auto* inner_shadow_blue = inner_shadow_rows.blue;
  auto* inner_shadow_instance_row = new QWidget(inner_shadow_group);
  auto* inner_shadow_instance_layout = new QHBoxLayout(inner_shadow_instance_row);
  inner_shadow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_shadow = new QPushButton(QStringLiteral("+"), inner_shadow_instance_row);
  add_inner_shadow->setObjectName(QStringLiteral("layerStyleAddInnerShadowButton"));
  add_inner_shadow->setToolTip(QObject::tr("Add Inner Shadow"));
  configure_compact_symbol_button(add_inner_shadow);
  auto* remove_inner_shadow = new QPushButton(QStringLiteral("-"), inner_shadow_instance_row);
  remove_inner_shadow->setObjectName(QStringLiteral("layerStyleRemoveInnerShadowButton"));
  remove_inner_shadow->setToolTip(QObject::tr("Remove Inner Shadow"));
  configure_compact_symbol_button(remove_inner_shadow);
  inner_shadow_instance_layout->addWidget(add_inner_shadow);
  inner_shadow_instance_layout->addWidget(remove_inner_shadow);
  inner_shadow_instance_layout->addStretch(1);
  inner_shadow_form->addRow(QObject::tr("Instances"), inner_shadow_instance_row);
  inner_shadow_layout->addWidget(inner_shadow_group);
  inner_shadow_layout->addStretch(1);

  auto* inner_glow_group = new QGroupBox(QObject::tr("Inner Glow"), controls);
  auto* inner_glow_form = new QFormLayout(inner_glow_group);
  auto* inner_glow_blend = new QComboBox(inner_glow_group);
  inner_glow_blend->setObjectName(QStringLiteral("layerStyleInnerGlowBlendModeCombo"));
  add_blend_mode_items(inner_glow_blend);
  inner_glow_blend->setCurrentIndex(std::max(0, inner_glow_blend->findData(static_cast<int>(inner_glow.blend_mode))));
  inner_glow_form->addRow(QObject::tr("Blend Mode"), inner_glow_blend);
  auto* inner_glow_opacity =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_glow_size = add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleInnerGlowSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(inner_glow.size)));
  auto* inner_glow_choke =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerGlowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.choke)), QStringLiteral("%"));
  auto* inner_glow_source = new QComboBox(inner_glow_group);
  inner_glow_source->setObjectName(QStringLiteral("layerStyleInnerGlowSourceCombo"));
  inner_glow_source->addItem(QObject::tr("Edge"), static_cast<int>(LayerInnerGlowSource::Edge));
  inner_glow_source->addItem(QObject::tr("Center"), static_cast<int>(LayerInnerGlowSource::Center));
  inner_glow_source->setCurrentIndex(std::max(0, inner_glow_source->findData(static_cast<int>(inner_glow.source))));
  inner_glow_form->addRow(QObject::tr("Source"), inner_glow_source);
  const auto inner_glow_rows = make_color_rows(inner_glow_form, inner_glow_group,
                                               QStringLiteral("layerStyleInnerGlow"), inner_glow.color);
  auto* inner_glow_red = inner_glow_rows.red;
  auto* inner_glow_green = inner_glow_rows.green;
  auto* inner_glow_blue = inner_glow_rows.blue;
  auto* inner_glow_instance_row = new QWidget(inner_glow_group);
  auto* inner_glow_instance_layout = new QHBoxLayout(inner_glow_instance_row);
  inner_glow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_glow = new QPushButton(QStringLiteral("+"), inner_glow_instance_row);
  add_inner_glow->setObjectName(QStringLiteral("layerStyleAddInnerGlowButton"));
  add_inner_glow->setToolTip(QObject::tr("Add Inner Glow"));
  configure_compact_symbol_button(add_inner_glow);
  auto* remove_inner_glow = new QPushButton(QStringLiteral("-"), inner_glow_instance_row);
  remove_inner_glow->setObjectName(QStringLiteral("layerStyleRemoveInnerGlowButton"));
  remove_inner_glow->setToolTip(QObject::tr("Remove Inner Glow"));
  configure_compact_symbol_button(remove_inner_glow);
  inner_glow_instance_layout->addWidget(add_inner_glow);
  inner_glow_instance_layout->addWidget(remove_inner_glow);
  inner_glow_instance_layout->addStretch(1);
  inner_glow_form->addRow(QObject::tr("Instances"), inner_glow_instance_row);
  inner_glow_layout->addWidget(inner_glow_group);
  inner_glow_layout->addStretch(1);

  auto* satin_group = new QGroupBox(QObject::tr("Satin"), controls);
  auto* satin_form = new QFormLayout(satin_group);
  auto* satin_blend = new QComboBox(satin_group);
  satin_blend->setObjectName(QStringLiteral("layerStyleSatinBlendModeCombo"));
  add_blend_mode_items(satin_blend);
  satin_blend->setCurrentIndex(std::max(0, satin_blend->findData(static_cast<int>(satin.blend_mode))));
  satin_form->addRow(QObject::tr("Blend Mode"), satin_blend);
  auto* satin_opacity =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleSatinOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(satin.opacity * 100.0F)), QStringLiteral("%"));
  auto* satin_angle =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleSatinAngleSpin"), -180, 180,
                          static_cast<int>(std::round(satin.angle_degrees)));
  auto* satin_distance =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Distance"),
                          QStringLiteral("layerStyleSatinDistanceSpin"), 0, 1000,
                          static_cast<int>(std::round(satin.distance)));
  auto* satin_size = add_slider_spin_row(satin_form, satin_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleSatinSizeSpin"), 0, 1000,
                                         static_cast<int>(std::round(satin.size)));
  auto* satin_invert = new QCheckBox(QObject::tr("Invert"), satin_group);
  satin_invert->setObjectName(QStringLiteral("layerStyleSatinInvertCheck"));
  satin_invert->setChecked(satin.invert);
  satin_form->addRow(QString(), satin_invert);
  const auto satin_rows =
      make_color_rows(satin_form, satin_group, QStringLiteral("layerStyleSatin"), satin.color);
  auto* satin_red = satin_rows.red;
  auto* satin_green = satin_rows.green;
  auto* satin_blue = satin_rows.blue;
  satin_layout->addWidget(satin_group);
  satin_layout->addStretch(1);

  auto* color_overlay_group = new QGroupBox(QObject::tr("Color Overlay"), controls);
  auto* color_overlay_form = new QFormLayout(color_overlay_group);
  auto* color_overlay_blend = new QComboBox(color_overlay_group);
  color_overlay_blend->setObjectName(QStringLiteral("layerStyleColorOverlayBlendModeCombo"));
  add_blend_mode_items(color_overlay_blend);
  color_overlay_blend->setCurrentIndex(
      std::max(0, color_overlay_blend->findData(static_cast<int>(color_overlay.blend_mode))));
  color_overlay_form->addRow(QObject::tr("Blend Mode"), color_overlay_blend);
  auto* color_overlay_opacity =
      add_slider_spin_row(color_overlay_form, color_overlay_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleColorOverlayOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(color_overlay.opacity * 100.0F)), QStringLiteral("%"));
  auto* color_overlay_color_row = new QWidget(color_overlay_group);
  auto* color_overlay_color_layout = new QVBoxLayout(color_overlay_color_row);
  color_overlay_color_layout->setContentsMargins(0, 0, 0, 0);
  color_overlay_color_layout->setSpacing(4);
  auto* color_overlay_red =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleColorOverlayRedSpin"), color_overlay.color.red);
  auto* color_overlay_green =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleColorOverlayGreenSpin"), color_overlay.color.green);
  auto* color_overlay_blue =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleColorOverlayBlueSpin"), color_overlay.color.blue);
  auto* color_overlay_preview_row = new QWidget(color_overlay_color_row);
  auto* color_overlay_preview_layout = new QHBoxLayout(color_overlay_preview_row);
  color_overlay_preview_layout->setContentsMargins(26, 0, 0, 0);
  color_overlay_preview_layout->setSpacing(8);
  auto* color_overlay_color_preview = new QLabel(color_overlay_group);
  color_overlay_color_preview->setObjectName(QStringLiteral("layerStyleColorOverlayColorPreview"));
  color_overlay_color_preview->setFixedSize(34, 24);
  color_overlay_color_preview->setToolTip(QObject::tr("Choose Color..."));
  auto* color_overlay_pick_color = new QPushButton(QObject::tr("Choose Color..."), color_overlay_group);
  color_overlay_pick_color->setObjectName(QStringLiteral("layerStyleColorOverlayPickColorButton"));
  // Double-clicking the patch opens the same picker as the button.
  new DoubleClickClicksButton(color_overlay_pick_color, color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_pick_color);
  color_overlay_preview_layout->addStretch(1);
  color_overlay_color_layout->addWidget(color_overlay_preview_row);
  RgbColorRowWidgets color_overlay_rows;
  color_overlay_rows.red = color_overlay_red;
  color_overlay_rows.green = color_overlay_green;
  color_overlay_rows.blue = color_overlay_blue;
  color_overlay_rows.preview = color_overlay_color_preview;
  color_overlay_rows.picker = color_overlay_pick_color;
  color_overlay_rows.row_container = color_overlay_color_row;
  color_overlay_rows.update_preview();
  color_overlay_form->addRow(QObject::tr("Color RGB"), color_overlay_color_row);
  color_overlay_layout->addWidget(color_overlay_group);
  color_overlay_layout->addStretch(1);

  auto* gradient_group = new QGroupBox(QObject::tr("Gradient Overlay"), controls);
  auto* gradient_form = new QFormLayout(gradient_group);
  auto* gradient_blend = new QComboBox(gradient_group);
  gradient_blend->setObjectName(QStringLiteral("layerStyleGradientBlendModeCombo"));
  add_blend_mode_items(gradient_blend);
  gradient_blend->setCurrentIndex(std::max(0, gradient_blend->findData(static_cast<int>(gradient.blend_mode))));
  gradient_form->addRow(QObject::tr("Blend Mode"), gradient_blend);
  auto* gradient_opacity =
      add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleGradientOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(gradient.opacity * 100.0F)), QStringLiteral("%"));
  auto gradient_controls =
      make_gradient_controls(gradient_form, gradient_group, QStringLiteral("layerStyleGradient"),
                             gradient.gradient, QObject::tr("Choose Gradient Stop Color"));
  gradient_layout->addWidget(gradient_group);
  gradient_layout->addStretch(1);

  auto* outer_glow_group = new QGroupBox(QObject::tr("Outer Glow"), controls);
  auto* outer_glow_form = new QFormLayout(outer_glow_group);
  auto* outer_glow_blend = new QComboBox(outer_glow_group);
  outer_glow_blend->setObjectName(QStringLiteral("layerStyleOuterGlowBlendModeCombo"));
  add_blend_mode_items(outer_glow_blend);
  outer_glow_blend->setCurrentIndex(std::max(0, outer_glow_blend->findData(static_cast<int>(outer_glow.blend_mode))));
  outer_glow_form->addRow(QObject::tr("Blend Mode"), outer_glow_blend);
  auto* outer_glow_technique = new QComboBox(outer_glow_group);
  outer_glow_technique->setObjectName(QStringLiteral("layerStyleOuterGlowTechniqueCombo"));
  outer_glow_technique->addItem(QObject::tr("Softer"), static_cast<int>(LayerGlowTechnique::Softer));
  outer_glow_technique->addItem(QObject::tr("Precise"), static_cast<int>(LayerGlowTechnique::Precise));
  outer_glow_technique->setCurrentIndex(
      std::max(0, outer_glow_technique->findData(static_cast<int>(outer_glow.technique))));
  outer_glow_form->addRow(QObject::tr("Technique"), outer_glow_technique);
  auto* outer_glow_opacity =
      add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleOuterGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(outer_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* outer_glow_size = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleOuterGlowSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(outer_glow.size)));
  auto* outer_glow_spread = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Spread"),
                                                QStringLiteral("layerStyleOuterGlowSpreadSpin"), 0, 100,
                                                static_cast<int>(std::round(outer_glow.spread)),
                                                QStringLiteral("%"));
  auto* outer_glow_range = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Range"),
                                               QStringLiteral("layerStyleOuterGlowRangeSpin"), 1, 100,
                                               static_cast<int>(std::round(outer_glow.range)),
                                               QStringLiteral("%"));
  const auto outer_glow_rows = make_color_rows(outer_glow_form, outer_glow_group,
                                               QStringLiteral("layerStyleOuterGlow"), outer_glow.color);
  auto* outer_glow_red = outer_glow_rows.red;
  auto* outer_glow_green = outer_glow_rows.green;
  auto* outer_glow_blue = outer_glow_rows.blue;
  outer_glow_layout->addWidget(outer_glow_group);
  outer_glow_layout->addStretch(1);

  auto* shadow_group = new QGroupBox(QObject::tr("Drop Shadow"), controls);
  auto* shadow_form = new QFormLayout(shadow_group);
  auto* shadow_blend = new QComboBox(shadow_group);
  shadow_blend->setObjectName(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
  add_blend_mode_items(shadow_blend);
  shadow_blend->setCurrentIndex(std::max(0, shadow_blend->findData(static_cast<int>(shadow.blend_mode))));
  shadow_form->addRow(QObject::tr("Blend Mode"), shadow_blend);
  auto* shadow_opacity = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleDropShadowOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(shadow.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* shadow_angle = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Angle"),
                                           QStringLiteral("layerStyleDropShadowAngleSpin"), -180, 180,
                                           static_cast<int>(std::round(shadow.angle_degrees)));
  auto* shadow_distance = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Distance"),
                                              QStringLiteral("layerStyleDropShadowDistanceSpin"), 0, 1000,
                                              static_cast<int>(std::round(shadow.distance)));
  auto* shadow_size = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleDropShadowSizeSpin"), 0, 1000,
                                          static_cast<int>(std::round(shadow.size)));
  auto* shadow_spread = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Spread"),
                                            QStringLiteral("layerStyleDropShadowSpreadSpin"), 0, 100,
                                            static_cast<int>(std::round(shadow.spread)), QStringLiteral("%"));
  const auto shadow_rows =
      make_color_rows(shadow_form, shadow_group, QStringLiteral("layerStyleDropShadow"), shadow.color);
  auto* shadow_red = shadow_rows.red;
  auto* shadow_green = shadow_rows.green;
  auto* shadow_blue = shadow_rows.blue;
  shadow_layout->addWidget(shadow_group);
  shadow_layout->addStretch(1);

  auto item_page = [](const QListWidgetItem* item) {
    return item == nullptr ? LayerStyleCategoryPage::Blending
                           : static_cast<LayerStyleCategoryPage>(item->data(kLayerStylePageRole).toInt());
  };
  auto item_kind = [](const QListWidgetItem* item) {
    return item == nullptr ? LayerStyleEffectKind::None
                           : static_cast<LayerStyleEffectKind>(item->data(kLayerStyleEffectKindRole).toInt());
  };
  auto item_effect_index = [](const QListWidgetItem* item) {
    return item == nullptr ? -1 : item->data(kLayerStyleEffectIndexRole).toInt();
  };
  auto item_checked = [](const QListWidgetItem* item) {
    return item != nullptr && item->checkState() == Qt::Checked;
  };
  auto set_combo_data = [](QComboBox* combo, int data) {
    combo->setCurrentIndex(std::max(0, combo->findData(data)));
  };
  auto indexed_object_name = [](const QString& base_name, int index) {
    return index <= 0 ? base_name : base_name + QString::number(index + 1);
  };
  auto is_stackable_kind = [](LayerStyleEffectKind kind) {
    return effect_vector_ops_for_kind(kind) != nullptr;
  };
  auto ensure_bevel = [](LayerStyle& target, int index) -> LayerBevelEmboss& {
    return ensure_effect(target.bevels, index, default_bevel_emboss);
  };
  auto apply_enabled_states = [&](LayerStyle& result) {
    for (int row = 0; row < categories->count(); ++row) {
      auto* category = categories->item(row);
      const auto enabled = item_checked(category);
      const auto index = item_effect_index(category);
      switch (item_kind(category)) {
        case LayerStyleEffectKind::BevelEmboss:
          if (enabled || !result.bevels.empty()) {
            ensure_bevel(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::Stroke:
          if (enabled || result.strokes.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.strokes, index, default_stroke).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::InnerShadow:
          if (enabled || result.inner_shadows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.inner_shadows, index, default_inner_shadow).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::InnerGlow:
          if (enabled || result.inner_glows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.inner_glows, index, default_inner_glow).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::Satin:
          if (enabled || result.satins.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.satins, index, default_satin).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::ColorOverlay:
          if (enabled || result.color_overlays.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.color_overlays, index, default_color_overlay).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::GradientOverlay:
          if (enabled || result.gradient_fills.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.gradient_fills, index, default_gradient_fill).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::PatternOverlay:
          if (enabled || result.pattern_overlays.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.pattern_overlays, index, default_pattern_overlay).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::OuterGlow:
          if (enabled || result.outer_glows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.outer_glows, index, default_outer_glow).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::DropShadow:
          if (enabled || result.drop_shadows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_effect(result.drop_shadows, index, default_drop_shadow).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::BevelContour:
          // The sub-option checkbox never force-enables the bevel itself: a
          // bevel created just to carry the flag stays disabled (PS behavior).
          if (enabled || !result.bevels.empty()) {
            const auto had_bevel = !result.bevels.empty();
            auto& target = ensure_bevel(result, 0);
            if (!had_bevel) {
              target.enabled = false;
            }
            target.contour.enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::BevelTexture:
          if (enabled || !result.bevels.empty()) {
            const auto had_bevel = !result.bevels.empty();
            auto& target = ensure_bevel(result, 0);
            if (!had_bevel) {
              target.enabled = false;
            }
            target.texture.enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::None:
          break;
      }
    }
  };
  auto save_controls_to_style = [&](LayerStyle& result, const QListWidgetItem* category) {
    const auto index = item_effect_index(category);
    if (index < 0) {
      return;
    }
    const auto enabled = item_checked(category);
    switch (item_kind(category)) {
      case LayerStyleEffectKind::BevelEmboss: {
        if (!enabled && result.bevels.empty()) {
          return;
        }
        auto& target = ensure_bevel(result, index);
        target.enabled = enabled;
        target.style = static_cast<BevelEmbossStyleKind>(bevel_style->currentData().toInt());
        target.technique = static_cast<BevelTechnique>(bevel_technique->currentData().toInt());
        target.size = static_cast<float>(bevel_size->value());
        target.soften = static_cast<float>(bevel_soften->value());
        target.depth = static_cast<float>(bevel_depth->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(bevel_angle->value());
        target.altitude_degrees = static_cast<float>(bevel_altitude->value());
        target.direction_up = bevel_direction->currentData().toBool();
        target.highlight_blend_mode =
            static_cast<BlendMode>(bevel_highlight_blend->currentData().toInt());
        target.highlight_color = bevel_highlight_color;
        target.highlight_opacity = static_cast<float>(bevel_highlight_opacity->value()) / 100.0F;
        target.shadow_blend_mode = static_cast<BlendMode>(bevel_shadow_blend->currentData().toInt());
        target.shadow_color = bevel_shadow_color;
        target.shadow_opacity = static_cast<float>(bevel_shadow_opacity->value()) / 100.0F;
        target.gloss_contour = contour_combo_value(bevel_gloss_contour, custom_gloss_contour);
        target.gloss_anti_aliased = bevel_gloss_anti_aliased->isChecked();
        break;
      }
      case LayerStyleEffectKind::BevelContour: {
        if (!enabled && result.bevels.empty()) {
          return;
        }
        const auto had_bevel = !result.bevels.empty();
        auto& target = ensure_bevel(result, 0);
        if (!had_bevel) {
          target.enabled = false;
        }
        target.contour.enabled = enabled;
        target.contour.contour = contour_combo_value(bevel_contour_combo, custom_bevel_sub_contour);
        target.contour.anti_aliased = bevel_contour_anti_aliased->isChecked();
        target.contour.range = static_cast<float>(bevel_contour_range->value()) / 100.0F;
        break;
      }
      case LayerStyleEffectKind::BevelTexture: {
        if (!enabled && result.bevels.empty()) {
          return;
        }
        const auto had_bevel = !result.bevels.empty();
        auto& target = ensure_bevel(result, 0);
        if (!had_bevel) {
          target.enabled = false;
        }
        target.texture.enabled = enabled;
        target.texture.pattern_id = pattern_combo_id(bevel_texture_pattern);
        target.texture.pattern_name = pattern_combo_persist_name(bevel_texture_pattern);
        target.texture.scale = static_cast<float>(bevel_texture_scale->value()) / 100.0F;
        target.texture.depth = static_cast<float>(bevel_texture_depth->value()) / 100.0F;
        target.texture.invert = bevel_texture_invert->isChecked();
        target.texture.link_with_layer = bevel_texture_link->isChecked();
        target.texture.phase_x = bevel.texture.phase_x;
        target.texture.phase_y = bevel.texture.phase_y;
        break;
      }
      case LayerStyleEffectKind::Stroke: {
        if (!enabled && result.strokes.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.strokes, index, default_stroke);
        target.enabled = enabled;
        target.size = static_cast<float>(stroke_size->value());
        target.blend_mode = static_cast<BlendMode>(stroke_blend->currentData().toInt());
        target.opacity = static_cast<float>(stroke_opacity->value()) / 100.0F;
        target.color = RgbColor{static_cast<std::uint8_t>(stroke_red->value()),
                                static_cast<std::uint8_t>(stroke_green->value()),
                                static_cast<std::uint8_t>(stroke_blue->value())};
        target.position = static_cast<LayerStrokePosition>(stroke_position->currentData().toInt());
        target.uses_gradient = stroke_fill->currentData().toBool();
        target.gradient = stroke_gradient_controls->value();
        break;
      }
      case LayerStyleEffectKind::InnerShadow: {
        if (!enabled && result.inner_shadows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.inner_shadows, index, default_inner_shadow);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(inner_shadow_blend->currentData().toInt());
        target.opacity = static_cast<float>(inner_shadow_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(inner_shadow_angle->value());
        target.distance = static_cast<float>(inner_shadow_distance->value());
        target.size = static_cast<float>(inner_shadow_size->value());
        target.choke = static_cast<float>(inner_shadow_choke->value());
        target.color = RgbColor{static_cast<std::uint8_t>(inner_shadow_red->value()),
                                static_cast<std::uint8_t>(inner_shadow_green->value()),
                                static_cast<std::uint8_t>(inner_shadow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::InnerGlow: {
        if (!enabled && result.inner_glows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.inner_glows, index, default_inner_glow);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(inner_glow_blend->currentData().toInt());
        target.opacity = static_cast<float>(inner_glow_opacity->value()) / 100.0F;
        target.size = static_cast<float>(inner_glow_size->value());
        target.choke = static_cast<float>(inner_glow_choke->value());
        target.source = static_cast<LayerInnerGlowSource>(inner_glow_source->currentData().toInt());
        target.color = RgbColor{static_cast<std::uint8_t>(inner_glow_red->value()),
                                static_cast<std::uint8_t>(inner_glow_green->value()),
                                static_cast<std::uint8_t>(inner_glow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::Satin: {
        if (!enabled && result.satins.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.satins, index, default_satin);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(satin_blend->currentData().toInt());
        target.opacity = static_cast<float>(satin_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(satin_angle->value());
        target.distance = static_cast<float>(satin_distance->value());
        target.size = static_cast<float>(satin_size->value());
        target.invert = satin_invert->isChecked();
        target.color = RgbColor{static_cast<std::uint8_t>(satin_red->value()),
                                static_cast<std::uint8_t>(satin_green->value()),
                                static_cast<std::uint8_t>(satin_blue->value())};
        break;
      }
      case LayerStyleEffectKind::ColorOverlay: {
        if (!enabled && result.color_overlays.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.color_overlays, index, default_color_overlay);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(color_overlay_blend->currentData().toInt());
        target.opacity = static_cast<float>(color_overlay_opacity->value()) / 100.0F;
        target.color = RgbColor{static_cast<std::uint8_t>(color_overlay_red->value()),
                                static_cast<std::uint8_t>(color_overlay_green->value()),
                                static_cast<std::uint8_t>(color_overlay_blue->value())};
        break;
      }
      case LayerStyleEffectKind::GradientOverlay: {
        if (!enabled && result.gradient_fills.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.gradient_fills, index, default_gradient_fill);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(gradient_blend->currentData().toInt());
        target.opacity = static_cast<float>(gradient_opacity->value()) / 100.0F;
        target.gradient = gradient_controls->value();
        break;
      }
      case LayerStyleEffectKind::PatternOverlay: {
        if (!enabled && result.pattern_overlays.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.pattern_overlays, index, default_pattern_overlay);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(pattern_overlay_blend->currentData().toInt());
        target.opacity = static_cast<float>(pattern_overlay_opacity->value()) / 100.0F;
        target.pattern_id = pattern_combo_id(pattern_overlay_pattern);
        target.pattern_name = pattern_combo_persist_name(pattern_overlay_pattern);
        target.angle_degrees = static_cast<float>(pattern_overlay_angle->value());
        target.scale = static_cast<float>(pattern_overlay_scale->value()) / 100.0F;
        target.link_with_layer = pattern_overlay_link->isChecked();
        target.phase_x = pattern_overlay.phase_x;
        target.phase_y = pattern_overlay.phase_y;
        break;
      }
      case LayerStyleEffectKind::OuterGlow: {
        if (!enabled && result.outer_glows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.outer_glows, index, default_outer_glow);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(outer_glow_blend->currentData().toInt());
        target.technique = static_cast<LayerGlowTechnique>(outer_glow_technique->currentData().toInt());
        target.opacity = static_cast<float>(outer_glow_opacity->value()) / 100.0F;
        target.size = static_cast<float>(outer_glow_size->value());
        target.spread = static_cast<float>(outer_glow_spread->value());
        target.range = static_cast<float>(outer_glow_range->value());
        target.color = RgbColor{static_cast<std::uint8_t>(outer_glow_red->value()),
                                static_cast<std::uint8_t>(outer_glow_green->value()),
                                static_cast<std::uint8_t>(outer_glow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::DropShadow: {
        if (!enabled && result.drop_shadows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_effect(result.drop_shadows, index, default_drop_shadow);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(shadow_blend->currentData().toInt());
        target.opacity = static_cast<float>(shadow_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(shadow_angle->value());
        target.distance = static_cast<float>(shadow_distance->value());
        target.size = static_cast<float>(shadow_size->value());
        target.spread = static_cast<float>(shadow_spread->value());
        target.color = RgbColor{static_cast<std::uint8_t>(shadow_red->value()),
                                static_cast<std::uint8_t>(shadow_green->value()),
                                static_cast<std::uint8_t>(shadow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::None:
        break;
    }
  };

  bool loading_controls = false;
  bool rebuilding_categories = false;
  std::function<LayerStyleSettings(const QListWidgetItem*)> build_current_settings_for_item;
  std::function<void(bool)> emit_preview;
  std::function<void(const QListWidgetItem*)> load_controls_from_style;
  std::function<void(LayerStyleEffectKind, int)> rebuild_category_list;

  build_current_settings_for_item = [&](const QListWidgetItem* category) {
    LayerStyle result = style;
    result.effects_visible = show_effects->isChecked();
    result.layer_mask_hides_effects = mask_hides_effects->isChecked();
    apply_enabled_states(result);
    save_controls_to_style(result, category);
    // Accepting any style edit regenerates the complete native lfx2 descriptor.
    // Every modeled Satin is therefore written with the Linear contour, even
    // when a different effect page is active.
    for (auto& satin : result.satins) {
      satin.unsupported_contour_options = false;
    }
    return LayerStyleSettings{opacity->value(), static_cast<BlendMode>(blend->currentData().toInt()),
                              std::move(result), blend_if, replace_unsupported_blend_if,
                              fill_opacity->value()};
  };
  auto build_current_settings = [&]() {
    return build_current_settings_for_item(categories->currentItem());
  };

  load_controls_from_style = [&](const QListWidgetItem* category) {
    loading_controls = true;
    controls->setCurrentIndex(static_cast<int>(item_page(category)));
    const auto kind = item_kind(category);
    const auto index = std::max(0, item_effect_index(category));
    const auto* stack_ops = effect_vector_ops_for_kind(kind);
    remove_selected_instance->setEnabled(stack_ops != nullptr && stack_ops->count(style) > 0U);

    switch (kind) {
      case LayerStyleEffectKind::BevelEmboss: {
        const auto value = style.bevels.size() > static_cast<std::size_t>(index) ? style.bevels[static_cast<std::size_t>(index)]
                                                                                 : default_bevel_emboss();
        set_combo_data(bevel_style, static_cast<int>(value.style));
        set_combo_data(bevel_technique, static_cast<int>(value.technique));
        bevel_size->setValue(static_cast<int>(std::round(value.size)));
        bevel_soften->setValue(static_cast<int>(std::round(value.soften)));
        bevel_depth->setValue(static_cast<int>(std::round(value.depth * 100.0F)));
        bevel_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        bevel_altitude->setValue(static_cast<int>(std::round(value.altitude_degrees)));
        bevel_direction->setCurrentIndex(value.direction_up ? 0 : 1);
        set_combo_data(bevel_highlight_blend, static_cast<int>(value.highlight_blend_mode));
        bevel_highlight_color = value.highlight_color;
        bevel_highlight_opacity->setValue(static_cast<int>(std::round(value.highlight_opacity * 100.0F)));
        set_combo_data(bevel_shadow_blend, static_cast<int>(value.shadow_blend_mode));
        bevel_shadow_color = value.shadow_color;
        bevel_shadow_opacity->setValue(static_cast<int>(std::round(value.shadow_opacity * 100.0F)));
        if (find_builtin_contour_preset(value.gloss_contour) == nullptr) {
          custom_gloss_contour = value.gloss_contour;
        }
        set_contour_combo_value(bevel_gloss_contour, value.gloss_contour);
        bevel_gloss_anti_aliased->setChecked(value.gloss_anti_aliased);
        update_bevel_color_previews();
        break;
      }
      case LayerStyleEffectKind::BevelContour: {
        const auto value = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
        if (find_builtin_contour_preset(value.contour.contour) == nullptr) {
          custom_bevel_sub_contour = value.contour.contour;
        }
        set_contour_combo_value(bevel_contour_combo, value.contour.contour);
        bevel_contour_anti_aliased->setChecked(value.contour.anti_aliased);
        bevel_contour_range->setValue(
            std::clamp(static_cast<int>(std::round(value.contour.range * 100.0F)), 1, 100));
        break;
      }
      case LayerStyleEffectKind::BevelTexture: {
        const auto value = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
        set_pattern_combo_id(bevel_texture_pattern, value.texture.pattern_id);
        bevel_texture_scale->setValue(
            std::clamp(static_cast<int>(std::round(value.texture.scale * 100.0F)), 1, 1000));
        bevel_texture_depth->setValue(
            std::clamp(static_cast<int>(std::round(value.texture.depth * 100.0F)), -1000, 1000));
        bevel_texture_invert->setChecked(value.texture.invert);
        bevel_texture_link->setChecked(value.texture.link_with_layer);
        bevel.texture.phase_x = value.texture.phase_x;
        bevel.texture.phase_y = value.texture.phase_y;
        break;
      }
      case LayerStyleEffectKind::Stroke: {
        const auto value =
            style.strokes.size() > static_cast<std::size_t>(index) ? style.strokes[static_cast<std::size_t>(index)]
                                                                   : default_stroke();
        stroke_size->setValue(static_cast<int>(std::round(value.size)));
        set_combo_data(stroke_blend, static_cast<int>(value.blend_mode));
        stroke_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        stroke_red->setValue(value.color.red);
        stroke_green->setValue(value.color.green);
        stroke_blue->setValue(value.color.blue);
        set_combo_data(stroke_position, static_cast<int>(value.position));
        stroke_fill->setCurrentIndex(value.uses_gradient ? 1 : 0);
        stroke_gradient_controls->load(value.gradient);
        update_stroke_fill_visibility();
        break;
      }
      case LayerStyleEffectKind::InnerShadow: {
        const auto value = style.inner_shadows.size() > static_cast<std::size_t>(index)
                               ? style.inner_shadows[static_cast<std::size_t>(index)]
                               : default_inner_shadow();
        set_combo_data(inner_shadow_blend, static_cast<int>(value.blend_mode));
        inner_shadow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        inner_shadow_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        inner_shadow_distance->setValue(static_cast<int>(std::round(value.distance)));
        inner_shadow_size->setValue(static_cast<int>(std::round(value.size)));
        inner_shadow_choke->setValue(static_cast<int>(std::round(value.choke)));
        inner_shadow_red->setValue(value.color.red);
        inner_shadow_green->setValue(value.color.green);
        inner_shadow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::InnerGlow: {
        const auto value = style.inner_glows.size() > static_cast<std::size_t>(index)
                               ? style.inner_glows[static_cast<std::size_t>(index)]
                               : default_inner_glow();
        set_combo_data(inner_glow_blend, static_cast<int>(value.blend_mode));
        inner_glow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        inner_glow_size->setValue(static_cast<int>(std::round(value.size)));
        inner_glow_choke->setValue(static_cast<int>(std::round(value.choke)));
        set_combo_data(inner_glow_source, static_cast<int>(value.source));
        inner_glow_red->setValue(value.color.red);
        inner_glow_green->setValue(value.color.green);
        inner_glow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::Satin: {
        const auto value = style.satins.size() > static_cast<std::size_t>(index)
                               ? style.satins[static_cast<std::size_t>(index)]
                               : default_satin();
        set_combo_data(satin_blend, static_cast<int>(value.blend_mode));
        satin_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        satin_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        satin_distance->setValue(static_cast<int>(std::round(value.distance)));
        satin_size->setValue(static_cast<int>(std::round(value.size)));
        satin_invert->setChecked(value.invert);
        satin_red->setValue(value.color.red);
        satin_green->setValue(value.color.green);
        satin_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::ColorOverlay: {
        const auto value = style.color_overlays.size() > static_cast<std::size_t>(index)
                               ? style.color_overlays[static_cast<std::size_t>(index)]
                               : default_color_overlay();
        set_combo_data(color_overlay_blend, static_cast<int>(value.blend_mode));
        color_overlay_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        color_overlay_red->setValue(value.color.red);
        color_overlay_green->setValue(value.color.green);
        color_overlay_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::GradientOverlay: {
        auto value = style.gradient_fills.size() > static_cast<std::size_t>(index)
                         ? style.gradient_fills[static_cast<std::size_t>(index)]
                         : default_gradient_fill();
        if (value.gradient.color_stops.empty()) {
          value.gradient = default_layer_style_gradient();
        }
        set_combo_data(gradient_blend, static_cast<int>(value.blend_mode));
        gradient_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        gradient_controls->load(value.gradient);
        break;
      }
      case LayerStyleEffectKind::PatternOverlay: {
        const auto value = style.pattern_overlays.size() > static_cast<std::size_t>(index)
                               ? style.pattern_overlays[static_cast<std::size_t>(index)]
                               : default_pattern_overlay();
        pattern_overlay = value;
        set_combo_data(pattern_overlay_blend, static_cast<int>(value.blend_mode));
        pattern_overlay_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        set_pattern_combo_id(pattern_overlay_pattern, value.pattern_id);
        pattern_overlay_angle->setValue(
            std::clamp(static_cast<int>(std::round(value.angle_degrees)), -180, 180));
        pattern_overlay_scale->setValue(
            std::clamp(static_cast<int>(std::round(value.scale * 100.0F)), 1, 1000));
        pattern_overlay_link->setChecked(value.link_with_layer);
        break;
      }
      case LayerStyleEffectKind::OuterGlow: {
        const auto value =
            style.outer_glows.size() > static_cast<std::size_t>(index) ? style.outer_glows[static_cast<std::size_t>(index)]
                                                                       : default_outer_glow();
        set_combo_data(outer_glow_blend, static_cast<int>(value.blend_mode));
        set_combo_data(outer_glow_technique, static_cast<int>(value.technique));
        outer_glow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        outer_glow_size->setValue(static_cast<int>(std::round(value.size)));
        outer_glow_spread->setValue(static_cast<int>(std::round(value.spread)));
        outer_glow_range->setValue(std::clamp(static_cast<int>(std::round(value.range)), 1, 100));
        outer_glow_red->setValue(value.color.red);
        outer_glow_green->setValue(value.color.green);
        outer_glow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::DropShadow: {
        const auto value = style.drop_shadows.size() > static_cast<std::size_t>(index)
                               ? style.drop_shadows[static_cast<std::size_t>(index)]
                               : default_drop_shadow();
        set_combo_data(shadow_blend, static_cast<int>(value.blend_mode));
        shadow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        shadow_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        shadow_distance->setValue(static_cast<int>(std::round(value.distance)));
        shadow_size->setValue(static_cast<int>(std::round(value.size)));
        shadow_spread->setValue(static_cast<int>(std::round(value.spread)));
        shadow_red->setValue(value.color.red);
        shadow_green->setValue(value.color.green);
        shadow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::None:
        break;
    }

    stroke_rows.update_preview();
    color_overlay_rows.update_preview();
    gradient_controls->update_previews();
    stroke_gradient_controls->update_previews();
    outer_glow_rows.update_preview();
    inner_glow_rows.update_preview();
    satin_rows.update_preview();
    shadow_rows.update_preview();
    inner_shadow_rows.update_preview();
    loading_controls = false;
  };

  auto add_effect_instance = [&](LayerStyleEffectKind kind, int source_index) {
    const auto* ops = effect_vector_ops_for_kind(kind);
    return ops != nullptr ? ops->add_instance(style, source_index) : 0;
  };

  auto remove_effect_instance = [&](LayerStyleEffectKind kind, int index) {
    const auto* ops = effect_vector_ops_for_kind(kind);
    return ops != nullptr ? ops->remove_instance(style, index) : 0;
  };

  auto category_check_object_name = [&](LayerStyleEffectKind kind, int index) {
    switch (kind) {
      case LayerStyleEffectKind::BevelEmboss:
        return indexed_object_name(QStringLiteral("layerStyleBevelEmbossCategoryCheck"), index);
      case LayerStyleEffectKind::Stroke:
        return indexed_object_name(QStringLiteral("layerStyleStrokeCategoryCheck"), index);
      case LayerStyleEffectKind::InnerShadow:
        return indexed_object_name(QStringLiteral("layerStyleInnerShadowCategoryCheck"), index);
      case LayerStyleEffectKind::InnerGlow:
        return indexed_object_name(QStringLiteral("layerStyleInnerGlowCategoryCheck"), index);
      case LayerStyleEffectKind::Satin:
        return indexed_object_name(QStringLiteral("layerStyleSatinCategoryCheck"), index);
      case LayerStyleEffectKind::ColorOverlay:
        return indexed_object_name(QStringLiteral("layerStyleColorOverlayCategoryCheck"), index);
      case LayerStyleEffectKind::GradientOverlay:
        return indexed_object_name(QStringLiteral("layerStyleGradientOverlayCategoryCheck"), index);
      case LayerStyleEffectKind::PatternOverlay:
        return indexed_object_name(QStringLiteral("layerStylePatternOverlayCategoryCheck"), index);
      case LayerStyleEffectKind::OuterGlow:
        return indexed_object_name(QStringLiteral("layerStyleOuterGlowCategoryCheck"), index);
      case LayerStyleEffectKind::DropShadow:
        return indexed_object_name(QStringLiteral("layerStyleDropShadowCategoryCheck"), index);
      case LayerStyleEffectKind::BevelContour:
        return indexed_object_name(QStringLiteral("layerStyleBevelContourCategoryCheck"), index);
      case LayerStyleEffectKind::BevelTexture:
        return indexed_object_name(QStringLiteral("layerStyleBevelTextureCategoryCheck"), index);
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };
  auto add_button_object_name = [&](LayerStyleEffectKind kind, int index) {
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return indexed_object_name(QStringLiteral("layerStyleAddStrokeInstanceButton"), index);
      case LayerStyleEffectKind::InnerShadow:
        return indexed_object_name(QStringLiteral("layerStyleAddInnerShadowInstanceButton"), index);
      case LayerStyleEffectKind::InnerGlow:
        return indexed_object_name(QStringLiteral("layerStyleAddInnerGlowInstanceButton"), index);
      case LayerStyleEffectKind::Satin:
        return indexed_object_name(QStringLiteral("layerStyleAddSatinInstanceButton"), index);
      case LayerStyleEffectKind::ColorOverlay:
        return indexed_object_name(QStringLiteral("layerStyleAddColorOverlayInstanceButton"), index);
      case LayerStyleEffectKind::GradientOverlay:
        return indexed_object_name(QStringLiteral("layerStyleAddGradientOverlayInstanceButton"), index);
      case LayerStyleEffectKind::PatternOverlay:
        return indexed_object_name(QStringLiteral("layerStyleAddPatternOverlayInstanceButton"), index);
      case LayerStyleEffectKind::OuterGlow:
        return indexed_object_name(QStringLiteral("layerStyleAddOuterGlowInstanceButton"), index);
      case LayerStyleEffectKind::DropShadow:
        return indexed_object_name(QStringLiteral("layerStyleAddDropShadowInstanceButton"), index);
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::BevelContour:
      case LayerStyleEffectKind::BevelTexture:
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };
  auto add_button_tooltip = [](LayerStyleEffectKind kind) {
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return QObject::tr("Add Stroke");
      case LayerStyleEffectKind::InnerShadow:
        return QObject::tr("Add Inner Shadow");
      case LayerStyleEffectKind::InnerGlow:
        return QObject::tr("Add Inner Glow");
      case LayerStyleEffectKind::Satin:
        return QObject::tr("Add Satin");
      case LayerStyleEffectKind::ColorOverlay:
        return QObject::tr("Add Color Overlay");
      case LayerStyleEffectKind::GradientOverlay:
        return QObject::tr("Add Gradient Overlay");
      case LayerStyleEffectKind::PatternOverlay:
        return QObject::tr("Add Pattern Overlay");
      case LayerStyleEffectKind::OuterGlow:
        return QObject::tr("Add Outer Glow");
      case LayerStyleEffectKind::DropShadow:
        return QObject::tr("Add Drop Shadow");
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::BevelContour:
      case LayerStyleEffectKind::BevelTexture:
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };

  // The row widgets are opaque and cover their QListWidget items completely, so
  // the built-in
  // ::item:selected background would only peek out around the widget edges.
  // Selection is therefore painted on the row widgets themselves, like the
  // layers panel does.
  auto restyle_category_rows = [categories] {
    for (int row = 0; row < categories->count(); ++row) {
      auto* item = categories->item(row);
      auto* row_widget = categories->itemWidget(item);
      if (row_widget == nullptr) {
        continue;
      }
      // The QLabel/QCheckBox backgrounds must be explicitly transparent: once
      // the row's stylesheet applies to them, they would otherwise fill with
      // the inherited palette.
      row_widget->setStyleSheet(
          item->isSelected()
              ? QStringLiteral("QWidget#layerStyleCategoryRow { background: "
                               "#2d4c6d; border: 1px solid #4f91ca; }"
                               "QWidget#layerStyleCategoryRow QLabel {"
                               " background: transparent; color: #ffffff; "
                               "font-weight: 600; }"
                               "QWidget#layerStyleCategoryRow QCheckBox { "
                               "background: transparent; }")
              : QStringLiteral("QWidget#layerStyleCategoryRow { background: #2b2b2b;"
                               " border: 0; border-bottom: 1px solid #3b3b3b; }"
                    "QWidget#layerStyleCategoryRow QLabel { background: "
                    "transparent; color: #e6e6e6; }"
                    "QWidget#layerStyleCategoryRow QCheckBox { background: "
                    "transparent; }"));
    }
  };

  rebuild_category_list = [&](LayerStyleEffectKind select_kind, int select_index) {
    rebuilding_categories = true;
    const QSignalBlocker blocker(categories);
    for (int row = 0; row < categories->count(); ++row) {
      auto* item = categories->item(row);
      auto* row_widget = categories->itemWidget(item);
      if (row_widget == nullptr) {
        continue;
      }
      categories->removeItemWidget(item);
      delete row_widget;
    }
    categories->clear();
    int selected_row = 0;

    auto install_category_widget = [&](QListWidgetItem* item, const QString& check_object_name,
                                       bool indented = false) {
      auto* row = new QWidget(categories);
      row->setObjectName(QStringLiteral("layerStyleCategoryRow"));
      row->setAttribute(Qt::WA_StyledBackground, true);
      row->setMinimumHeight(30);
      auto* layout = new QHBoxLayout(row);
      // Sub-option rows (Bevel's Contour/Texture) indent under their parent.
      layout->setContentsMargins(indented ? 28 : 10, 0, 10, 0);
      layout->setSpacing(4);
      QCheckBox* check = nullptr;
      if (!check_object_name.isEmpty()) {
        // The checkbox holds only the indicator; the name lives in a separate
        // label so clicking the name selects the row without toggling the
        // effect on/off.
        check = new QCheckBox(row);
        check->setObjectName(check_object_name);
        check->setChecked(item->checkState() == Qt::Checked);
        check->setMinimumHeight(24);
        layout->addWidget(check, 0, Qt::AlignVCenter);
      }
      auto* label = new QLabel(item->text(), row);
      label->setMinimumHeight(24);
      label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      layout->addWidget(label, 1, Qt::AlignVCenter);
      const auto kind = item_kind(item);
      if (is_stackable_kind(kind)) {
        auto* add_instance = new QPushButton(QStringLiteral("+"), row);
        add_instance->setObjectName(add_button_object_name(kind, item_effect_index(item)));
        add_instance->setToolTip(add_button_tooltip(kind));
        configure_compact_symbol_button(add_instance);
        layout->addWidget(add_instance, 0, Qt::AlignVCenter);
        QObject::connect(add_instance, &QPushButton::clicked, &dialog, [&, item, kind] {
          const auto pending_style = build_current_settings_for_item(categories->currentItem()).style;
          const auto source_index = item_effect_index(item);
          QTimer::singleShot(0, &dialog, [&, pending_style, kind, source_index] {
            style = pending_style;
            const auto new_index = add_effect_instance(kind, source_index);
            rebuild_category_list(kind, new_index);
            load_controls_from_style(categories->currentItem());
            emit_preview(true);
          });
        });
      }
      item->setSizeHint(QSize(0, 30));
      categories->setItemWidget(item, row);
      if (check != nullptr) {
        QObject::connect(check, &QCheckBox::toggled, &dialog, [categories, item](bool checked) {
          item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
          categories->setCurrentItem(item);
        });
      }
    };

    auto add_row = [&](const QString& text, LayerStyleCategoryPage page, LayerStyleEffectKind kind, int index,
                       bool checked, bool checkable, bool indented = false) {
      auto* item = add_layer_style_category(categories, text, checkable, checked, page, kind, index);
      install_category_widget(item, checkable ? category_check_object_name(kind, index) : QString(), indented);
      if (kind == select_kind && index == select_index) {
        selected_row = categories->row(item);
      }
      return item;
    };

    add_row(QObject::tr("Style Presets"), LayerStyleCategoryPage::Styles, LayerStyleEffectKind::None,
            kStylesCategoryIndex, true, false);
    add_row(QObject::tr("Blending Options"), LayerStyleCategoryPage::Blending, LayerStyleEffectKind::None, -1, true, false);
    add_row(QObject::tr("Bevel & Emboss"), LayerStyleCategoryPage::BevelEmboss,
            LayerStyleEffectKind::BevelEmboss, 0, !style.bevels.empty() && style.bevels.front().enabled, true);
    add_row(QObject::tr("Contour"), LayerStyleCategoryPage::BevelContour, LayerStyleEffectKind::BevelContour, 0,
            !style.bevels.empty() && style.bevels.front().contour.enabled, true, true);
    // Photoshop presents a Texture sub-option whose pattern cannot be resolved
    // as UNCHECKED (pinned on tips.psd, July 2026: useTexture=true in lfx2 with
    // a pattern in neither the document nor the presets shows and renders as
    // texture-off), so mirror that instead of a checked-but-unrenderable row.
    add_row(QObject::tr("Texture"), LayerStyleCategoryPage::BevelTexture, LayerStyleEffectKind::BevelTexture, 0,
            !style.bevels.empty() && style.bevels.front().texture.enabled &&
                pattern_reference_resolves(style.bevels.front().texture.pattern_id),
            true, true);

    auto add_vector_rows = [&](const QString& text, LayerStyleCategoryPage page, LayerStyleEffectKind kind,
                               const auto& vector) {
      if (vector.empty()) {
        add_row(text, page, kind, 0, false, true);
        return;
      }
      for (std::size_t index = 0; index < vector.size(); ++index) {
        add_row(text, page, kind, static_cast<int>(index), vector[index].enabled, true);
      }
    };
    add_vector_rows(QObject::tr("Stroke"), LayerStyleCategoryPage::Stroke, LayerStyleEffectKind::Stroke, style.strokes);
    add_vector_rows(QObject::tr("Inner Shadow"), LayerStyleCategoryPage::InnerShadow,
                    LayerStyleEffectKind::InnerShadow, style.inner_shadows);
    add_vector_rows(QObject::tr("Inner Glow"), LayerStyleCategoryPage::InnerGlow, LayerStyleEffectKind::InnerGlow,
                    style.inner_glows);
    add_vector_rows(QObject::tr("Satin"), LayerStyleCategoryPage::Satin, LayerStyleEffectKind::Satin, style.satins);
    add_vector_rows(QObject::tr("Color Overlay"), LayerStyleCategoryPage::ColorOverlay,
                    LayerStyleEffectKind::ColorOverlay, style.color_overlays);
    add_vector_rows(QObject::tr("Gradient Overlay"), LayerStyleCategoryPage::GradientOverlay,
                    LayerStyleEffectKind::GradientOverlay, style.gradient_fills);
    add_vector_rows(QObject::tr("Pattern Overlay"), LayerStyleCategoryPage::PatternOverlay,
                    LayerStyleEffectKind::PatternOverlay, style.pattern_overlays);
    add_vector_rows(QObject::tr("Outer Glow"), LayerStyleCategoryPage::OuterGlow, LayerStyleEffectKind::OuterGlow,
                    style.outer_glows);
    add_vector_rows(QObject::tr("Drop Shadow"), LayerStyleCategoryPage::DropShadow, LayerStyleEffectKind::DropShadow,
                    style.drop_shadows);

    categories->setCurrentRow(std::clamp(selected_row, 0, std::max(0, categories->count() - 1)));
    // Signals are blocked during the rebuild, so sync the row selection styling
    // directly.
    restyle_category_rows();
    rebuilding_categories = false;
  };

  CoalescedPreviewEmitter<LayerStyleSettings> preview_emitter(
      dialog, [&](const LayerStyleSettings& settings) {
        if (document_patterns != nullptr) {
          for (const auto& resource : transient_manager_patterns) {
            document_patterns->adopt(resource);
          }
        }
        if (preview_changed) {
          preview_changed(settings);
        }
      });

  emit_preview = [&](bool immediate) {
    if (loading_controls || rebuilding_categories) {
      return;
    }
    stroke_rows.update_preview();
    update_bevel_color_previews();
    color_overlay_rows.update_preview();
    gradient_controls->update_previews();
    stroke_gradient_controls->update_previews();
    outer_glow_rows.update_preview();
    inner_glow_rows.update_preview();
    satin_rows.update_preview();
    shadow_rows.update_preview();
    inner_shadow_rows.update_preview();
    auto settings = build_current_settings();
    style = settings.style;
    auto preview_settings = preview_check->isChecked() ? settings : original_settings;
    if (immediate) {
      preview_emitter.flush(std::move(preview_settings));
    } else {
      preview_emitter.schedule(std::move(preview_settings));
    }
  };

  const auto connect_blend_if_row = [&](BlendIfRowWidgets& row, bool this_layer) {
    auto* row_ptr = &row;
    row.editor->changed = [&, row_ptr, this_layer](BlendIfThresholds thresholds, bool immediate) {
      if (loading_blend_if_controls) {
        return;
      }
      auto& ranges = blend_if.channels[current_blend_if_channel_index()];
      (this_layer ? ranges.this_layer : ranges.underlying_layer) = thresholds;
      loading_blend_if_controls = true;
      set_blend_if_row(*row_ptr, thresholds);
      loading_blend_if_controls = false;
      emit_preview(immediate);
    };
    const auto update_from_spins = [&, row_ptr, this_layer] {
      if (loading_blend_if_controls) {
        return;
      }
      const BlendIfThresholds thresholds{
          static_cast<std::uint8_t>(row_ptr->black_low.spin->value()),
          static_cast<std::uint8_t>(row_ptr->black_high.spin->value()),
          static_cast<std::uint8_t>(row_ptr->white_low.spin->value()),
          static_cast<std::uint8_t>(row_ptr->white_high.spin->value()),
      };
      auto& ranges = blend_if.channels[current_blend_if_channel_index()];
      (this_layer ? ranges.this_layer : ranges.underlying_layer) = thresholds;
      loading_blend_if_controls = true;
      set_blend_if_row(*row_ptr, thresholds);
      loading_blend_if_controls = false;
      emit_preview(false);
    };
    for (auto* spin : {row.black_low.spin, row.black_high.spin, row.white_low.spin, row.white_high.spin}) {
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [update_from_spins](int) { update_from_spins(); });
    }
  };
  connect_blend_if_row(blend_if_this, true);
  connect_blend_if_row(blend_if_underlying, false);
  QObject::connect(blend_if_channel, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { load_blend_if_controls(); });
  QObject::connect(reset_blend_if_channel, &QPushButton::clicked, &dialog, [&] {
    blend_if.channels[current_blend_if_channel_index()] = {};
    load_blend_if_controls();
    emit_preview(true);
  });
  if (replace_blend_if_button != nullptr) {
    QObject::connect(replace_blend_if_button, &QPushButton::clicked, &dialog, [&] {
      replace_unsupported_blend_if = true;
      blend_if = {};
      blend_if_group->setEnabled(true);
      replace_blend_if_button->setEnabled(false);
      if (blend_if_unsupported_warning != nullptr) {
        blend_if_unsupported_warning->setText(
            QObject::tr(
                             "The preserved Photoshop Blend If payload will be "
                             "replaced with editable RGB defaults "
                        "when you choose OK."));
      }
      load_blend_if_controls();
      emit_preview(true);
    });
  }
  gradient_controls->changed = [&emit_preview](bool immediate) { emit_preview(immediate); };
  stroke_gradient_controls->changed = [&emit_preview](bool immediate) { emit_preview(immediate); };
  QObject::connect(categories, &QListWidget::itemSelectionChanged, &dialog, restyle_category_rows);
  QObject::connect(categories, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem* current, QListWidgetItem* previous) {
                     if (loading_controls || rebuilding_categories) {
                       return;
                     }
                     if (previous != nullptr) {
                       style = build_current_settings_for_item(previous).style;
                     }
                     load_controls_from_style(current);
                     emit_preview(true);
                   });
  QObject::connect(categories, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* changed) {
    if (auto* widget = categories->itemWidget(changed); widget != nullptr) {
      if (auto* check = widget->findChild<QCheckBox*>(); check != nullptr) {
        const QSignalBlocker blocker(check);
        check->setChecked(changed->checkState() == Qt::Checked);
      }
    }
    emit_preview(true);
  });
  rebuild_category_list(LayerStyleEffectKind::None, -1);
  load_controls_from_style(categories->currentItem());
  QObject::connect(blend, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(true); });
  QObject::connect(opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                   [&emit_preview](int) { emit_preview(false); });
  QObject::connect(fill_opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                   [&emit_preview](int) { emit_preview(false); });
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(show_effects, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(mask_hides_effects, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  for (auto* spin : {bevel_size, bevel_soften, bevel_depth, bevel_angle, bevel_altitude, bevel_highlight_opacity,
                     bevel_shadow_opacity, bevel_contour_range, bevel_texture_scale, bevel_texture_depth,
                     pattern_overlay_opacity, pattern_overlay_angle, pattern_overlay_scale,
                     stroke_size, stroke_opacity, stroke_red, stroke_green, stroke_blue,
                     color_overlay_opacity, color_overlay_red, color_overlay_green, color_overlay_blue,
                     gradient_opacity, outer_glow_opacity, outer_glow_size, outer_glow_spread, outer_glow_range,
                     outer_glow_red,
                     outer_glow_green, outer_glow_blue, inner_glow_opacity, inner_glow_size, inner_glow_choke,
                     inner_glow_red, inner_glow_green, inner_glow_blue, satin_opacity, satin_angle, satin_distance,
                     satin_size, satin_red, satin_green, satin_blue, shadow_opacity, shadow_angle, shadow_distance,
                     shadow_size, shadow_spread, shadow_red,
                     shadow_green, shadow_blue, inner_shadow_opacity, inner_shadow_angle, inner_shadow_distance,
                     inner_shadow_size, inner_shadow_choke, inner_shadow_red, inner_shadow_green, inner_shadow_blue}) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [&emit_preview](int) { emit_preview(false); });
  }
  QObject::connect(bevel_direction, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_style, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_technique, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_highlight_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_gloss_contour, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_gloss_anti_aliased, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(bevel_contour_combo, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_contour_anti_aliased, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  bevel_texture_manage->setEnabled(pattern_library != nullptr);
  pattern_overlay_manage->setEnabled(pattern_library != nullptr);
  const auto open_pattern_manager = [&](QComboBox* target_combo) {
    if (pattern_library == nullptr) {
      return;
    }
    const auto bevel_id = pattern_combo_id(bevel_texture_pattern);
    const auto bevel_name = pattern_combo_persist_name(bevel_texture_pattern);
    const auto overlay_id = pattern_combo_id(pattern_overlay_pattern);
    const auto overlay_name = pattern_combo_persist_name(pattern_overlay_pattern);
    const auto selected_storage_id = request_pattern_manager(
        &dialog, *pattern_library, target_combo->currentData(kPatternIdRole).toString(),
        open_pattern_as_image);
    QString selected_pattern_id;
    QString selected_pattern_name;
    if (!selected_storage_id.isEmpty()) {
      const auto* entry = pattern_library->find_entry(selected_storage_id);
      const auto selected_resource = pattern_library->resource_for_entry(selected_storage_id);
      if (entry != nullptr && selected_resource.has_value()) {
        auto resource = *selected_resource;
        selected_pattern_name = entry->name;
        if (document_patterns != nullptr) {
          if (const auto* embedded = document_patterns->find(resource.id);
              embedded != nullptr && !pattern_tiles_equal(embedded->tile, resource.tile)) {
            // A style stores only the Photoshop id, so two different tiles with
            // the same id cannot coexist in one document. Give the selected
            // library tile a document-local id and materialize it immediately;
            // MainWindow restores this transient insertion on cancel.
            const auto prior = std::find_if(
                transient_manager_patterns.begin(), transient_manager_patterns.end(),
                [&resource](const PatternResource& candidate) {
                  return candidate.name == resource.name &&
                         pattern_tiles_equal(candidate.tile, resource.tile);
                });
            if (prior != transient_manager_patterns.end()) {
              resource = *prior;
            } else {
              do {
                resource.id = generate_pattern_uuid();
              } while (document_patterns->find(resource.id) != nullptr ||
                       pattern_library->find_entry_by_pattern_id(
                           QString::fromStdString(resource.id)) != nullptr);
              resource.provenance = PatternProvenance::Authored;
              transient_manager_patterns.push_back(resource);
            }
            document_patterns->adopt(resource);
          }
        }
        selected_pattern_id = QString::fromStdString(resource.id);
      }
    }
    populate_pattern_combo(bevel_texture_pattern, bevel_id, bevel_name);
    populate_pattern_combo(pattern_overlay_pattern, overlay_id, overlay_name);
    if (!selected_pattern_id.isEmpty()) {
      set_pattern_combo_id(target_combo, selected_pattern_id.toStdString());
      if (target_combo->currentIndex() >= 0 && !selected_pattern_name.isEmpty()) {
        target_combo->setItemData(target_combo->currentIndex(), selected_pattern_name,
                                  kPatternPersistNameRole);
      }
    }
    emit_preview(true);
    if (missing_pattern_warning != nullptr) {
      const auto current_style = build_current_settings().style;
      auto has_missing = false;
      for (const auto& effect : current_style.pattern_overlays) {
        has_missing = has_missing ||
                      (effect.enabled && !effect.pattern_id.empty() &&
                       !pattern_reference_resolves(effect.pattern_id));
      }
      for (const auto& effect : current_style.bevels) {
        has_missing = has_missing ||
                      (effect.enabled && effect.texture.enabled && !effect.texture.pattern_id.empty() &&
                       !pattern_reference_resolves(effect.texture.pattern_id));
      }
      missing_pattern_warning->setVisible(has_missing);
    }
  };
  QObject::connect(bevel_texture_manage, &QPushButton::clicked, &dialog,
                   [&] { open_pattern_manager(bevel_texture_pattern); });
  QObject::connect(pattern_overlay_manage, &QPushButton::clicked, &dialog,
                   [&] { open_pattern_manager(pattern_overlay_pattern); });

  // --- Styles page behavior -------------------------------------------------
  const auto apply_style_library_entry = [&](const QString& storage_id) {
    if (style_library == nullptr) {
      return;
    }
    const auto* entry = style_library->find_entry(storage_id);
    if (entry == nullptr) {
      return;
    }
    auto applied = entry->style;
    // Materialize the preset's pattern tiles. A library tile whose id already
    // belongs to different document pixels is re-identified (the Pattern
    // Manager collision path) and the applied style's references follow it.
    if (document_patterns != nullptr) {
      for (const auto& library_resource : style_library->patterns_for_entry(storage_id)) {
        auto resource = library_resource;
        if (const auto* embedded = document_patterns->find(resource.id);
            embedded != nullptr && !pattern_tiles_equal(embedded->tile, resource.tile)) {
          const auto original_id = resource.id;
          const auto prior = std::find_if(
              transient_manager_patterns.begin(), transient_manager_patterns.end(),
              [&resource](const PatternResource& candidate) {
                return candidate.name == resource.name &&
                       pattern_tiles_equal(candidate.tile, resource.tile);
              });
          if (prior != transient_manager_patterns.end()) {
            resource = *prior;
          } else {
            do {
              resource.id = generate_pattern_uuid();
            } while (document_patterns->find(resource.id) != nullptr ||
                     (pattern_library != nullptr &&
                      pattern_library->find_entry_by_pattern_id(
                          QString::fromStdString(resource.id)) != nullptr));
            resource.provenance = PatternProvenance::Authored;
            transient_manager_patterns.push_back(resource);
          }
          for (auto& overlay : applied.pattern_overlays) {
            if (overlay.pattern_id == original_id) {
              overlay.pattern_id = resource.id;
            }
          }
          for (auto& bevel_effect : applied.bevels) {
            if (bevel_effect.texture.pattern_id == original_id) {
              bevel_effect.texture.pattern_id = resource.id;
            }
          }
        }
        document_patterns->adopt(resource);
      }
    }
    // Presets replace the effects; the dialog-level blending pieces a preset
    // does not carry stay as the user set them.
    applied.layer_mask_hides_effects = mask_hides_effects->isChecked();
    applied.effects_visible = true;
    loading_controls = true;
    show_effects->setChecked(true);
    loading_controls = false;
    style = std::move(applied);
    if (entry->blend_settings.has_value()) {
      loading_controls = true;
      opacity->setValue(entry->blend_settings->opacity);
      fill_opacity->setValue(entry->blend_settings->fill_opacity);
      set_combo_data(blend, static_cast<int>(entry->blend_settings->blend_mode));
      loading_controls = false;
      // Preserved-unsupported Blend If payloads stay untouched unless the user
      // explicitly replaced them (the existing warning-banner flow).
      if (blend_if_payload_status != BlendIfPayloadStatus::Unsupported ||
          replace_unsupported_blend_if) {
        blend_if = entry->blend_settings->blend_if;
        load_blend_if_controls();
      }
    }
    rebuild_category_list(LayerStyleEffectKind::None, kStylesCategoryIndex);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  };
  QObject::connect(styles_browser, &StyleBrowserWidget::style_clicked, &dialog,
                   apply_style_library_entry);
  QObject::connect(styles_browser, &StyleBrowserWidget::style_double_clicked, &dialog,
                   apply_style_library_entry);
  QObject::connect(styles_browser, &StyleBrowserWidget::no_style_clicked, &dialog, [&] {
    LayerStyle cleared;
    cleared.effects_visible = show_effects->isChecked();
    cleared.layer_mask_hides_effects = mask_hides_effects->isChecked();
    style = std::move(cleared);
    rebuild_category_list(LayerStyleEffectKind::None, kStylesCategoryIndex);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  });
  QObject::connect(manage_styles_button, &QPushButton::clicked, &dialog, [&] {
    if (style_library == nullptr) {
      return;
    }
    const auto storage_id = request_style_manager(&dialog, *style_library);
    if (!storage_id.isEmpty()) {
      styles_browser->reload(storage_id);
      apply_style_library_entry(storage_id);
    }
  });
  QObject::connect(new_style_button, &QPushButton::clicked, &dialog, [&] {
    if (style_library == nullptr) {
      return;
    }
    QDialog prompt(&dialog);
    prompt.setObjectName(QStringLiteral("layerStyleNewStyleDialog"));
    prompt.setWindowTitle(QObject::tr("New Style"));
    auto* form = new QFormLayout(&prompt);
    auto* name_edit = new QLineEdit(&prompt);
    name_edit->setObjectName(QStringLiteral("layerStyleNewStyleNameEdit"));
    name_edit->setText(QObject::tr("Style %1").arg(style_library->entries().size() + 1));
    name_edit->selectAll();
    form->addRow(QObject::tr("Name:"), name_edit);
    auto* folder_combo = new QComboBox(&prompt);
    folder_combo->setObjectName(QStringLiteral("layerStyleNewStyleFolderCombo"));
    folder_combo->setEditable(true);
    folder_combo->addItem(QString());
    folder_combo->addItems(style_library->folders());
    form->addRow(QObject::tr("Folder:"), folder_combo);
    auto* include_blend = new QCheckBox(
        QObject::tr("Include blending options (opacity, Fill, blend mode, Blend If)"), &prompt);
    include_blend->setObjectName(QStringLiteral("layerStyleNewStyleIncludeBlendCheck"));
    form->addRow(QString(), include_blend);
    auto* prompt_buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &prompt);
    form->addRow(prompt_buttons);
    QObject::connect(prompt_buttons, &QDialogButtonBox::accepted, &prompt, &QDialog::accept);
    QObject::connect(prompt_buttons, &QDialogButtonBox::rejected, &prompt, &QDialog::reject);
    if (prompt.exec() != QDialog::Accepted || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    const auto settings = build_current_settings();
    std::optional<psd::AslBlendSettings> blend_settings;
    if (include_blend->isChecked()) {
      blend_settings = psd::AslBlendSettings{settings.opacity, settings.blend_mode,
                                             settings.blend_if, settings.fill_opacity};
    }
    // Carry the referenced tiles so the preset stays self-contained.
    std::vector<PatternResource> preset_patterns;
    std::vector<std::string> referenced_ids;
    collect_referenced_pattern_ids(settings.style, referenced_ids);
    for (const auto& id : referenced_ids) {
      if (document_patterns != nullptr) {
        if (const auto* resource = document_patterns->find(id); resource != nullptr) {
          preset_patterns.push_back(*resource);
          continue;
        }
      }
      if (pattern_library != nullptr) {
        if (auto resource = pattern_library->resource(QString::fromStdString(id));
            resource.has_value()) {
          preset_patterns.push_back(std::move(*resource));
          continue;
        }
      }
      if (auto bundled = bundled_pattern_resource(id); bundled.has_value()) {
        preset_patterns.push_back(std::move(*bundled));
      }
    }
    const auto storage_id =
        style_library->add_style(name_edit->text(), settings.style, blend_settings,
                                 preset_patterns, folder_combo->currentText());
    if (storage_id.isEmpty()) {
      QMessageBox::warning(
          &dialog, QObject::tr("New Style"),
          QObject::tr("Could not save the style. Check that the style library "
                      "folder is writable."));
      return;
    }
    styles_browser->reload(storage_id);
  });
  QObject::connect(bevel_texture_pattern, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_texture_invert, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(bevel_texture_link, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(bevel_texture_snap, &QPushButton::clicked, &dialog, [&] {
    bevel.texture.phase_x = 0.0F;
    bevel.texture.phase_y = 0.0F;
    emit_preview(true);
  });
  QObject::connect(pattern_overlay_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(pattern_overlay_pattern, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(pattern_overlay_link, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(pattern_overlay_snap, &QPushButton::clicked, &dialog, [&] {
    pattern_overlay.phase_x = 0.0F;
    pattern_overlay.phase_y = 0.0F;
    emit_preview(true);
  });
  QObject::connect(color_overlay_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(outer_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(outer_glow_technique, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_glow_source, &QComboBox::currentIndexChanged, &dialog,
                    [&emit_preview](int) { emit_preview(true); });
  QObject::connect(satin_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(satin_invert, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_position, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_fill, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    update_stroke_fill_visibility();
    emit_preview(true);
  });
  QObject::connect(gradient_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  // Shared modal picker for the RGB color rows: no live preview while picking
  // (unlike the bevel and gradient-stop pickers), commit on accept only.
  const auto connect_color_row_picker = [&](const RgbColorRowWidgets& rows, const QString& title) {
    QObject::connect(rows.picker, &QAbstractButton::clicked, &dialog, [&, rows, title] {
      const auto current = rows.value();
      const auto chosen =
          request_patchy_color(&dialog, QColor(current.red, current.green, current.blue), title);
      if (!chosen.has_value()) {
        return;
      }
      rows.set_value(RgbColor{static_cast<std::uint8_t>(chosen->red()),
                              static_cast<std::uint8_t>(chosen->green()),
                              static_cast<std::uint8_t>(chosen->blue())});
      emit_preview(true);
    });
  };
  connect_color_row_picker(stroke_rows, QObject::tr("Choose Stroke Color"));
  auto choose_bevel_color = [&](RgbColor& target, const QString& title) {
    const auto original = target;
    auto apply_color = [&](QColor color) {
      if (!color.isValid()) {
        return;
      }
      target = RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                        static_cast<std::uint8_t>(color.blue())};
      update_bevel_color_previews();
    };
    const auto chosen = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue), title,
                                             [&](QColor color) {
                                               apply_color(color);
                                               emit_preview(false);
                                             });
    if (!chosen.has_value()) {
      apply_color(QColor(original.red, original.green, original.blue));
      emit_preview(true);
      return;
    }
    apply_color(*chosen);
    emit_preview(true);
  };
  QObject::connect(bevel_highlight_color_button, &QPushButton::clicked, &dialog, [&] {
    choose_bevel_color(bevel_highlight_color, QObject::tr("Choose Highlight Color"));
  });
  QObject::connect(bevel_shadow_color_button, &QPushButton::clicked, &dialog, [&] {
    choose_bevel_color(bevel_shadow_color, QObject::tr("Choose Shadow Color"));
  });
  connect_color_row_picker(color_overlay_rows, QObject::tr("Choose Color Overlay Color"));
  connect_color_row_picker(outer_glow_rows, QObject::tr("Choose Outer Glow Color"));
  connect_color_row_picker(inner_glow_rows, QObject::tr("Choose Inner Glow Color"));
  connect_color_row_picker(satin_rows, QObject::tr("Choose Satin Color"));
  connect_color_row_picker(shadow_rows, QObject::tr("Choose Drop Shadow Color"));
  connect_color_row_picker(inner_shadow_rows, QObject::tr("Choose Inner Shadow Color"));
  auto add_selected_instance = [&](LayerStyleEffectKind fallback_kind) {
    const auto* current = categories->currentItem();
    const auto kind = is_stackable_kind(item_kind(current)) ? item_kind(current) : fallback_kind;
    const auto source_index = item_effect_index(current) >= 0 ? item_effect_index(current) : 0;
    style = build_current_settings().style;
    const auto new_index = add_effect_instance(kind, source_index);
    rebuild_category_list(kind, new_index);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  };
  auto remove_selected_stackable_instance = [&] {
    const auto* current = categories->currentItem();
    const auto kind = item_kind(current);
    if (!is_stackable_kind(kind)) {
      return;
    }
    style = build_current_settings().style;
    const auto next_index = remove_effect_instance(kind, item_effect_index(current));
    rebuild_category_list(kind, next_index);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  };
  QObject::connect(add_inner_shadow, &QPushButton::clicked, &dialog, [&] {
    add_selected_instance(LayerStyleEffectKind::InnerShadow);
  });
  QObject::connect(remove_inner_shadow, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  QObject::connect(add_inner_glow, &QPushButton::clicked, &dialog, [&] {
    add_selected_instance(LayerStyleEffectKind::InnerGlow);
  });
  QObject::connect(remove_inner_glow, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  QObject::connect(remove_selected_instance, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* footer = new QHBoxLayout();
  footer->addWidget(preview_check);
  footer->addStretch(1);
  footer->addWidget(buttons);
  root->addLayout(footer);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (parent != nullptr) {
    const auto canvases = parent->findChildren<CanvasWidget *>();
    const auto visible =
        std::find_if(canvases.begin(), canvases.end(),
                     [](CanvasWidget *canvas) { return canvas->isVisible(); });
    if (visible != canvases.end()) {
      auto *overlay_gradient_state = gradient_controls.get();
      auto *stroke_gradient_state = stroke_gradient_controls.get();
      auto active_gradient_controls =
          [categories, stroke_fill, overlay_gradient_state, item_kind,
           stroke_gradient_state]() -> LayerStyleGradientControls * {
        const auto *current = categories->currentItem();
        if (item_kind(current) == LayerStyleEffectKind::GradientOverlay)
          return overlay_gradient_state;
        if (item_kind(current) == LayerStyleEffectKind::Stroke &&
            stroke_fill->currentData().toBool())
          return stroke_gradient_state;
        return nullptr;
      };
      new GradientCanvasRepositionFilter(
          *visible, layer.bounds(),
          [active_gradient_controls] {
            return active_gradient_controls() != nullptr;
          },
          [active_gradient_controls](float delta_x, float delta_y) {
            if (auto *state = active_gradient_controls(); state != nullptr) {
              state->offset_x->setValue(state->offset_x->value() +
                                        static_cast<int>(std::lround(delta_x)));
              state->offset_y->setValue(state->offset_y->value() +
                                        static_cast<int>(std::lround(delta_y)));
            }
          },
          &dialog);
    }
  }

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  if (document_patterns != nullptr) {
    for (const auto& resource : transient_manager_patterns) {
      document_patterns->adopt(resource);
    }
  }

  return build_current_settings();
}


}  // namespace patchy::ui
