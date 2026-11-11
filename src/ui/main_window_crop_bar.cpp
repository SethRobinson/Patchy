// MainWindow's Crop bar (docs/crop-tool.md): the Preset list that replaced the Style and
// Ratio lists (GitHub issue 696; rows and matching in ui/crop_presets.hpp) and the crop
// option keys taken while a changed crop box is pending (GitHub issues 698 and 699).

#include "ui/main_window.hpp"

#include "ui/canvas_widget.hpp"
#include "ui/crop_presets.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window_shared.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

struct CropComboRows {
  std::vector<CropPresetRow> rows;
  std::vector<int> combo_index;  // rows[i] sits at combo_index[i]
};

CropComboRows crop_combo_rows(const QComboBox& combo) {
  CropComboRows result;
  for (int index = 0; index < combo.count(); ++index) {
    const auto kind = combo.itemData(index, kCropPresetKindRole);
    if (!kind.isValid()) {
      continue;
    }
    CropPresetRow row;
    row.kind = static_cast<CropPresetKind>(kind.toInt());
    row.value = combo.itemData(index, kCropPresetValueRole).toSizeF();
    row.user_id = combo.itemData(index, kCropPresetIdRole).toString();
    result.rows.push_back(std::move(row));
    result.combo_index.push_back(index);
  }
  return result;
}

bool is_saved_crop_preset(CropPresetKind kind) {
  return kind == CropPresetKind::UserRatio || kind == CropPresetKind::UserSize;
}

bool sequence_in(const QList<QKeySequence>& shortcuts, const QKeySequence& key) {
  return std::any_of(shortcuts.begin(), shortcuts.end(), [&key](const QKeySequence& shortcut) {
    return shortcut.matches(key) == QKeySequence::ExactMatch;
  });
}

}  // namespace

void MainWindow::rebuild_crop_preset_combo() {
  auto* combo = crop_preset_combo_;
  if (combo == nullptr) {
    return;
  }
  {
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto& row : crop_preset_rows()) {
      if (row.starts_group) {
        combo->insertSeparator(combo->count());
      }
      combo->addItem(row.text, static_cast<int>(row.kind));
      const int index = combo->count() - 1;
      combo->setItemData(index, row.value, kCropPresetValueRole);
      combo->setItemData(index, row.user_id, kCropPresetIdRole);
    }
  }
  sync_crop_preset_combo();
}

void MainWindow::sync_crop_preset_combo() {
  auto* combo = crop_preset_combo_;
  if (combo == nullptr || combo->count() == 0) {
    return;
  }
  CropPresetState state;
  state.size_style = current_crop_style_ == 1;
  state.ratio = QSizeF(current_crop_ratio_w_, current_crop_ratio_h_);
  if (canvas_ != nullptr) {
    if (const auto rect = canvas_->crop_session_rect(); rect.has_value()) {
      state.box = rect->size();
    }
  }
  if (has_active_document()) {
    state.document = QSize(document().width(), document().height());
  }
  const auto list = crop_combo_rows(*combo);
  const int row = crop_preset_row_for_state(list.rows, state);
  const QSignalBlocker blocker(combo);
  if (row >= 0) {
    combo->setCurrentIndex(list.combo_index[static_cast<std::size_t>(row)]);
  }
  // Save Preset needs values no saved preset holds yet; Delete Preset removes the saved
  // preset the list shows.
  const bool shows_saved = row >= 0 && is_saved_crop_preset(list.rows[static_cast<std::size_t>(row)].kind);
  const bool has_values = state.size_style ? state.box.has_value()
                                           : state.ratio.width() > 0.0 && state.ratio.height() > 0.0;
  if (auto* model = qobject_cast<QStandardItemModel*>(combo->model()); model != nullptr) {
    for (std::size_t i = 0; i < list.rows.size(); ++i) {
      auto* item = model->item(list.combo_index[i]);
      if (item == nullptr) {
        continue;
      }
      if (list.rows[i].kind == CropPresetKind::Save) {
        item->setEnabled(has_values && !shows_saved);
      } else if (list.rows[i].kind == CropPresetKind::Delete) {
        item->setEnabled(shows_saved);
      }
    }
  }
}

