#include <QCoreApplication>
#include "ui/main_window.hpp"
#include "ui/brush_automation.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/script_engine.hpp"
#include "ui/dialog_utils.hpp"
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFontMetrics>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace patchy::ui {
BrushAutomationLibrary& MainWindow::brush_automation_library() {
  if (!brush_automation_library_) {
    brush_automation_library_ = new BrushAutomationLibrary(brush_tip_library(), this);
    connect(brush_automation_library_, &BrushAutomationLibrary::changed, this, &MainWindow::refresh_automation_brush_presets);
    auto* timer = new QTimer(this); timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
      if ((!script_engine_host_ || !script_engine_host_->run_active()) && (!canvas_ || !canvas_->pointer_gesture_active()))
        brush_automation_library_->refresh();
    });
    timer->start();
  }
  return *brush_automation_library_;
}
void MainWindow::refresh_automation_brush_presets() {
  if (!brush_preset_combo_ || !brush_automation_library_) return;
  const auto selected = brush_preset_combo_->currentData();
  const QSignalBlocker block(brush_preset_combo_);
  brush_preset_combo_->clear();
  for (const auto& value : brush_automation_library_->presets()) {
    const auto p = value.toObject(); brush_preset_combo_->addItem(p["name"].toString(), p["id"].toString());
  }
  brush_preset_combo_->insertSeparator(brush_preset_combo_->count());
  brush_preset_combo_->addItem(tr("Save Current Brush..."), "__saveBrush");
  brush_preset_combo_->addItem(tr("Manage Saved Brushes..."), "__manageBrushes");
  int popup_width = 0;
  const QFontMetrics metrics(brush_preset_combo_->font());
  for (int i=0;i<brush_preset_combo_->count();++i)
    popup_width = std::max(popup_width, metrics.horizontalAdvance(brush_preset_combo_->itemText(i)));
  brush_preset_combo_->view()->setMinimumWidth(popup_width+36);
  const auto index = brush_preset_combo_->findData(selected);
  brush_preset_combo_->setCurrentIndex(index >= 0 ? index : 0);
}
void MainWindow::activate_automation_brush(const ScriptStroke& input) {
  if (!canvas_) brush_input::invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "active document"));
  auto s = input;
  activate_tool(s.mixer ? CanvasTool::MixerBrush : s.erase ? CanvasTool::Eraser : CanvasTool::Brush);
  active_preset_tip_ = s.tip;
  active_automation_preset_id_ = s.preset_id;
  active_automation_brush_ = s;
  active_brush_tip_id_ = s.tip_id;
  if (s.tip_id.isEmpty()) active_brush_tip_id_ = builtin_round_brush_tip_id();
  round_brush_dynamics_ = s.dynamics; round_brush_base_angle_degrees_ = s.angle;
  round_brush_base_roundness_ = s.roundness;
  canvas_->apply_script_brush(s);
  current_mixer_wet_ = s.wet; current_mixer_load_ = s.load; current_mixer_mix_ = s.mix;
  current_mixer_flow_ = s.flow;
  current_brush_smoothing_ = s.smoothing; current_brush_smoothing_pulled_string_ = s.pulled_string;
  current_brush_smoothing_catch_up_ = s.catch_up; current_brush_smoothing_catch_up_end_ = s.catch_up_end;
  current_brush_smoothing_zoom_adjust_ = s.zoom_adjust;
  stash_active_brush_settings(); sync_brush_controls_from_canvas();
  if (brush_dynamics_button_) brush_dynamics_button_->set_round_session(builtin_round_brush_tip_id(), s.dynamics, s.angle, s.roundness);
  if (brush_tip_picker_) brush_tip_picker_->set_current_tip_id(active_brush_tip_id_);
  if (brush_tip_picker_ && s.tip) brush_tip_picker_->set_working_preview(
      s.label.isEmpty() ? tr("Working brush") : s.label, brush_tip_thumbnail(*s.tip, 32));
  if (brush_preset_combo_) {
    const QSignalBlocker block(brush_preset_combo_);
    brush_preset_combo_->setProperty("lastBrushPresetId", s.preset_id);
    brush_preset_combo_->setCurrentIndex(brush_preset_combo_->findData(s.preset_id));
  }
  canvas_->refresh_tool_cursor(); refresh_document_info();
}
void MainWindow::save_current_automation_brush() {
  if (!canvas_) return;
  bool accepted = false;
  const auto name = QInputDialog::getText(this, tr("Save Brush Preset"), tr("Name:"), QLineEdit::Normal, {}, &accepted);
  if (!accepted || name.trimmed().isEmpty()) return;
  try { (void)brush_automation_library().save(name, canvas_->current_script_brush(), false); }
  catch (const std::exception& e) { show_status_error(tr("Could not save brush preset: %1").arg(QString::fromUtf8(e.what()))); }
}
void MainWindow::manage_automation_brush_presets() {
  auto& library = brush_automation_library(); library.refresh();
  QDialog dialog(this); dialog.setObjectName("brushPresetManager"); dialog.setWindowTitle(tr("Saved Brushes"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* hint = new QLabel(tr("Select a saved brush. Update replaces it with the current brush settings. Document Undo does not change saved brushes."), &dialog);
  hint->setWordWrap(true); layout->addWidget(hint);
  auto* list = new QListWidget(&dialog); list->setObjectName("savedBrushList"); layout->addWidget(list);
  auto* row = new QHBoxLayout; layout->addLayout(row);
  auto reload = [&] {
    list->clear(); for (const auto& value : library.presets()) { const auto p = value.toObject();
      if (p["source"] != "user") continue;
      auto* item = new QListWidgetItem(p["name"].toString(), list); item->setData(Qt::UserRole, p["id"]); }
  };
  const auto action = [&](const QString& label, const std::function<void(const QString&)>& fn) {
    auto* button = new QPushButton(label, &dialog); row->addWidget(button);
    connect(button, &QPushButton::clicked, &dialog, [&, fn] {
      if (!list->currentItem()) return;
      try { fn(list->currentItem()->data(Qt::UserRole).toString()); reload(); }
      catch (const std::exception& e) { show_status_error(tr("Brush preset operation failed: %1").arg(QString::fromUtf8(e.what()))); }
    });
  };
  action(tr("Use"), [&](const QString& id) {
    auto s = library.resolve(QJsonObject{{"presetId", id}});
    if (canvas_ && !library.preset(id)["includeColors"].toBool()) { s.color=canvas_->primary_color();s.background=canvas_->secondary_color(); }
    activate_automation_brush(s);
  });
  action(tr("Update"), [&](const QString& id) {
    if (!canvas_) return;
    const auto p=library.preset(id);
    (void)library.save(p["name"].toString(),canvas_->current_script_brush(),p["includeColors"].toBool(),id,p["folder"].toString());
  });
  const auto rename = [&](const QString& id, bool duplicate) {
    const auto p=library.preset(id); bool accepted=false;
    const auto name=QInputDialog::getText(&dialog,duplicate?tr("Duplicate Brush"):tr("Rename Brush"),tr("Name:"),QLineEdit::Normal,p["name"].toString(),&accepted);
    if(accepted && !name.trimmed().isEmpty()) (void)library.save(name,library.resolve(QJsonObject{{"presetId",id}}),p["includeColors"].toBool(),duplicate?QString():id,p["folder"].toString());
  };
  action(tr("Duplicate"),[&](const QString& id){rename(id,true);});
  action(tr("Rename"),[&](const QString& id){rename(id,false);});
  action(tr("Delete"),[&](const QString& id){library.remove(id);});
  auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(close);
  connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  reload();dialog.resize(620,400);run_non_modal_dialog(dialog);
}
}  // namespace patchy::ui
