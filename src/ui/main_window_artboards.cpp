// MainWindow's artboard commands (docs/artboards.md): everything the Artboard tool, the
// artboard labels, the Layer > Artboards menu, the options bar and the Properties panel
// ask for. Each edit is one undo step, and every edit that changes artboard geometry grows
// the canvas until the artboards fit (the view follows, so nothing jumps on screen).

#include "ui/main_window.hpp"

#include "core/artboard.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_raster.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/measurement_units.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/smart_object_render.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace patchy::ui {
namespace {

QRect to_q(const Rect& rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

Rect to_core(QRect rect) {
  return Rect{rect.x(), rect.y(), rect.width(), rect.height()};
}

void collect_names(const std::vector<Layer>& layers, std::set<std::string>& names) {
  for (const auto& layer : layers) {
    names.insert(layer.name());
    collect_names(layer.children(), names);
  }
}

// Shape and vector-mask rasters are baked against the canvas: a moved or re-parented
// tree re-bakes so nothing stays clipped at its old place.
void rebake(Layer& layer, Rect canvas, const PatternStore* patterns) {
  for (auto& child : layer.children()) {
    rebake(child, canvas, patterns);
  }
  update_vector_shape_raster(layer, canvas, patterns);
  update_vector_mask_raster(layer, canvas);
  sync_layer_mask_feather_canvas(layer, canvas);
}

// Filtered Smart Objects in a moved tree: their filters depend on position, so they
// re-render at the new place (the Move tool's rule).
void collect_position_dependent_smart_objects(const Layer& layer, std::vector<LayerId>& ids) {
  if (move_layer_requires_smart_filter_rerender(layer)) {
    ids.push_back(layer.id());
  }
  for (const auto& child : layer.children()) {
    collect_position_dependent_smart_objects(child, ids);
  }
}

Layer* top_level_artboard(Document& document, std::optional<LayerId> id) {
  if (!id.has_value()) {
    return nullptr;
  }
  for (auto& layer : document.layers()) {
    if (layer.id() == *id && layer_is_artboard(std::as_const(layer))) {
      return &layer;
    }
  }
  return nullptr;
}

// Background tokens, shared by the options bar, the Properties panel and the New
// Artboard dialog: tools/artboardBackground persists them, so they are permanent.
QString background_token(ArtboardBackground background) {
  switch (background) {
    case ArtboardBackground::Black:
      return QStringLiteral("black");
    case ArtboardBackground::Transparent:
      return QStringLiteral("transparent");
    case ArtboardBackground::Custom:
      return QStringLiteral("other");
    case ArtboardBackground::White:
      break;
  }
  return QStringLiteral("white");
}

ArtboardBackground background_from_token(const QString& token) {
  if (token == QStringLiteral("black")) {
    return ArtboardBackground::Black;
  }
  if (token == QStringLiteral("transparent")) {
    return ArtboardBackground::Transparent;
  }
  if (token == QStringLiteral("other")) {
    return ArtboardBackground::Custom;
  }
  return ArtboardBackground::White;
}

}  // namespace

void populate_artboard_background_combo(QComboBox* combo) {
  combo->clear();
  combo->addItem(QObject::tr("White"), QStringLiteral("white"));
  combo->addItem(QObject::tr("Black"), QStringLiteral("black"));
  combo->addItem(QObject::tr("Transparent"), QStringLiteral("transparent"));
  combo->addItem(QObject::tr("Other..."), QStringLiteral("other"));
}

void populate_artboard_size_presets(QComboBox* combo) {
  combo->clear();
  combo->addItem(QObject::tr("Custom"), QSize());
  QString group;
  for (const auto& preset : artboard_size_presets()) {
    if (preset.group != group) {
      combo->insertSeparator(combo->count());
      group = preset.group;
    }
    combo->addItem(QObject::tr("%1 (%2 x %3)").arg(preset.name).arg(preset.size.width()).arg(preset.size.height()),
                   preset.size);
  }
}

void MainWindow::show_artboard_tool_hint_for_move_tool() {
  if (current_tool_ != CanvasTool::Move || !has_active_document()) {
    return;
  }
  // The Artboard tool's own hotkey when it has one, else the Move flyout cycle (Shift+V
  // by default); either way the registry's current binding, never a literal.
  QString shortcut;
  for (const char* id : {"tools.artboard", "tools.cycle.move"}) {
    if (const auto* command = hotkey_registry_.find_command(QLatin1String(id)); command != nullptr) {
      shortcut = action_shortcut_text(command->action);
      if (!shortcut.isEmpty()) {
        break;
      }
    }
  }
  statusBar()->showMessage(
      shortcut.isEmpty() ? tr("Switch to the Artboard tool to add, resize or duplicate artboards")
                         : tr("Switch to the Artboard tool (%1) to add, resize or duplicate artboards").arg(shortcut));
}

void MainWindow::handle_artboard_edit(const ArtboardEditRequest& request) {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  switch (request.kind) {
    case ArtboardEditRequest::Kind::Select:
      if (request.artboard_id.has_value()) {
        select_layers_in_layer_list({*request.artboard_id}, *request.artboard_id);
        refresh_layer_controls();
        refresh_options_bar();
        refresh_document_info();
        show_artboard_tool_hint_for_move_tool();
        if (canvas_ != nullptr) {
          canvas_->update();
        }
      }
      return;
    case ArtboardEditRequest::Kind::Rename:
      if (request.artboard_id.has_value()) {
        rename_artboard(*request.artboard_id);
      }
      return;
    case ArtboardEditRequest::Kind::Create: {
      const auto settings = current_artboard_background();
      create_artboard(request.rect, std::string(), settings.first, settings.second, tr("New artboard"));
      return;
    }
    case ArtboardEditRequest::Kind::Move:
      if (request.artboard_id.has_value()) {
        move_artboard(*request.artboard_id, request.delta, request.duplicate, request.nudge);
      }
      return;
    case ArtboardEditRequest::Kind::Resize:
      if (request.artboard_id.has_value()) {
        resize_artboard(*request.artboard_id, request.rect);
      }
      return;
    case ArtboardEditRequest::Kind::AddAdjacent:
      if (request.artboard_id.has_value()) {
        add_adjacent_artboard(*request.artboard_id, request.side);
      }
      return;
  }
}

std::pair<ArtboardBackground, RgbColor> MainWindow::current_artboard_background() const {
  auto settings = app_settings();
  const auto background =
      background_from_token(settings.value(QStringLiteral("tools/artboardBackground"), QStringLiteral("white")).toString());
  const QColor color(settings.value(QStringLiteral("tools/artboardBackgroundColor"), QStringLiteral("#ffffff")).toString());
  return {background, RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                               static_cast<std::uint8_t>(color.blue())}};
}

