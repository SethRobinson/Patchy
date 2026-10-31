#include "ui/stroke_preset_manager_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/theme_palette.hpp"

#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

QString alignment_text(VectorStrokeAlignment alignment) {
  switch (alignment) {
    case VectorStrokeAlignment::Center:
      return QObject::tr("Center");
    case VectorStrokeAlignment::Outside:
      return QObject::tr("Outside");
    case VectorStrokeAlignment::Inside:
      break;
  }
  return QObject::tr("Inside");
}

QString cap_text(VectorStrokeCap cap) {
  switch (cap) {
    case VectorStrokeCap::Round:
      return QObject::tr("Round");
    case VectorStrokeCap::Square:
      return QObject::tr("Square", "stroke cap");
    case VectorStrokeCap::Butt:
      break;
  }
  return QObject::tr("Butt");
}

QString join_text(VectorStrokeJoin join) {
  switch (join) {
    case VectorStrokeJoin::Round:
      return QObject::tr("Round");
    case VectorStrokeJoin::Bevel:
      return QObject::tr("Bevel");
    case VectorStrokeJoin::Miter:
      break;
  }
  return QObject::tr("Miter");
}

// A built-in pattern by its Dashes name, anything else as its lengths.
QString dashes_text(const StrokeStylePreset& preset) {
  if (const auto builtin = stroke_dash_preset_for(preset.dashes, preset.cap); builtin.has_value()) {
    return stroke_dash_preset_label(*builtin);
  }
  return QStringLiteral("%1 %2").arg(dash_pattern_text(preset.dashes), QObject::tr("x width"));
}

QString offset_text(const StrokeStylePreset& preset) {
  return QStringLiteral("%1 %2").arg(dash_pattern_text({preset.dash_offset}), QObject::tr("x width"));
}

}  // namespace

QPixmap stroke_style_preset_sample(const StrokeStylePreset& preset, int width, int height, double line_width) {
  QPixmap pixmap(std::max(1, width), std::max(1, height));
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(theme().text_primary, line_width);
  pen.setCapStyle(preset.cap == VectorStrokeCap::Round    ? Qt::RoundCap
                  : preset.cap == VectorStrokeCap::Square ? Qt::SquareCap
                                                          : Qt::FlatCap);
  pen.setJoinStyle(preset.join == VectorStrokeJoin::Round   ? Qt::RoundJoin
                   : preset.join == VectorStrokeJoin::Bevel ? Qt::BevelJoin
                                                            : Qt::MiterJoin);
  if (!preset.dashes.empty()) {
    // QPen wants positive lengths in pen widths, the model's unit; a 0 dash is a dot.
    QList<qreal> pattern;
    for (const auto length : preset.dashes) pattern << std::max(length, 0.001);
    if (pattern.size() % 2 != 0) pattern << pattern;
    pen.setDashPattern(pattern);
    pen.setDashOffset(preset.dash_offset);
  }
  painter.setPen(pen);
  // A zigzag, so the corners show as well as the ends.
  const auto margin = line_width * 1.5 + 2.0;
  const auto left = margin;
  const auto right = width - margin;
  const auto top = margin;
  const auto bottom = height - margin;
  QPainterPath path(QPointF(left, bottom));
  path.lineTo(left + (right - left) * 0.3, top);
  path.lineTo(left + (right - left) * 0.7, bottom);
  path.lineTo(right, top);
  painter.drawPath(path);
  return pixmap;
}

QString stroke_style_preset_summary(const StrokeStylePreset& preset) {
  const QStringList parts{QStringLiteral("%1 %2").arg(QObject::tr("Align:"), alignment_text(preset.alignment)),
                          QStringLiteral("%1 %2").arg(QObject::tr("Caps:"), cap_text(preset.cap)),
                          QStringLiteral("%1 %2").arg(QObject::tr("Corners:"), join_text(preset.join)),
                          QStringLiteral("%1 %2").arg(QObject::tr("Dashes:"), dashes_text(preset)),
                          QStringLiteral("%1 %2").arg(QObject::tr("Dash offset:"), offset_text(preset))};
  return parts.join(QLatin1Char('\n'));
}

