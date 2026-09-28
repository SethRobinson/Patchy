// Shape appearance dialog: fill (none/solid/gradient/pattern) and stroke
// (width/paint/alignment/caps/joins/dashes) for shape and fill layers, with
// live preview through the caller's callback. Gradient presets come from the
// GradientLibrary; patterns list the document store first, then the library
// (adoption into the store happens in the caller when applying).
#include "ui/shape_appearance_dialog.hpp"

#include "core/pattern_resource.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/pattern_library.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/action_icons.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

QIcon color_swatch_icon(RgbColor color) {
  QPixmap pixmap(28, 14);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(QColor(0, 0, 0, 160));
  painter.setBrush(QColor(color.red, color.green, color.blue));
  painter.drawRect(0, 0, 27, 13);
  return QIcon(pixmap);
}

// Dash presets, stored in stroke-width multiples like the vstk descriptor.
const std::vector<double>& dash_preset(int index) {
  static const std::vector<double> kSolid{};
  static const std::vector<double> kDashed{2.0, 2.0};
  static const std::vector<double> kDotted{0.0, 2.0};
  switch (index) {
    case 1:
      return kDashed;
    case 2:
      return kDotted;
    default:
      return kSolid;
  }
}

struct DialogState {
  ShapeAppearanceSettings settings;
  // Set on any stroke-paint interaction (paint kind, color, gradient, or
  // pattern controls); an untouched PSD-authored paint round-trips verbatim
  // because the model is only rewritten when a control changes it.
  bool stroke_paint_touched{false};
  // A PSD-authored dash pattern that matches no preset, restorable after
  // trying the presets.
  std::vector<double> custom_dashes;
};

// A chain button at the app's normal button size, centered on a thin bracket
// that reaches the middle of the first and last rows it ties (the Image Size
// dialog's Width / Height link). The bracket is painted here, behind the
// button, from the live positions of the two end rows (siblings in the same
// grid), so it follows any row height; it brightens while linked.
class LinkBracket : public QWidget {
public:
  LinkBracket(QToolButton* button, QWidget* first_row, QWidget* last_row, QWidget* parent)
      : QWidget(parent), button_(button), first_row_(first_row), last_row_(last_row) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    button_->setParent(this);
    layout->addWidget(button_, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    setFixedWidth(button_->maximumWidth());
    QObject::connect(button_, &QToolButton::toggled, this, qOverload<>(&QWidget::update));
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    const int top = first_row_->geometry().center().y() - y();
    const int bottom = last_row_->geometry().center().y() - y();
    const int x = width() / 2;
    QPainter painter(this);
    painter.setPen(QPen(button_->isChecked() ? theme().dlg_focus_border : theme().dlg_button_border, 1));
    painter.drawLine(x, top, x, bottom);
    painter.drawLine(x, top, width() - 1, top);
    painter.drawLine(x, bottom, width() - 1, bottom);
  }

private:
  QToolButton* button_;
  QWidget* first_row_;
  QWidget* last_row_;
};

}  // namespace

// GRD presets may defer stops to the tool colors; shape fills store concrete
// colors, so resolve at pick time (the layer-style dialog's convention).
GradientDefinition resolve_gradient_definition(GradientDefinition definition, RgbColor foreground,
                                               RgbColor background) {
  for (auto& stop : definition.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground) {
      stop.color = foreground;
    } else if (stop.kind == GradientColorStop::Kind::Background) {
      stop.color = background;
    }
    stop.kind = GradientColorStop::Kind::User;
  }
  return definition;
}