QSize MainWindow::current_artboard_size() const {
  auto settings = app_settings();
  return QSize(std::max(1, settings.value(QStringLiteral("tools/artboardWidth"), 1080).toInt()),
               std::max(1, settings.value(QStringLiteral("tools/artboardHeight"), 1080).toInt()));
}

void MainWindow::finish_artboard_edit(const QString& status, std::optional<LayerId> select, bool rows_unchanged) {
  auto& doc = document();
  // Every caller pushed its undo step before editing, so a refused edit undoes cleanly.
  if (!artboard_canvas_within_limit(std::as_const(doc))) {
    undo();
    show_status_error(tr("Artboards cannot make the canvas larger than %1 x %1 px").arg(kMaxArtboardCanvasSide));
    return;
  }
  const bool grows = artboard_canvas_would_grow(std::as_const(doc));
  if (grows && refuse_document_geometry_change()) {
    undo();
    return;
  }
  if (!doc.metadata().artboard_settings.has_value() && document_has_artboards(std::as_const(doc))) {
    doc.metadata().artboard_settings.emplace();
  }
  const auto shift = grow_canvas_to_artboards(doc);
  if (select.has_value()) {
    doc.set_active_layer(*select);
  }
  if (canvas_ != nullptr) {
    if (grows) {
      reset_canvas_after_artboard_canvas_change(shift);
    } else {
      canvas_->document_changed();
    }
  }
  if (rows_unchanged) {
    refresh_layer_thumbnails();
  } else {
    refresh_layer_list();
  }
  if (select.has_value()) {
    select_layers_in_layer_list({*select}, *select);
  }
  refresh_layer_controls();
  refresh_options_bar();
  refresh_document_info();
  update_document_action_state();
  statusBar()->showMessage(status);
}

std::optional<LayerId> MainWindow::create_artboard(QRect rect, std::string name, ArtboardBackground background,
                                                   RgbColor color, const QString& undo_label) {
  if (!has_active_document() || rect.width() < 1 || rect.height() < 1) {
    return std::nullopt;
  }
  finish_active_text_editor();
  auto& doc = document();
  if (name.empty()) {
    name = next_artboard_name(std::as_const(doc));
  }
  push_undo_snapshot(undo_label);
  auto artboard = make_artboard_group(doc.allocate_layer_id(), name, to_core(rect), background, color);
  const auto id = artboard.id();
  doc.layers().push_back(std::move(artboard));
  finish_artboard_edit(tr("Added artboard %1").arg(QString::fromStdString(name)), id);
  return id;
}

bool MainWindow::rerender_moved_artboard_smart_objects(LayerId root_id) {
  auto& doc = document();
  const auto* root = std::as_const(doc).find_layer(root_id);
  if (root == nullptr || canvas_ == nullptr) {
    return true;
  }
  std::vector<LayerId> ids;
  collect_position_dependent_smart_objects(*root, ids);
  const auto document_dir = session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer != nullptr &&
        !refresh_smart_object_layer_preview(doc, *layer, canvas_->transform_interpolation(), true, document_dir)) {
      return false;
    }
  }
  return true;
}

