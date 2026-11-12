// Shape appearance dialog: fill (none/solid/gradient/pattern) and stroke
// (width/paint/alignment/caps/joins/dashes) for shape and fill layers, with
// live preview through the caller's callback. Gradient presets come from the
// GradientLibrary; patterns list the document store first, then the library
// (adoption into the store happens in the caller when applying).
#include "ui/shape_appearance_dialog.hpp"

#include "ui/stroke_preset_manager_dialog.hpp"
#include "ui/appearance_properties.hpp"
#include "ui/angle_dial.hpp"

#include "core/pattern_resource.hpp"
#include "ui/color_panel.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/custom_shape_picker.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/pattern_library.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"

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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointer>
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
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Shape Appearance pattern rows: the name a picked pattern saves under.
constexpr int kPatternSavedNameRole = Qt::UserRole + 1;

QIcon color_swatch_icon(RgbColor color) {
  QPixmap pixmap(28, 14);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(theme().swatch_border);
  painter.setBrush(QColor(color.red, color.green, color.blue));
  painter.drawRect(0, 0, 27, 13);
  return QIcon(pixmap);
}

// Dashes combo rows carry a StrokeDashPreset id (or kDashCustom) as item data;
// the patterns are stroke-width multiples like the vstk descriptor and most
// follow the cap (stroke_dash_preset_pattern).
constexpr int kDashCustom = 3;

int dash_row_for(const std::vector<double>& dashes, VectorStrokeCap cap) {
  const auto preset = stroke_dash_preset_for(dashes, cap);
  return preset.has_value() ? static_cast<int>(*preset) : kDashCustom;
}

QLocale dash_pattern_locale() {
  QLocale locale;
  locale.setNumberOptions(QLocale::OmitGroupSeparator);
  return locale;
}

}  // namespace

QString stroke_dash_preset_label(StrokeDashPreset preset) {
  switch (preset) {
    case StrokeDashPreset::Solid:
      return QObject::tr("Solid");
    case StrokeDashPreset::Dashed:
      return QObject::tr("Dashed");
    case StrokeDashPreset::LongDashes:
      return QObject::tr("Long dashes");
    case StrokeDashPreset::ShortDashes:
      return QObject::tr("Short dashes");
    case StrokeDashPreset::Dotted:
      return QObject::tr("Dotted");
    case StrokeDashPreset::SparseDots:
      return QObject::tr("Sparse dots");
    case StrokeDashPreset::DashDot:
      return QObject::tr("Dash dot");
    case StrokeDashPreset::DashDotDot:
      return QObject::tr("Dash dot dot");
  }
  return {};
}

QString dash_pattern_text(const std::vector<double>& dashes) {
  const auto locale = dash_pattern_locale();
  QStringList parts;
  for (const auto dash : dashes) parts << locale.toString(dash, 'g', 6);
  return parts.join(QLatin1Char(' '));
}

// Lengths separated by spaces (or semicolons; commas too where the decimal
// point is not a comma). An odd count repeats once, as SVG and PostScript
// read it, so "4" is 4 on, 4 off. At least one length must be above zero.
std::optional<std::vector<double>> parse_dash_pattern_text(const QString& text) {
  const auto locale = dash_pattern_locale();
  QString separators = QStringLiteral(";");
  if (locale.decimalPoint() != QStringLiteral(",")) separators += QLatin1Char(',');
  QString spaced = text;
  for (const auto separator : separators) spaced.replace(separator, QLatin1Char(' '));
  const auto tokens = spaced.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  constexpr qsizetype kMaxDashes = 64;  // the scripting API's limit
  if (tokens.isEmpty() || tokens.size() > kMaxDashes) return std::nullopt;
  std::vector<double> dashes;
  bool any_positive = false;
  for (const auto& token : tokens) {
    bool ok = false;
    auto value = locale.toDouble(token, &ok);
    if (!ok) value = QLocale::c().toDouble(token, &ok);
    if (!ok || !std::isfinite(value) || value < 0.0 || value > 10000.0) return std::nullopt;
    any_positive |= value > 0.0;
    dashes.push_back(value);
  }
  if (!any_positive) return std::nullopt;
  if (dashes.size() % 2 != 0) {
    if (dashes.size() * 2 > static_cast<std::size_t>(kMaxDashes)) return std::nullopt;
    const auto copy = dashes;
    dashes.insert(dashes.end(), copy.begin(), copy.end());
  }
  return dashes;
}

namespace {

// The saved tokens match the scripting API's stroke values (script_vector_paints.cpp).
const std::array<const char*, 3> kAlignmentTokens{"inside", "center", "outside"};
const std::array<const char*, 3> kCapTokens{"butt", "round", "square"};
const std::array<const char*, 3> kJoinTokens{"miter", "round", "bevel"};

template <typename Enum>
QString enum_token(const std::array<const char*, 3>& tokens, Enum value) {
  const auto index = static_cast<std::size_t>(value);
  return QLatin1String(tokens[index < tokens.size() ? index : 0]);
}

template <typename Enum>
Enum enum_from_token(const std::array<const char*, 3>& tokens, const QString& token, Enum fallback) {
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (token == QLatin1String(tokens[i])) return static_cast<Enum>(i);
  }
  return fallback;
}

bool same_lengths(const std::vector<double>& a, const std::vector<double>& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) { return std::abs(x - y) < 1e-9; });
}

}  // namespace

StrokeStylePreset stroke_style_preset_from(const VectorStroke& stroke, QString name) {
  return {std::move(name), stroke.alignment, stroke.cap, stroke.join, stroke.dashes,
          stroke.dashes.empty() ? 0.0 : stroke.dash_offset};
}

void apply_stroke_style_preset(VectorStroke& stroke, const StrokeStylePreset& preset) {
  stroke.alignment = preset.alignment;
  stroke.cap = preset.cap;
  stroke.join = preset.join;
  stroke.dashes = preset.dashes;
  stroke.dash_offset = preset.dash_offset;
}

bool stroke_matches_style_preset(const VectorStroke& stroke, const StrokeStylePreset& preset) {
  // A solid stroke's offset draws nothing, so it never decides a match.
  const double offset = stroke.dashes.empty() ? 0.0 : stroke.dash_offset;
  return stroke.alignment == preset.alignment && stroke.cap == preset.cap && stroke.join == preset.join &&
         same_lengths(stroke.dashes, preset.dashes) && std::abs(offset - preset.dash_offset) < 1e-9;
}