std::optional<ShapeAppearanceSettings> request_shape_appearance_settings(
    QWidget* parent, std::function<void(const ShapeAppearanceSettings&)> preview_changed,
    ShapeAppearanceSettings initial, ShapeAppearanceSettings reset_defaults,
    GradientLibrary* gradient_library,
    PatternLibrary* pattern_library, const PatternStore* document_patterns, RgbColor foreground,
    RgbColor background) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("shapeAppearanceDialog"));
  dialog.setWindowTitle(QObject::tr("Shape Appearance"));
  auto* dialog_layout = new QVBoxLayout(&dialog);

  // Two columns (Layer / Geometry / Edge left, Fill / Stroke right) inside a
  // scroll area: the worst case (rounded rect, pattern fill, gradient stroke)
  // then fits a 1080p screen, and on a shorter screen the page scrolls
  // instead of running OK/Cancel off the bottom (the brush dynamics recipe;
  // desktop place_dialog only clamps a dialog's position, never its size).
  auto* scroll = new QScrollArea(&dialog);
  scroll->setObjectName(QStringLiteral("shapeAppearanceScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* page = new QWidget(scroll);
  page->setObjectName(QStringLiteral("shapeAppearancePage"));
  auto* columns = new QHBoxLayout(page);
  columns->setContentsMargins(0, 0, 0, 0);
  columns->setSpacing(10);
  // Each column is a widget with its own top-level layout, not a sub-layout:
  // Qt propagates size-hint changes (rows hiding per paint kind) across
  // widget boundaries through updateGeometry, while a nested sub-layout keeps
  // a stale cached hint until its parent layout re-activates.
  auto* left_widget = new QWidget(page);
  left_widget->setObjectName(QStringLiteral("shapeAppearanceLeftColumn"));
  auto* left_column = new QVBoxLayout(left_widget);
  left_column->setContentsMargins(0, 0, 0, 0);
  left_column->setSpacing(8);
  auto* right_widget = new QWidget(page);
  right_widget->setObjectName(QStringLiteral("shapeAppearanceRightColumn"));
  auto* right_column = new QVBoxLayout(right_widget);
  right_column->setContentsMargins(0, 0, 0, 0);
  right_column->setSpacing(8);
  columns->addWidget(left_widget, 1);
  columns->addWidget(right_widget, 1);
  scroll->setWidget(page);
  dialog_layout->addWidget(scroll, 1);
  append_themed_style(dialog, QStringLiteral("QScrollArea#shapeAppearanceScroll,"
                                             "QWidget#shapeAppearancePage, QWidget#shapeAppearanceLeftColumn,QWidget#shapeAppearanceRightColumn { background: transparent; }"));

  // Every numeric field gets compact - / + steppers. The row widgets are
  // tracked so the per-kind visibility and enabled toggles below act on the
  // whole row (spin plus buttons) and its label.
  std::map<QWidget*, QWidget*> field_rows;
  const auto add_spin_row = [&field_rows](QFormLayout* form, const QString& label,
                                          QAbstractSpinBox* spin) {
    auto* row = wrap_spin_with_step_buttons(spin, form->parentWidget(), label);
    field_rows[spin] = row;
    form->addRow(label, row);
    return row;
  };

  auto state = std::make_shared<DialogState>();
  state->settings = std::move(initial);

  const auto notify = [state, preview_changed] {
    if (preview_changed) {
      preview_changed(state->settings);
    }
  };

  // --- Layer (opacity and fill opacity, the Layers panel values) ---
  auto* layer_group = new QGroupBox(QObject::tr("Layer"), left_widget);
  auto* layer_layout = new QVBoxLayout(layer_group);
  layer_layout->setContentsMargins(10, 8, 10, 8);
  layer_layout->setSpacing(4);
  auto* layer_form = new QFormLayout();
  layer_form->setHorizontalSpacing(10);
  layer_form->setVerticalSpacing(8);
  layer_layout->addLayout(layer_form);
  auto* layer_opacity_spin = add_dialog_slider_spin_row(
      layer_form, layer_group, QObject::tr("Opacity:"), QStringLiteral("shapeLayerOpacitySlider"),
      QStringLiteral("shapeLayerOpacitySpin"), 0, 100,
      static_cast<int>(std::lround(state->settings.layer_opacity * 100.0F)), QStringLiteral("%"), 72,
      /*row_spacing=*/8, /*step_buttons=*/true);
  auto* layer_fill_opacity_spin = add_dialog_slider_spin_row(
      layer_form, layer_group, QObject::tr("Fill Opacity:"),
      QStringLiteral("shapeLayerFillOpacitySlider"), QStringLiteral("shapeLayerFillOpacitySpin"), 0,
      100, static_cast<int>(std::lround(state->settings.fill_opacity * 100.0F)), QStringLiteral("%"), 72,
      /*row_spacing=*/8, /*step_buttons=*/true);
  QObject::connect(layer_opacity_spin, &QSpinBox::valueChanged, &dialog, [state, notify](int value) {
    state->settings.layer_opacity = static_cast<float>(value) / 100.0F;
    notify();
  });
  QObject::connect(layer_fill_opacity_spin, &QSpinBox::valueChanged, &dialog,
                   [state, notify](int value) {
    state->settings.fill_opacity = static_cast<float>(value) / 100.0F;
    notify();
  });
  left_column->addWidget(layer_group);

  // --- Geometry (single live-shape layers only) ---
  if (state->settings.geometry.has_value()) {
    const auto kind = state->settings.geometry->kind;
    auto* geometry_group = new QGroupBox(QObject::tr("Geometry"), left_widget);
    auto* geometry_layout = new QVBoxLayout(geometry_group);
    geometry_layout->setContentsMargins(10, 8, 10, 8);
    geometry_layout->setSpacing(4);
    // A grid rather than a form: the link buttons sit between the label and
    // the field and span the rows they tie together (the Image Size dialog's
    // Width / Height bracket), which a QFormLayout row cannot do. Column 1 is
    // the link column; it stays empty on the rows without a link so every
    // label and field lines up.
    auto* geometry_grid = new QGridLayout();
    geometry_grid->setContentsMargins(0, 0, 0, 0);
    geometry_grid->setHorizontalSpacing(8);
    geometry_grid->setVerticalSpacing(8);
    geometry_grid->setColumnMinimumWidth(1, 24);
    geometry_grid->setColumnStretch(2, 1);
    geometry_layout->addLayout(geometry_grid);
    std::vector<QWidget*> geometry_rows;  // the [spin] - + row widget per grid row
    const auto add_geometry_row = [&field_rows, geometry_grid, geometry_group,
                                   &geometry_rows](const QString& label, QAbstractSpinBox* spin) {
      auto* row = wrap_spin_with_step_buttons(spin, geometry_group, label);
      field_rows[spin] = row;
      const int grid_row = static_cast<int>(geometry_rows.size());
      geometry_grid->addWidget(new QLabel(label, geometry_group), grid_row, 0,
                               Qt::AlignLeft | Qt::AlignVCenter);
      geometry_grid->addWidget(row, grid_row, 2);
      geometry_rows.push_back(row);
      return grid_row;
    };
    // A normal-sized checkable chain button centered on a thin bracket that
    // reaches the first and last of the `row_count` rows from `first_row`.
    // `bracket_name` names the bracket widget (its objectName), `name` the
    // button.
    const auto make_link_button = [geometry_grid, geometry_group, &geometry_rows](
                                      const char* name, const char* bracket_name,
                                      const QString& tooltip, int first_row, int row_count) {
      auto* button = new QToolButton(nullptr);
      button->setObjectName(QLatin1String(name));
      button->setProperty("geometryLink", true);
      button->setCheckable(true);
      button->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
      button->setIconSize(QSize(18, 18));
      button->setToolTip(tooltip);
      button->setFixedSize(24, 24);
      auto* bracket = new LinkBracket(button, geometry_rows[static_cast<std::size_t>(first_row)],
                                      geometry_rows[static_cast<std::size_t>(first_row + row_count - 1)],
                                      geometry_group);
      bracket->setObjectName(QLatin1String(bracket_name));
      geometry_grid->addWidget(bracket, first_row, 1, row_count, 1);
      return button;
    };
    const auto make_spin = [&](const char* name, double minimum, double maximum, double value) {
      auto* spin = new UnitSpinBox(SpinUnit::Pixels, geometry_group);
      spin->setObjectName(QLatin1String(name));
      spin->setRange(minimum, maximum);
      spin->setDecimals(1);
      spin->setValue(value);
      configure_dialog_spinbox(spin, 72);
      return spin;
    };
    const auto& geometry = *state->settings.geometry;
    if (kind == LiveShapeKind::Line) {
      auto* start_x = make_spin("shapeGeometryLineStartXSpin", -30000, 30000, geometry.line_start_x);
      auto* start_y = make_spin("shapeGeometryLineStartYSpin", -30000, 30000, geometry.line_start_y);
      auto* end_x = make_spin("shapeGeometryLineEndXSpin", -30000, 30000, geometry.line_end_x);
      auto* end_y = make_spin("shapeGeometryLineEndYSpin", -30000, 30000, geometry.line_end_y);
      auto* weight = make_spin("shapeGeometryLineWeightSpin", 0.5, 1000, geometry.line_weight);
      add_geometry_row(QObject::tr("Start X:"), start_x);
      add_geometry_row(QObject::tr("Start Y:"), start_y);
      add_geometry_row(QObject::tr("End X:"), end_x);
      add_geometry_row(QObject::tr("End Y:"), end_y);
      add_geometry_row(QObject::tr("Weight:"), weight);
      const auto apply_line = [state, notify, start_x, start_y, end_x, end_y, weight] {
        auto& params = *state->settings.geometry;
        params.line_start_x = start_x->value();
        params.line_start_y = start_y->value();
        params.line_end_x = end_x->value();
        params.line_end_y = end_y->value();
        params.line_weight = weight->value();
        if (params.arrow_start || params.arrow_end) {
          // Keep the default arrowhead proportions tied to the weight.
          params.arrow_width = params.line_weight * 5.0;
          params.arrow_length = params.line_weight * 10.0;
        }
        notify();
      };
      for (auto* spin : {start_x, start_y, end_x, end_y, weight}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_line);
      }
    } else {
      auto* x_spin = make_spin("shapeGeometryXSpin", -30000, 30000, geometry.left);
      auto* y_spin = make_spin("shapeGeometryYSpin", -30000, 30000, geometry.top);
      auto* width_spin =
          make_spin("shapeGeometryWidthSpin", 0.5, 60000, geometry.right - geometry.left);
      auto* height_spin =
          make_spin("shapeGeometryHeightSpin", 0.5, 60000, geometry.bottom - geometry.top);
      add_geometry_row(QObject::tr("X:"), x_spin);
      add_geometry_row(QObject::tr("Y:"), y_spin);
      const int width_row = add_geometry_row(QObject::tr("Width:"), width_spin);
      add_geometry_row(QObject::tr("Height:"), height_spin);
      // Link keeps the aspect ratio: editing one dimension moves the other by
      // the ratio captured when the link was switched on.
      auto* link_button = make_link_button("shapeGeometryLinkButton", "shapeGeometryLinkBracket",
                                           QObject::tr("Keep width and height in proportion"),
                                           width_row, 2);
      auto link_ratio = std::make_shared<double>(1.0);
      QObject::connect(link_button, &QToolButton::toggled, &dialog,
                       [link_ratio, width_spin, height_spin](bool checked) {
        if (checked) {
          *link_ratio = height_spin->value() > 1e-9 ? width_spin->value() / height_spin->value() : 1.0;
        }
      });
      // Connected before apply_box below, so the paired spin is already
      // updated when the geometry applies.
      QObject::connect(width_spin, &QDoubleSpinBox::valueChanged, &dialog,
                       [link_button, link_ratio, height_spin](double value) {
        if (link_button->isChecked() && *link_ratio > 1e-9) {
          QSignalBlocker blocker(height_spin);
          height_spin->setValue(value / *link_ratio);
        }
      });
      QObject::connect(height_spin, &QDoubleSpinBox::valueChanged, &dialog,
                       [link_button, link_ratio, width_spin](double value) {
        if (link_button->isChecked()) {
          QSignalBlocker blocker(width_spin);
          width_spin->setValue(value * *link_ratio);
        }
      });
      std::array<QDoubleSpinBox*, 4> radius_spins{nullptr, nullptr, nullptr, nullptr};
      if (kind == LiveShapeKind::Rectangle || kind == LiveShapeKind::RoundedRectangle) {
        // Model order TL, TR, BR, BL; a radius on a plain rect promotes it to
        // a rounded rect (the generator clamps oversized values).
        const std::array<const char*, 4> names{
            "shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
            "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin"};
        const std::array<QString, 4> labels{
            QObject::tr("Top left radius:"), QObject::tr("Top right radius:"),
            QObject::tr("Bottom right radius:"), QObject::tr("Bottom left radius:")};
        const int first_radius_row = static_cast<int>(geometry_rows.size());
        for (std::size_t corner = 0; corner < 4; ++corner) {
          radius_spins[corner] =
              make_spin(names[corner], 0, 30000, geometry.corner_radii[corner]);
          add_geometry_row(labels[corner], radius_spins[corner]);
        }
        // Linked, editing any corner sets all four. Starts linked when the
        // corners already agree (the common case: one radius for the whole
        // shape); a shape authored with distinct corners opens unlinked so
        // one edit cannot flatten them.
        auto* radius_link = make_link_button("shapeGeometryRadiusLinkButton",
                                             "shapeGeometryRadiusLinkBracket",
                                             QObject::tr("Change all four corner radii together"),
                                             first_radius_row, 4);
        const bool corners_agree =
            std::abs(geometry.corner_radii[1] - geometry.corner_radii[0]) < 1e-9 &&
            std::abs(geometry.corner_radii[2] - geometry.corner_radii[0]) < 1e-9 &&
            std::abs(geometry.corner_radii[3] - geometry.corner_radii[0]) < 1e-9;
        radius_link->setChecked(corners_agree);
        // Connected before apply_box below (the same ordering as the W / H
        // link) so every corner is already updated when the geometry applies.
        for (auto* spin : radius_spins) {
          QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog,
                           [radius_link, radius_spins, spin](double value) {
            if (!radius_link->isChecked()) {
              return;
            }
            for (auto* other : radius_spins) {
              if (other != spin) {
                QSignalBlocker blocker(other);
                other->setValue(value);
              }
            }
          });
        }
      }
      const auto apply_box = [state, notify, x_spin, y_spin, width_spin, height_spin,
                              radius_spins] {
        auto& params = *state->settings.geometry;
        params.left = x_spin->value();
        params.top = y_spin->value();
        params.right = x_spin->value() + width_spin->value();
        params.bottom = y_spin->value() + height_spin->value();
        if (radius_spins[0] != nullptr) {
          for (std::size_t corner = 0; corner < 4; ++corner) {
            params.corner_radii[corner] = radius_spins[corner]->value();
          }
          const bool any_radius =
              params.corner_radii[0] > 0.0 || params.corner_radii[1] > 0.0 ||
              params.corner_radii[2] > 0.0 || params.corner_radii[3] > 0.0;
          if (params.kind == LiveShapeKind::Rectangle && any_radius) {
            params.kind = LiveShapeKind::RoundedRectangle;
          }
        }
        notify();
      };
      for (auto* spin : {x_spin, y_spin, width_spin, height_spin}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_box);
      }
      for (auto* spin : radius_spins) {
        if (spin != nullptr) {
          QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_box);
        }
      }
    }
    // The link buttons: a flat chain glyph that lights up while linked (the
    // Image Size dialog's Width / Height link).
    append_themed_style(dialog, QStringLiteral(R"(
      QDialog#shapeAppearanceDialog QToolButton[geometryLink="true"] {
        background: @dlg_button_bg;
        border: 1px solid @dlg_button_border;
        border-radius: 4px;
        padding: 0;
        min-width: 24px;
        max-width: 24px;
        min-height: 24px;
        max-height: 24px;
      }
      QDialog#shapeAppearanceDialog QToolButton[geometryLink="true"]:checked {
        border-color: @dlg_focus_border;
        background: @dlg_anchor_active_bg;
      }
    )"));
    left_column->addWidget(geometry_group);
  }

  // --- Fill ---
  auto* fill_group = new QGroupBox(QObject::tr("Fill"), right_widget);
  auto* fill_layout = new QVBoxLayout(fill_group);
  fill_layout->setContentsMargins(10, 8, 10, 8);
  fill_layout->setSpacing(4);
  auto* fill_form = new QFormLayout();
  fill_form->setHorizontalSpacing(10);
  fill_form->setVerticalSpacing(8);
  fill_layout->addLayout(fill_form);

  auto* fill_kind_combo = new QComboBox(fill_group);
  fill_kind_combo->setObjectName(QStringLiteral("shapeFillKindCombo"));
  fill_kind_combo->addItem(QObject::tr("No Fill"), static_cast<int>(VectorFillKind::None));
  fill_kind_combo->addItem(QObject::tr("Solid Color"), static_cast<int>(VectorFillKind::Solid));
  fill_kind_combo->addItem(QObject::tr("Gradient"), static_cast<int>(VectorFillKind::Gradient));
  fill_kind_combo->addItem(QObject::tr("Pattern"), static_cast<int>(VectorFillKind::Pattern));
  fill_form->addRow(QObject::tr("Type:"), fill_kind_combo);

  auto* fill_color_button = new QPushButton(fill_group);
  fill_color_button->setObjectName(QStringLiteral("shapeFillColorButton"));
  fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
  fill_color_button->setText(QObject::tr("Color..."));
  auto* fill_color_row = fill_color_button;
  fill_form->addRow(QObject::tr("Color:"), fill_color_row);

  // Both the fill and the stroke paint offer the same preset lists.
  const auto populate_gradient_combo = [gradient_library](QComboBox* combo) {
    combo->setIconSize(QSize(64, 16));
    if (gradient_library != nullptr) {
      for (const auto& entry : gradient_library->entries()) {
        combo->addItem(QIcon(entry.thumbnail), entry.name, entry.storage_id);
      }
    }
  };
  const auto populate_pattern_combo = [pattern_library, document_patterns](QComboBox* combo) {
    combo->setIconSize(QSize(24, 24));
    if (document_patterns != nullptr) {
      for (const auto& resource : document_patterns->patterns) {
        const auto name = resource.name.empty() ? QObject::tr("Embedded pattern")
                                                : QString::fromStdString(resource.name);
        combo->addItem(name, QString::fromStdString(resource.id));
      }
    }
    if (pattern_library != nullptr) {
      for (const auto& entry : pattern_library->entries()) {
        if (combo->findData(entry.id) < 0) {
          combo->addItem(QIcon(entry.thumbnail), entry.name, entry.id);
        }
      }
    }
  };

  auto* fill_gradient_combo = new QComboBox(fill_group);
  fill_gradient_combo->setObjectName(QStringLiteral("shapeFillGradientCombo"));
  populate_gradient_combo(fill_gradient_combo);
  fill_form->addRow(QObject::tr("Gradient:"), fill_gradient_combo);

  auto* gradient_type_combo = new QComboBox(fill_group);
  gradient_type_combo->setObjectName(QStringLiteral("shapeGradientTypeCombo"));
  gradient_type_combo->addItem(QObject::tr("Linear"), static_cast<int>(LayerStyleGradientType::Linear));
  gradient_type_combo->addItem(QObject::tr("Radial"), static_cast<int>(LayerStyleGradientType::Radial));
  gradient_type_combo->addItem(QObject::tr("Angle"), static_cast<int>(LayerStyleGradientType::Angle));
  gradient_type_combo->addItem(QObject::tr("Reflected"),
                               static_cast<int>(LayerStyleGradientType::Reflected));
  gradient_type_combo->addItem(QObject::tr("Diamond"),
                               static_cast<int>(LayerStyleGradientType::Diamond));
  fill_form->addRow(QObject::tr("Style:"), gradient_type_combo);

  auto* gradient_angle_spin = new UnitIntSpinBox(SpinUnit::Degrees, fill_group);
  gradient_angle_spin->setObjectName(QStringLiteral("shapeGradientAngleSpin"));
  gradient_angle_spin->setRange(-180, 180);
  configure_dialog_spinbox(gradient_angle_spin, 72);
  add_spin_row(fill_form, QObject::tr("Angle:"), gradient_angle_spin);

  auto* gradient_scale_spin = new QSpinBox(fill_group);
  gradient_scale_spin->setObjectName(QStringLiteral("shapeGradientScaleSpin"));
  gradient_scale_spin->setRange(10, 1000);
  gradient_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(gradient_scale_spin, 72);
  add_spin_row(fill_form, QObject::tr("Scale:"), gradient_scale_spin);

  auto* gradient_reverse_check = new QCheckBox(QObject::tr("Reverse"), fill_group);
  gradient_reverse_check->setObjectName(QStringLiteral("shapeGradientReverseCheck"));
  fill_form->addRow(QString(), gradient_reverse_check);

  auto* fill_pattern_combo = new QComboBox(fill_group);
  fill_pattern_combo->setObjectName(QStringLiteral("shapeFillPatternCombo"));
  populate_pattern_combo(fill_pattern_combo);
  fill_form->addRow(QObject::tr("Pattern:"), fill_pattern_combo);

  auto* pattern_scale_spin = new QSpinBox(fill_group);
  pattern_scale_spin->setObjectName(QStringLiteral("shapePatternScaleSpin"));
  pattern_scale_spin->setRange(1, 1000);
  pattern_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(pattern_scale_spin, 72);
  add_spin_row(fill_form, QObject::tr("Scale:"), pattern_scale_spin);

  // Pattern placement (PtFl Angl / phase / Algn; rendered by the shared
  // PatternTileSampler and round-tripped through the PSD writer).
  auto* pattern_angle_spin = new UnitSpinBox(SpinUnit::Degrees, fill_group);
  pattern_angle_spin->setObjectName(QStringLiteral("shapePatternAngleSpin"));
  pattern_angle_spin->setRange(-180.0, 180.0);
  pattern_angle_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_angle_spin, 72);
  add_spin_row(fill_form, QObject::tr("Angle:"), pattern_angle_spin);

  auto* pattern_offset_x_spin = new UnitSpinBox(SpinUnit::Pixels, fill_group);
  pattern_offset_x_spin->setObjectName(QStringLiteral("shapePatternOffsetXSpin"));
  pattern_offset_x_spin->setRange(-30000.0, 30000.0);
  pattern_offset_x_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_offset_x_spin, 80);
  add_spin_row(fill_form, QObject::tr("Offset X:"), pattern_offset_x_spin);

  auto* pattern_offset_y_spin = new UnitSpinBox(SpinUnit::Pixels, fill_group);
  pattern_offset_y_spin->setObjectName(QStringLiteral("shapePatternOffsetYSpin"));
  pattern_offset_y_spin->setRange(-30000.0, 30000.0);
  pattern_offset_y_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_offset_y_spin, 80);
  add_spin_row(fill_form, QObject::tr("Offset Y:"), pattern_offset_y_spin);

  auto* pattern_align_check = new QCheckBox(QObject::tr("Align with layer"), fill_group);
  pattern_align_check->setObjectName(QStringLiteral("shapePatternAlignCheck"));
  pattern_align_check->setToolTip(
      QObject::tr("Anchor the tile grid to the layer's position; unchecked anchors it to the "
                  "document origin"));
  fill_form->addRow(QString(), pattern_align_check);

  right_column->addWidget(fill_group);

  // --- Edge: the shape's vector-mask Feather / Density (Photoshop's
  // Properties-panel pair on a shape layer; docs/vector-tools.md) ---
  auto* edge_group = new QGroupBox(QObject::tr("Edge"), left_widget);
  auto* edge_layout = new QVBoxLayout(edge_group);
  edge_layout->setContentsMargins(10, 8, 10, 8);
  edge_layout->setSpacing(4);
  auto* edge_form = new QFormLayout();
  edge_form->setHorizontalSpacing(10);
  edge_form->setVerticalSpacing(8);
  edge_layout->addLayout(edge_form);
  auto* feather_spin = new UnitSpinBox(SpinUnit::Pixels, edge_group);
  feather_spin->setObjectName(QStringLiteral("shapeFeatherSpin"));
  feather_spin->setRange(0.0, 1000.0);
  feather_spin->setDecimals(1);
  feather_spin->setValue(state->settings.feather);
  feather_spin->setToolTip(QObject::tr("Softens the whole shape, stroke included, like Photoshop's vector mask feather"));
  configure_dialog_spinbox(feather_spin, 80);
  add_spin_row(edge_form, QObject::tr("Feather:"), feather_spin);
  auto* density_spin = new QSpinBox(edge_group);
  density_spin->setObjectName(QStringLiteral("shapeDensitySpin"));
  density_spin->setRange(0, 100);
  density_spin->setSuffix(percent_suffix());
  density_spin->setValue(static_cast<int>(std::lround(state->settings.density * 100.0 / 255.0)));
  density_spin->setToolTip(QObject::tr("Below 100% the fill shows through everywhere, like Photoshop's vector mask density"));
  configure_dialog_spinbox(density_spin, 80);
  add_spin_row(edge_form, QObject::tr("Density:"), density_spin);
  QObject::connect(feather_spin, &QDoubleSpinBox::valueChanged, &dialog, [state, notify](double value) {
    state->settings.feather = value;
    notify();
  });
  QObject::connect(density_spin, &QSpinBox::valueChanged, &dialog, [state, notify](int value) {
    state->settings.density = static_cast<std::uint8_t>(std::lround(value * 255.0 / 100.0));
    notify();
  });
  left_column->addWidget(edge_group);

  // --- Stroke ---
  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), right_widget);
  auto* stroke_layout = new QVBoxLayout(stroke_group);
  stroke_layout->setContentsMargins(10, 8, 10, 8);
  stroke_layout->setSpacing(4);
  auto* stroke_form = new QFormLayout();
  stroke_form->setHorizontalSpacing(10);
  stroke_form->setVerticalSpacing(8);
  stroke_layout->addLayout(stroke_form);

  auto* stroke_check = new QCheckBox(QObject::tr("Stroke the shape outline"), stroke_group);
  stroke_check->setObjectName(QStringLiteral("shapeStrokeCheck"));
  stroke_check->setChecked(state->settings.stroke.enabled);
  stroke_form->addRow(QString(), stroke_check);

  auto* stroke_width_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_width_spin->setObjectName(QStringLiteral("shapeStrokeWidthSpin"));
  stroke_width_spin->setRange(0.1, 1000.0);
  stroke_width_spin->setDecimals(1);
  stroke_width_spin->setValue(state->settings.stroke.width);
  configure_dialog_spinbox(stroke_width_spin, 80);
  add_spin_row(stroke_form, QObject::tr("Width:"), stroke_width_spin);

  // vstk strokeStyleOpacity: the stroke's own transparency.
  auto* stroke_opacity_spin = new QSpinBox(stroke_group);
  stroke_opacity_spin->setObjectName(QStringLiteral("shapeStrokeOpacitySpin"));
  stroke_opacity_spin->setRange(0, 100);
  stroke_opacity_spin->setSuffix(percent_suffix());
  stroke_opacity_spin->setValue(static_cast<int>(std::lround(state->settings.stroke.opacity * 100.0)));
  configure_dialog_spinbox(stroke_opacity_spin, 80);
  add_spin_row(stroke_form, QObject::tr("Opacity:"), stroke_opacity_spin);
  QObject::connect(stroke_opacity_spin, &QSpinBox::valueChanged, &dialog, [state, notify](int value) {
    state->settings.stroke.opacity = value / 100.0;
    notify();
  });

  // Stroke paint: solid color, gradient, or pattern (vstk strokeStyleContent
  // takes the same three content shapes as the fill). A PSD-authored gradient
  // or pattern paint shows truthfully and stays untouched until edited.
  auto* stroke_paint_combo = new QComboBox(stroke_group);
  stroke_paint_combo->setObjectName(QStringLiteral("shapeStrokePaintCombo"));
  stroke_paint_combo->addItem(QObject::tr("Solid Color"), static_cast<int>(VectorFillKind::Solid));
  stroke_paint_combo->addItem(QObject::tr("Gradient"), static_cast<int>(VectorFillKind::Gradient));
  stroke_paint_combo->addItem(QObject::tr("Pattern"), static_cast<int>(VectorFillKind::Pattern));
  stroke_form->addRow(QObject::tr("Paint:"), stroke_paint_combo);

  auto* stroke_color_button = new QPushButton(stroke_group);
  stroke_color_button->setObjectName(QStringLiteral("shapeStrokeColorButton"));
  stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
  stroke_color_button->setText(QObject::tr("Color..."));
  stroke_form->addRow(QObject::tr("Color:"), stroke_color_button);

  auto* stroke_gradient_combo = new QComboBox(stroke_group);
  stroke_gradient_combo->setObjectName(QStringLiteral("shapeStrokeGradientCombo"));
  populate_gradient_combo(stroke_gradient_combo);
  stroke_form->addRow(QObject::tr("Gradient:"), stroke_gradient_combo);

  auto* stroke_gradient_type_combo = new QComboBox(stroke_group);
  stroke_gradient_type_combo->setObjectName(QStringLiteral("shapeStrokeGradientTypeCombo"));
  stroke_gradient_type_combo->addItem(QObject::tr("Linear"),
                                      static_cast<int>(LayerStyleGradientType::Linear));
  stroke_gradient_type_combo->addItem(QObject::tr("Radial"),
                                      static_cast<int>(LayerStyleGradientType::Radial));
  stroke_gradient_type_combo->addItem(QObject::tr("Angle"),
                                      static_cast<int>(LayerStyleGradientType::Angle));
  stroke_gradient_type_combo->addItem(QObject::tr("Reflected"),
                                      static_cast<int>(LayerStyleGradientType::Reflected));
  stroke_gradient_type_combo->addItem(QObject::tr("Diamond"),
                                      static_cast<int>(LayerStyleGradientType::Diamond));
  stroke_form->addRow(QObject::tr("Style:"), stroke_gradient_type_combo);

  auto* stroke_gradient_angle_spin = new UnitIntSpinBox(SpinUnit::Degrees, stroke_group);
  stroke_gradient_angle_spin->setObjectName(QStringLiteral("shapeStrokeGradientAngleSpin"));
  stroke_gradient_angle_spin->setRange(-180, 180);
  configure_dialog_spinbox(stroke_gradient_angle_spin, 72);
  add_spin_row(stroke_form, QObject::tr("Angle:"), stroke_gradient_angle_spin);

  auto* stroke_gradient_scale_spin = new QSpinBox(stroke_group);
  stroke_gradient_scale_spin->setObjectName(QStringLiteral("shapeStrokeGradientScaleSpin"));
  stroke_gradient_scale_spin->setRange(10, 1000);
  stroke_gradient_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(stroke_gradient_scale_spin, 72);
  add_spin_row(stroke_form, QObject::tr("Scale:"), stroke_gradient_scale_spin);

  auto* stroke_gradient_reverse_check = new QCheckBox(QObject::tr("Reverse"), stroke_group);
  stroke_gradient_reverse_check->setObjectName(
      QStringLiteral("shapeStrokeGradientReverseCheck"));
  stroke_form->addRow(QString(), stroke_gradient_reverse_check);

  auto* stroke_pattern_combo = new QComboBox(stroke_group);
  stroke_pattern_combo->setObjectName(QStringLiteral("shapeStrokePatternCombo"));
  populate_pattern_combo(stroke_pattern_combo);
  stroke_form->addRow(QObject::tr("Pattern:"), stroke_pattern_combo);

  auto* stroke_pattern_scale_spin = new QSpinBox(stroke_group);
  stroke_pattern_scale_spin->setObjectName(QStringLiteral("shapeStrokePatternScaleSpin"));
  stroke_pattern_scale_spin->setRange(1, 1000);
  stroke_pattern_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(stroke_pattern_scale_spin, 72);
  add_spin_row(stroke_form, QObject::tr("Scale:"), stroke_pattern_scale_spin);

  auto* stroke_pattern_angle_spin = new UnitSpinBox(SpinUnit::Degrees, stroke_group);
  stroke_pattern_angle_spin->setObjectName(QStringLiteral("shapeStrokePatternAngleSpin"));
  stroke_pattern_angle_spin->setRange(-180.0, 180.0);
  stroke_pattern_angle_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_angle_spin, 72);
  add_spin_row(stroke_form, QObject::tr("Angle:"), stroke_pattern_angle_spin);

  auto* stroke_pattern_offset_x_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_pattern_offset_x_spin->setObjectName(QStringLiteral("shapeStrokePatternOffsetXSpin"));
  stroke_pattern_offset_x_spin->setRange(-30000.0, 30000.0);
  stroke_pattern_offset_x_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_offset_x_spin, 80);
  add_spin_row(stroke_form, QObject::tr("Offset X:"), stroke_pattern_offset_x_spin);

  auto* stroke_pattern_offset_y_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_pattern_offset_y_spin->setObjectName(QStringLiteral("shapeStrokePatternOffsetYSpin"));
  stroke_pattern_offset_y_spin->setRange(-30000.0, 30000.0);
  stroke_pattern_offset_y_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_offset_y_spin, 80);
  add_spin_row(stroke_form, QObject::tr("Offset Y:"), stroke_pattern_offset_y_spin);

  auto* stroke_pattern_align_check = new QCheckBox(QObject::tr("Align with layer"), stroke_group);
  stroke_pattern_align_check->setObjectName(QStringLiteral("shapeStrokePatternAlignCheck"));
  stroke_pattern_align_check->setToolTip(
      QObject::tr("Anchor the tile grid to the layer's position; unchecked anchors it to the "
                  "document origin"));
  stroke_form->addRow(QString(), stroke_pattern_align_check);

  auto* stroke_align_combo = new QComboBox(stroke_group);
  stroke_align_combo->setObjectName(QStringLiteral("shapeStrokeAlignCombo"));
  stroke_align_combo->addItem(QObject::tr("Inside"),
                              static_cast<int>(VectorStrokeAlignment::Inside));
  stroke_align_combo->addItem(QObject::tr("Center"),
                              static_cast<int>(VectorStrokeAlignment::Center));
  stroke_align_combo->addItem(QObject::tr("Outside"),
                              static_cast<int>(VectorStrokeAlignment::Outside));
  stroke_form->addRow(QObject::tr("Align:"), stroke_align_combo);

  auto* stroke_cap_combo = new QComboBox(stroke_group);
  stroke_cap_combo->setObjectName(QStringLiteral("shapeStrokeCapCombo"));
  stroke_cap_combo->addItem(QObject::tr("Butt"), static_cast<int>(VectorStrokeCap::Butt));
  stroke_cap_combo->addItem(QObject::tr("Round"), static_cast<int>(VectorStrokeCap::Round));
  stroke_cap_combo->addItem(QObject::tr("Square", "stroke cap"),
                            static_cast<int>(VectorStrokeCap::Square));
  stroke_form->addRow(QObject::tr("Caps:"), stroke_cap_combo);

  auto* stroke_join_combo = new QComboBox(stroke_group);
  stroke_join_combo->setObjectName(QStringLiteral("shapeStrokeJoinCombo"));
  stroke_join_combo->addItem(QObject::tr("Miter"), static_cast<int>(VectorStrokeJoin::Miter));
  stroke_join_combo->addItem(QObject::tr("Round"), static_cast<int>(VectorStrokeJoin::Round));
  stroke_join_combo->addItem(QObject::tr("Bevel"), static_cast<int>(VectorStrokeJoin::Bevel));
  stroke_form->addRow(QObject::tr("Corners:"), stroke_join_combo);

  auto* stroke_dash_combo = new QComboBox(stroke_group);
  stroke_dash_combo->setObjectName(QStringLiteral("shapeStrokeDashCombo"));
  stroke_dash_combo->addItem(QObject::tr("Solid"));
  stroke_dash_combo->addItem(QObject::tr("Dashed"));
  stroke_dash_combo->addItem(QObject::tr("Dotted"));
  stroke_form->addRow(QObject::tr("Dashes:"), stroke_dash_combo);

  right_column->addWidget(stroke_group);

  left_column->addStretch(1);
  right_column->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* reset_button = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
  reset_button->setObjectName(QStringLiteral("shapeAppearanceResetButton"));
  reset_button->setToolTip(QObject::tr("Restore the default fill, stroke, opacity, and edge (the geometry stays)"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog_layout->addWidget(buttons);

  // Per-kind row visibility.
  const auto refresh_fill_rows = [=] {
    const auto kind = static_cast<VectorFillKind>(fill_kind_combo->currentData().toInt());
    const bool solid = kind == VectorFillKind::Solid;
    const bool gradient = kind == VectorFillKind::Gradient;
    const bool pattern = kind == VectorFillKind::Pattern;
    const auto set_row = [fill_form, field_rows](QWidget* field, bool visible) {
      if (const auto row = field_rows.find(field); row != field_rows.end()) {
        field = row->second;
      }
      field->setVisible(visible);
      if (auto* label = fill_form->labelForField(field); label != nullptr) {
        label->setVisible(visible);
      }
    };
    set_row(fill_color_row, solid);
    set_row(fill_gradient_combo, gradient);
    set_row(gradient_type_combo, gradient);
    set_row(gradient_angle_spin, gradient);
    set_row(gradient_scale_spin, gradient);
    set_row(gradient_reverse_check, gradient);
    set_row(fill_pattern_combo, pattern);
    set_row(pattern_scale_spin, pattern);
    set_row(pattern_angle_spin, pattern);
    set_row(pattern_offset_x_spin, pattern);
    set_row(pattern_offset_y_spin, pattern);
    set_row(pattern_align_check, pattern);
  };

  // The stroke rows only apply while the stroke is enabled; grey them out
  // rather than hiding so the dialog never changes height under the pointer.
  // The per-paint-kind rows additionally hide like the fill section's.
  const auto refresh_stroke_rows = [=] {
    const bool enabled = stroke_check->isChecked();
    const auto set_row = [stroke_form, field_rows](QWidget* field, bool row_enabled) {
      if (const auto row = field_rows.find(field); row != field_rows.end()) {
        field = row->second;
      }
      field->setEnabled(row_enabled);
      if (auto* label = stroke_form->labelForField(field); label != nullptr) {
        label->setEnabled(row_enabled);
      }
    };
    const auto set_row_visible = [stroke_form, field_rows](QWidget* field, bool visible) {
      if (const auto row = field_rows.find(field); row != field_rows.end()) {
        field = row->second;
      }
      field->setVisible(visible);
      if (auto* label = stroke_form->labelForField(field); label != nullptr) {
        label->setVisible(visible);
      }
    };
    const auto paint_kind = state->settings.stroke.content.kind;
    const bool solid_paint = paint_kind != VectorFillKind::Gradient &&
                             paint_kind != VectorFillKind::Pattern;
    const bool gradient_paint = paint_kind == VectorFillKind::Gradient;
    const bool pattern_paint = paint_kind == VectorFillKind::Pattern;
    set_row_visible(stroke_color_button, solid_paint);
    set_row_visible(stroke_gradient_combo, gradient_paint);
    set_row_visible(stroke_gradient_type_combo, gradient_paint);
    set_row_visible(stroke_gradient_angle_spin, gradient_paint);
    set_row_visible(stroke_gradient_scale_spin, gradient_paint);
    set_row_visible(stroke_gradient_reverse_check, gradient_paint);
    set_row_visible(stroke_pattern_combo, pattern_paint);
    set_row_visible(stroke_pattern_scale_spin, pattern_paint);
    set_row_visible(stroke_pattern_angle_spin, pattern_paint);
    set_row_visible(stroke_pattern_offset_x_spin, pattern_paint);
    set_row_visible(stroke_pattern_offset_y_spin, pattern_paint);
    set_row_visible(stroke_pattern_align_check, pattern_paint);
    for (QWidget* field :
         std::initializer_list<QWidget*>{stroke_width_spin, stroke_opacity_spin, stroke_paint_combo,
                                         stroke_color_button, stroke_gradient_combo,
                                         stroke_gradient_type_combo, stroke_gradient_angle_spin,
                                         stroke_gradient_scale_spin, stroke_gradient_reverse_check,
                                         stroke_pattern_combo, stroke_pattern_scale_spin,
                                         stroke_pattern_angle_spin, stroke_pattern_offset_x_spin,
                                         stroke_pattern_offset_y_spin, stroke_pattern_align_check,
                                         stroke_align_combo, stroke_cap_combo, stroke_join_combo,
                                         stroke_dash_combo}) {
      set_row(field, enabled);
    }
  };

  const auto sync_gradient_controls = [=] {
    QSignalBlocker type_blocker(gradient_type_combo);
    QSignalBlocker angle_blocker(gradient_angle_spin);
    QSignalBlocker scale_blocker(gradient_scale_spin);
    QSignalBlocker reverse_blocker(gradient_reverse_check);
    const auto& gradient = state->settings.fill.gradient;
    const auto type_index = gradient_type_combo->findData(static_cast<int>(gradient.type));
    gradient_type_combo->setCurrentIndex(std::max(0, type_index));
    gradient_angle_spin->setValue(static_cast<int>(std::lround(gradient.angle_degrees)));
    gradient_scale_spin->setValue(
        std::clamp(static_cast<int>(std::lround(gradient.scale * 100.0F)), 10, 1000));
    gradient_reverse_check->setChecked(gradient.reverse);
  };

  const auto sync_stroke_paint_controls = [=] {
    const auto& content = state->settings.stroke.content;
    {
      QSignalBlocker paint_blocker(stroke_paint_combo);
      const auto paint_index = stroke_paint_combo->findData(static_cast<int>(
          content.kind == VectorFillKind::Gradient || content.kind == VectorFillKind::Pattern
              ? content.kind
              : VectorFillKind::Solid));
      stroke_paint_combo->setCurrentIndex(std::max(0, paint_index));
    }
    {
      QSignalBlocker type_blocker(stroke_gradient_type_combo);
      QSignalBlocker angle_blocker(stroke_gradient_angle_spin);
      QSignalBlocker scale_blocker(stroke_gradient_scale_spin);
      QSignalBlocker reverse_blocker(stroke_gradient_reverse_check);
      const auto& gradient = content.gradient;
      stroke_gradient_type_combo->setCurrentIndex(std::max(
          0, stroke_gradient_type_combo->findData(static_cast<int>(gradient.type))));
      stroke_gradient_angle_spin->setValue(static_cast<int>(std::lround(gradient.angle_degrees)));
      stroke_gradient_scale_spin->setValue(
          std::clamp(static_cast<int>(std::lround(gradient.scale * 100.0F)), 10, 1000));
      stroke_gradient_reverse_check->setChecked(gradient.reverse);
    }
    {
      QSignalBlocker pattern_blocker(stroke_pattern_combo);
      if (const auto pattern_index =
              stroke_pattern_combo->findData(QString::fromStdString(content.pattern_id));
          pattern_index >= 0) {
        stroke_pattern_combo->setCurrentIndex(pattern_index);
      }
      QSignalBlocker scale_blocker(stroke_pattern_scale_spin);
      stroke_pattern_scale_spin->setValue(
          std::clamp(static_cast<int>(std::lround(content.pattern_scale * 100.0)), 1, 1000));
      QSignalBlocker angle_blocker(stroke_pattern_angle_spin);
      stroke_pattern_angle_spin->setValue(content.pattern_angle_degrees);
      QSignalBlocker offset_x_blocker(stroke_pattern_offset_x_spin);
      stroke_pattern_offset_x_spin->setValue(content.pattern_phase_x);
      QSignalBlocker offset_y_blocker(stroke_pattern_offset_y_spin);
      stroke_pattern_offset_y_spin->setValue(content.pattern_phase_y);
      QSignalBlocker align_blocker(stroke_pattern_align_check);
      stroke_pattern_align_check->setChecked(content.pattern_linked);
    }
  };

  // Syncs every control from state->settings (construction and Reset).
  const auto sync_all_controls = [=] {
    QSignalBlocker kind_blocker(fill_kind_combo);
    const auto kind_index = fill_kind_combo->findData(static_cast<int>(state->settings.fill.kind));
    fill_kind_combo->setCurrentIndex(std::max(0, kind_index));
    if (!state->settings.fill.pattern_id.empty()) {
      const auto pattern_index =
          fill_pattern_combo->findData(QString::fromStdString(state->settings.fill.pattern_id));
      if (pattern_index >= 0) {
        QSignalBlocker pattern_blocker(fill_pattern_combo);
        fill_pattern_combo->setCurrentIndex(pattern_index);
      }
    }
    QSignalBlocker pattern_scale_blocker(pattern_scale_spin);
    pattern_scale_spin->setValue(std::clamp(
        static_cast<int>(std::lround(state->settings.fill.pattern_scale * 100.0)), 1, 1000));
    QSignalBlocker pattern_angle_blocker(pattern_angle_spin);
    pattern_angle_spin->setValue(state->settings.fill.pattern_angle_degrees);
    QSignalBlocker pattern_offset_x_blocker(pattern_offset_x_spin);
    pattern_offset_x_spin->setValue(state->settings.fill.pattern_phase_x);
    QSignalBlocker pattern_offset_y_blocker(pattern_offset_y_spin);
    pattern_offset_y_spin->setValue(state->settings.fill.pattern_phase_y);
    QSignalBlocker pattern_align_blocker(pattern_align_check);
    pattern_align_check->setChecked(state->settings.fill.pattern_linked);
    QSignalBlocker align_blocker(stroke_align_combo);
    stroke_align_combo->setCurrentIndex(std::max(
        0, stroke_align_combo->findData(static_cast<int>(state->settings.stroke.alignment))));
    QSignalBlocker cap_blocker(stroke_cap_combo);
    stroke_cap_combo->setCurrentIndex(
        std::max(0, stroke_cap_combo->findData(static_cast<int>(state->settings.stroke.cap))));
    QSignalBlocker join_blocker(stroke_join_combo);
    stroke_join_combo->setCurrentIndex(
        std::max(0, stroke_join_combo->findData(static_cast<int>(state->settings.stroke.join))));
    QSignalBlocker dash_blocker(stroke_dash_combo);
    int dash_index = 0;
    for (int preset = 0; preset < 3; ++preset) {
      if (state->settings.stroke.dashes == dash_preset(preset)) {
        dash_index = preset;
        break;
      }
    }
    if (!state->settings.stroke.dashes.empty() &&
        state->settings.stroke.dashes != dash_preset(1) &&
        state->settings.stroke.dashes != dash_preset(2)) {
      // Preserve a custom dash pattern read from a PSD as its own entry.
      state->custom_dashes = state->settings.stroke.dashes;
      stroke_dash_combo->addItem(QObject::tr("Custom"));
      dash_index = 3;
    }
    stroke_dash_combo->setCurrentIndex(dash_index);
      for (auto* spin : {layer_opacity_spin, layer_fill_opacity_spin, stroke_opacity_spin, density_spin}) {
      QSignalBlocker blocker(spin);
      spin->setValue(spin == layer_opacity_spin ? static_cast<int>(std::lround(state->settings.layer_opacity * 100.0F))
                     : spin == layer_fill_opacity_spin ? static_cast<int>(std::lround(state->settings.fill_opacity * 100.0F))
                     : spin == stroke_opacity_spin ? static_cast<int>(std::lround(state->settings.stroke.opacity * 100.0))
                     : static_cast<int>(std::lround(state->settings.density * 100.0 / 255.0)));
    }
    {
      QSignalBlocker feather_blocker(feather_spin);
      feather_spin->setValue(state->settings.feather);
      QSignalBlocker width_blocker(stroke_width_spin);
      stroke_width_spin->setValue(state->settings.stroke.width);
      QSignalBlocker check_blocker(stroke_check);
      stroke_check->setChecked(state->settings.stroke.enabled);
    }
    fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
    stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
    sync_gradient_controls();
    sync_stroke_paint_controls();
  };
  sync_all_controls();
  // Measure the page while EVERY per-kind row is still visible, so switching
  // a paint kind later never widens the columns past the viewport (a hidden
  // horizontal bar would clip the steppers) and the dialog never changes
  // width under the pointer - the Layer Style stroke-page rule.
  page->layout()->activate();
  // Reserve the vertical scrollbar's width too: a paint-kind switch that
  // makes the page taller than the dialog adds the bar, which would otherwise
  // steal those pixels from the rows and clip the right column's steppers.
  scroll->setMinimumWidth(page->minimumSizeHint().width() +
                          scroll->verticalScrollBar()->sizeHint().width());
  refresh_fill_rows();
  refresh_stroke_rows();

  // --- Wiring ---
  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [=] {
    // Factory appearance; the geometry the dialog opened with stays.
    auto restored = reset_defaults;
    restored.geometry = state->settings.geometry;
    state->settings = std::move(restored);
    state->stroke_paint_touched = true;
    state->custom_dashes.clear();
    sync_all_controls();
    refresh_fill_rows();
    refresh_stroke_rows();
    notify();
  });
  QObject::connect(fill_kind_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.kind =
        static_cast<VectorFillKind>(fill_kind_combo->currentData().toInt());
    if (state->settings.fill.kind == VectorFillKind::Gradient &&
        state->settings.fill.gradient.color_stops.empty()) {
      // First switch to Gradient: seed from the selected preset (or FG->BG).
      if (gradient_library != nullptr && fill_gradient_combo->count() > 0) {
        if (const auto* entry =
                gradient_library->find_entry(fill_gradient_combo->currentData().toString());
            entry != nullptr) {
          static_cast<GradientDefinition&>(state->settings.fill.gradient) =
              resolve_gradient_definition(entry->definition, foreground, background);
        }
      }
      if (state->settings.fill.gradient.color_stops.empty()) {
        auto& gradient = state->settings.fill.gradient;
        gradient.color_stops = {GradientColorStop{0.0F, foreground, 0.5F},
                                GradientColorStop{1.0F, background, 0.5F}};
        gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F},
                                GradientAlphaStop{1.0F, 1.0F, 0.5F}};
      }
      sync_gradient_controls();
    }
    if (state->settings.fill.kind == VectorFillKind::Pattern &&
        state->settings.fill.pattern_id.empty() && fill_pattern_combo->count() > 0) {
      state->settings.fill.pattern_id = fill_pattern_combo->currentData().toString().toStdString();
      state->settings.fill.pattern_name = fill_pattern_combo->currentText().toStdString();
    }
    refresh_fill_rows();
    notify();
  });
  QObject::connect(fill_color_button, &QPushButton::clicked, &dialog, [=, &dialog] {
    const auto& current = state->settings.fill.color;
    const auto chosen = request_patchy_color(
        &dialog, QColor(current.red, current.green, current.blue), QObject::tr("Shape Fill Color"));
    if (chosen.has_value()) {
      state->settings.fill.color = RgbColor{static_cast<std::uint8_t>(chosen->red()),
                                            static_cast<std::uint8_t>(chosen->green()),
                                            static_cast<std::uint8_t>(chosen->blue())};
      fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
      notify();
    }
  });
  QObject::connect(fill_gradient_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    if (gradient_library == nullptr) {
      return;
    }
    if (const auto* entry =
            gradient_library->find_entry(fill_gradient_combo->currentData().toString());
        entry != nullptr) {
      // Presets replace the definition; the geometry (type/angle/scale/
      // reverse) is the user's and stays.
      static_cast<GradientDefinition&>(state->settings.fill.gradient) =
          resolve_gradient_definition(entry->definition, foreground, background);
      notify();
    }
  });
  QObject::connect(gradient_type_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.gradient.type =
        static_cast<LayerStyleGradientType>(gradient_type_combo->currentData().toInt());
    notify();
  });
  QObject::connect(gradient_angle_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.angle_degrees = static_cast<float>(value);
    notify();
  });
  QObject::connect(gradient_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.scale = static_cast<float>(value) / 100.0F;
    notify();
  });
  QObject::connect(gradient_reverse_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.fill.gradient.reverse = checked;
    notify();
  });
  QObject::connect(fill_pattern_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.pattern_id = fill_pattern_combo->currentData().toString().toStdString();
    state->settings.fill.pattern_name = fill_pattern_combo->currentText().toStdString();
    notify();
  });
  QObject::connect(pattern_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.pattern_scale = static_cast<double>(value) / 100.0;
    notify();
  });
  QObject::connect(pattern_angle_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_angle_degrees = value;
    notify();
  });
  QObject::connect(pattern_offset_x_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_phase_x = value;
    notify();
  });
  QObject::connect(pattern_offset_y_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_phase_y = value;
    notify();
  });
  QObject::connect(pattern_align_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.fill.pattern_linked = checked;
    notify();
  });
  QObject::connect(stroke_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.enabled = checked;
    refresh_stroke_rows();
    notify();
  });
  QObject::connect(stroke_width_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.stroke.width = value;
    notify();
  });
  QObject::connect(stroke_color_button, &QPushButton::clicked, &dialog, [=, &dialog] {
    const auto& current = state->settings.stroke.content.color;
    const auto chosen =
        request_patchy_color(&dialog, QColor(current.red, current.green, current.blue),
                             QObject::tr("Shape Stroke Color"));
    if (chosen.has_value()) {
      state->settings.stroke.content.kind = VectorFillKind::Solid;
      state->settings.stroke.content.color =
          RgbColor{static_cast<std::uint8_t>(chosen->red()),
                   static_cast<std::uint8_t>(chosen->green()),
                   static_cast<std::uint8_t>(chosen->blue())};
      state->stroke_paint_touched = true;
      stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
      notify();
    }
  });
  QObject::connect(stroke_align_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.alignment =
        static_cast<VectorStrokeAlignment>(stroke_align_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_cap_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.cap = static_cast<VectorStrokeCap>(stroke_cap_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_join_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.join =
        static_cast<VectorStrokeJoin>(stroke_join_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_dash_combo, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
    state->settings.stroke.dashes = index <= 2 ? dash_preset(index) : state->custom_dashes;
    notify();
  });
  QObject::connect(stroke_paint_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    auto& content = state->settings.stroke.content;
    content.kind = static_cast<VectorFillKind>(stroke_paint_combo->currentData().toInt());
    state->stroke_paint_touched = true;
    if (content.kind == VectorFillKind::Gradient && content.gradient.color_stops.empty()) {
      // First switch to Gradient: seed from the selected preset (or FG->BG),
      // the fill kind-combo's convention.
      if (gradient_library != nullptr && stroke_gradient_combo->count() > 0) {
        if (const auto* entry =
                gradient_library->find_entry(stroke_gradient_combo->currentData().toString());
            entry != nullptr) {
          static_cast<GradientDefinition&>(content.gradient) =
              resolve_gradient_definition(entry->definition, foreground, background);
        }
      }
      if (content.gradient.color_stops.empty()) {
        content.gradient.color_stops = {GradientColorStop{0.0F, foreground, 0.5F},
                                        GradientColorStop{1.0F, background, 0.5F}};
        content.gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F},
                                        GradientAlphaStop{1.0F, 1.0F, 0.5F}};
      }
      sync_stroke_paint_controls();
    }
    if (content.kind == VectorFillKind::Pattern && content.pattern_id.empty() &&
        stroke_pattern_combo->count() > 0) {
      content.pattern_id = stroke_pattern_combo->currentData().toString().toStdString();
      content.pattern_name = stroke_pattern_combo->currentText().toStdString();
    }
    refresh_stroke_rows();
    notify();
  });
  QObject::connect(stroke_gradient_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    if (gradient_library == nullptr) {
      return;
    }
    if (const auto* entry =
            gradient_library->find_entry(stroke_gradient_combo->currentData().toString());
        entry != nullptr) {
      static_cast<GradientDefinition&>(state->settings.stroke.content.gradient) =
          resolve_gradient_definition(entry->definition, foreground, background);
      state->stroke_paint_touched = true;
      notify();
    }
  });
  QObject::connect(stroke_gradient_type_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.content.gradient.type =
        static_cast<LayerStyleGradientType>(stroke_gradient_type_combo->currentData().toInt());
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_gradient_angle_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.gradient.angle_degrees = static_cast<float>(value);
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_gradient_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.gradient.scale = static_cast<float>(value) / 100.0F;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_gradient_reverse_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.content.gradient.reverse = checked;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.content.pattern_id =
        stroke_pattern_combo->currentData().toString().toStdString();
    state->settings.stroke.content.pattern_name = stroke_pattern_combo->currentText().toStdString();
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.pattern_scale = static_cast<double>(value) / 100.0;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_angle_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_angle_degrees = value;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_offset_x_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_phase_x = value;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_offset_y_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_phase_y = value;
    state->stroke_paint_touched = true;
    notify();
  });
  QObject::connect(stroke_pattern_align_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.content.pattern_linked = checked;
    state->stroke_paint_touched = true;
    notify();
  });

  // Value spins commit on Enter/arrows/focus-out only: per-keystroke preview
  // renders made typing "10" into the pattern scale render at "1" first (a
  // 10000-texel-per-pixel minification).
  for (auto* spin : dialog.findChildren<QDoubleSpinBox*>()) {
    spin->setKeyboardTracking(false);
  }
  for (auto* spin : dialog.findChildren<QSpinBox*>()) {
    spin->setKeyboardTracking(false);
  }

  // Size from the page's own hint (QScrollArea::sizeHint caps at ~24 font
  // heights, which would show a needless scrollbar on any normal screen) and
  // cap at the available screen height; the scrollbar earns its width only
  // when the cap applies. Resizing here, before remember_dialog_position,
  // keeps the centering and on-screen clamp working from the real size.
  // The width measurement above cached every widget's hint inside its parent
  // layout's item (QWidgetItemV2) with all rows visible; the per-kind hides
  // since then invalidated layouts but not those caches, which only a
  // widget's own updateGeometry clears (the dialog is not shown yet, so the
  // posted layout requests have not run). Clear them so the height below
  // counts the visible rows only.
  for (auto* child : page->findChildren<QWidget*>()) {
    child->updateGeometry();
  }
  for (auto* layout : page->findChildren<QLayout*>()) {
    layout->invalidate();
  }
  page->layout()->activate();
  const auto page_hint = page->sizeHint();
  const auto margins = dialog_layout->contentsMargins();
  int width = std::max(page_hint.width(), scroll->minimumWidth()) + margins.left() + margins.right();
  int height = page_hint.height() + buttons->sizeHint().height() + dialog_layout->spacing() +
               margins.top() + margins.bottom();
  const QScreen* screen = parent != nullptr ? parent->screen() : nullptr;
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    const auto max_height = std::max(320, available.height() - 40);
    if (height > max_height) {
      height = max_height;  // the bar's width is already reserved above
    }
  }
  dialog.resize(width, height);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return state->settings;
}

}  // namespace patchy::ui