bool MainWindow::continues_artboard_field_edit(LayerId id) const {
  const auto& active_session = session();
  // A save in between ends the run, like a nudge run.
  return artboard_field_edit_run_.session_id == active_session.session_id && artboard_field_edit_run_.artboard == id &&
         artboard_field_edit_run_.revision == active_session.revision &&
         active_session.saved_revision != active_session.revision;
}

void MainWindow::move_artboard(LayerId id, QPoint delta, bool duplicate, bool nudge, bool field_edit) {
  auto& doc = document();
  auto* artboard = top_level_artboard(doc, id);
  if (artboard == nullptr || (delta.isNull() && !duplicate)) {
    return;
  }
  finish_active_text_editor();
  // A run of arrow-key nudges on one artboard is one undo step: the first pushes the
  // snapshot, the rest only move while nothing else has happened in between.
  auto& active_session = session();
  // A save in between ends the run: a nudge after it must mark the document modified.
  const bool continues_run = (nudge && !duplicate && artboard_nudge_run_.session_id == active_session.session_id &&
                              artboard_nudge_run_.artboard == id && artboard_nudge_run_.revision == active_session.revision &&
                              active_session.saved_revision != active_session.revision) ||
                             (field_edit && !duplicate && continues_artboard_field_edit(id));
  if (!continues_run) {
    push_undo_snapshot(duplicate ? tr("Duplicate artboard") : nudge ? tr("Nudge artboard") : tr("Move artboard"));
    artboard = top_level_artboard(doc, id);
  }
  const auto canvas = Rect::from_size(doc.width(), doc.height());
  LayerId moved_id = id;
  if (duplicate) {
    auto copy = clone_layer_tree_with_document_ids(doc, *artboard);
    if (!copy.has_value()) {
      undo();
      show_status_error(tr("Smart Filter cache data could not be duplicated safely"));
      return;
    }
    std::set<std::string> names;
    collect_names(doc.layers(), names);
    copy->set_name(next_duplicate_name(artboard->name(), names));
    offset_copied_layer_tree(*copy, delta.x(), delta.y(), doc.width(), doc.height());
    rebake(*copy, canvas, &std::as_const(doc).metadata().patterns);
    moved_id = copy->id();
    const auto index = static_cast<std::ptrdiff_t>(artboard - doc.layers().data());
    doc.layers().insert(doc.layers().begin() + index + 1, std::move(*copy));
  } else {
    offset_copied_layer_tree(*artboard, delta.x(), delta.y(), doc.width(), doc.height());
    rebake(*artboard, canvas, &std::as_const(doc).metadata().patterns);
  }
  if (!rerender_moved_artboard_smart_objects(moved_id)) {
    undo();
    show_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
    artboard_nudge_run_ = ArtboardNudgeRun{};
    artboard_field_edit_run_ = ArtboardNudgeRun{};
    return;
  }
  finish_artboard_edit(duplicate ? tr("Duplicated artboard") : tr("Moved artboard"), moved_id, !duplicate);
  const ArtboardNudgeRun run{active_session.session_id, id, active_session.revision};
  artboard_nudge_run_ = nudge ? run : ArtboardNudgeRun{};
  artboard_field_edit_run_ = field_edit && !duplicate ? run : ArtboardNudgeRun{};
}

void MainWindow::resize_artboard(LayerId id, QRect rect, bool field_edit) {
  auto& doc = document();
  auto* artboard = top_level_artboard(doc, id);
  if (artboard == nullptr || rect.width() < 1 || rect.height() < 1) {
    return;
  }
  auto changed = *std::as_const(*artboard).artboard();
  if (changed.rect == to_core(rect)) {
    return;
  }
  finish_active_text_editor();
  if (!field_edit || !continues_artboard_field_edit(id)) {
    push_undo_snapshot(tr("Resize artboard"));
    artboard = top_level_artboard(doc, id);
  }
  changed.rect = to_core(rect);
  artboard->set_artboard(std::move(changed));
  finish_artboard_edit(tr("Artboard is %1 x %2 px").arg(rect.width()).arg(rect.height()), id, true);
  const auto& active_session = session();
  artboard_field_edit_run_ =
      field_edit ? ArtboardNudgeRun{active_session.session_id, id, active_session.revision} : ArtboardNudgeRun{};
  artboard_nudge_run_ = ArtboardNudgeRun{};
}

void MainWindow::set_artboard_background(LayerId id, ArtboardBackground background, RgbColor color) {
  auto& doc = document();
  auto* artboard = top_level_artboard(doc, id);
  if (artboard == nullptr) {
    return;
  }
  auto changed = *std::as_const(*artboard).artboard();
  changed.background = background;
  changed.color = background == ArtboardBackground::Custom ? color : RgbColor{255, 255, 255};
  if (changed == *std::as_const(*artboard).artboard()) {
    return;
  }
  push_undo_snapshot(tr("Artboard background"));
  artboard = top_level_artboard(doc, id);
  artboard->set_artboard(std::move(changed));
  finish_artboard_edit(tr("Changed the artboard background"), id, true);
}

