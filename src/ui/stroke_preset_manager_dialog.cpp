#include "ui/stroke_preset_manager_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/theme_palette.hpp"

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
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
#include <cmath>
#include <functional>
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

// A built-in pattern by its Dashes name and lengths, anything else as its lengths.
QString dashes_text(const StrokeStylePreset& preset) {
  if (const auto builtin = stroke_dash_preset_for(preset.dashes, preset.cap); builtin.has_value()) {
    if (preset.dashes.empty()) return stroke_dash_preset_label(*builtin);
    return QStringLiteral("%1 (%2)").arg(stroke_dash_preset_label(*builtin), dash_pattern_text(preset.dashes));
  }
  return QStringLiteral("%1 %2").arg(dash_pattern_text(preset.dashes), QObject::tr("x width"));
}

QString offset_text(const StrokeStylePreset& preset) {
  return QStringLiteral("%1 %2").arg(dash_pattern_text({preset.dash_offset}), QObject::tr("x width"));
}

}  // namespace

QString stroke_style_preset_dash_values(const StrokeStylePreset& preset) {
  if (preset.dashes.empty()) return {};
  QStringList pairs;
  for (std::size_t i = 0; i < preset.dashes.size(); i += 2) {
    std::vector<double> pair{preset.dashes[i]};
    if (i + 1 < preset.dashes.size()) pair.push_back(preset.dashes[i + 1]);
    pairs << dash_pattern_text(pair);
  }
  auto text = pairs.join(QStringLiteral(" | "));
  if (std::abs(preset.dash_offset) > 1e-9) {
    text += QStringLiteral("   ") + QObject::tr("offset %1").arg(dash_pattern_text({preset.dash_offset}));
  }
  return text;
}

QString stroke_preset_copy_name(const QString& name, const std::vector<StrokeStylePreset>& saved) {
  const auto taken = [&](const QString& candidate) {
    return stroke_preset_name_is_builtin(candidate) ||
           std::any_of(saved.begin(), saved.end(),
                       [&](const StrokeStylePreset& preset) { return preset.name == candidate; });
  };
  auto candidate = QObject::tr("%1 copy").arg(name);
  for (int number = 2; taken(candidate); ++number) {
    candidate = QObject::tr("%1 copy %2").arg(name).arg(number);
  }
  return candidate;
}

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