std::optional<StrokeStylePreset> request_stroke_preset_manager(QWidget* parent, const QString& initial_name) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("strokePresetManagerDialog"),
                                 QObject::tr("Stroke Presets"));
  auto presets = load_stroke_style_presets();

  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("strokePresetManagerTree"));
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(false);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setIconSize(QSize(56, 20));
  scaffold.add_tree(tree, 240);

  auto* empty_hint = new QLabel(
      QObject::tr("No saved stroke presets yet. In Shape Appearance, set up the stroke and click Save... "
                  "on the Preset row."),
      &dialog);
  empty_hint->setObjectName(QStringLiteral("strokePresetManagerEmptyHint"));
  empty_hint->setWordWrap(true);
  scaffold.right()->addWidget(empty_hint);

  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("strokePresetManagerPreview"));
  preview->setMinimumSize(240, 110);
  preview->setAlignment(Qt::AlignCenter);
  scaffold.right()->addWidget(preview, 1);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("strokePresetManagerNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  const auto add_value_row = [&](const QString& label, const char* object_name) {
    auto* value = new QLabel(&dialog);
    value->setObjectName(QLatin1String(object_name));
    form->addRow(label, value);
    return value;
  };
  auto* align_value = add_value_row(QObject::tr("Align:"), "strokePresetManagerAlignLabel");
  auto* cap_value = add_value_row(QObject::tr("Caps:"), "strokePresetManagerCapsLabel");
  auto* join_value = add_value_row(QObject::tr("Corners:"), "strokePresetManagerCornersLabel");
  auto* dashes_value = add_value_row(QObject::tr("Dashes:"), "strokePresetManagerDashesLabel");
  auto* offset_value = add_value_row(QObject::tr("Dash offset:"), "strokePresetManagerOffsetLabel");
  scaffold.right()->addLayout(form);

  auto* up_button = new QPushButton(QObject::tr("Move Up"), &dialog);
  up_button->setObjectName(QStringLiteral("strokePresetManagerUpButton"));
  auto* down_button = new QPushButton(QObject::tr("Move Down"), &dialog);
  down_button->setObjectName(QStringLiteral("strokePresetManagerDownButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("strokePresetManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected preset (Del)"));
  scaffold.add_action_row({up_button, down_button}, {delete_button});

  std::optional<StrokeStylePreset> chosen;
  const auto current_row = [&] {
    auto* item = tree->currentItem();
    return item != nullptr && item->isSelected() ? tree->indexOfTopLevelItem(item) : -1;
  };
  const auto use_selected = [&] {
    const auto row = current_row();
    if (row < 0 || row >= static_cast<int>(presets.size())) return;
    chosen = presets[static_cast<std::size_t>(row)];
    dialog.accept();
  };
  auto* use_button = scaffold.add_dialog_buttons(QObject::tr("Use Preset"),
                                                 QStringLiteral("strokePresetManagerUseButton"), use_selected);
  use_button->setToolTip(QObject::tr("Use the preset for new shapes and the selected shape layers"));

  const auto refresh_details = [&] {
    const auto row = current_row();
    const bool single = row >= 0 && row < static_cast<int>(presets.size());
    empty_hint->setVisible(presets.empty());
    name_edit->setEnabled(single);
    up_button->setEnabled(single && row > 0);
    down_button->setEnabled(single && row + 1 < static_cast<int>(presets.size()));
    delete_button->setEnabled(single);
    use_button->setEnabled(single);
    QLabel* values[] = {align_value, cap_value, join_value, dashes_value, offset_value};
    if (!single) {
      name_edit->clear();
      for (auto* value : values) value->clear();
      preview->setPixmap(QPixmap());
      return;
    }
    const auto& preset = presets[static_cast<std::size_t>(row)];
    {
      const QSignalBlocker blocker(name_edit);
      name_edit->setText(preset.name);
    }
    align_value->setText(alignment_text(preset.alignment));
    cap_value->setText(cap_text(preset.cap));
    join_value->setText(join_text(preset.join));
    dashes_value->setText(dashes_text(preset));
    offset_value->setText(offset_text(preset));
    preview->setPixmap(stroke_style_preset_sample(preset, 220, 96, 9.0));
  };
  const auto reload = [&](int select_row) {
    {
      const QSignalBlocker blocker(tree);
      tree->clear();
      for (const auto& preset : presets) {
        auto* item = new QTreeWidgetItem(tree, QStringList{preset.name});
        item->setIcon(0, QIcon(stroke_style_preset_sample(preset, 56, 20, 3.0)));
        item->setToolTip(0, stroke_style_preset_summary(preset));
      }
      if (select_row >= 0 && select_row < tree->topLevelItemCount()) {
        tree->setCurrentItem(tree->topLevelItem(select_row));
      }
    }
    refresh_details();
  };
  const auto save_and_reload = [&](int select_row) {
    save_stroke_style_presets(presets);
    reload(select_row);
  };
  const auto move_selected = [&](int step) {
    const auto row = current_row();
    const auto target = row + step;
    if (row < 0 || target < 0 || target >= static_cast<int>(presets.size())) return;
    std::swap(presets[static_cast<std::size_t>(row)], presets[static_cast<std::size_t>(target)]);
    save_and_reload(target);
  };
  const auto delete_selected = [&] {
    const auto row = current_row();
    if (row < 0 || row >= static_cast<int>(presets.size())) return;
    if (QMessageBox::question(&dialog, QObject::tr("Delete Stroke Preset"),
                              QObject::tr("Delete stroke preset \"%1\"?")
                                  .arg(presets[static_cast<std::size_t>(row)].name)) != QMessageBox::Yes) {
      return;
    }
    presets.erase(presets.begin() + row);
    save_and_reload(std::min(row, static_cast<int>(presets.size()) - 1));
  };

  scaffold.connect_selection_changed(refresh_details);
  scaffold.add_delete_plumbing(delete_button, delete_selected);
  QObject::connect(up_button, &QPushButton::clicked, &dialog, [&] { move_selected(-1); });
  QObject::connect(down_button, &QPushButton::clicked, &dialog, [&] { move_selected(1); });
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem*, int) { use_selected(); });
  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto row = current_row();
    const auto name = name_edit->text().trimmed();
    if (row < 0 || row >= static_cast<int>(presets.size()) || name.isEmpty() ||
        name == presets[static_cast<std::size_t>(row)].name) {
      refresh_details();
      return;
    }
    // Names are how Save... in Shape Appearance finds a preset to replace, so they stay unique.
    const bool taken = std::any_of(presets.begin(), presets.end(),
                                   [&](const StrokeStylePreset& preset) { return preset.name == name; });
    if (taken) {
      QMessageBox::warning(&dialog, QObject::tr("Stroke Presets"),
                           QObject::tr("A stroke preset named \"%1\" already exists.").arg(name));
      refresh_details();
      return;
    }
    presets[static_cast<std::size_t>(row)].name = name;
    save_and_reload(row);
  });

  int initial_row = presets.empty() ? -1 : 0;
  for (std::size_t i = 0; i < presets.size(); ++i) {
    if (presets[i].name == initial_name) initial_row = static_cast<int>(i);
  }
  reload(initial_row);
  dialog.resize(640, 400);
  exec_dialog(dialog);
  return chosen;
}

}  // namespace patchy::ui