void MainWindow::add_adjacent_artboard(LayerId id, int side) {
  auto& doc = document();
  const auto* artboard = top_level_artboard(doc, id);
  if (artboard == nullptr) {
    return;
  }
  // The new artboard copies the size and background of the one whose button was clicked
  // (never of whichever artboard was edited last; docs/legal-constraints.md).
  const auto source = *artboard->artboard();
  const auto rect = artboard_rect_next_to(source.rect, side, source.rect.width, source.rect.height);
  create_artboard(to_q(rect), std::string(), source.background, source.color, tr("New artboard"));
}

void MainWindow::rename_artboard(LayerId id) {
  const auto* before = top_level_artboard(document(), id);
  if (before == nullptr) {
    return;
  }
  const auto session_id = session().session_id;
  bool accepted = false;
  const auto name = QInputDialog::getText(this, tr("Rename Artboard"), tr("Name:"), QLineEdit::Normal,
                                          QString::fromStdString(before->name()), &accepted)
                        .trimmed();
  // The dialog runs an event loop: the document may have changed or closed meanwhile.
  if (!accepted || name.isEmpty() || !has_active_document() || session().session_id != session_id) {
    return;
  }
  auto* artboard = top_level_artboard(document(), id);
  if (artboard == nullptr || name.toStdString() == artboard->name()) {
    return;
  }
  push_undo_snapshot(tr("Rename artboard"));
  artboard = top_level_artboard(document(), id);
  artboard->set_name(name.toStdString());
  finish_artboard_edit(tr("Renamed the artboard %1").arg(name), id);
}

void MainWindow::new_artboard() {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  auto& doc = document();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newArtboardDialog"));
  dialog.setWindowTitle(tr("New Artboard"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* name = new QLineEdit(QString::fromStdString(next_artboard_name(std::as_const(doc))), &dialog);
  name->setObjectName(QStringLiteral("newArtboardNameEdit"));
  form->addRow(tr("Name:"), name);
  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("newArtboardPresetCombo"));
  populate_artboard_size_presets(preset);
  form->addRow(tr("Size:"), preset);
  const auto size = current_artboard_size();
  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("newArtboardWidthSpin"));
  width->setRange(1, 30000);
  width->setSuffix(pixel_suffix());
  width->setValue(size.width());
  configure_dialog_spinbox(width);
  form->addRow(tr("Width:"), width);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("newArtboardHeightSpin"));
  height->setRange(1, 30000);
  height->setSuffix(pixel_suffix());
  height->setValue(size.height());
  configure_dialog_spinbox(height);
  form->addRow(tr("Height:"), height);
  auto* background = new QComboBox(&dialog);
  background->setObjectName(QStringLiteral("newArtboardBackgroundCombo"));
  populate_artboard_background_combo(background);
  const auto current = current_artboard_background();
  background->setCurrentIndex(std::max(0, background->findData(background_token(current.first))));
  form->addRow(tr("Background:"), background);
  layout->addLayout(form);
  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [preset, width, height](int index) {
    if (const auto size = preset->itemData(index).toSize(); size.isValid()) {
      width->setValue(size.width());
      height->setValue(size.height());
    }
  });
  auto color = current.second;
  QObject::connect(background, &QComboBox::activated, &dialog, [this, background, &color](int index) {
    if (background->itemData(index).toString() != QStringLiteral("other")) {
      return;
    }
    if (const auto picked = request_patchy_color(this, QColor(color.red, color.green, color.blue), tr("Artboard Background"));
        picked.has_value()) {
      color = RgbColor{static_cast<std::uint8_t>(picked->red()), static_cast<std::uint8_t>(picked->green()),
                       static_cast<std::uint8_t>(picked->blue())};
    }
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("tools/artboardWidth"), width->value());
  settings.setValue(QStringLiteral("tools/artboardHeight"), height->value());
  const auto chosen = background_from_token(background->currentData().toString());
  // An artboard document places the new one beside the others; the first artboard of a
  // plain document starts at the canvas origin.
  const auto rect = to_q(artboard_rect_beside(std::as_const(doc), width->value(), height->value()));
  create_artboard(rect, name->text().trimmed().toStdString(), chosen, color, tr("New artboard"));
}

void MainWindow::artboard_from_group() {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  auto& doc = document();
  const auto active = std::as_const(doc).active_layer_id();
  Layer* group = nullptr;
  for (auto& layer : doc.layers()) {
    if (active.has_value() && layer.id() == *active) {
      group = &layer;
    }
  }
  if (group == nullptr || group->kind() != LayerKind::Group || layer_is_artboard(std::as_const(*group))) {
    show_status_error(tr("Select a top-level group to turn into an artboard"));
    return;
  }
  auto bounds = layer_render_bounds(std::as_const(*group));
  if (bounds.empty()) {
    bounds = Rect::from_size(doc.width(), doc.height());
  }
  push_undo_snapshot(tr("Artboard from group"));
  for (auto& layer : doc.layers()) {
    if (layer.id() == *active) {
      group = &layer;
    }
  }
  if (group->blend_mode() == BlendMode::PassThrough) {
    group->set_blend_mode(BlendMode::Normal);
  }
  LayerArtboard artboard;
  artboard.rect = bounds;
  group->set_artboard(std::move(artboard));
  finish_artboard_edit(tr("Made %1 an artboard").arg(QString::fromStdString(group->name())), group->id());
}