void MainWindow::set_crop_style_and_ratio(int style, double ratio_width, double ratio_height) {
  current_crop_style_ = std::clamp(style, 0, 1);
  current_crop_ratio_w_ = std::clamp(ratio_width, 0.0, 10000.0);
  current_crop_ratio_h_ = std::clamp(ratio_height, 0.0, 10000.0);
  if (canvas_ != nullptr) {
    // Size lifts the constraint (a custom box keeps its shape, the automatic frame grows
    // back to the canvas); a ratio re-fits the box like a typed one.
    canvas_->set_crop_ratio(effective_crop_ratio_width(), effective_crop_ratio_height());
    canvas_->set_crop_keep_proportions(crop_size_link_active());
  }
  schedule_save_tool_settings();
  refresh_options_bar();  // the style's fields, the ratio values and the list's row
}

void MainWindow::apply_crop_preset_row(int combo_index) {
  auto* combo = crop_preset_combo_;
  if (combo == nullptr || combo_index < 0) {
    return;
  }
  const auto kind_data = combo->itemData(combo_index, kCropPresetKindRole);
  if (!kind_data.isValid()) {
    sync_crop_preset_combo();  // a separator
    return;
  }
  const auto kind = static_cast<CropPresetKind>(kind_data.toInt());
  const auto value = combo->itemData(combo_index, kCropPresetValueRole).toSizeF();
  switch (kind) {
    case CropPresetKind::FreeRatio:
      // The ratio fields with nothing set: what None was in the old Ratio list.
      set_crop_style_and_ratio(0, 0.0, 0.0);
      break;
    case CropPresetKind::FreeSize:
      set_crop_style_and_ratio(1, current_crop_ratio_w_, current_crop_ratio_h_);
      break;
    case CropPresetKind::OriginalRatio: {
      const auto ratio =
          has_active_document() ? crop_original_ratio(QSize(document().width(), document().height())) : QSizeF();
      if (!ratio.isEmpty()) {
        set_crop_style_and_ratio(0, ratio.width(), ratio.height());
      }
      break;
    }
    case CropPresetKind::Ratio:
    case CropPresetKind::UserRatio:
      set_crop_style_and_ratio(0, value.width(), value.height());
      break;
    case CropPresetKind::Size:
    case CropPresetKind::UserSize:
      // The box takes that many pixels about its center. Nothing is resampled: the crop
      // keeps the document's pixels, and a box larger than the canvas extends it.
      set_crop_style_and_ratio(1, current_crop_ratio_w_, current_crop_ratio_h_);
      if (canvas_ != nullptr) {
        canvas_->set_crop_session_size(value.toSize());  // notifies, which re-syncs the bar
      }
      break;
    case CropPresetKind::Save:
    case CropPresetKind::Delete: {
      // Back on the entry the bar matches, then the command, once the popup has closed.
      const bool save = kind == CropPresetKind::Save;
      QTimer::singleShot(0, this, [this, save] {
        auto* presets = crop_preset_combo_;
        if (presets == nullptr) {
          return;
        }
        sync_crop_preset_combo();
        const auto shown = static_cast<CropPresetKind>(presets->currentData(kCropPresetKindRole).toInt());
        if (!save) {
          if (!is_saved_crop_preset(shown)) {
            return;
          }
          const auto name = presets->currentText();
          if (remove_crop_user_preset(presets->currentData(kCropPresetIdRole).toString())) {
            rebuild_crop_preset_combo();
            statusBar()->showMessage(tr("Crop preset deleted: %1").arg(name));
          }
          return;
        }
        const bool size_style = current_crop_style_ == 1;
        QSizeF values(current_crop_ratio_w_, current_crop_ratio_h_);
        if (size_style) {
          const auto rect = canvas_ != nullptr ? canvas_->crop_session_rect() : std::optional<QRect>{};
          if (!rect.has_value()) {
            return;
          }
          values = QSizeF(rect->size());
        }
        if (values.width() <= 0.0 || values.height() <= 0.0 || is_saved_crop_preset(shown)) {
          return;
        }
        QInputDialog prompt(this);
        prompt.setObjectName(QStringLiteral("cropSavePresetDialog"));
        prompt.setWindowTitle(tr("Save Crop Preset"));
        prompt.setLabelText(tr("Preset name:"));
        prompt.setInputMode(QInputDialog::TextInput);
        prompt.setTextValue(size_style ? crop_size_preset_name(values.toSize()) : crop_ratio_preset_name(values));
        if (exec_dialog(prompt) != QDialog::Accepted) {
          return;
        }
        const auto name = prompt.textValue().simplified();
        if (name.isEmpty()) {
          return;
        }
        add_crop_user_preset(name, size_style, values);
        rebuild_crop_preset_combo();
        statusBar()->showMessage(tr("Crop preset saved: %1").arg(name));
      });
      break;
    }
  }
  sync_crop_preset_combo();
}

