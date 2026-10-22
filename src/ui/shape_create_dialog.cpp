// Create <Shape> dialog for a tapped shape tool (docs/vector-tools.md).
#include "ui/shape_create_dialog.hpp"

#include "ui/action_icons.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::ui {

std::optional<ShapeCreateResult> request_shape_create_settings(QWidget* parent,
                                                               const ShapeCreateRequest& request) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("shapeCreateDialog"));
  switch (request.tool) {
    case CanvasTool::Ellipse:
      dialog.setWindowTitle(QObject::tr("Create Ellipse"));
      break;
    case CanvasTool::Polygon:
      dialog.setWindowTitle(QObject::tr("Create Polygon"));
      break;
    case CanvasTool::CustomShape:
      dialog.setWindowTitle(QObject::tr("Create Custom Shape"));
      break;
    default:
      dialog.setWindowTitle(QObject::tr("Create Rectangle"));
      break;
  }
  // A system title bar adds the window icon and buttons beside the title;
  // leave the whole title room in every language (GitHub issue 309).
  QFont title_font = dialog.font();
  title_font.setBold(true);
  dialog.setMinimumWidth(
      std::max(320, QFontMetrics(title_font).horizontalAdvance(dialog.windowTitle()) + 200));
  auto* dialog_layout = new QVBoxLayout(&dialog);

  // Label, field, label, field: Width and Height share a row, and the
  // per-corner radii sit under them in the same columns.
  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(8);
  grid->setColumnStretch(4, 1);
  dialog_layout->addLayout(grid);
  int row = 0;
  const auto make_label = [&dialog](const QString& text) {
    auto* label = new QLabel(text, &dialog);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
  };
  const auto make_spin = [&dialog](const char* name, double minimum, double maximum, double value) {
    auto* spin = new UnitSpinBox(SpinUnit::Pixels, &dialog);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(minimum, maximum);
    spin->setDecimals(1);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    configure_dialog_spinbox(spin, 96);
    return spin;
  };

  // Custom Shape: the options bar's Shape picker, on the tool's current shape.
  QComboBox* shape_combo = nullptr;
  if (request.tool == CanvasTool::CustomShape) {
    shape_combo = new QComboBox(&dialog);
    shape_combo->setObjectName(QStringLiteral("shapeCreateCustomShapeCombo"));
    if (request.custom_shapes != nullptr) {
      populate_custom_shape_combo(*shape_combo, *request.custom_shapes);
    }
    if (const auto index = shape_combo->findData(request.custom_shape_id); index >= 0) {
      shape_combo->setCurrentIndex(index);
    }
    grid->addWidget(make_label(QObject::tr("Shape:")), row, 0);
    grid->addWidget(shape_combo, row, 1, 1, 3);
    ++row;
  }

  auto* width_spin = make_spin("shapeCreateWidthSpin", 1.0, 30000.0, request.width);
  auto* height_spin = make_spin("shapeCreateHeightSpin", 1.0, 30000.0, request.height);
  apply_document_field_units(width_spin, request.units, true);
  apply_document_field_units(height_spin, request.units, false);
  link_field_unit_picks({width_spin, height_spin});
  grid->addWidget(make_label(QObject::tr("Width:")), row, 0);
  grid->addWidget(width_spin, row, 1);
  grid->addWidget(make_label(QObject::tr("Height:")), row, 2);
  grid->addWidget(height_spin, row, 3);
  ++row;

  QSpinBox* sides_spin = nullptr;
  if (request.tool == CanvasTool::Polygon) {
    sides_spin = new QSpinBox(&dialog);
    sides_spin->setObjectName(QStringLiteral("shapeCreateSidesSpin"));
    sides_spin->setRange(3, 100);
    sides_spin->setValue(std::clamp(request.polygon_sides, 3, 100));
    sides_spin->setKeyboardTracking(false);
    configure_dialog_spinbox(sides_spin, 96);
    grid->addWidget(make_label(QObject::tr("Sides:")), row, 0);
    grid->addWidget(sides_spin, row, 1);
    ++row;
  }

  // Rectangle: one Radius for all four corners while linked (the common case);
  // unlinking shows the four corners. Corners that differ open unlinked so
  // the dialog never flattens them.
  QDoubleSpinBox* radius_spin = nullptr;
  QToolButton* radius_link = nullptr;
  std::array<QDoubleSpinBox*, 4> corner_spins{nullptr, nullptr, nullptr, nullptr};
  if (request.tool == CanvasTool::Rectangle) {
    auto radius_units = request.units;  // a radius has no percent basis
    radius_units.document_width = 0.0;
    radius_units.document_height = 0.0;
    const auto make_radius_spin = [&](const char* name, double value) {
      auto* spin = make_spin(name, 0.0, 30000.0, value);
      spin->set_context_provider([radius_units] { return document_field_context(radius_units, true); });
      return spin;
    };
    const auto& radii = request.corner_radii;
    const bool corners_agree = std::all_of(radii.begin(), radii.end(), [&](double radius) {
      return std::abs(radius - radii.front()) < 1e-9;
    });
    radius_spin = make_radius_spin("shapeCreateRadiusSpin", radii.front());
    radius_link = new QToolButton(&dialog);
    radius_link->setObjectName(QStringLiteral("shapeCreateRadiusLinkButton"));
    radius_link->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
    radius_link->setIconSize(QSize(18, 18));
    radius_link->setCheckable(true);
    radius_link->setChecked(corners_agree);
    radius_link->setToolTip(QObject::tr("Change all four corner radii together"));
    auto* radius_row = new QHBoxLayout();
    radius_row->setSpacing(6);
    radius_row->addWidget(radius_spin);
    radius_row->addWidget(radius_link);
    radius_row->addStretch(1);
    grid->addWidget(make_label(QObject::tr("Radius:")), row, 0);
    grid->addLayout(radius_row, row, 1, 1, 3);
    ++row;

    // Model order TL, TR, BR, BL; laid out as the corners sit.
    const std::array<const char*, 4> names{
        "shapeCreateRadiusTopLeftSpin", "shapeCreateRadiusTopRightSpin",
        "shapeCreateRadiusBottomRightSpin", "shapeCreateRadiusBottomLeftSpin"};
    const std::array<QString, 4> labels{QObject::tr("Top left:"), QObject::tr("Top right:"),
                                        QObject::tr("Bottom right:"), QObject::tr("Bottom left:")};
    const std::array<std::pair<int, int>, 4> cells{std::pair{0, 0}, std::pair{0, 2}, std::pair{1, 2},
                                                   std::pair{1, 0}};
    std::array<QLabel*, 4> corner_labels{nullptr, nullptr, nullptr, nullptr};
    // Insert row by row, label before field, so each label scrubs its own field.
    for (const std::size_t corner : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{2}}) {
      corner_labels[corner] = make_label(labels[corner]);
      corner_spins[corner] = make_radius_spin(names[corner], radii[corner]);
      const auto [corner_row, column] = cells[corner];
      grid->addWidget(corner_labels[corner], row + corner_row, column);
      grid->addWidget(corner_spins[corner], row + corner_row, column + 1);
    }
    row += 2;

    const auto show_linked = [&dialog, radius_spin, corner_spins, corner_labels](bool linked) {
      radius_spin->setVisible(linked);
      for (std::size_t corner = 0; corner < 4; ++corner) {
        corner_labels[corner]->setVisible(!linked);
        corner_spins[corner]->setVisible(!linked);
      }
      if (dialog.isVisible()) {
        dialog.adjustSize();
      }
    };
    show_linked(corners_agree);
    QObject::connect(radius_spin, &QDoubleSpinBox::valueChanged, &dialog,
                     [radius_link, corner_spins](double value) {
                       if (!radius_link->isChecked()) {
                         return;
                       }
                       for (auto* spin : corner_spins) {
                         spin->setValue(value);
                       }
                     });
    // Linking takes the top-left corner for all four; unlinking starts every
    // corner at the shared radius.
    QObject::connect(radius_link, &QToolButton::toggled, &dialog,
                     [radius_spin, corner_spins, show_linked](bool linked) {
                       if (linked) {
                         const double value = corner_spins[0]->value();
                         radius_spin->setValue(value);
                         for (auto* spin : corner_spins) {
                           spin->setValue(value);
                         }
                       } else {
                         for (auto* spin : corner_spins) {
                           spin->setValue(radius_spin->value());
                         }
                       }
                       show_linked(linked);
                     });
  }

  auto* from_center = new QCheckBox(QObject::tr("From Center"), &dialog);
  from_center->setObjectName(QStringLiteral("shapeCreateFromCenterCheck"));
  from_center->setChecked(request.from_center);
  grid->addWidget(from_center, row, 1, 1, 3);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog_layout->addWidget(buttons);
  append_themed_style(dialog, dialog_spinbox_button_style());
  width_spin->setFocus();
  width_spin->selectAll();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  ShapeCreateResult result;
  result.width = width_spin->value();
  result.height = height_spin->value();
  result.from_center = from_center->isChecked();
  if (radius_spin != nullptr) {
    const bool linked = radius_link->isChecked();
    for (std::size_t corner = 0; corner < 4; ++corner) {
      result.corner_radii[corner] = linked ? radius_spin->value() : corner_spins[corner]->value();
    }
  }
  result.polygon_sides = sides_spin != nullptr ? sides_spin->value() : request.polygon_sides;
  result.custom_shape_id =
      shape_combo != nullptr ? shape_combo->currentData().toString() : request.custom_shape_id;
  return result;
}

}  // namespace patchy::ui