QString stroke_style_presets_json(const std::vector<StrokeStylePreset>& presets) {
  QJsonArray array;
  for (const auto& preset : presets) {
    QJsonArray dashes;
    for (const auto dash : preset.dashes) dashes.append(dash);
    array.append(QJsonObject{{QStringLiteral("name"), preset.name},
                             {QStringLiteral("alignment"), enum_token(kAlignmentTokens, preset.alignment)},
                             {QStringLiteral("cap"), enum_token(kCapTokens, preset.cap)},
                             {QStringLiteral("join"), enum_token(kJoinTokens, preset.join)},
                             {QStringLiteral("dashes"), dashes},
                             {QStringLiteral("dashOffset"), preset.dash_offset}});
  }
  return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

std::vector<StrokeStylePreset> parse_stroke_style_presets_json(const QString& json) {
  std::vector<StrokeStylePreset> presets;
  const auto document = QJsonDocument::fromJson(json.toUtf8());
  if (!document.isArray()) return presets;
  for (const auto& value : document.array()) {
    const auto object = value.toObject();
    StrokeStylePreset preset;
    preset.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (preset.name.isEmpty()) continue;
    preset.alignment = enum_from_token(kAlignmentTokens, object.value(QStringLiteral("alignment")).toString(),
                                       VectorStrokeAlignment::Inside);
    preset.cap = enum_from_token(kCapTokens, object.value(QStringLiteral("cap")).toString(), VectorStrokeCap::Butt);
    preset.join =
        enum_from_token(kJoinTokens, object.value(QStringLiteral("join")).toString(), VectorStrokeJoin::Miter);
    bool usable = true;
    for (const auto& dash : object.value(QStringLiteral("dashes")).toArray()) {
      const auto length = dash.toDouble(-1.0);
      usable = usable && std::isfinite(length) && length >= 0.0 && length <= 10000.0;
      preset.dashes.push_back(length);
    }
    usable = usable && preset.dashes.size() <= 64 &&
             (preset.dashes.empty() ||
              std::any_of(preset.dashes.begin(), preset.dashes.end(), [](double d) { return d > 0.0; }));
    const auto offset = object.value(QStringLiteral("dashOffset")).toDouble(0.0);
    preset.dash_offset = std::isfinite(offset) && !preset.dashes.empty() ? std::clamp(offset, -1000.0, 1000.0) : 0.0;
    if (usable) presets.push_back(std::move(preset));
  }
  return presets;
}

bool stroke_preset_name_is_builtin(const QString& name) {
  const auto trimmed = name.trimmed();
  return std::any_of(stroke_dash_presets_in_menu_order().begin(), stroke_dash_presets_in_menu_order().end(),
                     [&](StrokeDashPreset preset) {
                       return stroke_dash_preset_label(preset).compare(trimmed, Qt::CaseInsensitive) == 0;
                     });
}

StrokeStylePreset builtin_stroke_style_preset(StrokeDashPreset preset) {
  VectorStroke stroke;
  stroke.alignment = VectorStrokeAlignment::Inside;
  stroke.cap = VectorStrokeCap::Butt;
  stroke.join = VectorStrokeJoin::Miter;
  apply_stroke_dash_preset(stroke, preset);
  return stroke_style_preset_from(stroke, stroke_dash_preset_label(preset));
}

int matching_stroke_style_preset(const VectorStroke& stroke, const std::vector<StrokeStylePreset>& presets) {
  for (std::size_t i = 0; i < presets.size(); ++i) {
    if (stroke_matches_style_preset(stroke, presets[i])) return static_cast<int>(i);
  }
  return -1;
}

std::vector<StrokeStylePreset> load_stroke_style_presets() {
  return parse_stroke_style_presets_json(
      app_settings().value(QLatin1String(kStrokeStylePresetsKey)).toString());
}

void save_stroke_style_presets(const std::vector<StrokeStylePreset>& presets) {
  auto settings = app_settings();
  if (presets.empty()) {
    settings.remove(QLatin1String(kStrokeStylePresetsKey));
  } else {
    settings.setValue(QLatin1String(kStrokeStylePresetsKey), stroke_style_presets_json(presets));
  }
  settings.sync();
}

namespace {

struct DialogState {
  ShapeAppearanceSettings settings;
  // Set on any stroke-paint interaction (paint kind, color, gradient, or
  // pattern controls); an untouched PSD-authored paint round-trips verbatim
  // because the model is only rewritten when a control changes it.
  bool stroke_paint_touched{false};
  // The Custom row's pattern: one read from a file or typed in the Pattern
  // field, restorable after trying the presets.
  std::vector<double> custom_dashes;
  // The user picked Custom: keep that row even when the pattern matches a preset.
  bool custom_dash_row{false};
  // The gradients the fill and stroke Custom rows restore: the layer's own when it
  // matches no preset (GitHub #578, #579).
  GradientDefinition custom_fill_gradient;
  GradientDefinition custom_stroke_gradient;
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

// The left column's sections share one label column and one field column (GitHub #641).
// Geometry and Radius are grids: label, link column, field. The link column holds the
// chain brackets (the Image Size dialog's Width / Height link), which a QFormLayout row
// cannot span, and stays empty on rows without one. The form sections use the link
// column's width as their label-to-field gap, so their fields start at the same x.
constexpr int kSectionColumnGap = 8;
constexpr int kSectionLinkColumnWidth = 24;
constexpr int kSectionFormFieldGap = kSectionColumnGap * 2 + kSectionLinkColumnWidth;

struct GridSection {
  QGroupBox* group{nullptr};
  QVBoxLayout* layout{nullptr};
  QGridLayout* grid{nullptr};
  std::vector<QWidget*> rows;  // the field widget per grid row
};

GridSection make_grid_section(const QString& title, QWidget* parent) {
  GridSection section;
  section.group = new QGroupBox(title, parent);
  section.layout = new QVBoxLayout(section.group);
  section.layout->setContentsMargins(10, 8, 10, 8);
  section.layout->setSpacing(4);
  section.grid = new QGridLayout();
  section.grid->setContentsMargins(0, 0, 0, 0);
  section.grid->setHorizontalSpacing(kSectionColumnGap);
  section.grid->setVerticalSpacing(8);
  section.grid->setColumnMinimumWidth(1, kSectionLinkColumnWidth);
  section.grid->setColumnStretch(2, 1);
  section.layout->addLayout(section.grid);
  return section;
}

int add_grid_widget_row(GridSection& section, const QString& label, QWidget* field) {
  const int grid_row = static_cast<int>(section.rows.size());
  section.grid->addWidget(new QLabel(label, section.group), grid_row, 0, Qt::AlignLeft | Qt::AlignVCenter);
  section.grid->addWidget(field, grid_row, 2);
  section.rows.push_back(field);
  return grid_row;
}

// A field with its - / + steppers; `field_rows` maps the spin to its row widget.
int add_grid_spin_row(GridSection& section, std::map<QWidget*, QWidget*>& field_rows, const QString& label,
                      QAbstractSpinBox* spin) {
  auto* row = wrap_spin_with_step_buttons(spin, section.group, label);
  field_rows[spin] = row;
  return add_grid_widget_row(section, label, row);
}

// A normal-sized checkable chain button centered on a thin bracket that reaches the first
// and last of the `row_count` rows from `first_row`. `bracket_name` names the bracket
// widget (its objectName), `name` the button.
QToolButton* add_grid_link_button(GridSection& section, const char* name, const char* bracket_name,
                                  const QString& tooltip, int first_row, int row_count) {
  auto* button = new QToolButton(nullptr);
  button->setObjectName(QLatin1String(name));
  button->setProperty("geometryLink", true);
  button->setCheckable(true);
  button->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  button->setIconSize(QSize(18, 18));
  button->setToolTip(tooltip);
  button->setFixedSize(kSectionLinkColumnWidth, kSectionLinkColumnWidth);
  auto* bracket = new LinkBracket(button, section.rows[static_cast<std::size_t>(first_row)],
                                  section.rows[static_cast<std::size_t>(first_row + row_count - 1)], section.group);
  bracket->setObjectName(QLatin1String(bracket_name));
  section.grid->addWidget(bracket, first_row, 1, row_count, 1);
  return button;
}

// Left-aligns the forms' labels and adds them to `labels`.
void collect_form_labels(const std::vector<QFormLayout*>& forms, std::vector<QLabel*>& labels) {
  for (auto* form : forms) {
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int row = 0; row < form->rowCount(); ++row)
      if (auto* item = form->itemAt(row, QFormLayout::LabelRole); item != nullptr)
        if (auto* label = qobject_cast<QLabel*>(item->widget()); label != nullptr) labels.push_back(label);
  }
}

// Widens every label to the widest of them and returns that width.
int share_label_width(const std::vector<QLabel*>& labels) {
  int width = 0;
  for (auto* label : labels) {
    label->ensurePolished();  // the sheet's font, not the default one
    width = std::max(width, label->sizeHint().width());
  }
  for (auto* label : labels) label->setMinimumWidth(width);
  return width;
}

// One label column width for every left-column section: the widest label of them all.
void align_section_columns(const std::vector<QFormLayout*>& forms, const std::vector<QGridLayout*>& grids) {
  std::vector<QLabel*> labels;
  for (auto* form : forms) form->setHorizontalSpacing(kSectionFormFieldGap);
  collect_form_labels(forms, labels);
  for (auto* grid : grids)
    for (int row = 0; row < grid->rowCount(); ++row)
      if (auto* item = grid->itemAtPosition(row, 0); item != nullptr)
        if (auto* label = qobject_cast<QLabel*>(item->widget()); label != nullptr) labels.push_back(label);
  const int width = share_label_width(labels);
  for (auto* grid : grids) grid->setColumnMinimumWidth(0, width);
}

// One label column for a set of forms that keep their own label-to-field gap
// (the right column's Fill and Stroke, GitHub #679).
void align_form_columns(const std::vector<QFormLayout*>& forms) {
  std::vector<QLabel*> labels;
  collect_form_labels(forms, labels);
  share_label_width(labels);
}

}  // namespace

ShapeAppearanceBody build_shape_appearance_body(QWidget& host, QDialog& window, ShapeAppearanceBodyOptions options) {
  // `dialog` is the widget the rows are built into: the window itself for the modal
  // dialog, a replaceable page inside the stay-open window otherwise. Everything that
  // must outlive a page or act on the top-level window goes through `window_ptr`.
  QWidget& dialog = host;
  QDialog* const window_ptr = &window;
  const bool live = options.mode == ShapeAppearanceBodyMode::Live;
  auto preview_changed = std::move(options.changed);
  auto initial = std::move(options.initial);
  const auto reset_defaults = std::move(options.reset_defaults);
  auto* const gradient_library = options.gradient_library;
  auto* const pattern_library = options.pattern_library;
  const auto* const document_patterns = options.document_patterns;
  const auto foreground = options.foreground;
  const auto background = options.background;
  const auto units = options.units;
  const auto custom_shapes = options.custom_shapes;
  // The rows read the batch context for as long as they live, which for a page is longer
  // than the call that built it.
  const auto batch_owner =
      options.batch != nullptr
          ? std::make_shared<const AppearanceDialogContext<ShapeAppearanceSettings>>(*options.batch)
          : nullptr;
  const auto* const batch = batch_owner.get();
  if (batch_owner) QObject::connect(&host, &QObject::destroyed, [batch_owner] {});
  // A dialog a row opens (color picker, Gradient Manager, Shapes Manager) runs through the
  // host: the stay-open window locks the document around it. `body` returns false on Cancel.
  const auto run_child = [handler = std::move(options.run_child_dialog)](const std::function<bool()>& body) {
    if (handler) {
      handler(body);
    } else {
      body();
    }
  };
  // Thicknesses and radii have no percent basis; positions and sizes take the
  // document extent on their axis.
  auto thickness_units = units;
  thickness_units.document_width = 0.0;
  thickness_units.document_height = 0.0;
  const auto pixel_field_context = [thickness_units] { return document_field_context(thickness_units, true); };
  // Every field shown in the ruler unit; a unit picked on one shows on all of them.
  std::vector<UnitSpinBox*> unit_fields;
  window.setWindowTitle(QObject::tr("Shape Appearance"));
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
  // Angle fields get the round AngleDial after their - / + pair (GitHub
  // issue 618); the dial follows the field both ways.
  const auto add_angle_row = [&add_spin_row](QFormLayout* form, const QString& label, auto* spin) {
    auto* row = add_spin_row(form, label, spin);
    auto* dial = new AngleDial(AngleDial::Mode::Angle, row);
    dial->setObjectName(angle_dial_object_name(spin->objectName()));
    if (auto* layout = qobject_cast<QHBoxLayout*>(row->layout()); layout != nullptr) {
      // The field and its buttons stay level with the row's label.
      for (int index = 0; index < layout->count(); ++index) {
        if (auto* widget = layout->itemAt(index)->widget(); widget != nullptr) {
          layout->setAlignment(widget, Qt::AlignTop);
        }
      }
      layout->addSpacing(8);
      layout->addWidget(dial);
    }
    bind_angle_dial(*dial, *spin);
    return row;
  };
  // A paint's own settings form a nested block under its picker row (GitHub
  // #552 follow-up): a gradient's Style, Angle, Scale and Reverse under
  // Gradient, a pattern's Scale, Angle, Offset X/Y and Align with layer under
  // Pattern. The block is the field of a label-less row, so its labels start in
  // the outer field column and its fields in a column of their own further
  // right, on every style (macOS right-aligns form labels, which keeps the
  // block nested too). The block shows and hides as one row.
  const auto add_paint_block = [](QFormLayout* outer, QWidget* parent, const char* name) {
    auto* block = new QWidget(parent);
    block->setObjectName(QLatin1String(name));
    auto* inner = new QFormLayout(block);
    inner->setContentsMargins(0, 0, 0, 0);
    inner->setHorizontalSpacing(outer->horizontalSpacing());
    inner->setVerticalSpacing(outer->verticalSpacing());
    inner->setFieldGrowthPolicy(outer->fieldGrowthPolicy());
    outer->addRow(QString(), block);
    return std::pair{block, inner};
  };

  auto state = std::make_shared<DialogState>();
  state->settings = std::move(initial);


  const bool multiple = batch != nullptr && batch->selected_count > 1;
  // A fill layer without a path has nothing for Layer, Edge or Stroke to change
  // here: only its Fill section shows (GitHub #582).
  const bool fill_layer_only =
      state->settings.fill_layer_only &&
      (batch == nullptr || std::all_of(batch->originals.begin(), batch->originals.end(),
                                       [](const ShapeAppearanceSettings& value) {
                                         return value.fill_layer_only;
                                       }));
  if (multiple) {
    const auto rectangle = std::find_if(batch->originals.begin(), batch->originals.end(),
                                        appearance_has_editable_radii);
    state->settings.geometry = rectangle != batch->originals.end() ? rectangle->geometry : std::nullopt;
  }
  if (batch != nullptr && batch->selected_count > 1) {
    auto* summary = new QLabel(appearance_selection_summary(
        batch->selected_count, batch->originals.size(),
        batch->names.empty() ? QString() : batch->names.front(), batch->skipped_reason), &dialog);
    summary->setObjectName(QStringLiteral("shapeAppearanceSelectionSummary"));
    summary->setWordWrap(true);
    dialog_layout->insertWidget(0, summary);
  }
  const auto properties = shape_appearance_properties();
  auto edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
  auto previous = std::make_shared<ShapeAppearanceSettings>(state->settings);
  // What each target looked like before the edits captured so far: the layers as the
  // dialog opened, or, in the stay-open window, as its last applied change left them.
  auto baselines = std::make_shared<std::vector<ShapeAppearanceSettings>>(
      batch != nullptr ? batch->originals : std::vector<ShapeAppearanceSettings>{});
  auto refresh_mixed = std::make_shared<std::function<void()>>();
  // Shows the saved stroke preset the current line settings match (set below).
  auto refresh_preset_match = std::make_shared<std::function<void()>>();
  // Picks the wording of the note under Density: it depends on whether the stroke draws.
  auto refresh_edge_note = std::make_shared<std::function<void()>>();
  // `captured`: the caller already appended its edits (the multi-layer Arrowheads rows).
  const auto notify = [state, previous, edits, properties, batch, preview_changed, refresh_mixed,
                       refresh_preset_match, refresh_edge_note](const std::vector<std::string>& fields = {},
                                                                bool captured = false) {
    if (batch != nullptr) {
      if (!captured && fields.empty()) {
        capture_appearance_edits(*edits, properties, *previous, state->settings);
      } else if (!captured) {
        for (const auto& property : properties)
          if (std::find(fields.begin(), fields.end(), property.key) != fields.end())
            edits->append(property.capture(state->settings));
      }
      state->settings.edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>(*edits);
    }
    *previous = state->settings;
    if (*refresh_mixed) (*refresh_mixed)();
    if (*refresh_preset_match) (*refresh_preset_match)();
    if (*refresh_edge_note) (*refresh_edge_note)();
    if (preview_changed) preview_changed(state->settings);
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
    notify({"layer_opacity"});
  });
  QObject::connect(layer_fill_opacity_spin, &QSpinBox::valueChanged, &dialog,
                   [state, notify](int value) {
    state->settings.fill_opacity = static_cast<float>(value) / 100.0F;
    notify({"fill_opacity"});
  });
  left_column->addWidget(layer_group);

  // --- Geometry, Radius, Arrowheads, Polygon (GitHub #641, #642, #643, #644, #622) ---
  // Geometry holds the shape's position and size: X, Y, Width, Height for a box shape
  // (rectangle, ellipse, polygon) or a plain path (custom shape, Pen outline), Start / End
  // and Weight for a line, and the Shape picker for a plain path. With several layers
  // selected its single-layer rows are off and Weight and Shape apply to every target.
  // Radius, Arrowheads and Polygon follow, each for the layers it edits; with several
  // layers each field applies on its own, so only the fields changed reach the others.
  std::optional<LiveShapeParams> line_reference;
  std::ptrdiff_t line_count = 0;
  std::size_t first_line_index = 0;
  std::optional<LiveShapeParams> polygon_reference;
  if (!multiple) {
    if (appearance_has_editable_line(state->settings)) line_reference = state->settings.geometry;
    if (appearance_has_editable_polygon(state->settings)) polygon_reference = state->settings.geometry;
  } else {
    line_count = std::count_if(batch->originals.begin(), batch->originals.end(), appearance_has_editable_line);
    const auto first = std::find_if(batch->originals.begin(), batch->originals.end(), appearance_has_editable_line);
    if (first != batch->originals.end()) {
      line_reference = first->geometry;
      first_line_index = static_cast<std::size_t>(std::distance(batch->originals.begin(), first));
    }
    const auto polygon = std::find_if(batch->originals.begin(), batch->originals.end(),
                                      appearance_has_editable_polygon);
    if (polygon != batch->originals.end()) polygon_reference = polygon->geometry;
  }
  // One layer: the edit changes the dialog's geometry. Several layers: the edit is the
  // named fields, captured from the reference shape's values and replayed on every layer.
  const auto edit_live_shape = [state, multiple, edits, properties, notify](
                                   const std::shared_ptr<LiveShapeParams>& reference,
                                   const std::vector<std::string>& keys,
                                   const std::function<void(LiveShapeParams&)>& change) {
    if (!multiple) {
      change(*state->settings.geometry);
      *reference = *state->settings.geometry;
      notify({"geometry"});
      return;
    }
    change(*reference);
    ShapeAppearanceSettings source;
    source.geometry = *reference;
    for (const auto& property : properties)
      if (std::find(keys.begin(), keys.end(), property.key) != keys.end()) edits->append(property.capture(source));
    notify({}, true);
  };
  const auto make_spin = [](QWidget* parent, const char* name, double minimum, double maximum, double value) {
    auto* spin = new UnitSpinBox(SpinUnit::Pixels, parent);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(minimum, maximum);
    spin->setDecimals(1);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 72);
    return spin;
  };
  // Every left-column section lines its fields up (GitHub #641): the label column width
  // is shared (set once every section is built), and the forms keep the grids' link
  // column as their label-to-field gap.
  std::vector<QFormLayout*> left_forms{layer_form};
  std::vector<QGridLayout*> left_grids;
  GridSection geometry_section;
  const auto ensure_geometry_section = [&]() -> GridSection& {
    if (geometry_section.group == nullptr) {
      geometry_section = make_grid_section(QObject::tr("Geometry"), left_widget);
      geometry_section.group->setObjectName(QStringLiteral("shapeGeometryGroup"));
      left_grids.push_back(geometry_section.grid);
    }
    return geometry_section;
  };
  const bool path_shape_layer = !multiple && state->settings.path_box.has_value();
  if (multiple) state->settings.path_box.reset();  // position and size stay single-layer edits
  if (state->settings.geometry.has_value() || path_shape_layer) {
    auto& section = ensure_geometry_section();
    auto* geometry_group = section.group;
    const auto kind = state->settings.geometry ? state->settings.geometry->kind : LiveShapeKind::None;
    if (kind == LiveShapeKind::Line) {
      const auto& geometry = *state->settings.geometry;
      auto* start_x = make_spin(geometry_group, "shapeGeometryLineStartXSpin", -30000, 30000, geometry.line_start_x);
      auto* start_y = make_spin(geometry_group, "shapeGeometryLineStartYSpin", -30000, 30000, geometry.line_start_y);
      auto* end_x = make_spin(geometry_group, "shapeGeometryLineEndXSpin", -30000, 30000, geometry.line_end_x);
      auto* end_y = make_spin(geometry_group, "shapeGeometryLineEndYSpin", -30000, 30000, geometry.line_end_y);
      apply_document_field_units(start_x, units, true);
      apply_document_field_units(start_y, units, false);
      apply_document_field_units(end_x, units, true);
      apply_document_field_units(end_y, units, false);
      unit_fields.insert(unit_fields.end(), {start_x, start_y, end_x, end_y});
      add_grid_spin_row(section, field_rows, QObject::tr("Start X:"), start_x);
      add_grid_spin_row(section, field_rows, QObject::tr("Start Y:"), start_y);
      add_grid_spin_row(section, field_rows, QObject::tr("End X:"), end_x);
      add_grid_spin_row(section, field_rows, QObject::tr("End Y:"), end_y);
      // Weight follows End Y (the Weight row below).
      const auto apply_line = [state, notify, start_x, start_y, end_x, end_y] {
        auto& params = *state->settings.geometry;
        params.line_start_x = start_x->value();
        params.line_start_y = start_y->value();
        params.line_end_x = end_x->value();
        params.line_end_y = end_y->value();
        notify({"geometry"});
      };
      for (auto* spin : {start_x, start_y, end_x, end_y}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_line);
      }
    } else {
      // A box: a live shape's left/top/right/bottom, or a plain path's bounds.
      const ShapePathBox box = state->settings.geometry
          ? ShapePathBox{state->settings.geometry->left, state->settings.geometry->top,
                         state->settings.geometry->right, state->settings.geometry->bottom}
          : *state->settings.path_box;
      auto* x_spin = make_spin(geometry_group, "shapeGeometryXSpin", -30000, 30000, box.left);
      auto* y_spin = make_spin(geometry_group, "shapeGeometryYSpin", -30000, 30000, box.top);
      auto* width_spin = make_spin(geometry_group, "shapeGeometryWidthSpin", 0.5, 60000, box.right - box.left);
      auto* height_spin = make_spin(geometry_group, "shapeGeometryHeightSpin", 0.5, 60000, box.bottom - box.top);
      apply_document_field_units(x_spin, units, true);
      apply_document_field_units(y_spin, units, false);
      apply_document_field_units(width_spin, units, true);
      apply_document_field_units(height_spin, units, false);
      unit_fields.insert(unit_fields.end(), {x_spin, y_spin, width_spin, height_spin});
      add_grid_spin_row(section, field_rows, QObject::tr("X:"), x_spin);
      add_grid_spin_row(section, field_rows, QObject::tr("Y:"), y_spin);
      const int width_row = add_grid_spin_row(section, field_rows, QObject::tr("Width:"), width_spin);
      add_grid_spin_row(section, field_rows, QObject::tr("Height:"), height_spin);
      // Link keeps the aspect ratio: editing one dimension moves the other by
      // the ratio captured when the link was switched on.
      auto* link_button = add_grid_link_button(section, "shapeGeometryLinkButton", "shapeGeometryLinkBracket",
                                               QObject::tr("Keep width and height in proportion"), width_row, 2);
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
      const auto apply_box = [state, notify, x_spin, y_spin, width_spin, height_spin] {
        const ShapePathBox edited{x_spin->value(), y_spin->value(), x_spin->value() + width_spin->value(),
                                  y_spin->value() + height_spin->value()};
        if (state->settings.geometry) {
          auto& params = *state->settings.geometry;
          params.left = edited.left;
          params.top = edited.top;
          params.right = edited.right;
          params.bottom = edited.bottom;
          notify({"geometry"});
        } else {
          state->settings.path_box = edited;
          notify({"path.box"});
        }
      };
      for (auto* spin : {x_spin, y_spin, width_spin, height_spin}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_box);
      }
    }
  }

  // Weight is the row after End Y; with several lines selected the Geometry section holds
  // it alone (or under the reference rectangle's rows) and it still applies to every line
  // (GitHub #622 follow-up 2).
  auto line = std::make_shared<LiveShapeParams>(line_reference.value_or(LiveShapeParams{}));
  if (line_reference) {
    auto& section = ensure_geometry_section();
    auto* weight = new UnitSpinBox(SpinUnit::Pixels, section.group);
    weight->setObjectName(QStringLiteral("shapeGeometryLineWeightSpin"));
    weight->setRange(0.5, 1000);
    weight->setDecimals(1);
    weight->setValue(line->line_weight);
    configure_dialog_spinbox(weight, 72);
    apply_document_field_units(weight, thickness_units, true);
    unit_fields.push_back(weight);
    add_grid_spin_row(section, field_rows, QObject::tr("Weight:"), weight);
    QObject::connect(weight, &QDoubleSpinBox::valueChanged, &dialog, [edit_live_shape, line](double value) {
      edit_live_shape(line, {"line.weight"}, [value](LiveShapeParams& params) {
        const double previous = params.line_weight;
        params.line_weight = value;
        tie_line_arrowheads_to_weight(params, previous);
      });
    });
  }

  // The Shape picker (GitHub #644): the Custom Shape tool's library. It opens on the shape
  // the layer was stamped from (or last picked here) while the outline still is that shape
  // fitted to its box: one lookup by id, one comparison (follow-up); otherwise, and with
  // several layers selected, none is chosen. A pick replaces the outline with that shape
  // fitted to the box above (each selected plain-path layer's own box when several are).
  const bool any_path_box = multiple
      ? std::any_of(batch->originals.begin(), batch->originals.end(),
                    [](const ShapeAppearanceSettings& value) { return value.path_box.has_value(); })
      : path_shape_layer;
  if (any_path_box && custom_shapes.library && custom_shapes.library() != nullptr) {
    auto& section = ensure_geometry_section();
    const auto library = custom_shapes.library;
    const auto manage = custom_shapes.manage;
    // The popup's Shapes Manager... button opens the manager over this dialog; the list
    // follows its changes.
    auto combo_ref = std::make_shared<QPointer<CustomShapePickerCombo>>();
    auto* shape_combo = new CustomShapePickerCombo(library, [library, manage, combo_ref, run_child,
                                                             dialog_ptr = window_ptr] {
      run_child([&] {
        if (manage) manage(dialog_ptr);
        if (const auto* current = library(); current != nullptr && !combo_ref->isNull())
          populate_custom_shape_combo(**combo_ref, *current);
        return true;
      });
    }, section.group);
    *combo_ref = shape_combo;
    shape_combo->setObjectName(QStringLiteral("shapeGeometryCustomShapeCombo"));
    populate_custom_shape_combo(*shape_combo, *library());
    {
      const QSignalBlocker blocker(shape_combo);
      int stamped = -1;
      if (!multiple && !state->settings.library_shape_id.empty() && state->settings.library_shape_outline) {
        const auto id = QString::fromStdString(state->settings.library_shape_id);
        const auto* entry = library()->find_entry_by_shape_id(id);
        if (entry != nullptr && vector_path_is_fitted_shape(*state->settings.library_shape_outline, entry->path))
          stamped = shape_combo->findData(id);
      }
      shape_combo->setCurrentIndex(stamped);
    }
    add_grid_widget_row(section, QObject::tr("Shape:"), shape_combo);
    QObject::connect(shape_combo, &QComboBox::currentIndexChanged, &dialog,
                     [state, notify, library, shape_combo](int index) {
      const auto* current = index >= 0 ? library() : nullptr;
      const auto* entry =
          current != nullptr ? current->find_entry_by_shape_id(shape_combo->itemData(index).toString()) : nullptr;
      if (entry == nullptr || entry->path.empty()) return;
      state->settings.path_shape =
          std::make_shared<const PickedCustomShape>(PickedCustomShape{entry->id.toStdString(), entry->path});
      notify({"path.shape"});
    });
  }
  if (geometry_section.group != nullptr) left_column->addWidget(geometry_section.group);

  // --- Radius (rectangles; GitHub #641): the four corners and their link ---
  QVBoxLayout* radius_layout = nullptr;
  if (appearance_has_editable_radii(state->settings)) {
    auto section = make_grid_section(QObject::tr("Radius"), left_widget);
    section.group->setObjectName(QStringLiteral("shapeRadiusGroup"));
    left_grids.push_back(section.grid);
    radius_layout = section.layout;
    const auto& geometry = *state->settings.geometry;
    // Model order TL, TR, BR, BL; a radius on a plain rect promotes it to
    // a rounded rect (the generator clamps oversized values).
    const std::array<const char*, 4> names{
        "shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
        "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin"};
    const std::array<QString, 4> labels{QObject::tr("Top left:"), QObject::tr("Top right:"),
                                        QObject::tr("Bottom right:"), QObject::tr("Bottom left:")};
    std::array<QDoubleSpinBox*, 4> radius_spins{nullptr, nullptr, nullptr, nullptr};
    for (std::size_t corner = 0; corner < 4; ++corner) {
      auto* radius_spin = make_spin(section.group, names[corner], 0, 30000, geometry.corner_radii[corner]);
      radius_spin->set_context_provider(pixel_field_context);  // stays px; "2 mm" converts
      radius_spins[corner] = radius_spin;
      add_grid_spin_row(section, field_rows, labels[corner], radius_spin);
    }
    // Linked, editing any corner sets all four. Starts linked when the
    // corners already agree (the common case: one radius for the whole
    // shape); a shape authored with distinct corners opens unlinked so
    // one edit cannot flatten them.
    auto* radius_link = add_grid_link_button(section, "shapeGeometryRadiusLinkButton",
                                             "shapeGeometryRadiusLinkBracket",
                                             QObject::tr("Change all four corner radii together"), 0, 4);
    const bool corners_agree =
        std::abs(geometry.corner_radii[1] - geometry.corner_radii[0]) < 1e-9 &&
        std::abs(geometry.corner_radii[2] - geometry.corner_radii[0]) < 1e-9 &&
        std::abs(geometry.corner_radii[3] - geometry.corner_radii[0]) < 1e-9;
    const bool every_shape_symmetric = batch == nullptr ||
        std::all_of(batch->originals.begin(), batch->originals.end(), [](const auto& value) {
          if (!appearance_has_editable_radii(value)) return true;
          const auto& radii = value.geometry->corner_radii;
          return std::all_of(radii.begin(), radii.end(), [&](double radius) {
            return std::abs(radius - radii.front()) < 1e-9;
          });
        });
    radius_link->setChecked(corners_agree && every_shape_symmetric);
    // Connected before apply_radii below so every corner is already updated when the
    // geometry applies.
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
    const auto apply_radii = [state, notify, radius_spins](const std::vector<std::string>& fields) {
      auto& params = *state->settings.geometry;
      for (std::size_t corner = 0; corner < 4; ++corner) {
        params.corner_radii[corner] = radius_spins[corner]->value();
      }
      const bool any_radius = std::any_of(params.corner_radii.begin(), params.corner_radii.end(),
                                          [](double radius) { return radius > 0.0; });
      if (params.kind == LiveShapeKind::Rectangle && any_radius) {
        params.kind = LiveShapeKind::RoundedRectangle;
      }
      notify(fields);
    };
    for (std::size_t corner = 0; corner < radius_spins.size(); ++corner) {
      QObject::connect(radius_spins[corner], &QDoubleSpinBox::valueChanged, &dialog,
                       [apply_radii, corner, radius_link] {
        if (radius_link->isChecked()) apply_radii({"radius.0", "radius.1", "radius.2", "radius.3"});
        else apply_radii({"radius." + std::to_string(corner)});
      });
    }
    left_column->addWidget(section.group);
  }
  if (!left_grids.empty()) {
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
  }

  // --- Arrowheads (line layers; GitHub #622 follow-up) ---
  // The Start / End heads, and the heads' Width and Length as a percent of the weight and
  // their Concavity. With several layers selected the fields show the first line's values
  // (mixed where the lines differ).
  if (line_reference) {
    auto* arrow_group = new QGroupBox(QObject::tr("Arrowheads"), left_widget);
    arrow_group->setObjectName(QStringLiteral("shapeArrowheadsGroup"));
    auto* arrow_layout = new QVBoxLayout(arrow_group);
    arrow_layout->setContentsMargins(10, 8, 10, 8);
    arrow_layout->setSpacing(4);
    if (multiple && line_count > 0 && first_line_index < batch->names.size()) {
      auto* hint = new QLabel(QObject::tr("Editable lines for arrowheads: %n", nullptr, static_cast<int>(line_count)) +
                                  QStringLiteral("\n") + QObject::tr("Values from: %1").arg(batch->names[first_line_index]),
                              arrow_group);
      hint->setObjectName(QStringLiteral("shapeArrowheadSelectionSummary"));
      hint->setWordWrap(true);
      arrow_layout->addWidget(hint);
    }
    auto* arrow_form = new QFormLayout();
    arrow_form->setVerticalSpacing(8);
    arrow_layout->addLayout(arrow_form);
    left_forms.push_back(arrow_form);

    // Start and End, the Line options bar's two toggles.
    auto* ends = new QWidget(arrow_group);
    auto* ends_layout = new QHBoxLayout(ends);
    ends_layout->setContentsMargins(0, 0, 0, 0);
    ends_layout->setSpacing(16);
    auto* arrow_start = new QCheckBox(QObject::tr("Start"), ends);
    arrow_start->setObjectName(QStringLiteral("shapeGeometryLineArrowStartCheck"));
    arrow_start->setToolTip(QCoreApplication::translate("patchy::ui::MainWindow", "Add an arrowhead at the line start"));
    arrow_start->setChecked(line->arrow_start);
    auto* arrow_end = new QCheckBox(QObject::tr("End"), ends);
    arrow_end->setObjectName(QStringLiteral("shapeGeometryLineArrowEndCheck"));
    arrow_end->setToolTip(QCoreApplication::translate("patchy::ui::MainWindow", "Add an arrowhead at the line end"));
    arrow_end->setChecked(line->arrow_end);
    ends_layout->addWidget(arrow_start);
    ends_layout->addWidget(arrow_end);
    ends_layout->addStretch(1);
    // The row's label (GitHub #622 follow-up 3): which heads the line has.
    auto* ends_label = new QLabel(QObject::tr("Heads:"), arrow_group);
    ends_label->setObjectName(QStringLiteral("shapeArrowHeadsLabel"));
    arrow_form->addRow(ends_label, ends);

    const auto make_percent_spin = [arrow_group](const char* name, int minimum, int maximum, double value,
                                                 const QString& tooltip) {
      auto* spin = new QSpinBox(arrow_group);
      spin->setObjectName(QLatin1String(name));
      spin->setRange(minimum, maximum);
      spin->setSuffix(QStringLiteral("%"));
      spin->setValue(static_cast<int>(std::lround(std::clamp(value, static_cast<double>(minimum),
                                                             static_cast<double>(maximum)))));
      spin->setToolTip(tooltip);
      configure_dialog_spinbox(spin, 72);
      return spin;
    };
    auto* arrow_width = make_percent_spin("shapeArrowWidthSpin", kLineArrowMinPercent, kLineArrowMaxPercent,
                                          line_arrow_width_percent(*line),
                                          QObject::tr("Arrowhead width, in percent of the line weight"));
    auto* arrow_length = make_percent_spin("shapeArrowLengthSpin", kLineArrowMinPercent, kLineArrowMaxPercent,
                                           line_arrow_length_percent(*line),
                                           QObject::tr("Arrowhead length, in percent of the line weight"));
    // The range runs from a tenth of the weight to a thousand times it (local-051), so a
    // step is a tenth of the value's size: 1% under 100%, 100% from 1000%.
    for (auto* spin : {arrow_width, arrow_length}) spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    auto* arrow_concavity = make_percent_spin(
        "shapeArrowConcavitySpin", -50, 50, line->arrow_concavity,
        QObject::tr("How far the back of the arrowhead curves in toward the tip (negative bulges out)"));
    const std::vector<QWidget*> head_rows{add_spin_row(arrow_form, QObject::tr("Width:"), arrow_width),
                                          add_spin_row(arrow_form, QObject::tr("Length:"), arrow_length),
                                          add_spin_row(arrow_form, QObject::tr("Concavity:"), arrow_concavity)};
    // Width, Length and Concavity shape the heads, so they wait for one.
    bool any_head = false;
    if (multiple)
      for (const auto& value : batch->originals)
        any_head = any_head || (appearance_has_editable_line(value) &&
                                (value.geometry->arrow_start || value.geometry->arrow_end));
    const auto update_head_rows = [arrow_start, arrow_end, head_rows, arrow_form, any_head] {
      const bool on = any_head || arrow_start->isChecked() || arrow_end->isChecked();
      for (auto* row : head_rows) {
        row->setEnabled(on);
        if (auto* label = arrow_form->labelForField(row)) label->setEnabled(on);
      }
    };
    update_head_rows();

    for (const bool at_end : {false, true}) {
      QObject::connect(at_end ? arrow_end : arrow_start, &QCheckBox::toggled, &dialog,
                       [edit_live_shape, line, update_head_rows, at_end](bool checked) {
        edit_live_shape(line, {at_end ? "line.arrow_end" : "line.arrow_start"},
                        [at_end, checked](LiveShapeParams& params) {
          (at_end ? params.arrow_end : params.arrow_start) = checked;
          // A new head gets the default size; a head the line already had keeps its own.
          if (checked) give_line_arrowheads_default_size(params);
        });
        update_head_rows();
      });
    }
    for (const bool length : {false, true}) {
      QObject::connect(length ? arrow_length : arrow_width, &QSpinBox::valueChanged, &dialog,
                       [edit_live_shape, line, length](int percent) {
        edit_live_shape(line, {length ? "line.arrow_length" : "line.arrow_width"},
                        [length, percent](LiveShapeParams& params) {
          (length ? params.arrow_length : params.arrow_width) =
              line_arrow_size_from_percent(params.line_weight, percent);
        });
      });
    }
    QObject::connect(arrow_concavity, &QSpinBox::valueChanged, &dialog, [edit_live_shape, line](int percent) {
      edit_live_shape(line, {"line.arrow_concavity"},
                      [percent](LiveShapeParams& params) { params.arrow_concavity = percent; });
    });
    left_column->addWidget(arrow_group);
  }

  // --- Polygon (GitHub #643): the Polygon options bar's Sides and Star inset ---
  if (polygon_reference) {
    auto polygon = std::make_shared<LiveShapeParams>(*polygon_reference);
    auto* polygon_group =
        new QGroupBox(QCoreApplication::translate("patchy::ui::MainWindow", "Polygon"), left_widget);
    polygon_group->setObjectName(QStringLiteral("shapePolygonGroup"));
    auto* polygon_layout = new QVBoxLayout(polygon_group);
    polygon_layout->setContentsMargins(10, 8, 10, 8);
    polygon_layout->setSpacing(4);
    auto* polygon_form = new QFormLayout();
    polygon_form->setVerticalSpacing(8);
    polygon_layout->addLayout(polygon_form);
    left_forms.push_back(polygon_form);
    auto* sides = new QSpinBox(polygon_group);
    sides->setObjectName(QStringLiteral("shapePolygonSidesSpin"));
    sides->setRange(3, 100);
    sides->setValue(std::clamp(polygon->polygon_sides, 3, 100));
    configure_dialog_spinbox(sides, 72);
    add_spin_row(polygon_form, QObject::tr("Sides:"), sides);
    auto* star_inset = new QSpinBox(polygon_group);
    star_inset->setObjectName(QStringLiteral("shapePolygonStarInsetSpin"));
    star_inset->setRange(0, 99);
    star_inset->setSuffix(percent_suffix());
    star_inset->setValue(std::clamp(polygon->polygon_star_inset, 0, 99));
    star_inset->setToolTip(QCoreApplication::translate(
        "patchy::ui::MainWindow", "0 makes a plain polygon; higher values pull in star points"));
    configure_dialog_spinbox(star_inset, 72);
    add_spin_row(polygon_form, QCoreApplication::translate("patchy::ui::MainWindow", "Star inset:"), star_inset);
    QObject::connect(sides, &QSpinBox::valueChanged, &dialog, [edit_live_shape, polygon](int value) {
      edit_live_shape(polygon, {"polygon.sides"}, [value](LiveShapeParams& params) { params.polygon_sides = value; });
    });
    QObject::connect(star_inset, &QSpinBox::valueChanged, &dialog, [edit_live_shape, polygon](int value) {
      edit_live_shape(polygon, {"polygon.star_inset"},
                      [value](LiveShapeParams& params) { params.polygon_star_inset = value; });
    });
    left_column->addWidget(polygon_group);
  }

  // --- Fill ---
  // The fill-only dialog is all Fill, so its section carries no "Fill" heading (GitHub #582
  // follow-up); the group keeps its frame.
  auto* fill_group = new QGroupBox(fill_layer_only ? QString() : QObject::tr("Fill"), right_widget);
  fill_group->setObjectName(QStringLiteral("shapeFillGroup"));
  auto* fill_layout = new QVBoxLayout(fill_group);
  fill_layout->setContentsMargins(10, 8, 10, 8);
  fill_layout->setSpacing(4);
  auto* fill_form = new QFormLayout();
  fill_form->setHorizontalSpacing(10);
  fill_form->setVerticalSpacing(8);
  fill_layout->addLayout(fill_form);

  auto* fill_kind_combo = new QComboBox(fill_group);
  fill_kind_combo->setObjectName(QStringLiteral("shapeFillKindCombo"));
  // A fill layer is its fill: it switches between Solid Color, Gradient and
  // Pattern (a fill layer of that type in a PSD) but never to none.
  if (!fill_layer_only) {
    fill_kind_combo->addItem(QObject::tr("No Fill"), static_cast<int>(VectorFillKind::None));
  }
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

  // Both the fill and the stroke paint offer the same preset lists, their
  // Foreground/Background stops drawn in the colors a pick resolves them with (#581).
  const GradientPreviewColors preview_colors{foreground, background};
  const auto populate_gradient_combo = [gradient_library, preview_colors](QComboBox* combo) {
    combo->setIconSize(QSize(64, 16));
    use_scrolling_list_popup(*combo);
    if (gradient_library != nullptr) {
      for (const auto& entry : gradient_library->entries()) {
        const bool cached = gradient_library->preview_colors() == preview_colors ||
                            !gradient_has_dynamic_stops(entry.definition);
        combo->addItem(QIcon(cached ? entry.thumbnail
                                    : gradient_thumbnail(entry.definition, 112, 28, preview_colors)),
                       entry.name, entry.storage_id);
      }
    }
  };
  // Shows the preset the gradient was made from, or a Custom row (empty data, at
  // the top) holding the gradient when it matches none: an edited or imported one
  // (GitHub #578, #579). A paint without a gradient yet shows the first preset,
  // which a switch to Gradient seeds from. Signals stay blocked.
  const auto select_gradient_row = [gradient_library, preview_colors](
                                       QComboBox* combo, const GradientDefinition& gradient,
                                       GradientDefinition& custom) {
    QSignalBlocker blocker(combo);
    const auto id = gradient_library != nullptr
                        ? matching_gradient_library_entry(*gradient_library, gradient, preview_colors)
                        : QString();
    if (const auto row = id.isEmpty() ? -1 : combo->findData(id); row >= 0) {
      combo->setCurrentIndex(row);
      return;
    }
    const int custom_row = combo->count() > 0 && combo->itemData(0).toString().isEmpty() ? 0 : -1;
    if (gradient.form == GradientDefinitionForm::Solid && gradient.color_stops.empty()) {
      combo->setCurrentIndex(custom_row == 0 && combo->count() > 1 ? 1 : 0);
      return;
    }
    custom = gradient;
    const auto icon = QIcon(gradient_thumbnail(gradient, 112, 28, preview_colors));
    if (custom_row < 0) {
      combo->insertItem(0, icon, QObject::tr("Custom"), QString());
    } else {
      combo->setItemIcon(0, icon);
    }
    combo->setCurrentIndex(0);
  };
  // Each row keeps the name the saved paint carries (kPatternSavedNameRole): a Photoshop
  // preset shows its readable name but writes back its own localization key.
  const auto populate_pattern_combo = [pattern_library, document_patterns](QComboBox* combo) {
    combo->setIconSize(QSize(24, 24));
    use_scrolling_list_popup(*combo);
    if (document_patterns != nullptr) {
      for (const auto& resource : document_patterns->patterns) {
        const auto display = document_pattern_display_name(resource);
        const auto name = display.isEmpty() ? QObject::tr("Embedded pattern") : display;
        combo->addItem(QIcon(pattern_thumbnail(resource.tile, combo->iconSize().width())), name,
                       QString::fromStdString(resource.id));
        combo->setItemData(combo->count() - 1,
                           resource.name.empty() ? name : QString::fromStdString(resource.name),
                           kPatternSavedNameRole);
      }
    }
    if (pattern_library != nullptr) {
      for (const auto& entry : pattern_library->entries()) {
        if (combo->findData(entry.id) < 0) {
          combo->addItem(QIcon(entry.thumbnail), entry.name, entry.id);
          combo->setItemData(combo->count() - 1, entry.name, kPatternSavedNameRole);
        }
      }
    }
  };
  const auto saved_pattern_name = [](const QComboBox* combo) {
    return combo->currentData(kPatternSavedNameRole).toString().toStdString();
  };
  // The Gradient row: the preset list and a Manage... button that opens the
  // Gradient Manager (GitHub #552 follow-up), where the current gradient can be
  // saved as a preset (New from Current) and presets renamed, imported or
  // deleted. Afterwards the list is rebuilt and shows the preset the gradient
  // matches, or Custom; Use Gradient picks a preset like the list does.
  const auto add_gradient_row = [&field_rows, &dialog, window_ptr, run_child, state, gradient_library,
                                 preview_colors, populate_gradient_combo, select_gradient_row](
                                    QFormLayout* form, QWidget* parent, bool for_stroke) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(6);
    auto* combo = new QComboBox(row);
    combo->setObjectName(for_stroke ? QStringLiteral("shapeStrokeGradientCombo")
                                    : QStringLiteral("shapeFillGradientCombo"));
    populate_gradient_combo(combo);
    auto* manage = new QPushButton(QObject::tr("Manage..."), row);
    manage->setObjectName(for_stroke ? QStringLiteral("shapeStrokeGradientManageButton")
                                     : QStringLiteral("shapeFillGradientManageButton"));
    manage->setToolTip(QObject::tr("Open the Gradient Manager to save, rename, import or delete gradient presets"));
    manage->setAccessibleName(QObject::tr("Manage gradient presets"));
    manage->setEnabled(gradient_library != nullptr);
    row_layout->addWidget(combo, 1);
    row_layout->addWidget(manage);
    form->addRow(QObject::tr("Gradient:"), row);
    field_rows[combo] = row;
    QObject::connect(manage, &QPushButton::clicked, &dialog, [=] {
      if (gradient_library == nullptr) return;
      run_child([=] {
        const GradientDefinition current =
            for_stroke ? static_cast<const GradientDefinition&>(state->settings.stroke.content.gradient)
                       : static_cast<const GradientDefinition&>(state->settings.fill.gradient);
        auto& custom = for_stroke ? state->custom_stroke_gradient : state->custom_fill_gradient;
        const auto selected =
            request_gradient_manager(window_ptr, *gradient_library, combo->currentData().toString(), current);
        const bool had_custom_row = combo->count() > 0 && combo->itemData(0).toString().isEmpty();
        {
          QSignalBlocker blocker(combo);
          combo->clear();
          populate_gradient_combo(combo);
          if (had_custom_row) {
            combo->insertItem(0, QIcon(gradient_thumbnail(custom, 112, 28, preview_colors)),
                              QObject::tr("Custom"), QString());
          }
        }
        select_gradient_row(combo, current, custom);
        if (const auto picked = selected.isEmpty() ? -1 : combo->findData(selected); picked >= 0) {
          combo->setCurrentIndex(picked);
        }
        return true;
      });
    });
    return combo;
  };

  auto* fill_gradient_combo = add_gradient_row(fill_form, fill_group, false);

  auto* gradient_type_combo = new QComboBox(fill_group);
  gradient_type_combo->setObjectName(QStringLiteral("shapeGradientTypeCombo"));
  gradient_type_combo->addItem(QObject::tr("Linear"), static_cast<int>(LayerStyleGradientType::Linear));
  gradient_type_combo->addItem(QObject::tr("Radial"), static_cast<int>(LayerStyleGradientType::Radial));
  gradient_type_combo->addItem(QObject::tr("Angle"), static_cast<int>(LayerStyleGradientType::Angle));
  gradient_type_combo->addItem(QObject::tr("Reflected"),
                               static_cast<int>(LayerStyleGradientType::Reflected));
  gradient_type_combo->addItem(QObject::tr("Diamond"),
                               static_cast<int>(LayerStyleGradientType::Diamond));
  const auto fill_gradient = add_paint_block(fill_form, fill_group, "shapeFillGradientBlock");
  QWidget* const fill_gradient_block = fill_gradient.first;
  QFormLayout* const fill_gradient_form = fill_gradient.second;
  fill_gradient_form->addRow(QObject::tr("Style:"), gradient_type_combo);

  auto* gradient_angle_spin = new UnitIntSpinBox(SpinUnit::Degrees, fill_group);
  gradient_angle_spin->setObjectName(QStringLiteral("shapeGradientAngleSpin"));
  gradient_angle_spin->setRange(-180, 180);
  configure_dialog_spinbox(gradient_angle_spin, 72);
  add_angle_row(fill_gradient_form, QObject::tr("Angle:"), gradient_angle_spin);

  auto* gradient_scale_spin = new QSpinBox(fill_group);
  gradient_scale_spin->setObjectName(QStringLiteral("shapeGradientScaleSpin"));
  gradient_scale_spin->setRange(10, 1000);
  gradient_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(gradient_scale_spin, 72);
  add_spin_row(fill_gradient_form, QObject::tr("Scale:"), gradient_scale_spin);

  auto* gradient_reverse_check = new QCheckBox(QObject::tr("Reverse"), fill_group);
  gradient_reverse_check->setObjectName(QStringLiteral("shapeGradientReverseCheck"));
  fill_gradient_form->addRow(QString(), gradient_reverse_check);

  auto* fill_pattern_combo = new QComboBox(fill_group);
  fill_pattern_combo->setObjectName(QStringLiteral("shapeFillPatternCombo"));
  populate_pattern_combo(fill_pattern_combo);
  fill_form->addRow(QObject::tr("Pattern:"), fill_pattern_combo);
  const auto fill_pattern = add_paint_block(fill_form, fill_group, "shapeFillPatternBlock");
  QWidget* const fill_pattern_block = fill_pattern.first;
  QFormLayout* const fill_pattern_form = fill_pattern.second;

  auto* pattern_scale_spin = new QSpinBox(fill_group);
  pattern_scale_spin->setObjectName(QStringLiteral("shapePatternScaleSpin"));
  pattern_scale_spin->setRange(1, 1000);
  pattern_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(pattern_scale_spin, 72);
  add_spin_row(fill_pattern_form, QObject::tr("Scale:"), pattern_scale_spin);

  // Pattern placement (PtFl Angl / phase / Algn; rendered by the shared
  // PatternTileSampler and round-tripped through the PSD writer).
  auto* pattern_angle_spin = new UnitSpinBox(SpinUnit::Degrees, fill_group);
  pattern_angle_spin->setObjectName(QStringLiteral("shapePatternAngleSpin"));
  pattern_angle_spin->setRange(-180.0, 180.0);
  pattern_angle_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_angle_spin, 72);
  add_angle_row(fill_pattern_form, QObject::tr("Angle:"), pattern_angle_spin);

  auto* pattern_offset_x_spin = new UnitSpinBox(SpinUnit::Pixels, fill_group);
  pattern_offset_x_spin->setObjectName(QStringLiteral("shapePatternOffsetXSpin"));
  pattern_offset_x_spin->set_context_provider(pixel_field_context);
  pattern_offset_x_spin->setRange(-30000.0, 30000.0);
  pattern_offset_x_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_offset_x_spin, 80);
  add_spin_row(fill_pattern_form, QObject::tr("Offset X:"), pattern_offset_x_spin);

  auto* pattern_offset_y_spin = new UnitSpinBox(SpinUnit::Pixels, fill_group);
  pattern_offset_y_spin->setObjectName(QStringLiteral("shapePatternOffsetYSpin"));
  pattern_offset_y_spin->set_context_provider(pixel_field_context);
  pattern_offset_y_spin->setRange(-30000.0, 30000.0);
  pattern_offset_y_spin->setDecimals(1);
  configure_dialog_spinbox(pattern_offset_y_spin, 80);
  add_spin_row(fill_pattern_form, QObject::tr("Offset Y:"), pattern_offset_y_spin);

  auto* pattern_align_check = new QCheckBox(QObject::tr("Align with layer"), fill_group);
  pattern_align_check->setObjectName(QStringLiteral("shapePatternAlignCheck"));
  pattern_align_check->setToolTip(
      QObject::tr("Anchor the tile grid to the layer's position; unchecked anchors it to the "
                  "document origin"));
  fill_pattern_form->addRow(QString(), pattern_align_check);

  right_column->addWidget(fill_group);

  // --- Edge: the shape's vector-mask Feather / Density (Photoshop's
  // Properties-panel pair on a shape layer; docs/vector-tools-appearance.md) ---
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
  feather_spin->set_context_provider(pixel_field_context);
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
  density_spin->setToolTip(QObject::tr("Below 100% the fill, or the stroke's color when the shape has a stroke, shows over the "
                                       "whole canvas, like Photoshop's vector mask density"));
  configure_dialog_spinbox(density_spin, 80);
  add_spin_row(edge_form, QObject::tr("Density:"), density_spin);
  // Density below 100% shows the fill over the whole canvas, not only at the shape
  // (Photoshop's vector mask density on a shape layer, and what the file stores). The note
  // says so while that is the case (local-051 follow-up 2: it reads as a bug otherwise).
  // With a stroke that draws, the stroke's paint is what spreads, over the canvas and as a
  // wash over the fill (rasterize_vector_shape), and the note says that instead; it follows
  // the stroke's width, paint and opacity as they change.
  // Like the Stroke section's note it sets no width, and while hidden its height stays as
  // room under the section, so the dialog keeps its size (its height fits either wording).
  const QString density_fill_note = QObject::tr("Below 100% the fill also shows over the whole canvas, as in Photoshop.");
  const QString density_stroke_note =
      QObject::tr("Below 100% the stroke's color also shows over the whole canvas and over the fill, as in Photoshop.");
  auto* density_note = new QLabel(density_fill_note, edge_group);
  density_note->setObjectName(QStringLiteral("shapeDensityNote"));
  density_note->setWordWrap(true);
  density_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  {
    auto policy = density_note->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    density_note->setSizePolicy(policy);
  }
  edge_form->addRow(density_note);
  auto* density_note_room = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
  const auto refresh_density_note = [state, density_note, density_note_room, edge_form, left_column, fill_layer_only,
                                     density_fill_note, density_stroke_note] {
    const bool shown = state->settings.density != 255;
    const auto& stroke = state->settings.stroke;
    const bool stroke_draws = stroke.enabled && stroke.width > 0.0 && stroke.opacity > 0.0 &&
                              stroke.content.kind != VectorFillKind::None;
    const auto& wording = stroke_draws ? density_stroke_note : density_fill_note;
    if (density_note->text() != wording) density_note->setText(wording);
    density_note->setVisible(shown);
    // Its height is fixed once the page is built (below); a fill layer's dialog has no Edge.
    const int room = !fill_layer_only && density_note->minimumHeight() > 0
                         ? density_note->minimumHeight() + edge_form->verticalSpacing()
                         : 0;
    density_note_room->changeSize(0, shown ? 0 : room, QSizePolicy::Minimum, QSizePolicy::Fixed);
    left_column->invalidate();
  };
  QObject::connect(feather_spin, &QDoubleSpinBox::valueChanged, &dialog, [state, notify](double value) {
    state->settings.feather = value;
    notify({"feather"});
  });
  QObject::connect(density_spin, &QSpinBox::valueChanged, &dialog, [state, notify, refresh_density_note](int value) {
    state->settings.density = static_cast<std::uint8_t>(std::lround(value * 255.0 / 100.0));
    refresh_density_note();
    notify({"density"});
  });
  *refresh_edge_note = refresh_density_note;
  left_column->addWidget(edge_group);
  left_column->addSpacerItem(density_note_room);
  left_forms.push_back(edge_form);
  align_section_columns(left_forms, left_grids);

  // --- Stroke ---
  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), right_widget);
  auto* stroke_layout = new QVBoxLayout(stroke_group);
  stroke_layout->setContentsMargins(10, 8, 10, 8);
  stroke_layout->setSpacing(4);
  auto* stroke_form = new QFormLayout();
  stroke_form->setHorizontalSpacing(10);
  stroke_form->setVerticalSpacing(8);
  stroke_layout->addLayout(stroke_form);

  // No "Stroke the shape outline" checkbox (GitHub issue 311): Width 0 means no stroke. The
  // model keeps strokeEnabled and the last real width, so a PSD never stores a zero width.
  const auto stroke_field_value = [state] {
    return state->settings.stroke.enabled ? state->settings.stroke.width : 0.0;
  };
  auto* stroke_width_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_width_spin->setObjectName(QStringLiteral("shapeStrokeWidthSpin"));
  stroke_width_spin->setRange(0.0, 1000.0);
  stroke_width_spin->setDecimals(1);
  stroke_width_spin->setValue(stroke_field_value());
  stroke_width_spin->setToolTip(QObject::tr("Stroke width (0 for no stroke)"));
  apply_document_field_units(stroke_width_spin, thickness_units, true);
  unit_fields.push_back(stroke_width_spin);
  link_field_unit_picks(unit_fields);
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
    notify({"stroke.opacity"});
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

  auto* stroke_gradient_combo = add_gradient_row(stroke_form, stroke_group, true);

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
  const auto stroke_gradient = add_paint_block(stroke_form, stroke_group, "shapeStrokeGradientBlock");
  QWidget* const stroke_gradient_block = stroke_gradient.first;
  QFormLayout* const stroke_gradient_form = stroke_gradient.second;
  stroke_gradient_form->addRow(QObject::tr("Style:"), stroke_gradient_type_combo);

  auto* stroke_gradient_angle_spin = new UnitIntSpinBox(SpinUnit::Degrees, stroke_group);
  stroke_gradient_angle_spin->setObjectName(QStringLiteral("shapeStrokeGradientAngleSpin"));
  stroke_gradient_angle_spin->setRange(-180, 180);
  configure_dialog_spinbox(stroke_gradient_angle_spin, 72);
  add_angle_row(stroke_gradient_form, QObject::tr("Angle:"), stroke_gradient_angle_spin);

  auto* stroke_gradient_scale_spin = new QSpinBox(stroke_group);
  stroke_gradient_scale_spin->setObjectName(QStringLiteral("shapeStrokeGradientScaleSpin"));
  stroke_gradient_scale_spin->setRange(10, 1000);
  stroke_gradient_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(stroke_gradient_scale_spin, 72);
  add_spin_row(stroke_gradient_form, QObject::tr("Scale:"), stroke_gradient_scale_spin);

  auto* stroke_gradient_reverse_check = new QCheckBox(QObject::tr("Reverse"), stroke_group);
  stroke_gradient_reverse_check->setObjectName(
      QStringLiteral("shapeStrokeGradientReverseCheck"));
  stroke_gradient_form->addRow(QString(), stroke_gradient_reverse_check);

  auto* stroke_pattern_combo = new QComboBox(stroke_group);
  stroke_pattern_combo->setObjectName(QStringLiteral("shapeStrokePatternCombo"));
  populate_pattern_combo(stroke_pattern_combo);
  stroke_form->addRow(QObject::tr("Pattern:"), stroke_pattern_combo);
  const auto stroke_pattern = add_paint_block(stroke_form, stroke_group, "shapeStrokePatternBlock");
  QWidget* const stroke_pattern_block = stroke_pattern.first;
  QFormLayout* const stroke_pattern_form = stroke_pattern.second;

  auto* stroke_pattern_scale_spin = new QSpinBox(stroke_group);
  stroke_pattern_scale_spin->setObjectName(QStringLiteral("shapeStrokePatternScaleSpin"));
  stroke_pattern_scale_spin->setRange(1, 1000);
  stroke_pattern_scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(stroke_pattern_scale_spin, 72);
  add_spin_row(stroke_pattern_form, QObject::tr("Scale:"), stroke_pattern_scale_spin);

  auto* stroke_pattern_angle_spin = new UnitSpinBox(SpinUnit::Degrees, stroke_group);
  stroke_pattern_angle_spin->setObjectName(QStringLiteral("shapeStrokePatternAngleSpin"));
  stroke_pattern_angle_spin->setRange(-180.0, 180.0);
  stroke_pattern_angle_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_angle_spin, 72);
  add_angle_row(stroke_pattern_form, QObject::tr("Angle:"), stroke_pattern_angle_spin);

  auto* stroke_pattern_offset_x_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_pattern_offset_x_spin->setObjectName(QStringLiteral("shapeStrokePatternOffsetXSpin"));
  stroke_pattern_offset_x_spin->set_context_provider(pixel_field_context);
  stroke_pattern_offset_x_spin->setRange(-30000.0, 30000.0);
  stroke_pattern_offset_x_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_offset_x_spin, 80);
  add_spin_row(stroke_pattern_form, QObject::tr("Offset X:"), stroke_pattern_offset_x_spin);

  auto* stroke_pattern_offset_y_spin = new UnitSpinBox(SpinUnit::Pixels, stroke_group);
  stroke_pattern_offset_y_spin->setObjectName(QStringLiteral("shapeStrokePatternOffsetYSpin"));
  stroke_pattern_offset_y_spin->set_context_provider(pixel_field_context);
  stroke_pattern_offset_y_spin->setRange(-30000.0, 30000.0);
  stroke_pattern_offset_y_spin->setDecimals(1);
  configure_dialog_spinbox(stroke_pattern_offset_y_spin, 80);
  add_spin_row(stroke_pattern_form, QObject::tr("Offset Y:"), stroke_pattern_offset_y_spin);

  auto* stroke_pattern_align_check = new QCheckBox(QObject::tr("Align with layer"), stroke_group);
  stroke_pattern_align_check->setObjectName(QStringLiteral("shapeStrokePatternAlignCheck"));
  stroke_pattern_align_check->setToolTip(
      QObject::tr("Anchor the tile grid to the layer's position; unchecked anchors it to the "
                  "document origin"));
  stroke_pattern_form->addRow(QString(), stroke_pattern_align_check);

  auto* stroke_align_combo = new QComboBox(stroke_group);
  stroke_align_combo->setObjectName(QStringLiteral("shapeStrokeAlignCombo"));
  stroke_align_combo->addItem(QObject::tr("Inside"),
                              static_cast<int>(VectorStrokeAlignment::Inside));
  stroke_align_combo->addItem(QObject::tr("Center"),
                              static_cast<int>(VectorStrokeAlignment::Center));
  stroke_align_combo->addItem(QObject::tr("Outside"),
                              static_cast<int>(VectorStrokeAlignment::Outside));
  stroke_form->addRow(QObject::tr("Align:"), stroke_align_combo);
  // A line is a filled outline as wide as its Weight (local-051), so an Inside stroke only
  // recolours it and no Width makes the line thicker: say so under Align while that is the
  // case. The note never sets a width, and while it is hidden its height is kept as room
  // under the Stroke section (stroke_hint_room), so the rows close up but the page, and
  // with it the dialog, keeps its size when the note comes and goes.
  QLabel* stroke_inside_line_hint = nullptr;
  QSpacerItem* stroke_hint_room = nullptr;
  if (line_reference) {
    stroke_hint_room = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    stroke_inside_line_hint = new QLabel(
        QObject::tr("Inside keeps the stroke within the line's Weight. Choose Center or Outside to make the "
                    "line thicker."),
        stroke_group);
    stroke_inside_line_hint->setObjectName(QStringLiteral("shapeStrokeInsideLineHint"));
    stroke_inside_line_hint->setWordWrap(true);
    stroke_inside_line_hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto policy = stroke_inside_line_hint->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    stroke_inside_line_hint->setSizePolicy(policy);
    stroke_form->addRow(stroke_inside_line_hint);
  }
  const auto refresh_inside_line_hint = [state, stroke_inside_line_hint, stroke_hint_room, stroke_form, right_column] {
    if (stroke_inside_line_hint == nullptr) {
      return;
    }
    const bool shown = state->settings.stroke.enabled &&
                       state->settings.stroke.alignment == VectorStrokeAlignment::Inside;
    stroke_inside_line_hint->setVisible(shown);
    // The note's height is fixed once the form is built (below); until then it has no room.
    const int room = stroke_inside_line_hint->minimumHeight() > 0
                         ? stroke_inside_line_hint->minimumHeight() + stroke_form->verticalSpacing()
                         : 0;
    stroke_hint_room->changeSize(0, shown ? 0 : room, QSizePolicy::Minimum, QSizePolicy::Fixed);
    right_column->invalidate();
  };

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

  // The Dashes row (GitHub #556, #557 and its follow-up): one list of the built-in dash
  // presets, the saved stroke presets (Align, Caps, Corners, the dashes and the offset under
  // a name, in the app settings; width and paint stay out) and Custom. Save... stores the
  // current line settings, Delete removes the chosen saved preset. The list shows what the
  // settings match, a saved preset before a built-in.
  auto stroke_presets = std::make_shared<std::vector<StrokeStylePreset>>(load_stroke_style_presets());
  auto* stroke_dash_row = new QWidget(stroke_group);
  auto* stroke_dash_layout = new QHBoxLayout(stroke_dash_row);
  stroke_dash_layout->setContentsMargins(0, 0, 0, 0);
  stroke_dash_layout->setSpacing(6);
  auto* stroke_dash_combo = new QComboBox(stroke_dash_row);
  stroke_dash_combo->setObjectName(QStringLiteral("shapeStrokeDashCombo"));
  use_scrolling_list_popup(*stroke_dash_combo);
  auto* stroke_preset_save = new QPushButton(QObject::tr("Save..."), stroke_dash_row);
  stroke_preset_save->setObjectName(QStringLiteral("shapeStrokePresetSaveButton"));
  stroke_preset_save->setToolTip(
      QObject::tr("Save Align, Caps, Corners, Dashes and Dash offset as a preset"));
  auto* stroke_preset_delete = new QPushButton(QObject::tr("Delete"), stroke_dash_row);
  stroke_preset_delete->setObjectName(QStringLiteral("shapeStrokePresetDeleteButton"));
  stroke_preset_delete->setToolTip(QObject::tr("Delete the chosen preset"));
  stroke_dash_layout->addWidget(stroke_dash_combo, 1);
  stroke_dash_layout->addWidget(stroke_preset_save);
  stroke_dash_layout->addWidget(stroke_preset_delete);
  stroke_form->addRow(QObject::tr("Dashes:"), stroke_dash_row);
  field_rows[stroke_dash_combo] = stroke_dash_row;
  const auto rebuild_dash_list = [stroke_dash_combo, stroke_presets] {
    QSignalBlocker blocker(stroke_dash_combo);
    stroke_dash_combo->clear();
    for (const auto preset : stroke_dash_presets_in_menu_order()) {
      stroke_dash_combo->addItem(stroke_dash_preset_label(preset), static_cast<int>(preset));
    }
    if (!stroke_presets->empty()) stroke_dash_combo->insertSeparator(stroke_dash_combo->count());
    for (std::size_t i = 0; i < stroke_presets->size(); ++i) {
      stroke_dash_combo->addItem((*stroke_presets)[i].name, kSavedStrokePresetRow + static_cast<int>(i));
      stroke_dash_combo->setItemData(stroke_dash_combo->count() - 1,
                                     stroke_style_preset_summary((*stroke_presets)[i]), Qt::ToolTipRole);
    }
    stroke_dash_combo->addItem(QObject::tr("Custom"), kDashCustom);
  };
  rebuild_dash_list();

  auto* stroke_dash_pattern_row = new QWidget(stroke_group);
  auto* stroke_dash_pattern_layout = new QHBoxLayout(stroke_dash_pattern_row);
  stroke_dash_pattern_layout->setContentsMargins(0, 0, 0, 0);
  stroke_dash_pattern_layout->setSpacing(6);
  auto* stroke_dash_pattern_edit = new QLineEdit(stroke_dash_pattern_row);
  stroke_dash_pattern_edit->setObjectName(QStringLiteral("shapeStrokeDashPatternEdit"));
  stroke_dash_pattern_edit->setPlaceholderText(QStringLiteral("4 2"));
  stroke_dash_pattern_edit->setToolTip(
      QObject::tr("Dash and gap lengths in stroke widths, separated by spaces, for example "
                  "4 2 or 6 2 1 2. A length of 0 draws a dot with Round or Square caps."));
  auto* stroke_dash_pattern_unit = new QLabel(QObject::tr("x width"), stroke_dash_pattern_row);
  stroke_dash_pattern_layout->addWidget(stroke_dash_pattern_edit, 1);
  stroke_dash_pattern_layout->addWidget(stroke_dash_pattern_unit);
  stroke_form->addRow(QObject::tr("Dash pattern:"), stroke_dash_pattern_row);
  field_rows[stroke_dash_pattern_edit] = stroke_dash_pattern_row;

  // vstk strokeStyleLineDashOffset: where the pattern starts along the path.
  auto* stroke_dash_offset_spin = new QDoubleSpinBox(stroke_group);
  stroke_dash_offset_spin->setObjectName(QStringLiteral("shapeStrokeDashOffsetSpin"));
  stroke_dash_offset_spin->setRange(-1000.0, 1000.0);
  stroke_dash_offset_spin->setDecimals(2);
  stroke_dash_offset_spin->setSingleStep(0.5);
  stroke_dash_offset_spin->setToolTip(
      QObject::tr("Moves the dash pattern along the path, in stroke widths"));
  configure_dialog_spinbox(stroke_dash_offset_spin, 72);
  auto* stroke_dash_offset_row = new QWidget(stroke_group);
  auto* stroke_dash_offset_layout = new QHBoxLayout(stroke_dash_offset_row);
  stroke_dash_offset_layout->setContentsMargins(0, 0, 0, 0);
  stroke_dash_offset_layout->setSpacing(6);
  stroke_dash_offset_layout->addWidget(
      wrap_spin_with_step_buttons(stroke_dash_offset_spin, stroke_dash_offset_row, QObject::tr("Dash offset:")));
  stroke_dash_offset_layout->addWidget(new QLabel(QObject::tr("x width"), stroke_dash_offset_row));
  stroke_dash_offset_layout->addStretch(1);
  stroke_form->addRow(QObject::tr("Dash offset:"), stroke_dash_offset_row);
  field_rows[stroke_dash_offset_spin] = stroke_dash_offset_row;

  right_column->addWidget(stroke_group);
  if (stroke_hint_room != nullptr) {
    right_column->addSpacerItem(stroke_hint_room);
  }
  // Fill's and Stroke's fields start in one column, and so do the fields of their
  // nested gradient and pattern blocks (GitHub #679). The fill-only dialog hides
  // Stroke, so its labels do not widen Fill's column there.
  if (fill_layer_only) {
    align_form_columns({fill_form});
    align_form_columns({fill_gradient_form, fill_pattern_form});
  } else {
    align_form_columns({fill_form, stroke_form});
    align_form_columns({fill_gradient_form, fill_pattern_form, stroke_gradient_form, stroke_pattern_form});
  }
  if (fill_layer_only) {
    for (QWidget* group : std::initializer_list<QWidget*>{layer_group, edge_group, stroke_group}) {
      group->setVisible(false);
    }
    left_widget->setVisible(false);
  }

  left_column->addStretch(1);
  right_column->addStretch(1);
  // The stay-open window applies every change at once, so it has Close in place of OK and
  // Cancel and no Preview box.
  auto* buttons = new QDialogButtonBox(
      live ? QDialogButtonBox::StandardButtons(QDialogButtonBox::Close)
           : QDialogButtonBox::StandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel),
      &dialog);
  auto* reset_button = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
  reset_button->setObjectName(QStringLiteral("shapeAppearanceResetButton"));
  reset_button->setToolTip(QObject::tr("Restore the default fill, stroke, opacity, and edge (the geometry stays)"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, window_ptr, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, window_ptr, &QDialog::reject);
  auto* footer = new QHBoxLayout();
  // The stay-open window can show the fill gradient's on-canvas handles under any tool
  // (GitHub issue 694). The window owns the state; a new page starts from it.
  QCheckBox* gradient_handles_check = nullptr;
  if (live && options.gradient_handles_toggled) {
    gradient_handles_check = new QCheckBox(QObject::tr("Show gradient handles"), &dialog);
    gradient_handles_check->setObjectName(QStringLiteral("shapeAppearanceGradientHandlesCheck"));
    gradient_handles_check->setToolTip(
        QObject::tr("Show the fill gradient's handles on the canvas with any tool while this window is open. "
                    "Drag them to move the gradient and its stops."));
    gradient_handles_check->setChecked(options.show_gradient_handles);
    footer->addWidget(gradient_handles_check);
    QObject::connect(gradient_handles_check, &QCheckBox::toggled, &dialog,
                     [toggled = std::move(options.gradient_handles_toggled)](bool checked) { toggled(checked); });
  }
  if (!live) {
    auto* preview_check = new QCheckBox(QObject::tr("Preview"), &dialog);
    preview_check->setObjectName(QStringLiteral("shapeAppearancePreviewCheck"));
    preview_check->setChecked(true);
    // Preview sits at the left of the button row, before Reset, as in the other
    // preview dialogs (#582). The box keeps its own stretch between Reset and OK.
    footer->addWidget(preview_check);
    QObject::connect(preview_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
      state->settings.preview_enabled = checked;
      notify();
    });
  }
  footer->addWidget(buttons, 1);
  dialog_layout->addLayout(footer);


  if (batch != nullptr && batch->selected_count > 1) {
    auto* apply_all = buttons->addButton(QObject::tr("Apply All Settings to Selected Layers"),
                                       QDialogButtonBox::ActionRole);
    apply_all->setObjectName(QStringLiteral("shapeAppearanceApplyAllButton"));
    QObject::connect(apply_all, &QPushButton::clicked, &dialog, [=] {
      auto recipe = state->settings;
      recipe.edits.reset();
      edits->append({{}, [recipe, fill_layer_only](ShapeAppearanceSettings& target) {
        if (fill_layer_only) {  // only the Fill section shows
          target.fill = recipe.fill;
          return;
        }
        const auto geometry = target.geometry;
        const auto path_box = target.path_box;
        const auto path_shape = target.path_shape;
        target = recipe;
        target.geometry = geometry;
        target.path_box = path_box;
        target.path_shape = path_shape;
        if (appearance_has_editable_radii(target) && appearance_has_editable_radii(recipe)) {
          target.geometry->corner_radii = recipe.geometry->corner_radii;
          if (std::any_of(target.geometry->corner_radii.begin(), target.geometry->corner_radii.end(),
                          [](double value) { return value > 0.0; }))
            target.geometry->kind = LiveShapeKind::RoundedRectangle;
        }
      }});
      *previous = state->settings;
      notify();
    });
  }

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
    set_row(fill_gradient_block, gradient);
    set_row(fill_pattern_combo, pattern);
    set_row(fill_pattern_block, pattern);
    if (gradient_handles_check != nullptr) {
      gradient_handles_check->setEnabled(gradient);  // only a gradient fill has handles
    }
  };

  // The stroke rows stay enabled: the width is the stroke switch (0 = none). The
  // per-paint-kind rows hide like the fill section's.
  const auto refresh_stroke_rows = [=] {
    const bool enabled = true;
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
    set_row_visible(stroke_gradient_block, gradient_paint);
    set_row_visible(stroke_pattern_combo, pattern_paint);
    set_row_visible(stroke_pattern_block, pattern_paint);
    set_row_visible(stroke_dash_pattern_edit, stroke_dash_combo->currentData().toInt() == kDashCustom);
    for (QWidget* field :
         std::initializer_list<QWidget*>{stroke_width_spin, stroke_opacity_spin, stroke_paint_combo,
                                         stroke_color_button, stroke_gradient_combo,
                                         stroke_gradient_type_combo, stroke_gradient_angle_spin,
                                         stroke_gradient_scale_spin, stroke_gradient_reverse_check,
                                         stroke_pattern_combo, stroke_pattern_scale_spin,
                                         stroke_pattern_angle_spin, stroke_pattern_offset_x_spin,
                                         stroke_pattern_offset_y_spin, stroke_pattern_align_check,
                                         stroke_align_combo, stroke_cap_combo, stroke_join_combo,
                                         stroke_dash_combo, stroke_dash_pattern_edit}) {
      set_row(field, enabled);
    }
    set_row(stroke_dash_offset_spin, !state->settings.stroke.dashes.empty());
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
    sync_angle_dial(*gradient_angle_spin);
    gradient_scale_spin->setValue(
        std::clamp(static_cast<int>(std::lround(gradient.scale * 100.0F)), 10, 1000));
    gradient_reverse_check->setChecked(gradient.reverse);
    select_gradient_row(fill_gradient_combo, gradient, state->custom_fill_gradient);
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
      sync_angle_dial(*stroke_gradient_angle_spin);
      stroke_gradient_scale_spin->setValue(
          std::clamp(static_cast<int>(std::lround(gradient.scale * 100.0F)), 10, 1000));
      stroke_gradient_reverse_check->setChecked(gradient.reverse);
      select_gradient_row(stroke_gradient_combo, gradient, state->custom_stroke_gradient);
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
      sync_angle_dial(*stroke_pattern_angle_spin);
      QSignalBlocker offset_x_blocker(stroke_pattern_offset_x_spin);
      stroke_pattern_offset_x_spin->setValue(content.pattern_phase_x);
      QSignalBlocker offset_y_blocker(stroke_pattern_offset_y_spin);
      stroke_pattern_offset_y_spin->setValue(content.pattern_phase_y);
      QSignalBlocker align_blocker(stroke_pattern_align_check);
      stroke_pattern_align_check->setChecked(content.pattern_linked);
    }
  };

  // Align, Caps, Corners, Dashes and Dash offset from state->settings (also a
  // saved preset's pick).
  // Custom while the user works on a pattern, else the saved preset the settings match,
  // else the built-in (or Custom for a pattern no built-in has).
  const auto select_dash_row = [=] {
    QSignalBlocker dash_blocker(stroke_dash_combo);
    const auto& dashes = state->settings.stroke.dashes;
    auto dash_row = dash_row_for(dashes, state->settings.stroke.cap);
    if (state->custom_dash_row && !dashes.empty()) {
      dash_row = kDashCustom;
    } else if (const auto saved = matching_stroke_style_preset(state->settings.stroke, *stroke_presets);
               saved >= 0) {
      dash_row = kSavedStrokePresetRow + saved;
    }
    // A pattern that matches no preset is the Custom row's, restorable after
    // trying the presets.
    if (dash_row == kDashCustom) state->custom_dashes = dashes;
    stroke_dash_combo->setCurrentIndex(std::max(0, stroke_dash_combo->findData(dash_row)));
    stroke_preset_delete->setEnabled(dash_row >= kSavedStrokePresetRow);
  };
  const auto sync_stroke_style_controls = [=] {
    QSignalBlocker align_blocker(stroke_align_combo);
    stroke_align_combo->setCurrentIndex(std::max(
        0, stroke_align_combo->findData(static_cast<int>(state->settings.stroke.alignment))));
    QSignalBlocker cap_blocker(stroke_cap_combo);
    stroke_cap_combo->setCurrentIndex(
        std::max(0, stroke_cap_combo->findData(static_cast<int>(state->settings.stroke.cap))));
    QSignalBlocker join_blocker(stroke_join_combo);
    stroke_join_combo->setCurrentIndex(
        std::max(0, stroke_join_combo->findData(static_cast<int>(state->settings.stroke.join))));
    select_dash_row();
    stroke_dash_pattern_edit->setText(dash_pattern_text(state->custom_dashes));
    stroke_dash_pattern_edit->setModified(false);
    QSignalBlocker offset_blocker(stroke_dash_offset_spin);
    stroke_dash_offset_spin->setValue(state->settings.stroke.dash_offset);
    refresh_inside_line_hint();
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
    sync_stroke_style_controls();
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
      refresh_density_note();
      QSignalBlocker width_blocker(stroke_width_spin);
      stroke_width_spin->setValue(stroke_field_value());
    }
    fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
    stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
    sync_gradient_controls();
    sync_stroke_paint_controls();
  };
  sync_all_controls();
  // The Opacity and Fill Opacity sliders keep a usable width: the box layout
  // shrinks a column toward its minimum when the other one asks for more, and
  // a bare QSlider's minimum is a few pixels (issue 552).
  for (auto* slider : page->findChildren<QSlider*>()) {
    slider->setMinimumWidth(std::max(slider->minimumWidth(), 72));
  }
  if (stroke_inside_line_hint != nullptr) {
    // Its height is what the text needs at the Stroke form's narrowest, measured while every
    // row is there: the note is never cut off, and a wider dialog leaves blank room under it.
    stroke_inside_line_hint->setFixedHeight(
        stroke_inside_line_hint->heightForWidth(std::max(120, stroke_form->minimumSize().width())));
    refresh_inside_line_hint();
  }
  {
    // Room for the longer of the two wordings at the Edge form's narrowest.
    const int note_width = std::max(120, edge_form->minimumSize().width());
    int note_height = 0;
    for (const auto& wording : {density_fill_note, density_stroke_note}) {
      density_note->setText(wording);
      note_height = std::max(note_height, density_note->heightForWidth(note_width));
    }
    density_note->setFixedHeight(note_height);
  }
  refresh_density_note();
  // Measure the page while EVERY per-kind row is still visible, so switching
  // a paint kind later never pushes the columns' minimum past the viewport (a
  // hidden horizontal bar would clip the steppers), the Layer Style
  // stroke-page rule. grow_to_fit below may still widen the dialog to the
  // page's preferred width.
  page->layout()->activate();
  // Reserve the vertical scrollbar's width too: a paint-kind switch that
  // makes the page taller than the dialog adds the bar, which would otherwise
  // steal those pixels from the rows and clip the right column's steppers.
  scroll->setMinimumWidth(page->minimumSizeHint().width() +
                          scroll->verticalScrollBar()->sizeHint().width());
  refresh_fill_rows();
  refresh_stroke_rows();

  // The dialog size that shows the whole page with the rows visible now.
  // Sized from the page's own hint (QScrollArea::sizeHint caps at ~24 font
  // heights, which would show a needless scrollbar on any normal screen) and
  // capped at the screen's available height; past that cap the scrollbar
  // shows, and its width is added so it never takes width from the columns.
  // Rows hidden or shown since the last measurement invalidated their
  // layouts but not the hints cached in each parent layout's item
  // (QWidgetItemV2), which only a widget's own updateGeometry clears (before
  // the dialog shows, the posted layout requests have not run). Clear them so
  // the height counts the visible rows only.
  QDialog* const dialog_ptr = window_ptr;
  QWidget* const host_ptr = &host;
  // `chrome_height`: what the window around a page adds above and below it (its title bar,
  // margins and size grip), which counts against the screen's height too.
  const auto fitted_dialog_size = [page, scroll, dialog_layout](const QScreen* screen, int chrome_height = 0) {
    for (auto* child : page->findChildren<QWidget*>()) {
      child->updateGeometry();
    }
    for (auto* layout : page->findChildren<QLayout*>()) {
      layout->invalidate();
    }
    page->layout()->activate();
    scroll->updateGeometry();
    dialog_layout->invalidate();
    const auto page_hint = page->sizeHint();
    const auto margins = dialog_layout->contentsMargins();
    int page_width = page_hint.width();
    // The dialog layout's own hint counts every row under the page (the Preview
    // and button row) with their spacings and margins; only its scroll area
    // entry is wrong (the capped QScrollArea hint), so swap that for the page's
    // hint. Summing the rows by hand once missed a row and opened the dialog one
    // row short, with a scrollbar on a tall screen (Seth, October 2026).
    int height = dialog_layout->sizeHint().height() - scroll->sizeHint().height() + page_hint.height();
    int max_width = std::numeric_limits<int>::max();
    if (screen != nullptr) {
      const auto available = screen->availableGeometry();
      const auto max_height = std::max(320, available.height() - 40 - std::max(0, chrome_height));
      if (height > max_height) {
        height = max_height;
        page_width += scroll->verticalScrollBar()->sizeHint().width();
      }
      max_width = std::max(320, available.width() - 40);
    }
    const int width = std::max(page_width, scroll->minimumWidth()) + margins.left() + margins.right();
    return QSize(std::min(width, max_width), height);
  };
  // A paint or fill kind switch that adds rows grows the open dialog to show
  // them, up to the screen, instead of leaving a scrollbar that narrows the
  // columns (issue 552). It never shrinks: a size the user dragged larger stays.
  const auto grow_to_fit = [dialog_ptr, host_ptr, fitted_dialog_size] {
    if (!dialog_ptr->isVisible()) {
      return;
    }
    const QScreen* screen = dialog_ptr->screen();
    const auto current = dialog_ptr->size();
    // A page inside the stay-open window: the window's own title bar and margins around it.
    const auto window_extra = host_ptr == dialog_ptr ? QSize(0, 0) : current - host_ptr->size();
    const auto grown = current.expandedTo(fitted_dialog_size(screen, window_extra.height()) + window_extra);
    if (grown == current) {
      return;
    }
    const auto frame = dialog_ptr->frameGeometry();
    const auto frame_extra = frame.size() - current;
    dialog_ptr->resize(grown);
    if (screen == nullptr) {
      return;
    }
    // Keep the grown dialog on its screen: it grows down and right, so move
    // it up or left as far as the screen's edge allows.
    const auto available = screen->availableGeometry();
    const auto grown_frame = grown + frame_extra;
    auto top_left = frame.topLeft();
    if (top_left.y() + grown_frame.height() > available.bottom() + 1) {
      top_left.setY(std::max(available.top(), available.bottom() + 1 - grown_frame.height()));
    }
    if (top_left.x() + grown_frame.width() > available.right() + 1) {
      top_left.setX(std::max(available.left(), available.right() + 1 - grown_frame.width()));
    }
    if (top_left != frame.topLeft()) {
      dialog_ptr->move(top_left);
    }
  };

  // --- Wiring ---
  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [=] {
    // Factory appearance; the geometry the dialog opened with stays.
    auto restored = reset_defaults;
    restored.geometry = state->settings.geometry;
    restored.path_box = state->settings.path_box;
    restored.path_shape = state->settings.path_shape;
    restored.preview_enabled = state->settings.preview_enabled;
    // A line's factory stroke is centered, like a newly drawn line's: an Inside stroke could
    // not show past its Weight (local-051).
    if (appearance_has_editable_line(restored)) restored.stroke.alignment = VectorStrokeAlignment::Center;
    // A fill layer's dialog resets only the Fill section it shows.
    const auto keep_hidden_sections = [fill_layer_only](ShapeAppearanceSettings& target,
                                                        const ShapeAppearanceSettings& source) {
      if (!fill_layer_only) return;
      target.stroke = source.stroke;
      target.layer_opacity = source.layer_opacity;
      target.fill_opacity = source.fill_opacity;
      target.feather = source.feather;
      target.density = source.density;
      target.fill_layer_only = source.fill_layer_only;
    };
    keep_hidden_sections(restored, state->settings);

    state->settings = std::move(restored);
    if (batch != nullptr) {
      auto recipe = reset_defaults;
      recipe.edits.reset();
      edits->append({{}, [recipe, keep_hidden_sections](ShapeAppearanceSettings& target) {
        const auto original = target;
        target = recipe;
        target.geometry = original.geometry;
        target.path_box = original.path_box;
        target.path_shape = original.path_shape;
        if (appearance_has_editable_line(target)) target.stroke.alignment = VectorStrokeAlignment::Center;
        keep_hidden_sections(target, original);
      }});
      *previous = state->settings;
    }
    state->stroke_paint_touched = true;
    state->custom_dashes.clear();
    state->custom_dash_row = false;
    sync_all_controls();
    refresh_fill_rows();
    refresh_stroke_rows();
    grow_to_fit();
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
      state->settings.fill.pattern_name = saved_pattern_name(fill_pattern_combo);
    }
    refresh_fill_rows();
    grow_to_fit();
    notify({"fill.kind"});
  });

  QObject::connect(fill_color_button, &QPushButton::clicked, &dialog, [=] {
    run_child([=] {
      const auto saved = state->settings;
      const auto saved_edits = *edits;
      const auto& current = saved.fill.color;
      bool selected = false;
      const auto apply_color = [=](QColor color) {
        state->settings.fill.kind = VectorFillKind::Solid;
        state->settings.fill.color = RgbColor{
            static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue())};
        state->stroke_paint_touched = true;
        fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
        notify({"fill.color"});
      };
      const auto chosen = request_patchy_color(
          window_ptr, QColor(current.red, current.green, current.blue), QObject::tr("Shape Fill Color"),
          apply_color, &selected);
      if (!chosen.has_value()) {
        state->settings = saved;
        *edits = saved_edits;
        *previous = saved;
        fill_color_button->setIcon(color_swatch_icon(saved.fill.color));
        notify();
        return false;
      }
      if (selected || batch == nullptr) {
        apply_color(*chosen);
      }
      return true;
    });
  });
  QObject::connect(fill_gradient_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    if (fill_gradient_combo->currentData().toString().isEmpty()) {
      static_cast<GradientDefinition&>(state->settings.fill.gradient) = state->custom_fill_gradient;
      notify({"fill.gradient.definition"});
      return;
    }
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
      notify({"fill.gradient.definition"});
    }
  });
  QObject::connect(gradient_type_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.gradient.type =
        static_cast<LayerStyleGradientType>(gradient_type_combo->currentData().toInt());
    notify({"fill.gradient.type"});
  });
  QObject::connect(gradient_angle_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.angle_degrees = static_cast<float>(value);
    notify({"fill.gradient.angle_degrees"});
  });
  QObject::connect(gradient_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.scale = static_cast<float>(value) / 100.0F;
    notify({"fill.gradient.scale"});
  });
  QObject::connect(gradient_reverse_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.fill.gradient.reverse = checked;
    notify({"fill.gradient.reverse"});
  });
  QObject::connect(fill_pattern_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.pattern_id = fill_pattern_combo->currentData().toString().toStdString();
    state->settings.fill.pattern_name = saved_pattern_name(fill_pattern_combo);
    notify({"fill.pattern_id", "fill.pattern_name"});
  });
  QObject::connect(pattern_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.pattern_scale = static_cast<double>(value) / 100.0;
    notify({"fill.pattern_scale"});
  });
  QObject::connect(pattern_angle_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_angle_degrees = value;
    notify({"fill.pattern_angle_degrees"});
  });
  QObject::connect(pattern_offset_x_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_phase_x = value;
    notify({"fill.pattern_phase_x"});
  });
  QObject::connect(pattern_offset_y_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.fill.pattern_phase_y = value;
    notify({"fill.pattern_phase_y"});
  });
  QObject::connect(pattern_align_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.fill.pattern_linked = checked;
    notify({"fill.pattern_linked"});
  });
  QObject::connect(stroke_width_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.stroke.enabled = value > 0.0;
    if (value > 0.0) state->settings.stroke.width = std::clamp(value, 0.1, 1000.0);
    // 0 leaves every shape's own width alone (a batch must not copy this one's).
    refresh_inside_line_hint();
    notify(value > 0.0 ? std::vector<std::string>{"stroke.enabled", "stroke.width"}
                       : std::vector<std::string>{"stroke.enabled"});
  });

  QObject::connect(stroke_color_button, &QPushButton::clicked, &dialog, [=] {
    run_child([=] {
      const auto saved = state->settings;
      const auto saved_edits = *edits;
      const auto& current = saved.stroke.content.color;
      bool selected = false;
      const auto apply_color = [=](QColor color) {
        state->settings.stroke.content.kind = VectorFillKind::Solid;
        state->settings.stroke.content.color = RgbColor{
            static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue())};
        state->stroke_paint_touched = true;
        stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
        notify({"stroke.content.color"});
      };
      const auto chosen = request_patchy_color(
          window_ptr, QColor(current.red, current.green, current.blue), QObject::tr("Shape Stroke Color"),
          apply_color, &selected);
      if (!chosen.has_value()) {
        state->settings = saved;
        *edits = saved_edits;
        *previous = saved;
        stroke_color_button->setIcon(color_swatch_icon(saved.stroke.content.color));
        notify();
        return false;
      }
      if (selected || batch == nullptr) {
        apply_color(*chosen);
      }
      return true;
    });
  });
  QObject::connect(stroke_align_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.alignment =
        static_cast<VectorStrokeAlignment>(stroke_align_combo->currentData().toInt());
    refresh_inside_line_hint();
    notify({"stroke.alignment"});
  });
  QObject::connect(stroke_cap_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    const auto old_cap = state->settings.stroke.cap;
    state->settings.stroke.cap = static_cast<VectorStrokeCap>(stroke_cap_combo->currentData().toInt());
    // The presets follow the cap, so a Butt cap still draws Dotted's dots and
    // a Round cap keeps the gaps open; a saved preset's pattern counts as its built-in.
    auto dash_row = stroke_dash_combo->currentData().toInt();
    if (dash_row >= kSavedStrokePresetRow) dash_row = dash_row_for(state->settings.stroke.dashes, old_cap);
    if (dash_row != kDashCustom && stroke_dash_preset_follows_cap(static_cast<StrokeDashPreset>(dash_row))) {
      state->settings.stroke.dashes =
          stroke_dash_preset_pattern(static_cast<StrokeDashPreset>(dash_row), state->settings.stroke.cap);
      notify({"stroke.cap", "stroke.dashes"});
      return;
    }
    notify({"stroke.cap"});
  });
  QObject::connect(stroke_join_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.join =
        static_cast<VectorStrokeJoin>(stroke_join_combo->currentData().toInt());
    notify({"stroke.join"});
  });
  QObject::connect(stroke_dash_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    const auto row = stroke_dash_combo->currentData().toInt();
    if (row >= kSavedStrokePresetRow) {
      const auto index = static_cast<std::size_t>(row - kSavedStrokePresetRow);
      if (index >= stroke_presets->size()) return;
      apply_stroke_style_preset(state->settings.stroke, (*stroke_presets)[index]);
      state->custom_dash_row = false;
      sync_stroke_style_controls();
      refresh_stroke_rows();
      grow_to_fit();
      notify({"stroke.alignment", "stroke.cap", "stroke.join", "stroke.dashes", "stroke.dash_offset"});
      return;
    }
    state->custom_dash_row = row == kDashCustom;
    std::vector<std::string> changed{"stroke.dashes"};
    if (row == kDashCustom) {
      // Start from the pattern on screen, the last custom one, or 4 on, 2 off.
      if (state->custom_dashes.empty()) {
        state->custom_dashes = !state->settings.stroke.dashes.empty()
                                   ? state->settings.stroke.dashes
                                   : std::vector<double>{4.0, 2.0};
      }
      state->settings.stroke.dashes = state->custom_dashes;
      stroke_dash_pattern_edit->setText(dash_pattern_text(state->custom_dashes));
      stroke_dash_pattern_edit->setModified(false);
    } else {
      // The preset sets its caps and corners too (apply_stroke_dash_preset); both
      // stay editable after.
      const auto preset = static_cast<StrokeDashPreset>(row);
      if (stroke_dash_preset_cap(preset).has_value()) changed.push_back("stroke.cap");
      if (stroke_dash_preset_join(preset).has_value()) changed.push_back("stroke.join");
      apply_stroke_dash_preset(state->settings.stroke, preset);
      QSignalBlocker cap_blocker(stroke_cap_combo);
      stroke_cap_combo->setCurrentIndex(
          std::max(0, stroke_cap_combo->findData(static_cast<int>(state->settings.stroke.cap))));
      QSignalBlocker join_blocker(stroke_join_combo);
      stroke_join_combo->setCurrentIndex(
          std::max(0, stroke_join_combo->findData(static_cast<int>(state->settings.stroke.join))));
    }
    refresh_stroke_rows();
    grow_to_fit();
    notify(changed);
  });
  QObject::connect(stroke_dash_pattern_edit, &QLineEdit::editingFinished, &dialog, [=] {
    // Commits on Enter or focus-out, like the value spins; leaving the field
    // untouched is not an edit (a batch would otherwise apply it to every layer).
    if (!stroke_dash_pattern_edit->isModified()) return;
    stroke_dash_pattern_edit->setModified(false);
    const auto parsed = parse_dash_pattern_text(stroke_dash_pattern_edit->text());
    if (parsed.has_value()) state->custom_dashes = *parsed;
    stroke_dash_pattern_edit->setText(dash_pattern_text(state->custom_dashes));
    if (!parsed.has_value() || state->settings.stroke.dashes == state->custom_dashes) return;
    state->settings.stroke.dashes = state->custom_dashes;
    notify({"stroke.dashes"});
  });
  QObject::connect(stroke_dash_offset_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.stroke.dash_offset = value;
    notify({"stroke.dash_offset"});
  });

  // Saved stroke presets: the list is read once per dialog and written back on every
  // Save or Delete.
  *refresh_preset_match = select_dash_row;
  QObject::connect(stroke_preset_save, &QPushButton::clicked, &dialog, [=, &dialog] {
    const auto current = stroke_dash_combo->currentData().toInt() - kSavedStrokePresetRow;
    const bool saved_chosen = current >= 0 && current < static_cast<int>(stroke_presets->size());
    QInputDialog prompt(&dialog);
    prompt.setObjectName(QStringLiteral("shapeStrokePresetNameDialog"));
    prompt.setWindowTitle(QObject::tr("Save Stroke Preset"));
    prompt.setLabelText(QObject::tr("Preset name:"));
    prompt.setTextValue(saved_chosen ? (*stroke_presets)[static_cast<std::size_t>(current)].name
                                     : QObject::tr("Stroke preset %1").arg(static_cast<int>(stroke_presets->size()) + 1));
    if (exec_dialog(prompt) != QDialog::Accepted) return;
    const auto name = prompt.textValue().trimmed();
    if (name.isEmpty()) return;
    if (stroke_preset_name_is_builtin(name)) {
      QMessageBox::warning(&dialog, QObject::tr("Save Stroke Preset"),
                           QObject::tr("A stroke preset named \"%1\" already exists.").arg(name));
      return;
    }
    // A name already in the list replaces that preset in place.
    auto preset = stroke_style_preset_from(state->settings.stroke, name);
    const auto existing = std::find_if(stroke_presets->begin(), stroke_presets->end(),
                                       [&](const StrokeStylePreset& entry) { return entry.name == name; });
    if (existing != stroke_presets->end()) {
      *existing = std::move(preset);
    } else {
      stroke_presets->push_back(std::move(preset));
    }
    save_stroke_style_presets(*stroke_presets);
    rebuild_dash_list();
    state->custom_dash_row = false;
    select_dash_row();
    refresh_stroke_rows();
  });
  QObject::connect(stroke_preset_delete, &QPushButton::clicked, &dialog, [=] {
    const auto current = stroke_dash_combo->currentData().toInt() - kSavedStrokePresetRow;
    if (current < 0 || current >= static_cast<int>(stroke_presets->size())) return;
    stroke_presets->erase(stroke_presets->begin() + current);
    save_stroke_style_presets(*stroke_presets);
    rebuild_dash_list();
    select_dash_row();
    refresh_stroke_rows();
    grow_to_fit();
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
      content.pattern_name = saved_pattern_name(stroke_pattern_combo);
    }
    refresh_stroke_rows();
    grow_to_fit();
    notify({"stroke.content.kind"});
  });
  QObject::connect(stroke_gradient_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    if (stroke_gradient_combo->currentData().toString().isEmpty()) {
      static_cast<GradientDefinition&>(state->settings.stroke.content.gradient) =
          state->custom_stroke_gradient;
      state->stroke_paint_touched = true;
      notify({"stroke.content.gradient.definition"});
      return;
    }
    if (gradient_library == nullptr) {
      return;
    }
    if (const auto* entry =
            gradient_library->find_entry(stroke_gradient_combo->currentData().toString());
        entry != nullptr) {
      static_cast<GradientDefinition&>(state->settings.stroke.content.gradient) =
          resolve_gradient_definition(entry->definition, foreground, background);
      state->stroke_paint_touched = true;
      notify({"stroke.content.gradient.definition"});
    }
  });
  QObject::connect(stroke_gradient_type_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.content.gradient.type =
        static_cast<LayerStyleGradientType>(stroke_gradient_type_combo->currentData().toInt());
    state->stroke_paint_touched = true;
    notify({"stroke.content.gradient.type"});
  });
  QObject::connect(stroke_gradient_angle_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.gradient.angle_degrees = static_cast<float>(value);
    state->stroke_paint_touched = true;
    notify({"stroke.content.gradient.angle_degrees"});
  });
  QObject::connect(stroke_gradient_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.gradient.scale = static_cast<float>(value) / 100.0F;
    state->stroke_paint_touched = true;
    notify({"stroke.content.gradient.scale"});
  });
  QObject::connect(stroke_gradient_reverse_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.content.gradient.reverse = checked;
    state->stroke_paint_touched = true;
    notify({"stroke.content.gradient.reverse"});
  });
  QObject::connect(stroke_pattern_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.content.pattern_id =
        stroke_pattern_combo->currentData().toString().toStdString();
    state->settings.stroke.content.pattern_name = saved_pattern_name(stroke_pattern_combo);
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_id", "stroke.content.pattern_name"});
  });
  QObject::connect(stroke_pattern_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.stroke.content.pattern_scale = static_cast<double>(value) / 100.0;
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_scale"});
  });
  QObject::connect(stroke_pattern_angle_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_angle_degrees = value;
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_angle_degrees"});
  });
  QObject::connect(stroke_pattern_offset_x_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_phase_x = value;
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_phase_x"});
  });
  QObject::connect(stroke_pattern_offset_y_spin, &QDoubleSpinBox::valueChanged, &dialog,
                   [=](double value) {
    state->settings.stroke.content.pattern_phase_y = value;
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_phase_y"});
  });
  QObject::connect(stroke_pattern_align_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.content.pattern_linked = checked;
    state->stroke_paint_touched = true;
    notify({"stroke.content.pattern_linked"});
  });


  if (batch != nullptr) {
    auto* fill_applicability = new QLabel(fill_group);
    auto* stroke_applicability = new QLabel(stroke_group);
    fill_applicability->setObjectName(QStringLiteral("shapeFillPaintApplicability"));
    stroke_applicability->setObjectName(QStringLiteral("shapeStrokePaintApplicability"));
    for (auto* label : {fill_applicability, stroke_applicability}) label->setWordWrap(true);
    fill_layout->addWidget(fill_applicability);
    stroke_layout->addWidget(stroke_applicability);
    auto bindings = std::make_shared<std::vector<std::pair<QWidget*, std::vector<std::string>>>>();
    const auto bind_field = [=](QWidget* widget, std::vector<std::string> keys) {
      bindings->emplace_back(widget, keys);
      // The color buttons launch a picker; opening one is not a color edit.
      if (qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QComboBox*>(widget))
        install_appearance_edit_intent(widget, [=] {
          // Re-read the displayed value: imported values may have more
          // precision than this control, even when its text did not change.
          if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget))
            QMetaObject::invokeMethod(spin, "valueChanged", Qt::DirectConnection, Q_ARG(double, spin->value()));
          else if (auto* integer_spin = qobject_cast<QSpinBox*>(widget))
            QMetaObject::invokeMethod(integer_spin, "valueChanged", Qt::DirectConnection, Q_ARG(int, integer_spin->value()));
          else notify(keys);
        });
    };
    bind_field(layer_opacity_spin, {"layer_opacity"});
    bind_field(layer_fill_opacity_spin, {"fill_opacity"});
    bind_field(feather_spin, {"feather"});
    bind_field(density_spin, {"density"});
    bind_field(stroke_opacity_spin, {"stroke.opacity"});
    bind_field(fill_kind_combo, {"fill.kind"});
    bind_field(fill_gradient_combo, {"fill.gradient.definition"});
    bind_field(gradient_type_combo, {"fill.gradient.type"});
    bind_field(gradient_angle_spin, {"fill.gradient.angle_degrees"});
    bind_field(gradient_scale_spin, {"fill.gradient.scale"});
    bind_field(gradient_reverse_check, {"fill.gradient.reverse"});
    bind_field(fill_pattern_combo, {"fill.pattern_id", "fill.pattern_name"});
    bind_field(pattern_scale_spin, {"fill.pattern_scale"});
    bind_field(pattern_angle_spin, {"fill.pattern_angle_degrees"});
    bind_field(pattern_offset_x_spin, {"fill.pattern_phase_x"});
    bind_field(pattern_offset_y_spin, {"fill.pattern_phase_y"});
    bind_field(pattern_align_check, {"fill.pattern_linked"});
    bind_field(stroke_width_spin, {"stroke.enabled", "stroke.width"});
    bind_field(stroke_align_combo, {"stroke.alignment"});
    bind_field(stroke_cap_combo, {"stroke.cap"});
    bind_field(stroke_join_combo, {"stroke.join"});
    bind_field(stroke_dash_combo, {"stroke.dashes"});
    bind_field(stroke_dash_pattern_edit, {"stroke.dashes"});
    bind_field(stroke_dash_offset_spin, {"stroke.dash_offset"});
    bind_field(stroke_paint_combo, {"stroke.content.kind"});
    bind_field(stroke_gradient_combo, {"stroke.content.gradient.definition"});
    bind_field(stroke_gradient_type_combo, {"stroke.content.gradient.type"});
    bind_field(stroke_gradient_angle_spin, {"stroke.content.gradient.angle_degrees"});
    bind_field(stroke_gradient_scale_spin, {"stroke.content.gradient.scale"});
    bind_field(stroke_gradient_reverse_check, {"stroke.content.gradient.reverse"});
    bind_field(stroke_pattern_combo, {"stroke.content.pattern_id", "stroke.content.pattern_name"});
    bind_field(stroke_pattern_scale_spin, {"stroke.content.pattern_scale"});
    bind_field(stroke_pattern_angle_spin, {"stroke.content.pattern_angle_degrees"});
    bind_field(stroke_pattern_offset_x_spin, {"stroke.content.pattern_phase_x"});
    bind_field(stroke_pattern_offset_y_spin, {"stroke.content.pattern_phase_y"});
    bind_field(stroke_pattern_align_check, {"stroke.content.pattern_linked"});
    bind_field(fill_color_button, {"fill.color"});
    bind_field(stroke_color_button, {"stroke.content.color"});
    const std::array<const char*, 4> names{
        "shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
        "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin"};
    for (std::size_t corner = 0; corner < names.size(); ++corner) {
      if (auto* spin = dialog.findChild<QDoubleSpinBox*>(QLatin1String(names[corner]))) {
        const auto key = "radius." + std::to_string(corner);
        bindings->emplace_back(spin, std::vector<std::string>{key});
        install_appearance_edit_intent(spin, [=, &dialog] {
          auto* link = dialog.findChild<QToolButton*>(QStringLiteral("shapeGeometryRadiusLinkButton"));
          if (link && link->isChecked()) {
            state->settings.geometry->corner_radii.fill(spin->value());
            if (spin->value() > 0) state->settings.geometry->kind = LiveShapeKind::RoundedRectangle;
            for (const char* name : {"shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
                                     "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin"}) {
              if (auto* other = dialog.findChild<QDoubleSpinBox*>(QLatin1String(name))) {
                const QSignalBlocker blocker(other);
                other->setValue(spin->value());
              }
            }
            notify({"radius.0", "radius.1", "radius.2", "radius.3"});
          }
          else {
            state->settings.geometry->corner_radii[corner] = spin->value();
            if (spin->value() > 0) state->settings.geometry->kind = LiveShapeKind::RoundedRectangle;
            notify({key});
          }
        });
      }
    }
    // The Weight, Arrowheads and Polygon rows (GitHub #622 follow-up, #643): a deliberate
    // commit of an unchanged value still applies it to every line or polygon.
    for (const auto& [name, key] : {std::pair{"shapeGeometryLineWeightSpin", "line.weight"},
                                    std::pair{"shapeGeometryLineArrowStartCheck", "line.arrow_start"},
                                    std::pair{"shapeGeometryLineArrowEndCheck", "line.arrow_end"},
                                    std::pair{"shapeArrowWidthSpin", "line.arrow_width"},
                                    std::pair{"shapeArrowLengthSpin", "line.arrow_length"},
                                    std::pair{"shapeArrowConcavitySpin", "line.arrow_concavity"},
                                    std::pair{"shapePolygonSidesSpin", "polygon.sides"},
                                    std::pair{"shapePolygonStarInsetSpin", "polygon.star_inset"}}) {
      auto* widget = dialog.findChild<QWidget*>(QLatin1String(name));
      if (widget == nullptr) continue;
      bindings->emplace_back(widget, std::vector<std::string>{key});
      if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget))
        install_appearance_edit_intent(spin, [spin] {
          QMetaObject::invokeMethod(spin, "valueChanged", Qt::DirectConnection, Q_ARG(double, spin->value()));
        });
      else if (auto* integer_spin = qobject_cast<QSpinBox*>(widget))
        install_appearance_edit_intent(integer_spin, [integer_spin] {
          QMetaObject::invokeMethod(integer_spin, "valueChanged", Qt::DirectConnection, Q_ARG(int, integer_spin->value()));
        });
    }
    if (multiple) {
      for (const char* name : {"shapeGeometryXSpin", "shapeGeometryYSpin", "shapeGeometryWidthSpin",
                               "shapeGeometryHeightSpin", "shapeGeometryLinkButton"})
        if (auto* widget = dialog.findChild<QWidget*>(QLatin1String(name))) {
          widget->setEnabled(false);
          if (const auto row = field_rows.find(widget); row != field_rows.end()) row->second->setEnabled(false);
        }
      const auto count = std::count_if(batch->originals.begin(), batch->originals.end(),
                                       appearance_has_editable_radii);
      if (count > 0) {
        const auto first = std::find_if(batch->originals.begin(), batch->originals.end(), appearance_has_editable_radii);
        const auto index = static_cast<std::size_t>(std::distance(batch->originals.begin(), first));
        auto* hint = new QLabel(QObject::tr("Editable rectangles for corner radii: %n", nullptr, static_cast<int>(count)) +
            QStringLiteral("\n") + QObject::tr("Values from: %1").arg(batch->names[index]), left_widget);
        hint->setObjectName(QStringLiteral("shapeRadiusSelectionSummary"));
        hint->setWordWrap(true);
        if (radius_layout != nullptr) radius_layout->insertWidget(0, hint);
      }
    }
    *refresh_mixed = [=] {
      std::vector<ShapeAppearanceSettings> current;
      for (const auto& original : *baselines) current.push_back(edits->applied(original));
      const auto describe_paint_targets = [&](QLabel* label, bool stroke) {
        const auto kind = stroke ? state->settings.stroke.content.kind : state->settings.fill.kind;
        const auto count = std::count_if(current.begin(), current.end(), [&](const auto& value) {
          return (stroke ? value.stroke.content.kind : value.fill.kind) == kind;
        });
        const bool specific = kind == VectorFillKind::Gradient || kind == VectorFillKind::Pattern;
        label->setVisible(specific && count != static_cast<std::ptrdiff_t>(current.size()));
        if (specific) label->setText((kind == VectorFillKind::Gradient
            ? QObject::tr("Gradient settings apply to %1 of %2 editable layers.")
            : QObject::tr("Pattern settings apply to %1 of %2 editable layers."))
                .arg(count).arg(current.size()));
      };
      describe_paint_targets(fill_applicability, false);
      describe_paint_targets(stroke_applicability, true);
      for (const auto& [widget, keys] : *bindings) {
        bool mixed = false;
        for (const auto& property : properties) {
          if (std::find(keys.begin(), keys.end(), property.key) == keys.end()) continue;
          if (property.key.starts_with("radius.") || property.key.starts_with("line.") ||
              property.key.starts_with("polygon.")) {
            // Compared only among the layers the field edits (rectangles, lines, polygons).
            const auto editable = property.key.starts_with("line.")      ? &appearance_has_editable_line
                                  : property.key.starts_with("polygon.") ? &appearance_has_editable_polygon
                                                                         : &appearance_has_editable_radii;
            const auto reference = std::find_if(current.begin(), current.end(), editable);
            if (reference != current.end()) for (const auto& value : current)
              if (editable(value)) mixed = mixed || !property.equal(*reference, value);
          } else for (const auto& value : current)
            mixed = mixed || !property.equal(current.front(), value);
        }
        set_appearance_mixed(widget, mixed);
      }
    };
    (*refresh_mixed)();
    state->settings.edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>(*edits);
  }

  // The Fill-only dialog is that fill layer's settings (GitHub issue 580 follow-up), with one
  // fixed title for every fill type and layer name (GitHub #582 follow-up).
  if (fill_layer_only) {
    window.setProperty("fillLayerSettings", true);
    window.setWindowTitle(QObject::tr("Fill Layer Settings"));
  } else if (live) {
    window.setProperty("fillLayerSettings", false);  // the window's last page may have set it
  }

  // Value spins commit on Enter/arrows/focus-out only: per-keystroke preview
  // renders made typing "10" into the pattern scale render at "1" first (a
  // 10000-texel-per-pixel minification).
  for (auto* spin : dialog.findChildren<QDoubleSpinBox*>()) {
    spin->setKeyboardTracking(false);
  }
  for (auto* spin : dialog.findChildren<QSpinBox*>()) {
    spin->setKeyboardTracking(false);
  }

  ShapeAppearanceBody body;
  body.fill_layer_only = fill_layer_only;
  body.fitted_size = [fitted_dialog_size](const QScreen* screen, int chrome_height) {
    return fitted_dialog_size(screen, chrome_height);
  };
  body.settings = [state] { return state->settings; };
  body.finish = [state, notify] {
    if (!state->settings.preview_enabled) {
      state->settings.preview_enabled = true;
      notify();
    }
    return state->settings;
  };
  body.rebase = [state, edits, previous, baselines, refresh_mixed,
                 has_batch = batch != nullptr](std::vector<ShapeAppearanceSettings> current) {
    *baselines = std::move(current);
    edits->operations.clear();
    if (has_batch) state->settings.edits = std::make_shared<AppearanceEdits<ShapeAppearanceSettings>>();
    *previous = state->settings;
    if (*refresh_mixed) (*refresh_mixed)();
  };
  return body;
}