// O, Shift+O, H and P belong to tools (the Dodge group, its cycle, the Hand, the Pen). A
// crop takes them only while one of those still holds the key: a key the user or the
// shortcut set gave to another command keeps running that command (docs/hotkey-sets.md).
bool MainWindow::crop_option_key_available(const QKeySequence& key) const {
  for (const auto& command : hotkey_registry_.commands()) {
    if (command.action == nullptr || !sequence_in(command.action->shortcuts(), key)) {
      continue;
    }
    if (!sequence_in(command.default_shortcuts, key)) {
      return false;
    }
  }
  return true;
}

bool MainWindow::handle_crop_option_key_event(QObject* watched, QEvent* event) {
  if ((event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) || canvas_ == nullptr ||
      shutting_down_ || canvas_->tool() != CanvasTool::Crop) {
    return false;
  }
  auto* key_event = static_cast<QKeyEvent*>(event);
  const auto modifiers = key_event->modifiers() & ~Qt::KeypadModifier;
  QAction* action = nullptr;
  if (key_event->key() == Qt::Key_O && modifiers == Qt::NoModifier) {
    action = crop_overlay_next_action_;
  } else if (key_event->key() == Qt::Key_O && modifiers == Qt::ShiftModifier) {
    action = crop_overlay_turn_action_;
  } else if (key_event->key() == Qt::Key_H && modifiers == Qt::NoModifier) {
    action = crop_show_cropped_area_action_;
  } else if (key_event->key() == Qt::Key_P && modifiers == Qt::NoModifier) {
    // Move Image Under Crop Box (GitHub issue 699).
    action = crop_move_image_action_;
  }
  // Only a box the user could lose (the X button's rule): with the automatic frame the
  // letters switch tools as on any other tool.
  if (action == nullptr || !canvas_->crop_session_can_reset()) {
    return false;
  }
  // An application filter sees the event once per widget it propagates through: only the
  // first delivery, to the focus widget (or this window when nothing has focus), counts.
  auto* widget = qobject_cast<QWidget*>(watched);
  auto* focus = QApplication::focusWidget();
  if (widget == nullptr || widget != (focus != nullptr ? focus : static_cast<QWidget*>(this)) ||
      !spacebar_canvas_pan_target_in_window(widget) || QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr || spacebar_canvas_pan_blocked_by_text_input(widget)) {
    return false;
  }
  if (!crop_option_key_available(QKeySequence(QKeyCombination(modifiers, Qt::Key(key_event->key()))))) {
    return false;
  }
  if (event->type() == QEvent::ShortcutOverride) {
    // Keeps the tool shortcut from firing; the key press follows.
    key_event->accept();
    return true;
  }
  // Shift+O with an overlay that has nothing to turn is swallowed, never a tool switch.
  if (!key_event->isAutoRepeat() && action->isEnabled()) {
    action->trigger();
  }
  key_event->accept();
  return true;
}

void MainWindow::refresh_crop_key_hints(bool menu_opening) {
  const auto key_text = [this](const QKeySequence& key) {
    return crop_option_key_available(key) ? key.toString(QKeySequence::NativeText) : QString();
  };
  const auto next_key = key_text(QKeySequence(Qt::Key_O));
  if (menu_opening) {
    // The menu's shortcut column: the text after a tab.
    const auto show = [](QAction* action, const QString& text, const QString& key) {
      if (action == nullptr) {
        return;
      }
      const QString shown = key.isEmpty() ? text : text + QLatin1Char('\t') + key;
      if (action->text() != shown) {
        action->setText(shown);
      }
    };
    show(crop_overlay_next_action_, tr("Next Overlay"), next_key);
    show(crop_overlay_turn_action_, tr("Turn Overlay"), key_text(QKeySequence(Qt::SHIFT | Qt::Key_O)));
    show(crop_show_cropped_area_action_, tr("Show Cropped Area"), key_text(QKeySequence(Qt::Key_H)));
    show(crop_move_image_action_, tr("Move Image Under Crop Box"), key_text(QKeySequence(Qt::Key_P)));
  }
  if (crop_overlay_combo_ != nullptr) {
    auto tip = tr("Composition guides drawn inside the crop box");
    if (!next_key.isEmpty()) {
      tip += QLatin1Char('\n') + tr("%1 picks the next overlay once the crop box has been changed").arg(next_key);
    }
    if (crop_overlay_combo_->toolTip() != tip) {
      crop_overlay_combo_->setToolTip(tip);
    }
  }
}

}  // namespace patchy::ui
