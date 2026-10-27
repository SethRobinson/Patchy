// Type > Resolve Missing Fonts (GitHub issue 169) and the text-session state the layer panel and
// the options bar read about missing fonts (issues 178 and 180). See docs/font-resolution.md,
// "Missing fonts".
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/font_picker.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/missing_fonts_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_font_service.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTextEdit>

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

// Persisted settings keys (never rename): whether an interactive open shows the dialog, and
// whether a Free Transform of type with a missing font asks first (no UI; the UI test suite
// turns it off so fixtures whose fonts a test host lacks do not block on the prompt).
constexpr auto kShowMissingFontsDialogKey = "imports/showMissingFontsDialog";
constexpr auto kWarnMissingFontsOnTransformKey = "text/warnMissingFontsOnTransform";

struct DocumentMissingFont {
  QString family;
  bool no_glyphs{false};
  std::vector<LayerId> layers;
  QString first_text;  // the first layer's text, to ask which installed face draws it
};

// Every family the document's text layers name but cannot draw, in layer-stack order, hidden
// layers and layers inside groups included.
std::vector<DocumentMissingFont> document_missing_fonts(const Document& document) {
  std::vector<DocumentMissingFont> fonts;
  const auto note = [&fonts](const QString& family, bool no_glyphs, const Layer& layer) {
    const auto key = text_font_replacement_key(family);
    auto found = std::ranges::find_if(
        fonts, [&key](const DocumentMissingFont& font) { return text_font_replacement_key(font.family) == key; });
    if (found == fonts.end()) {
      const auto text_value = layer.metadata().find(kLayerMetadataText);
      fonts.push_back(DocumentMissingFont{
          family, no_glyphs, {},
          text_value == layer.metadata().end() ? QString() : QString::fromStdString(text_value->second)});
      found = std::prev(fonts.end());
    }
    if (std::ranges::find(found->layers, layer.id()) == found->layers.end()) {
      found->layers.push_back(layer.id());
    }
  };
  const std::function<void(const std::vector<Layer>&)> visit = [&](const std::vector<Layer>& layers) {
    // Top of the stack first, the order the Layers panel lists them.
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      if (layer_is_text(layer)) {
        const auto problems = text_font_problems_for_layer(layer);
        for (const auto& family : problems.not_installed) {
          note(family, false, layer);
        }
        for (const auto& family : problems.no_glyphs) {
          note(family, true, layer);
        }
      }
      if (!layer.children().empty()) {
        visit(layer.children());
      }
    }
  };
  visit(document.layers());
  return fonts;
}

}  // namespace

std::optional<LayerId> MainWindow::live_text_session_layer_id() const {
  if (canvas_ == nullptr) {
    return std::nullopt;
  }
  const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return std::nullopt;
  }
  const auto id = editor->property("patchy.editingLayerId");
  if (!id.isValid()) {
    return std::nullopt;
  }
  return static_cast<LayerId>(id.toULongLong());
}

QString MainWindow::text_options_bar_family() const {
  if (text_font_combo_ == nullptr) {
    return default_text_family();
  }
  const auto family = text_font_combo_->currentFont().family();
  if (available_text_family_match(family).has_value() || available_text_family_style_match(family).has_value()) {
    return family;
  }
  // Mirroring a layer set in a missing family parks the combo on another row while
  // currentFont() keeps the missing name; new type takes the row the user is looking at.
  const auto shown = text_font_combo_->currentText().trimmed();
  return shown.isEmpty() ? family : shown;
}

bool MainWindow::confirm_missing_font_transform(const Document& document, const std::vector<LayerId>& ids) {
  // Never a modal for automation (CLI, --headless, the MCP connector, any running script but an
  // artist's manual-edit pause): those transforms go ahead exactly as if Continue were pressed.
  const bool script_running = script_engine_host_ != nullptr && script_engine_host_->run_active() &&
                              !script_engine_host_->manual_edit_pause();
  if (shutting_down_ || unattended_automation() || script_running ||
      !app_settings().value(QLatin1String(kWarnMissingFontsOnTransformKey), true).toBool()) {
    return true;
  }
  QString single_layer;
  int text_layers = 0;
  QStringList fonts;
  for (const auto id : ids) {
    const auto* layer = document.find_layer(id);
    if (layer == nullptr || !text_transform_keeps_missing_font_pixels(*layer)) {
      continue;
    }
    const auto missing = missing_text_families_for_layer(*layer);
    ++text_layers;
    single_layer = QString::fromStdString(layer->name());
    for (const auto& family : missing) {
      if (!fonts.contains(family, Qt::CaseInsensitive)) {
        fonts.push_back(family);
      }
    }
  }
  if (text_layers == 0) {
    return true;
  }
  QMessageBox dialog(QMessageBox::Warning, QObject::tr("Missing Font"), QString(), QMessageBox::NoButton, this);
  dialog.setObjectName(QStringLiteral("missingFontTransformMessageBox"));
  const auto font_list = fonts.join(QStringLiteral(", "));
  dialog.setText(text_layers == 1
                     ? tr("The following fonts are missing for text layer \"%1\": %2. The existing pixels will be "
                          "transformed instead of re-rendered. Continue?")
                           .arg(single_layer, font_list)
                     : tr("The following fonts are missing for %n text layer(s): %1. The existing pixels will be "
                          "transformed instead of re-rendered. Continue?",
                          nullptr, text_layers)
                           .arg(font_list));
  auto* continue_button = dialog.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
  dialog.addButton(QMessageBox::Cancel);
  dialog.setDefaultButton(continue_button);
  exec_dialog(dialog);
  return dialog.clickedButton() == continue_button;
}