void MainWindow::artboard_from_layers() {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  auto ids = root_drop_layer_ids(std::as_const(doc).layers(), selected_or_active_layer_ids());
  if (ids.empty()) {
    show_status_error(tr("Select the layers to put on the new artboard"));
    return;
  }
  for (const auto id : ids) {
    const auto* layer = std::as_const(doc).find_layer(id);
    if (layer == nullptr || layer_is_artboard(*layer) || artboard_owning_layer(std::as_const(doc), id) != nullptr) {
      show_status_error(tr("Artboard from Layers takes layers that are not on an artboard"));
      return;
    }
  }
  push_undo_snapshot(tr("Artboard from layers"));
  const auto id = put_layers_on_new_artboard(doc, ids);
  if (!id.has_value()) {
    undo();
    show_status_error(tr("Artboard from Layers takes layers that are not on an artboard"));
    return;
  }
  finish_artboard_edit(tr("Put the layers on a new artboard"), *id);
}

void MainWindow::convert_artboard_to_group() {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  auto& doc = document();
  const auto id = canvas_ != nullptr ? canvas_->selected_root_artboard_id() : std::nullopt;
  auto* artboard = top_level_artboard(doc, id);
  if (artboard == nullptr) {
    show_status_error(tr("Select an artboard to turn into a group"));
    return;
  }
  push_undo_snapshot(tr("Convert artboard to group"));
  artboard = top_level_artboard(doc, id);
  artboard->clear_artboard();
  finish_artboard_edit(tr("Converted the artboard to a group"), *id);
}

void MainWindow::reset_canvas_after_artboard_canvas_change(std::optional<std::array<std::int32_t, 2>> shift) {
  // Canvas Size's rules: the selection does not survive a new canvas, and the channel
  // being edited (a layer mask, an alpha channel) stays the target.
  canvas_->clear_selection();
  const auto previous_channel_target = canvas_->layer_edit_target();
  const auto previous_channel_id = canvas_->active_document_channel_id();
  const auto previous_channel_display = canvas_->mask_display_mode();
  canvas_->set_document_after_canvas_growth(
      &document(), shift.has_value() ? QPoint((*shift)[0], (*shift)[1]) : QPoint());
  restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                              previous_channel_display);
  if (shift.has_value()) {
    // Every document coordinate moved: pan so the artwork stays where it was on screen.
    canvas_->shift_view_for_document_offset(QPoint((*shift)[0], (*shift)[1]));
  }
}

