// Layer > Lock Layers... (GitHub issue 544; docs/layer-shortcuts.md, "Lock Layers dialog").
#include "ui/layer_lock_dialog.hpp"

#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <array>

namespace patchy::ui {

namespace {

// Shows a mixed lock partly checked, but a click only ever turns it on or off: a partly
// checked box goes on, as does an empty one; a checked one goes off.
class LockCheckBox final : public QCheckBox {
public:
  LockCheckBox(const QString& text, QWidget* parent) : QCheckBox(text, parent) { setTristate(true); }

protected:
  void nextCheckState() override { setCheckState(checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked); }
};

LayerLockChoice choice_for(std::span<const LayerLockFlags> flags, LayerLockFlags flag) {
  int set = 0;
  for (const auto layer_flags : flags) {
    if ((layer_flags & flag) != kLayerLockNone) {
      ++set;
    }
  }
  if (set == 0) {
    return LayerLockChoice::Off;
  }
  return set == static_cast<int>(flags.size()) ? LayerLockChoice::On : LayerLockChoice::Mixed;
}

Qt::CheckState check_state_for(LayerLockChoice choice) {
  switch (choice) {
    case LayerLockChoice::On:
      return Qt::Checked;
    case LayerLockChoice::Mixed:
      return Qt::PartiallyChecked;
    case LayerLockChoice::Off:
      break;
  }
  return Qt::Unchecked;
}

LayerLockChoice choice_for(Qt::CheckState state) {
  switch (state) {
    case Qt::Checked:
      return LayerLockChoice::On;
    case Qt::PartiallyChecked:
      return LayerLockChoice::Mixed;
    case Qt::Unchecked:
      break;
  }
  return LayerLockChoice::Off;
}

LayerLockFlags apply_choice(LayerLockFlags flags, LayerLockFlags flag, LayerLockChoice choice) {
  switch (choice) {
    case LayerLockChoice::On:
      return flags | flag;
    case LayerLockChoice::Off:
      return flags & ~flag;
    case LayerLockChoice::Mixed:
      break;
  }
  return flags;
}

}  // namespace

LayerLockChoices layer_lock_choices_for(std::span<const LayerLockFlags> flags) {
  LayerLockChoices choices;
  if (flags.empty()) {
    return choices;
  }
  choices.transparent_pixels = choice_for(flags, kLayerLockTransparentPixels);
  choices.image_pixels = choice_for(flags, kLayerLockImagePixels);
  choices.position = choice_for(flags, kLayerLockPosition);
  choices.auto_nesting = choice_for(flags, kLayerLockArtboardAutonest);
  return choices;
}

LayerLockFlags apply_layer_lock_choices(LayerLockFlags flags, const LayerLockChoices& choices) {
  flags = apply_choice(flags, kLayerLockTransparentPixels, choices.transparent_pixels);
  flags = apply_choice(flags, kLayerLockImagePixels, choices.image_pixels);
  flags = apply_choice(flags, kLayerLockPosition, choices.position);
  return apply_choice(flags, kLayerLockArtboardAutonest, choices.auto_nesting);
}

std::optional<LayerLockChoices> request_layer_locks(QWidget* parent, const LayerLockChoices& initial,
                                                    int layer_count) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("layerLocksDialog"));
  dialog.setWindowTitle(QCoreApplication::translate("LayerLockDialog", "Lock Layers"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* target = new QLabel(QCoreApplication::translate("LayerLockDialog", "Selected layers: %1").arg(layer_count),
                            &dialog);
  target->setObjectName(QStringLiteral("layerLocksTargetLabel"));
  layout->addWidget(target);

  // "All" heads the three editing locks, which sit indented under it.
  auto* editing_box = new QGroupBox(QCoreApplication::translate("LayerLockDialog", "Editing"), &dialog);
  auto* editing = new QVBoxLayout(editing_box);
  auto* all_check = new LockCheckBox(QCoreApplication::translate("LayerLockDialog", "All"), editing_box);
  all_check->setObjectName(QStringLiteral("layerLocksAllCheck"));
  all_check->setToolTip(QCoreApplication::translate("LayerLockDialog", "Lock or unlock all three editing locks"));
  editing->addWidget(all_check);
  struct LockRow {
    const char* object_name;
    const char* text;
    const char* tip;
  };
  const std::array<LockRow, 3> rows{{
      {"layerLocksTransparentCheck", QT_TRANSLATE_NOOP("LayerLockDialog", "Transparent Pixels"),
       QT_TRANSLATE_NOOP("LayerLockDialog", "Paint and fill only where the layer already has pixels")},
      {"layerLocksImageCheck", QT_TRANSLATE_NOOP("LayerLockDialog", "Image Pixels"),
       QT_TRANSLATE_NOOP("LayerLockDialog", "Keep the layer's pixels from being painted or edited")},
      {"layerLocksPositionCheck", QT_TRANSLATE_NOOP("LayerLockDialog", "Position"),
       QT_TRANSLATE_NOOP("LayerLockDialog", "Keep the layer from being moved or transformed")},
  }};
  std::array<LockCheckBox*, 3> lock_checks{};
  const int indent = all_check->sizeHint().height();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    auto* check = new LockCheckBox(QCoreApplication::translate("LayerLockDialog", rows[i].text), editing_box);
    check->setObjectName(QLatin1String(rows[i].object_name));
    check->setToolTip(QCoreApplication::translate("LayerLockDialog", rows[i].tip));
    auto* row = new QHBoxLayout;
    row->addSpacing(indent);
    row->addWidget(check, 1);
    editing->addLayout(row);
    lock_checks[i] = check;
  }
  layout->addWidget(editing_box);

  auto* artboards_box = new QGroupBox(QCoreApplication::translate("LayerLockDialog", "Artboards"), &dialog);
  auto* artboards = new QVBoxLayout(artboards_box);
  auto* nest_check =
      new LockCheckBox(QCoreApplication::translate("LayerLockDialog", "Prevent Auto-Nesting"), artboards_box);
  nest_check->setObjectName(QStringLiteral("layerLocksAutoNestCheck"));
  nest_check->setToolTip(QCoreApplication::translate(
      "LayerLockDialog", "Keep the layer from moving into or out of an artboard when it is dragged"));
  artboards->addWidget(nest_check);
  layout->addWidget(artboards_box);

  lock_checks[0]->setCheckState(check_state_for(initial.transparent_pixels));
  lock_checks[1]->setCheckState(check_state_for(initial.image_pixels));
  lock_checks[2]->setCheckState(check_state_for(initial.position));
  nest_check->setCheckState(check_state_for(initial.auto_nesting));
  // All reads checked when the three are, empty when none is, partly checked otherwise.
  const auto refresh_all = [all_check, lock_checks] {
    int checked = 0;
    int unchecked = 0;
    for (auto* check : lock_checks) {
      checked += check->checkState() == Qt::Checked ? 1 : 0;
      unchecked += check->checkState() == Qt::Unchecked ? 1 : 0;
    }
    all_check->setCheckState(checked == 3 ? Qt::Checked : unchecked == 3 ? Qt::Unchecked : Qt::PartiallyChecked);
  };
  refresh_all();
  for (auto* check : lock_checks) {
    QObject::connect(check, &QCheckBox::clicked, &dialog, refresh_all);
  }
  QObject::connect(all_check, &QCheckBox::clicked, &dialog, [all_check, lock_checks] {
    for (auto* check : lock_checks) {
      check->setCheckState(all_check->checkState());
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  all_check->setFocus();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  LayerLockChoices chosen;
  chosen.transparent_pixels = choice_for(lock_checks[0]->checkState());
  chosen.image_pixels = choice_for(lock_checks[1]->checkState());
  chosen.position = choice_for(lock_checks[2]->checkState());
  chosen.auto_nesting = choice_for(nest_check->checkState());
  return chosen;
}

}  // namespace patchy::ui