std::optional<ShapeAppearanceSettings> request_shape_appearance_settings(
    QWidget* parent, std::function<void(const ShapeAppearanceSettings&)> preview_changed,
    ShapeAppearanceSettings initial, ShapeAppearanceSettings reset_defaults,
    GradientLibrary* gradient_library,
    PatternLibrary* pattern_library, const PatternStore* document_patterns, RgbColor foreground,
    RgbColor background, const DocumentFieldUnits& units,
    const AppearanceDialogContext<ShapeAppearanceSettings>* batch, const ShapeAppearanceShapeSource& custom_shapes) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("shapeAppearanceDialog"));
  ShapeAppearanceBodyOptions options;
  options.changed = std::move(preview_changed);
  options.initial = std::move(initial);
  options.reset_defaults = std::move(reset_defaults);
  options.gradient_library = gradient_library;
  options.pattern_library = pattern_library;
  options.document_patterns = document_patterns;
  options.foreground = foreground;
  options.background = background;
  options.units = units;
  options.batch = batch;
  options.custom_shapes = custom_shapes;
  const auto body = build_shape_appearance_body(dialog, dialog, std::move(options));

  // Open at the size that shows the visible rows. Resizing here, before
  // remember_dialog_position, keeps the centering and on-screen clamp
  // working from the real size.
  const QScreen* screen = parent != nullptr ? parent->screen() : nullptr;
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  dialog.resize(body.fitted_size(screen, 0));

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return body.finish();
}

}  // namespace patchy::ui
