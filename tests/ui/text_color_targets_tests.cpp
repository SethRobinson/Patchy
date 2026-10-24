// Where a color pick lands while text is being edited (GitHub issue 371): the Text Color
// window follows the current selection and text layer, and the Color and Swatches panels
// recolor selected text the way the Foreground Color window does. See docs/text-tool.md.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/palette_presets.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_dock_panel.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

namespace {

QTextEdit* live_editor(patchy::ui::CanvasWidget& canvas) {
  auto* editor = canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  return editor != nullptr && !editor->property("patchy.textEditorFinished").toBool() ? editor : nullptr;
}

// The color of the character after `position` in the session's document.
QColor color_at(QTextEdit& editor, int position) {
  QTextCursor cursor(editor.document());
  cursor.setPosition(position + 1);
  return cursor.charFormat().foreground().color();
}

void select_range(QTextEdit& editor, int start, int end) {
  auto cursor = editor.textCursor();
  cursor.setPosition(start);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
  editor.setTextCursor(cursor);
  QApplication::processEvents();
}

void commit_session(patchy::ui::CanvasWidget& canvas) {
  if (auto* editor = live_editor(canvas); editor != nullptr) {
    send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
    QApplication::processEvents();
    process_events_for(150);
  }
}

// A new text layer holding `text`, committed; returns its id.
std::optional<patchy::LayerId> make_text_layer(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                               QPoint at, const QString& text) {
  MainWindowTestAccess::add_text_at(window, at);
  QApplication::processEvents();
  auto* editor = live_editor(canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return std::nullopt;
  }
  editor->textCursor().insertText(text);  // replaces the selected "Type" placeholder
  QApplication::processEvents();
  commit_session(canvas);
  return MainWindowTestAccess::document(window).active_layer_id();
}

QTextEdit* open_session(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, patchy::LayerId id) {
  MainWindowTestAccess::edit_text_layer(window, id);
  QApplication::processEvents();
  process_events_for(150);
  return live_editor(canvas);
}

bool same_rgb(QColor a, QColor b) { return a.rgb() == b.rgb(); }

std::string stored_text_color(patchy::ui::MainWindow& window, patchy::LayerId id) {
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
  if (layer == nullptr) {
    return {};
  }
  const auto found = layer->metadata().find(patchy::kLayerMetadataTextColor);
  return found != layer->metadata().end() ? found->second : std::string();
}

void trigger_action(patchy::ui::MainWindow& window, const char* object_name) {
  auto* action = window.findChild<QAction*>(QString::fromLatin1(object_name));
  CHECK(action != nullptr);
  if (action != nullptr) {
    action->trigger();
  }
  QApplication::processEvents();
}

struct TextWindow {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
  std::optional<patchy::LayerId> first;
  std::optional<patchy::LayerId> second;
};

// Two text layers, "Alpha Beta" and "Gamma", with the Type tool active.
void build_text_window(TextWindow& setup) {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  show_window(setup.window);
  setup.canvas = require_canvas(setup.window);
  setup.canvas->set_zoom(1.0);
  require_action_by_text(setup.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  setup.first = make_text_layer(setup.window, *setup.canvas, QPoint(30, 40), QStringLiteral("Alpha Beta"));
  setup.second = make_text_layer(setup.window, *setup.canvas, QPoint(30, 200), QStringLiteral("Gamma"));
  CHECK(setup.first.has_value() && setup.second.has_value() && *setup.first != *setup.second);
}

void ui_text_color_window_follows_the_selection_and_layer() {
  TextWindow setup;
  build_text_window(setup);
  if (!setup.first.has_value() || !setup.second.has_value()) {
    return;
  }
  auto* editor = open_session(setup.window, *setup.canvas, *setup.first);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  select_range(*editor, 0, 5);  // Alpha
  auto* swatch = setup.window.findChild<QToolButton*>(QStringLiteral("textColorButton"));
  CHECK(swatch != nullptr);
  if (swatch == nullptr) {
    return;
  }
  swatch->click();
  QApplication::processEvents();
  auto* panel = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(panel != nullptr);
  auto* picker =
      panel != nullptr ? panel->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"))
                       : nullptr;
  CHECK(picker != nullptr);
  if (picker == nullptr) {
    return;
  }

  save_widget_artifact("tct_text_color_window", *panel);
  // This window's button reads OK: picks still apply live.
  auto* buttons = panel->findChild<QDialogButtonBox*>();
  CHECK(buttons != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr &&
        buttons->button(QDialogButtonBox::Close) == nullptr);

  const QColor red(220, 20, 30);
  picker->setCurrentColor(red);
  QApplication::processEvents();
  CHECK(same_rgb(color_at(*editor, 1), red));

  // Another word, the window still open: the pick goes to it.
  const QColor blue(20, 40, 210);
  select_range(*editor, 6, 10);  // Beta
  picker->setCurrentColor(blue);
  QApplication::processEvents();
  std::printf("  Alpha %s, Beta %s\n", color_at(*editor, 1).name().toStdString().c_str(),
              color_at(*editor, 7).name().toStdString().c_str());
  CHECK(same_rgb(color_at(*editor, 1), red));
  CHECK(same_rgb(color_at(*editor, 7), blue));

  // Another text layer, the window still open: the pick goes to the new session.
  auto* second_editor = open_session(setup.window, *setup.canvas, *setup.second);
  CHECK(second_editor != nullptr);
  CHECK(find_top_level_dialog(QStringLiteral("patchyColorDialog")) == panel);
  if (second_editor == nullptr) {
    return;
  }
  const QColor green(30, 170, 60);
  picker->setCurrentColor(green);
  QApplication::processEvents();
  std::printf("  Gamma %s\n", color_at(*second_editor, 1).name().toStdString().c_str());
  CHECK(same_rgb(color_at(*second_editor, 1), green));

  // The session over, a pick does what the Foreground Color window does: the foreground
  // takes it and no text layer changes.
  commit_session(*setup.canvas);
  const auto gamma_before = stored_text_color(setup.window, *setup.second);
  const QColor yellow(230, 200, 20);
  picker->setCurrentColor(yellow);
  QApplication::processEvents();
  process_events_for(300);  // past the no-session apply's settle delay
  CHECK(same_rgb(setup.canvas->primary_color(), yellow));
  CHECK(!gamma_before.empty() && stored_text_color(setup.window, *setup.second) == gamma_before);
  panel->close();
  QApplication::processEvents();
}

void ui_color_and_swatches_panels_recolor_selected_text() {
  TextWindow setup;
  build_text_window(setup);
  if (!setup.first.has_value()) {
    return;
  }
  MainWindowTestAccess::show_panel(setup.window, QStringLiteral("colorDock"));
  QApplication::processEvents();
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(setup.window);
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  if (preset == nullptr) {
    return;
  }
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 904;
  document.palette_editing() = editing;
  MainWindowTestAccess::refresh_document_info(setup.window);
  QApplication::processEvents();

  auto* editor = open_session(setup.window, *setup.canvas, *setup.first);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->setFocus(Qt::OtherFocusReason);
  select_range(*editor, 0, 5);  // Alpha

  // The Color panel: a click in its field, routed the way the window system routes it.
  auto* panel = setup.window.findChild<patchy::ui::ColorDockPanel*>(QStringLiteral("colorPanel"));
  CHECK(panel != nullptr && panel->field()->isVisible());
  if (panel == nullptr) {
    return;
  }
  click_widget_like_a_user(*panel->field(), QPoint(panel->field()->width() * 3 / 4, panel->field()->height() / 3));
  QApplication::processEvents();
  if (auto* focus = QApplication::focusWidget(); focus != nullptr) {
    std::printf("  focus after the Color panel click: %s '%s'\n", focus->metaObject()->className(),
                focus->objectName().toStdString().c_str());
  }
  editor = live_editor(*setup.canvas);
  CHECK(editor != nullptr);  // the session survives the click
  if (editor == nullptr) {
    return;
  }
  const auto panel_color = setup.canvas->primary_color();
  std::printf("  Color panel pick %s, Alpha %s\n", panel_color.name().toStdString().c_str(),
              color_at(*editor, 1).name().toStdString().c_str());
  CHECK(same_rgb(color_at(*editor, 1), panel_color));
  CHECK(!same_rgb(color_at(*editor, 7), panel_color));

  // The Swatches panel on the next word.
  MainWindowTestAccess::show_panel(setup.window, QStringLiteral("paletteDock"));
  QApplication::processEvents();
  QApplication::processEvents();
  editor = live_editor(*setup.canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  select_range(*editor, 6, 10);  // Beta
  auto* grid = setup.window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr && grid->isVisible());
  if (grid == nullptr) {
    return;
  }
  click_widget_like_a_user(*grid, QPoint(8 * 20 + 9, 9));  // pico8 index 8
  QApplication::processEvents();
  editor = live_editor(*setup.canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  const auto& entry = preset->colors[8];
  const QColor swatch(entry.red, entry.green, entry.blue);
  std::printf("  swatch %s, Beta %s, Alpha %s\n", swatch.name().toStdString().c_str(),
              color_at(*editor, 7).name().toStdString().c_str(), color_at(*editor, 1).name().toStdString().c_str());
  CHECK(same_rgb(color_at(*editor, 7), swatch));
  CHECK(same_rgb(color_at(*editor, 1), panel_color));
  // The keyboard is back in the session: the next key types into the text.
  CHECK(QApplication::focusWidget() == editor);
  commit_session(*setup.canvas);
}

void ui_swap_and_default_colors_recolor_selected_text() {
  TextWindow setup;
  build_text_window(setup);
  if (!setup.first.has_value()) {
    return;
  }
  auto* editor = open_session(setup.window, *setup.canvas, *setup.first);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  setup.canvas->set_primary_color(QColor(10, 10, 10));
  setup.canvas->set_secondary_color(QColor(200, 30, 40));
  select_range(*editor, 0, 5);  // Alpha
  trigger_action(setup.window, "colorSwapAction");
  editor = live_editor(*setup.canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(same_rgb(setup.canvas->primary_color(), QColor(200, 30, 40)));
  CHECK(same_rgb(color_at(*editor, 1), QColor(200, 30, 40)));
  select_range(*editor, 6, 10);  // Beta
  trigger_action(setup.window, "colorDefaultAction");
  CHECK(same_rgb(color_at(*editor, 7), Qt::black));
  CHECK(same_rgb(color_at(*editor, 1), QColor(200, 30, 40)));
  commit_session(*setup.canvas);
}

}  // namespace

std::vector<patchy::test::TestCase> text_color_targets_tests() {
  return {
      {"ui_text_color_window_follows_the_selection_and_layer", ui_text_color_window_follows_the_selection_and_layer},
      {"ui_color_and_swatches_panels_recolor_selected_text", ui_color_and_swatches_panels_recolor_selected_text},
      {"ui_swap_and_default_colors_recolor_selected_text", ui_swap_and_default_colors_recolor_selected_text},
  };
}
