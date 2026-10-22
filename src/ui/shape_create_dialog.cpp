// Create <Shape> dialog for a tapped shape tool (docs/vector-tools.md).
#include "ui/shape_create_dialog.hpp"

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
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
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

  // Rectangle: per-corner radii in the Width/Height columns.
  std::array<QDoubleSpinBox*, 4> corner_spins{nullptr, nullptr, nullptr, nullptr};
  if (request.tool == CanvasTool::Rectangle) {
    auto radius_units = request.units;  // a radius has no percent basis
    radius_units.document_width = 0.0;
    radius_units.document_height = 0.0;
    // Model order TL, TR, BR, BL; laid out as the corners sit.
    const std::array<const char*, 4> names{
        "shapeCreateRadiusTopLeftSpin", "shapeCreateRadiusTopRightSpin",
        "shapeCreateRadiusBottomRightSpin", "shapeCreateRadiusBottomLeftSpin"};
    const std::array<QString, 4> labels{QObject::tr("Top left:"), QObject::tr("Top right:"),
                                        QObject::tr("Bottom right:"), QObject::tr("Bottom left:")};
    const std::array<std::pair<int, int>, 4> cells{std::pair{0, 0}, std::pair{0, 2}, std::pair{1, 2},
                                                   std::pair{1, 0}};
    // Insert row by row, label before field, so each label scrubs its own field.
    for (const std::size_t corner : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{2}}) {
      auto* spin = make_spin(names[corner], 0.0, 30000.0, request.corner_radii[corner]);
      spin->set_context_provider([radius_units] { return document_field_context(radius_units, true); });
      corner_spins[corner] = spin;
      const auto [corner_row, column] = cells[corner];
      grid->addWidget(make_label(labels[corner]), row + corner_row, column);
      grid->addWidget(spin, row + corner_row, column + 1);
    }
    row += 2;
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
  for (std::size_t corner = 0; corner < 4; ++corner) {
    result.corner_radii[corner] = corner_spins[corner] != nullptr ? corner_spins[corner]->value() : 0.0;
  }
  result.polygon_sides = sides_spin != nullptr ? sides_spin->value() : request.polygon_sides;
  result.custom_shape_id =
      shape_combo != nullptr ? shape_combo->currentData().toString() : request.custom_shape_id;
  return result;
}

}  // namespace patchy::ui