void MainWindow::fit_canvas_to_artboards_command() {
  if (!has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  finish_active_text_editor();
  auto& doc = document();
  if (!document_has_artboards(std::as_const(doc))) {
    show_status_error(tr("The document has no artboards"));
    return;
  }
  const auto extent = artboard_document_extent(std::as_const(doc));
  if (!extent.has_value() || *extent == Rect::from_size(doc.width(), doc.height())) {
    statusBar()->showMessage(tr("The canvas already fits the artboards"));
    return;
  }
  if (extent->width > kMaxArtboardCanvasSide || extent->height > kMaxArtboardCanvasSide) {
    show_status_error(tr("Artboards cannot make the canvas larger than %1 x %1 px").arg(kMaxArtboardCanvasSide));
    return;
  }
  if (refuse_document_geometry_change()) {
    return;
  }
  push_undo_snapshot(tr("Fit canvas to artboards"));
  const auto shift = fit_canvas_to_artboards(document());
  if (canvas_ != nullptr) {
    reset_canvas_after_artboard_canvas_change(shift);
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Canvas is %1 x %2 px").arg(doc.width()).arg(doc.height()));
}

void MainWindow::place_duplicated_artboards(const std::vector<LayerId>& copy_ids_top_to_bottom) {
  auto& doc = document();
  bool moved = false;
  for (auto it = copy_ids_top_to_bottom.rbegin(); it != copy_ids_top_to_bottom.rend(); ++it) {
    auto* copy = top_level_artboard(doc, *it);
    if (copy == nullptr) {
      continue;
    }
    // Beside the rightmost artboard, as Photoshop places a duplicated artboard. The copy
    // still sits on its source, so it must not count as the rightmost itself.
    const auto artboard = *std::as_const(*copy).artboard();
    copy->clear_artboard();
    const auto target = artboard_rect_beside(std::as_const(doc), artboard.rect.width, artboard.rect.height);
    copy->set_artboard(artboard);
    offset_copied_layer_tree(*copy, target.x - artboard.rect.x, target.y - artboard.rect.y, doc.width(), doc.height());
    rebake(*copy, Rect::from_size(doc.width(), doc.height()), &std::as_const(doc).metadata().patterns);
    if (!rerender_moved_artboard_smart_objects(*it)) {
      undo();
      show_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
      return;
    }
    moved = true;
  }
  if (moved) {
    finish_artboard_edit(tr("Duplicated artboard"), copy_ids_top_to_bottom.front());
  }
}

void MainWindow::refresh_after_artboard_nesting() {
  // The Move tool already re-parented the layers inside its own undo step and patched the
  // canvas (docs/artboards.md, auto-nesting): only the panels follow.
  if (!has_active_document()) {
    return;
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
}

void MainWindow::sync_artboard_controls() {
  const auto* doc = has_active_document() ? &std::as_const(document()) : nullptr;
  std::optional<LayerId> selected;
  if (doc != nullptr && canvas_ != nullptr) {
    if (const auto active = doc->active_layer_id(); active.has_value()) {
      if (const auto* owner = artboard_owning_layer(*doc, *active); owner != nullptr) {
        selected = owner->id();
      }
    }
  }
  const auto* artboard = selected.has_value() ? doc->find_layer(*selected) : nullptr;
  // W/H mirror the active layer's artboard, which a paint press can make the last-edited
  // one. Display only: new artboards never take their size from these fields
  // (add_artboard_beside; docs/legal-constraints.md, "Artboards").
  const auto size = artboard != nullptr ? QSize(artboard->artboard()->rect.width, artboard->artboard()->rect.height)
                                        : current_artboard_size();
  if (canvas_ != nullptr) {
    canvas_->set_artboard_default_size(current_artboard_size());
  }
  const auto background = artboard != nullptr ? artboard->artboard()->background : current_artboard_background().first;
  for (auto* spin : {artboard_width_spin_, artboard_height_spin_}) {
    if (spin != nullptr && !spin_box_scrub_active(spin)) {
      const QSignalBlocker blocker(spin);
      spin->setValue(spin == artboard_width_spin_ ? size.width() : size.height());
    }
  }
  if (artboard_preset_combo_ != nullptr) {
    const QSignalBlocker blocker(artboard_preset_combo_);
    int match = 0;
    for (int row = 1; row < artboard_preset_combo_->count(); ++row) {
      if (artboard_preset_combo_->itemData(row).toSize() == size) {
        match = row;
        break;
      }
    }
    artboard_preset_combo_->setCurrentIndex(match);
  }
  if (artboard_background_combo_ != nullptr) {
    const QSignalBlocker blocker(artboard_background_combo_);
    artboard_background_combo_->setCurrentIndex(
        std::max(0, artboard_background_combo_->findData(background_token(background))));
  }
  // The Properties panel's artboard section: only while an artboard itself is selected.
  const auto root = canvas_ != nullptr ? canvas_->selected_root_artboard_id() : std::nullopt;
  const auto* root_artboard = root.has_value() && doc != nullptr ? doc->find_layer(*root) : nullptr;
  if (properties_artboard_panel_ != nullptr) {
    properties_artboard_panel_->setVisible(root_artboard != nullptr);
    if (root_artboard != nullptr) {
      const auto& rect = root_artboard->artboard()->rect;
      for (auto [spin, value] : {std::pair{properties_artboard_x_spin_, rect.x}, std::pair{properties_artboard_y_spin_, rect.y},
                                 std::pair{properties_artboard_width_spin_, rect.width},
                                 std::pair{properties_artboard_height_spin_, rect.height}}) {
        if (spin != nullptr && !spin_box_scrub_active(spin)) {
          const QSignalBlocker blocker(spin);
          spin->setValue(value);
        }
      }
      if (properties_artboard_background_combo_ != nullptr) {
        const QSignalBlocker blocker(properties_artboard_background_combo_);
        properties_artboard_background_combo_->setCurrentIndex(std::max(
            0, properties_artboard_background_combo_->findData(background_token(root_artboard->artboard()->background))));
      }
    }
  }
}

void MainWindow::apply_artboard_size_option(QSize size, bool field_edit) {
  if (size.width() < 1 || size.height() < 1) {
    return;
  }
  const auto* doc = has_active_document() ? &std::as_const(document()) : nullptr;
  std::optional<LayerId> selected;
  if (doc != nullptr) {
    if (const auto active = doc->active_layer_id(); active.has_value()) {
      if (const auto* owner = artboard_owning_layer(*doc, *active); owner != nullptr) {
        selected = owner->id();
      }
    }
  }
  if (selected.has_value()) {
    // Resizes the selected artboard about its top-left corner, content untouched.
    const auto& rect = doc->find_layer(*selected)->artboard()->rect;
    resize_artboard(*selected, QRect(rect.x, rect.y, size.width(), size.height()), field_edit);
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("tools/artboardWidth"), size.width());
  settings.setValue(QStringLiteral("tools/artboardHeight"), size.height());
  sync_artboard_controls();
}

void MainWindow::apply_artboard_background_option(const QString& token, bool from_properties) {
  auto background = background_from_token(token);
  auto color = current_artboard_background().second;
  const auto* doc = has_active_document() ? &std::as_const(document()) : nullptr;
  std::optional<LayerId> selected;
  if (doc != nullptr && canvas_ != nullptr) {
    if (from_properties) {
      selected = canvas_->selected_root_artboard_id();
    }
    if (!selected.has_value()) {
      if (const auto active = doc->active_layer_id(); active.has_value()) {
        if (const auto* owner = artboard_owning_layer(*doc, *active); owner != nullptr) {
          selected = owner->id();
        }
      }
    }
  }
  if (background == ArtboardBackground::Custom) {
    const auto initial = selected.has_value() ? doc->find_layer(*selected)->artboard()->color : color;
    const auto picked =
        request_patchy_color(this, QColor(initial.red, initial.green, initial.blue), tr("Artboard Background"));
    if (!picked.has_value()) {
      sync_artboard_controls();
      return;
    }
    color = RgbColor{static_cast<std::uint8_t>(picked->red()), static_cast<std::uint8_t>(picked->green()),
                     static_cast<std::uint8_t>(picked->blue())};
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("tools/artboardBackground"), background_token(background));
  settings.setValue(QStringLiteral("tools/artboardBackgroundColor"),
                    QColor(color.red, color.green, color.blue).name(QColor::HexRgb));
  if (selected.has_value()) {
    set_artboard_background(*selected, background, color);
  } else {
    sync_artboard_controls();
  }
}

void MainWindow::add_artboard_beside() {
  if (!has_active_document()) {
    return;
  }
  // The size is the saved new-artboard setting, never the options-bar W/H (which mirror the
  // active artboard) nor the active or last-edited artboard (docs/legal-constraints.md).
  const auto size = current_artboard_size();
  const auto background = current_artboard_background();
  const auto rect = to_q(artboard_rect_beside(std::as_const(document()), size.width(), size.height()));
  create_artboard(rect, std::string(), background.first, background.second, tr("New artboard"));
}

void MainWindow::build_properties_artboard_panel(QWidget* parent, QVBoxLayout* layout) {
  // X / Y / W / H and the background of the selected artboard (Photoshop's artboard
  // Properties); shown only while an artboard itself is selected (sync_artboard_controls).
  properties_artboard_panel_ = new QWidget(parent);
  properties_artboard_panel_->setObjectName(QStringLiteral("propertiesArtboardPanel"));
  properties_artboard_panel_->hide();
  auto* grid = new QGridLayout(properties_artboard_panel_);
  grid->setContentsMargins(0, 2, 0, 2);
  grid->setHorizontalSpacing(4);
  grid->setVerticalSpacing(2);
  auto* title = new QLabel(properties_artboard_panel_);
  bind_widget_text(title, QT_TR_NOOP("Artboard"));
  grid->addWidget(title, 0, 0, 1, 4);
  const auto make_spin = [this](const char* name, int minimum, const char* tooltip) {
    auto* spin = new QSpinBox(properties_artboard_panel_);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(minimum, 100000);
    spin->setSuffix(pixel_suffix());
    QPointer<QSpinBox> guarded(spin);
    register_retranslation([guarded] {
      if (guarded != nullptr) {
        guarded->setSuffix(pixel_suffix());
      }
    });
    spin->setKeyboardTracking(false);
    bind_tooltip(spin, tooltip);
    configure_dialog_spinbox(spin, 84);
    connect(spin, &QSpinBox::valueChanged, this, [this, spin](int) {
      if (!spin_box_scrub_active(spin) || !preview_artboard_field_rect(true)) {
        apply_properties_artboard_rect();
      }
    });
    connect(spin, &QAbstractSpinBox::editingFinished, this, [this] {
      commit_artboard_field_preview();
      artboard_field_edit_run_ = ArtboardNudgeRun{};
    });
    return spin;
  };
  const auto add_label = [this, grid](const char* source, int row, int column) {
    auto* label = new QLabel(properties_artboard_panel_);
    bind_widget_text(label, source);
    grid->addWidget(label, row, column);
  };
  add_label(QT_TR_NOOP("X:"), 1, 0);
  properties_artboard_x_spin_ = make_spin("propertiesArtboardXSpin", -100000, QT_TR_NOOP("Left edge of the artboard"));
  grid->addWidget(properties_artboard_x_spin_, 1, 1);
  add_label(QT_TR_NOOP("Y:"), 1, 2);
  properties_artboard_y_spin_ = make_spin("propertiesArtboardYSpin", -100000, QT_TR_NOOP("Top edge of the artboard"));
  grid->addWidget(properties_artboard_y_spin_, 1, 3);
  add_label(QT_TR_NOOP("W:"), 2, 0);
  properties_artboard_width_spin_ = make_spin("propertiesArtboardWidthSpin", 1, QT_TR_NOOP("Width of the artboard"));
  grid->addWidget(properties_artboard_width_spin_, 2, 1);
  add_label(QT_TR_NOOP("H:"), 2, 2);
  properties_artboard_height_spin_ = make_spin("propertiesArtboardHeightSpin", 1, QT_TR_NOOP("Height of the artboard"));
  grid->addWidget(properties_artboard_height_spin_, 2, 3);
  add_label(QT_TR_NOOP("Background:"), 3, 0);
  properties_artboard_background_combo_ = new QComboBox(properties_artboard_panel_);
  properties_artboard_background_combo_->setObjectName(QStringLiteral("propertiesArtboardBackgroundCombo"));
  populate_artboard_background_combo(properties_artboard_background_combo_);
  QPointer<QComboBox> backgrounds(properties_artboard_background_combo_);
  register_retranslation([backgrounds] {
    if (backgrounds != nullptr) {
      const QSignalBlocker blocker(backgrounds);
      const auto index = backgrounds->currentIndex();
      populate_artboard_background_combo(backgrounds);
      backgrounds->setCurrentIndex(index);
    }
  });
  grid->addWidget(properties_artboard_background_combo_, 3, 1, 1, 3);
  connect(properties_artboard_background_combo_, &QComboBox::activated, this, [this](int index) {
    apply_artboard_background_option(properties_artboard_background_combo_->itemData(index).toString(), true);
  });
  grid->setColumnStretch(4, 1);
  layout->addWidget(properties_artboard_panel_);
  install_scrub_labels_in(properties_artboard_panel_);  // drag "X:" ... "H:" to scrub (issue 46)
}

bool MainWindow::preview_artboard_field_rect(bool from_properties) {
  if (!has_active_document() || canvas_ == nullptr) {
    return false;
  }
  const auto& doc = std::as_const(document());
  std::optional<LayerId> id;
  if (from_properties) {
    id = canvas_->selected_root_artboard_id();
  } else if (const auto active = doc.active_layer_id(); active.has_value()) {
    // The options-bar W/H follow the artboard holding the active layer (apply_artboard_size_option).
    if (const auto* owner = artboard_owning_layer(doc, *active); owner != nullptr) {
      id = owner->id();
    }
  }
  const auto* artboard = id.has_value() ? doc.find_layer(*id) : nullptr;
  if (artboard == nullptr || !artboard->artboard().has_value()) {
    return false;
  }
  const auto& rect = artboard->artboard()->rect;
  const auto wanted =
      from_properties
          ? QRect(properties_artboard_x_spin_->value(), properties_artboard_y_spin_->value(),
                  properties_artboard_width_spin_->value(), properties_artboard_height_spin_->value())
          : QRect(rect.x, rect.y, artboard_width_spin_->value(), artboard_height_spin_->value());
  canvas_->preview_artboard_rect(*id, wanted);
  artboard_field_preview_ = from_properties ? ArtboardFieldPreview::Properties : ArtboardFieldPreview::OptionsBar;
  artboard_field_preview_session_ = session().session_id;
  return true;
}

void MainWindow::commit_artboard_field_preview() {
  const auto preview = std::exchange(artboard_field_preview_, ArtboardFieldPreview::None);
  if (preview == ArtboardFieldPreview::None) {
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->end_artboard_rect_preview();
  }
  if (!has_active_document() || session().session_id != artboard_field_preview_session_) {
    // The document changed under the scrub: the preview is dropped and the fields reread.
    sync_artboard_controls();
    return;
  }
  if (preview == ArtboardFieldPreview::Properties) {
    apply_properties_artboard_rect();
  } else {
    apply_artboard_size_option(QSize(artboard_width_spin_->value(), artboard_height_spin_->value()), true);
  }
}

void MainWindow::apply_properties_artboard_rect() {
  if (!has_active_document() || canvas_ == nullptr || properties_artboard_x_spin_ == nullptr) {
    return;
  }
  const auto id = canvas_->selected_root_artboard_id();
  const auto* artboard = id.has_value() ? std::as_const(document()).find_layer(*id) : nullptr;
  if (artboard == nullptr) {
    return;
  }
  const auto& rect = artboard->artboard()->rect;
  const QRect wanted(properties_artboard_x_spin_->value(), properties_artboard_y_spin_->value(),
                     properties_artboard_width_spin_->value(), properties_artboard_height_spin_->value());
  if (wanted.topLeft() != QPoint(rect.x, rect.y) && wanted.size() == QSize(rect.width, rect.height)) {
    // A new position moves the artboard with its content, like a drag.
    move_artboard(*id, wanted.topLeft() - QPoint(rect.x, rect.y), false, false, true);
  } else if (wanted != to_q(rect)) {
    resize_artboard(*id, wanted, true);
  }
}

}  // namespace patchy::ui