void MainWindow::show_resolve_missing_fonts_dialog(bool on_open) {
  if (canvas_ == nullptr || !has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  auto settings = app_settings();
  const bool show_on_open = settings.value(QLatin1String(kShowMissingFontsDialogKey), true).toBool();
  if (on_open && (unattended_automation() || !show_on_open)) {
    return;
  }
  if (!on_open) {
    finish_active_text_editor();
  }
  const auto fonts = document_missing_fonts(std::as_const(document()));
  if (fonts.empty()) {
    if (!on_open) {
      statusBar()->showMessage(tr("This document has no missing fonts"));
    }
    return;
  }
  std::vector<MissingFontEntry> entries;
  entries.reserve(fonts.size());
  for (const auto& font : fonts) {
    MissingFontEntry entry{font.family, font.no_glyphs, static_cast<int>(font.layers.size()), {}};
    // The built-in substitute table first (issue 463): metric matches, then look-alikes, then the
    // face Qt draws the text in now.
    if (const auto substitutes = installed_text_font_substitutes(font.family, font.first_text); !substitutes.empty()) {
      entry.suggested = substitutes.front().family;
      entry.suggestion =
          substitutes.front().metric ? MissingFontSuggestion::metric : MissingFontSuggestion::lookalike;
    } else {
      entry.suggested = substituted_text_family(font.family, font.first_text);
      if (entry.suggested.compare(font.family, Qt::CaseInsensitive) == 0) {
        // Nothing installed draws the text; offer the UI font rather than the missing name.
        entry.suggested = default_text_family();
      }
    }
    entries.push_back(entry);
  }
  bool show_on_open_after = show_on_open;
  const auto result = run_missing_fonts_dialog(this, entries, show_on_open, &show_on_open_after);
  if (show_on_open_after != show_on_open) {
    settings.setValue(QLatin1String(kShowMissingFontsDialogKey), show_on_open_after);
  }
  if (!result.has_value() || result->replacements.isEmpty()) {
    return;
  }
  int skipped_locked = 0;
  const auto changed = resolve_missing_fonts(result->replacements, &skipped_locked);
  auto message = tr("Replaced missing fonts in %n text layer(s)", nullptr, changed);
  if (skipped_locked > 0) {
    message += QLatin1Char(' ') + tr("(%n locked layer(s) kept their fonts)", nullptr, skipped_locked);
  }
  statusBar()->showMessage(message);
}

int MainWindow::resolve_missing_fonts(const QHash<QString, QString>& replacements, int* skipped_locked) {
  if (skipped_locked != nullptr) {
    *skipped_locked = 0;
  }
  if (canvas_ == nullptr || !has_active_document() || replacements.isEmpty() || preview_dialog_edit_locked()) {
    return 0;
  }
  finish_active_text_editor();
  std::vector<LayerId> targets;
  for (const auto& font : document_missing_fonts(std::as_const(document()))) {
    if (!replacements.contains(text_font_replacement_key(font.family))) {
      continue;
    }
    for (const auto id : font.layers) {
      if (std::ranges::find(targets, id) == targets.end()) {
        targets.push_back(id);
      }
    }
  }
  // Locked pixels and protected text geometry refuse a session; count them instead.
  std::erase_if(targets, [this, skipped_locked](LayerId id) {
    const auto* layer = std::as_const(document()).find_layer(id);
    const bool refused = layer == nullptr || layer_id_locks_image_pixels(id) || text_geometry_is_protected(*layer);
    if (refused && layer != nullptr && skipped_locked != nullptr) {
      ++*skipped_locked;
    }
    return refused;
  });
  if (targets.empty()) {
    return 0;
  }

  const auto original_active = document().active_layer_id();
  const auto original_selection = selected_layer_ids();
  // One undo step for the whole pass, taken before any layer changes; every commit below skips
  // its own "Type" snapshot (the apply_text_character_edit pattern).
  push_undo_snapshot(tr("Resolve Missing Fonts"));
  text_font_replacements_ = replacements;
  applying_text_options_to_layers_ = true;
  text_commit_snapshot_suppressed_ = true;
  const auto cleanup = qScopeGuard([this, original_active, original_selection] {
    text_font_replacements_.reset();
    applying_text_options_to_layers_ = false;
    text_commit_snapshot_suppressed_ = false;
    if (original_active.has_value() && document().find_layer(*original_active) != nullptr) {
      document().set_active_layer(*original_active);
    }
    if (original_selection.size() > 1U) {
      select_layers_in_layer_list(original_selection, original_active.value_or(original_selection.front()));
    }
    refresh_layer_list();
    refresh_layer_controls();
    sync_text_options_from_active_layer();
  });
  int changed = 0;
  for (const auto id : targets) {
    const auto* layer = std::as_const(document()).find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    const auto bounds = layer->bounds();
    document().set_active_layer(id);
    // A hidden session (no editor shown) re-lays the layer out in the replacement faces and
    // commits through the normal path, so the layer metadata, its raster and the Txt2 object
    // the next save writes all name the new font.
    add_text_at(QPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2), {}, false, std::nullopt, id);
    QPointer<QTextEdit> hidden = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (hidden == nullptr) {
      continue;
    }
    hidden->setObjectName(QString());
    mark_text_editor_changed(hidden);
    const QPoint origin(hidden->property("patchy.documentTextX").toInt(),
                        hidden->property("patchy.documentTextY").toInt());
    commit_text_editor(hidden, origin, id);
    ++changed;
  }
  return changed;
}

}  // namespace patchy::ui