StrokePresetChoice request_stroke_preset_manager(QWidget* parent, const QString& initial_name) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("strokePresetManagerDialog"),
                                 QObject::tr("Stroke Presets"));
  // Rows: the built-ins first (locked), then the saved presets.
  const auto& builtins = stroke_dash_presets_in_menu_order();
  const int builtin_count = static_cast<int>(builtins.size());
  auto presets = load_stroke_style_presets();
  const auto row_count = [&] { return builtin_count + static_cast<int>(presets.size()); };
  const auto preset_at = [&](int row) {
    return row < builtin_count ? builtin_stroke_style_preset(builtins[static_cast<std::size_t>(row)])
                               : presets[static_cast<std::size_t>(row - builtin_count)];
  };

  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("strokePresetManagerTree"));
  tree->setHeaderHidden(true);
  // Name, the dash and gap values, Built-in.
  tree->setColumnCount(3);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  tree->setRootIsDecorated(false);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setIconSize(QSize(56, 20));
  scaffold.add_tree(tree, 380);

  auto* builtin_note = new QLabel(
      QObject::tr("Built-in preset: it can be used, but not renamed, moved or deleted. Duplicate it to "
                  "make a copy you can edit."),
      &dialog);
  builtin_note->setObjectName(QStringLiteral("strokePresetManagerBuiltinNote"));
  builtin_note->setWordWrap(true);
  scaffold.right()->addWidget(builtin_note);
  auto* empty_hint = new QLabel(
      QObject::tr("No saved stroke presets yet. In Shape Appearance, set up the stroke and click Save... "
                  "beside Dashes."),
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
  // Each setting shows as text for a built-in and as an editor for a saved preset.
  const auto add_value_row = [&](const QString& label, const char* object_name, QWidget* editor) {
    auto* cell = new QWidget(&dialog);
    auto* layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* value = new QLabel(cell);
    value->setObjectName(QLatin1String(object_name));
    layout->addWidget(value);
    layout->addWidget(editor, 1);
    form->addRow(label, cell);
    return value;
  };
  const auto make_combo = [&](const char* object_name, std::initializer_list<std::pair<QString, int>> items) {
    auto* combo = new QComboBox(&dialog);
    combo->setObjectName(QLatin1String(object_name));
    for (const auto& [text, value] : items) combo->addItem(text, value);
    return combo;
  };
  auto* align_combo = make_combo("strokePresetManagerAlignCombo",
                                 {{alignment_text(VectorStrokeAlignment::Inside), static_cast<int>(VectorStrokeAlignment::Inside)},
                                  {alignment_text(VectorStrokeAlignment::Center), static_cast<int>(VectorStrokeAlignment::Center)},
                                  {alignment_text(VectorStrokeAlignment::Outside), static_cast<int>(VectorStrokeAlignment::Outside)}});
  auto* cap_combo = make_combo("strokePresetManagerCapsCombo",
                               {{cap_text(VectorStrokeCap::Butt), static_cast<int>(VectorStrokeCap::Butt)},
                                {cap_text(VectorStrokeCap::Round), static_cast<int>(VectorStrokeCap::Round)},
                                {cap_text(VectorStrokeCap::Square), static_cast<int>(VectorStrokeCap::Square)}});
  auto* join_combo = make_combo("strokePresetManagerCornersCombo",
                                {{join_text(VectorStrokeJoin::Miter), static_cast<int>(VectorStrokeJoin::Miter)},
                                 {join_text(VectorStrokeJoin::Round), static_cast<int>(VectorStrokeJoin::Round)},
                                 {join_text(VectorStrokeJoin::Bevel), static_cast<int>(VectorStrokeJoin::Bevel)}});
  // Dash and gap lengths in stroke widths, the Dash pattern field's format; empty is solid.
  auto* dashes_editor = new QWidget(&dialog);
  auto* dashes_layout = new QHBoxLayout(dashes_editor);
  dashes_layout->setContentsMargins(0, 0, 0, 0);
  dashes_layout->setSpacing(6);
  auto* dashes_edit = new QLineEdit(dashes_editor);
  dashes_edit->setObjectName(QStringLiteral("strokePresetManagerDashesEdit"));
  dashes_edit->setPlaceholderText(stroke_dash_preset_label(StrokeDashPreset::Solid));
  dashes_edit->setToolTip(QObject::tr("Dash and gap lengths in stroke widths, separated by spaces, for example "
                                      "4 2 or 6 2 1 2; leave it empty for a solid line"));
  dashes_layout->addWidget(dashes_edit, 1);
  dashes_layout->addWidget(new QLabel(QObject::tr("x width"), dashes_editor));
  auto* offset_editor = new QWidget(&dialog);
  auto* offset_layout = new QHBoxLayout(offset_editor);
  offset_layout->setContentsMargins(0, 0, 0, 0);
  offset_layout->setSpacing(6);
  auto* offset_spin = new QDoubleSpinBox(offset_editor);
  offset_spin->setObjectName(QStringLiteral("strokePresetManagerOffsetSpin"));
  offset_spin->setRange(-1000.0, 1000.0);
  offset_spin->setDecimals(2);
  offset_spin->setSingleStep(0.5);
  offset_spin->setKeyboardTracking(false);
  offset_spin->setToolTip(QObject::tr("Moves the dash pattern along the path, in stroke widths"));
  offset_layout->addWidget(offset_spin);
  offset_layout->addWidget(new QLabel(QObject::tr("x width"), offset_editor));
  offset_layout->addStretch(1);
  auto* align_value = add_value_row(QObject::tr("Align:"), "strokePresetManagerAlignLabel", align_combo);
  auto* cap_value = add_value_row(QObject::tr("Caps:"), "strokePresetManagerCapsLabel", cap_combo);
  auto* join_value = add_value_row(QObject::tr("Corners:"), "strokePresetManagerCornersLabel", join_combo);
  auto* dashes_value = add_value_row(QObject::tr("Dashes:"), "strokePresetManagerDashesLabel", dashes_editor);
  auto* offset_value = add_value_row(QObject::tr("Dash offset:"), "strokePresetManagerOffsetLabel", offset_editor);
  scaffold.right()->addLayout(form);

  auto* up_button = new QPushButton(QObject::tr("Move Up"), &dialog);
  up_button->setObjectName(QStringLiteral("strokePresetManagerUpButton"));
  auto* down_button = new QPushButton(QObject::tr("Move Down"), &dialog);
  down_button->setObjectName(QStringLiteral("strokePresetManagerDownButton"));
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("strokePresetManagerDuplicateButton"));
  duplicate_button->setToolTip(QObject::tr("Copy the selected preset into a new saved preset you can rename and edit"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("strokePresetManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected preset (Del)"));
  scaffold.add_action_row({up_button, down_button, duplicate_button}, {delete_button});

  StrokePresetChoice chosen;
  const auto current_row = [&] {
    auto* item = tree->currentItem();
    return item != nullptr && item->isSelected() ? tree->indexOfTopLevelItem(item) : -1;
  };
  // The saved preset's index for a row, or -1 for a built-in or no row.
  const auto saved_index = [&](int row) {
    return row >= builtin_count && row < row_count() ? row - builtin_count : -1;
  };
  const auto use_selected = [&] {
    const auto row = current_row();
    if (row < 0 || row >= row_count()) return;
    if (row < builtin_count) {
      chosen.builtin = builtins[static_cast<std::size_t>(row)];
    } else {
      chosen.saved = presets[static_cast<std::size_t>(row - builtin_count)];
    }
    dialog.accept();
  };
  auto* use_button = scaffold.add_dialog_buttons(QObject::tr("Use Preset"),
                                                 QStringLiteral("strokePresetManagerUseButton"), use_selected);
  use_button->setToolTip(QObject::tr("Use the preset for new shapes and the selected shape layers"));

  const auto refresh_details = [&] {
    const auto row = current_row();
    const bool any = row >= 0 && row < row_count();
    const auto saved = saved_index(row);
    const bool builtin = any && saved < 0;
    builtin_note->setVisible(builtin);
    empty_hint->setVisible(presets.empty());
    name_edit->setEnabled(saved >= 0);
    up_button->setEnabled(saved > 0);
    down_button->setEnabled(saved >= 0 && saved + 1 < static_cast<int>(presets.size()));
    delete_button->setEnabled(saved >= 0);
    duplicate_button->setEnabled(any);
    use_button->setEnabled(any);
    QLabel* values[] = {align_value, cap_value, join_value, dashes_value, offset_value};
    QWidget* editors[] = {align_combo, cap_combo, join_combo, dashes_editor, offset_editor};
    for (auto* value : values) value->setVisible(saved < 0);
    for (auto* editor : editors) editor->setVisible(saved >= 0);
    if (!any) {
      name_edit->clear();
      for (auto* value : values) value->clear();
      preview->setPixmap(QPixmap());
      return;
    }
    const auto preset = preset_at(row);
    {
      const QSignalBlocker blocker(name_edit);
      name_edit->setText(preset.name);
    }
    dashes_value->setText(dashes_text(preset));
    if (builtin) {
      // A built-in sets only the dashes and, for most, the caps and corners.
      const auto dash = builtins[static_cast<std::size_t>(row)];
      const auto unchanged = QObject::tr("Unchanged");
      align_value->setText(unchanged);
      cap_value->setText(stroke_dash_preset_cap(dash).has_value() ? cap_text(preset.cap) : unchanged);
      join_value->setText(stroke_dash_preset_join(dash).has_value() ? join_text(preset.join) : unchanged);
      offset_value->setText(unchanged);
    } else {
      const QSignalBlocker align_blocker(align_combo);
      align_combo->setCurrentIndex(align_combo->findData(static_cast<int>(preset.alignment)));
      const QSignalBlocker cap_blocker(cap_combo);
      cap_combo->setCurrentIndex(cap_combo->findData(static_cast<int>(preset.cap)));
      const QSignalBlocker join_blocker(join_combo);
      join_combo->setCurrentIndex(join_combo->findData(static_cast<int>(preset.join)));
      const QSignalBlocker dashes_blocker(dashes_edit);
      dashes_edit->setText(dash_pattern_text(preset.dashes));
      dashes_edit->setModified(false);
      const QSignalBlocker offset_blocker(offset_spin);
      offset_spin->setValue(preset.dash_offset);
      offset_spin->setEnabled(!preset.dashes.empty());
    }
    preview->setPixmap(stroke_style_preset_sample(preset, 220, 96, 9.0));
  };
  const auto reload = [&](int select_row) {
    {
      const QSignalBlocker blocker(tree);
      tree->clear();
      for (int row = 0; row < row_count(); ++row) {
        const auto preset = preset_at(row);
        auto* item = new QTreeWidgetItem(tree, QStringList{preset.name});
        item->setIcon(0, QIcon(stroke_style_preset_sample(preset, 56, 20, 3.0)));
        item->setToolTip(0, stroke_style_preset_summary(preset));
        item->setText(1, stroke_style_preset_dash_values(preset));
        item->setForeground(1, theme().text_secondary);
        if (row < builtin_count) {
          item->setText(2, QObject::tr("Built-in"));
          item->setForeground(2, theme().text_secondary);
        }
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
    const auto saved = saved_index(current_row());
    const auto target = saved + step;
    if (saved < 0 || target < 0 || target >= static_cast<int>(presets.size())) return;
    std::swap(presets[static_cast<std::size_t>(saved)], presets[static_cast<std::size_t>(target)]);
    save_and_reload(builtin_count + target);
  };
  const auto delete_selected = [&] {
    const auto saved = saved_index(current_row());
    if (saved < 0) return;
    if (QMessageBox::question(&dialog, QObject::tr("Delete Stroke Preset"),
                              QObject::tr("Delete stroke preset \"%1\"?")
                                  .arg(presets[static_cast<std::size_t>(saved)].name)) != QMessageBox::Yes) {
      return;
    }
    presets.erase(presets.begin() + saved);
    save_and_reload(presets.empty() ? builtin_count - 1
                                    : builtin_count + std::min(saved, static_cast<int>(presets.size()) - 1));
  };

  // A copy of any row, built-in or saved, becomes a new saved preset, selected and ready to rename.
  const auto duplicate_selected = [&] {
    const auto row = current_row();
    if (row < 0 || row >= row_count()) return;
    auto copy = preset_at(row);
    copy.name = stroke_preset_copy_name(copy.name, presets);
    presets.push_back(std::move(copy));
    save_and_reload(row_count() - 1);
    name_edit->setFocus();
    name_edit->selectAll();
  };
  // Applies one edit to the selected saved preset and saves the list.
  const auto edit_saved = [&](const std::function<void(StrokeStylePreset&)>& change) {
    const auto saved = saved_index(current_row());
    if (saved < 0) return;
    change(presets[static_cast<std::size_t>(saved)]);
    save_and_reload(builtin_count + saved);
  };

  scaffold.connect_selection_changed(refresh_details);
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, duplicate_selected);
  QObject::connect(align_combo, &QComboBox::currentIndexChanged, &dialog, [&] {
    edit_saved([&](StrokeStylePreset& preset) {
      preset.alignment = static_cast<VectorStrokeAlignment>(align_combo->currentData().toInt());
    });
  });
  QObject::connect(cap_combo, &QComboBox::currentIndexChanged, &dialog, [&] {
    edit_saved([&](StrokeStylePreset& preset) {
      preset.cap = static_cast<VectorStrokeCap>(cap_combo->currentData().toInt());
    });
  });
  QObject::connect(join_combo, &QComboBox::currentIndexChanged, &dialog, [&] {
    edit_saved([&](StrokeStylePreset& preset) {
      preset.join = static_cast<VectorStrokeJoin>(join_combo->currentData().toInt());
    });
  });
  QObject::connect(dashes_edit, &QLineEdit::editingFinished, &dialog, [&] {
    // Commits on Enter or focus-out once typed in; text that is not a pattern reverts.
    if (!dashes_edit->isModified()) return;
    dashes_edit->setModified(false);
    const auto text = dashes_edit->text().trimmed();
    if (text.isEmpty()) {
      edit_saved([](StrokeStylePreset& preset) {
        preset.dashes.clear();
        preset.dash_offset = 0.0;
      });
      return;
    }
    const auto parsed = parse_dash_pattern_text(text);
    if (!parsed.has_value()) {
      refresh_details();
      return;
    }
    edit_saved([&](StrokeStylePreset& preset) { preset.dashes = *parsed; });
  });
  QObject::connect(offset_spin, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
    edit_saved([&](StrokeStylePreset& preset) { preset.dash_offset = preset.dashes.empty() ? 0.0 : value; });
  });
  scaffold.add_delete_plumbing(delete_button, delete_selected);
  QObject::connect(up_button, &QPushButton::clicked, &dialog, [&] { move_selected(-1); });
  QObject::connect(down_button, &QPushButton::clicked, &dialog, [&] { move_selected(1); });
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem*, int) { use_selected(); });
  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto saved = saved_index(current_row());
    const auto name = name_edit->text().trimmed();
    if (saved < 0 || name.isEmpty() || name == presets[static_cast<std::size_t>(saved)].name) {
      refresh_details();
      return;
    }
    // Names are how Save... in Shape Appearance finds a preset to replace, so they stay
    // unique, and a built-in's name stays the built-in's.
    const bool taken = stroke_preset_name_is_builtin(name) ||
                       std::any_of(presets.begin(), presets.end(),
                                   [&](const StrokeStylePreset& preset) { return preset.name == name; });
    if (taken) {
      QMessageBox::warning(&dialog, QObject::tr("Stroke Presets"),
                           QObject::tr("A stroke preset named \"%1\" already exists.").arg(name));
      refresh_details();
      return;
    }
    presets[static_cast<std::size_t>(saved)].name = name;
    save_and_reload(builtin_count + saved);
  });

  // The named row, else the first saved preset, else the first built-in.
  int initial_row = presets.empty() ? 0 : builtin_count;
  for (int row = 0; row < row_count(); ++row) {
    if (!initial_name.isEmpty() && preset_at(row).name == initial_name) {
      initial_row = row;
      break;
    }
  }
  reload(initial_row);
  dialog.resize(800, 500);
  exec_dialog(dialog);
  return chosen;
}

}  // namespace patchy::ui
