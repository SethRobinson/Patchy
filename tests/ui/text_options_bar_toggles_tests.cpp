// GitHub issue 673: the Type tool's options bar has Bold, Italic and Underline (the Character
// panel's faux bold, faux italic and underline), a Justify toggle on top of L / C / R, and an
// RTL toggle; its Smoothing list moved to the Character panel and Type > Anti-Alias.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_warp.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

namespace {

struct BarTextLayers {
  patchy::LayerId alpha{0};
  patchy::LayerId beta{0};
};

// Two rendered text layers, no session (the issue 31 no-session path).
BarTextLayers build_bar_text_layers(patchy::ui::MainWindow& window) {
  patchy::Document built(520, 360, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background", solid_pixels(520, 360, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto family = QApplication::font().family().toStdString();
  const auto add = [&](const char* name, QPoint origin) {
    patchy::Layer layer(built.allocate_layer_id(), name,
                        solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
    const auto id = layer.id();
    layer.set_bounds(patchy::Rect{origin.x(), origin.y(), 1, 1});
    layer.metadata()[patchy::kLayerMetadataText] = name;
    layer.metadata()[patchy::kLayerMetadataTextSize] = "30";
    layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
    layer.metadata()[patchy::kLayerMetadataTextFont] = family;
    built.add_layer(std::move(layer));
    return id;
  };
  BarTextLayers ids;
  ids.alpha = add("Alpha", QPoint(40, 40));
  ids.beta = add("Beta", QPoint(40, 160));
  built.set_active_layer(ids.alpha);
  window.add_document_session(std::move(built), QStringLiteral("Issue673Bar"));
  auto& document = MainWindowTestAccess::document(window);
  for (const auto id : {ids.alpha, ids.beta}) {
    CHECK(MainWindowTestAccess::apply_text_warp(window, *document.find_layer(id), patchy::TextWarp{}));
  }
  MainWindowTestAccess::refresh_layer_ui(window);
  return ids;
}

void select_row(patchy::ui::MainWindow& window, const QString& name) {
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  if (list != nullptr) {
    list->setCurrentItem(require_layer_item(*list, name), QItemSelectionModel::ClearAndSelect);
    QApplication::processEvents();
  }
}

std::string layer_metadata(const patchy::Document& document, patchy::LayerId id, const char* key) {
  const auto* layer = document.find_layer(id);
  if (layer == nullptr) {
    return {};
  }
  const auto found = layer->metadata().find(key);
  return found == layer->metadata().end() ? std::string() : found->second;
}

// A column of the first data line of a runs table (patchy.text.runs: 11 faux bold, 13 faux
// italic; patchy.text.paragraph_runs: 2 alignment, 9 direction).
QString first_line_column(const patchy::Document& document, patchy::LayerId id, const char* key, int column) {
  for (const auto& raw_line : QString::fromStdString(layer_metadata(document, id, key)).split(QLatin1Char('\n'))) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('v'))) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    return fields.size() > column ? fields[column] : QString();
  }
  return {};
}

QString alignment_of(const patchy::Document& document, patchy::LayerId id) {
  return first_line_column(document, id, patchy::kLayerMetadataTextParagraphRuns, 2);
}

QString direction_of(const patchy::Document& document, patchy::LayerId id) {
  return first_line_column(document, id, patchy::kLayerMetadataTextParagraphRuns, 9);
}

QPushButton* bar_button(patchy::ui::MainWindow& window, const char* name) {
  auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
  CHECK(button != nullptr);
  return button;
}

// The bar's B / I / U are the Character panel's faux bold, faux italic and underline: one undo
// step each, and the bar and the panel show each other's changes.
void ui_text_options_bar_style_toggles_follow_the_character_panel() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  const auto ids = build_bar_text_layers(window);
  show_window(window);
  require_canvas(window)->set_zoom(1.0);
  auto& document = MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* bold = bar_button(window, "textFauxBoldButton");
  auto* italic = bar_button(window, "textFauxItalicButton");
  auto* underline = bar_button(window, "textUnderlineButton");
  if (bold == nullptr || italic == nullptr || underline == nullptr) {
    return;
  }
  CHECK(bold->isVisible() && italic->isVisible() && underline->isVisible());
  CHECK(bold->focusPolicy() == Qt::NoFocus);
  select_row(window, QStringLiteral("Beta"));
  CHECK(bold->isEnabled() && !bold->isChecked() && !italic->isChecked() && !underline->isChecked());

  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  bold->click();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(first_line_column(document, ids.beta, patchy::kLayerMetadataTextRuns, 11).toInt() != 0);
  CHECK(first_line_column(document, ids.alpha, patchy::kLayerMetadataTextRuns, 11).toInt() == 0);
  CHECK(bold->isChecked());

  bool drove = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* panel_bold = dialog->findChild<QPushButton*>(QStringLiteral("textCharacterFauxBold"));
    auto* panel_italic = dialog->findChild<QPushButton*>(QStringLiteral("textCharacterFauxItalic"));
    auto* panel_underline = dialog->findChild<QPushButton*>(QStringLiteral("textCharacterUnderlineButton"));
    CHECK(panel_bold != nullptr && panel_italic != nullptr && panel_underline != nullptr);
    if (panel_bold != nullptr && panel_italic != nullptr && panel_underline != nullptr) {
      // Bar to panel.
      CHECK(panel_bold->isChecked() && !panel_italic->isChecked() && !panel_underline->isChecked());
      italic->click();
      QApplication::processEvents();
      CHECK(panel_italic->isChecked());
      // Panel to bar.
      panel_underline->click();
      QApplication::processEvents();
      CHECK(underline->isChecked());
      panel_bold->click();
      QApplication::processEvents();
      CHECK(!bold->isChecked());
      drove = true;
    }
    dialog->reject();
  }));
  bar_button(window, "textCharacterButton")->click();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 4);
  CHECK(first_line_column(document, ids.beta, patchy::kLayerMetadataTextRuns, 11).toInt() == 0);
  CHECK(first_line_column(document, ids.beta, patchy::kLayerMetadataTextRuns, 13).toInt() != 0);

  // The bar follows the selection, and undo.
  select_row(window, QStringLiteral("Alpha"));
  CHECK(!bold->isChecked() && !italic->isChecked() && !underline->isChecked());
  select_row(window, QStringLiteral("Beta"));
  CHECK(!bold->isChecked() && italic->isChecked() && underline->isChecked());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  select_row(window, QStringLiteral("Beta"));
  CHECK(bold->isChecked() && italic->isChecked() && underline->isChecked());

  // With no text layer to edit there is nothing to toggle.
  select_row(window, QStringLiteral("Background"));
  CHECK(!bold->isEnabled() && !bold->isChecked());
  CHECK(!bar_button(window, "textRtlButton")->isEnabled());
}

// Justify sits on top of L / C / R: L, C and R name the last line, Justify all shows Justify
// alone, and the Paragraph panel and Type > Alignment stay in step.
void ui_text_options_bar_justify_toggle_sets_the_justify_modes() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  const auto ids = build_bar_text_layers(window);
  show_window(window);
  require_canvas(window)->set_zoom(1.0);
  auto& document = MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* left = bar_button(window, "textAlignLeftButton");
  auto* center = bar_button(window, "textAlignCenterButton");
  auto* right = bar_button(window, "textAlignRightButton");
  auto* justify = bar_button(window, "textJustifyButton");
  if (left == nullptr || center == nullptr || right == nullptr || justify == nullptr) {
    return;
  }
  CHECK(justify->isVisible() && justify->focusPolicy() == Qt::NoFocus);
  select_row(window, QStringLiteral("Beta"));
  const auto shows = [&](bool l, bool c, bool r, bool j) {
    return left->isChecked() == l && center->isChecked() == c && right->isChecked() == r && justify->isChecked() == j;
  };
  const auto expect = [&](const char* name, bool l, bool c, bool r, bool j) {
    const auto stored = alignment_of(document, ids.beta);
    if (stored != QLatin1String(name) || !shows(l, c, r, j)) {
      std::fprintf(stderr, "  expected %s, stored %s; L%d C%d R%d J%d\n", name, qPrintable(stored),
                   left->isChecked() ? 1 : 0, center->isChecked() ? 1 : 0, right->isChecked() ? 1 : 0,
                   justify->isChecked() ? 1 : 0);
    }
    CHECK(stored == QLatin1String(name));
    CHECK(shows(l, c, r, j));
  };
  CHECK(shows(true, false, false, false));
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  justify->click();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  expect("justify", true, false, false, true);
  center->click();
  QApplication::processEvents();
  expect("justifyCenter", false, true, false, true);
  right->click();
  QApplication::processEvents();
  expect("justifyRight", false, false, true, true);
  justify->click();
  QApplication::processEvents();
  expect("right", false, false, true, false);
  // Justify all: Justify alone; L picks Justify (last line left), and Justify off goes to Left.
  require_action(window, "typeAlignJustifyAllAction")->trigger();
  QApplication::processEvents();
  expect("justifyAll", false, false, false, true);
  left->click();
  QApplication::processEvents();
  expect("justify", true, false, false, true);
  require_action(window, "typeAlignJustifyAllAction")->trigger();
  QApplication::processEvents();
  justify->click();
  QApplication::processEvents();
  expect("left", true, false, false, false);
  // Type > Alignment reads what the bar set.
  center->click();
  justify->click();
  QApplication::processEvents();
  expect("justifyCenter", false, true, false, true);
  emit window.findChild<QMenu*>(QStringLiteral("typeMenu"))->aboutToShow();
  CHECK(require_action(window, "typeAlignJustifyCenterAction")->isChecked());

  // The Paragraph panel both ways.
  bool drove = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textParagraphDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* align = dialog->findChild<QComboBox*>(QStringLiteral("textParagraphAlignCombo"));
    CHECK(align != nullptr);
    if (align != nullptr) {
      CHECK(align->currentData().toString() == QStringLiteral("justifyCenter"));
      align->setCurrentIndex(align->findData(QStringLiteral("justifyRight")));
      QApplication::processEvents();
      expect("justifyRight", false, false, true, true);
      justify->click();
      QApplication::processEvents();
      CHECK(align->currentData().toString() == QStringLiteral("right"));
      drove = true;
    }
    dialog->reject();
  }));
  bar_button(window, "textParagraphButton")->click();
  QApplication::processEvents();
  CHECK(drove);
  // Other layers keep their own alignment and the bar shows each.
  select_row(window, QStringLiteral("Alpha"));
  CHECK(shows(true, false, false, false));
}

// RTL is checked only for an explicit right to left paragraph; a press sets right to left, the
// next left to right, and the Paragraph panel's Direction list follows both ways.
void ui_text_options_bar_rtl_toggle_sets_the_paragraph_direction() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  const auto ids = build_bar_text_layers(window);
  show_window(window);
  require_canvas(window)->set_zoom(1.0);
  auto& document = MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* rtl = bar_button(window, "textRtlButton");
  auto* vertical = bar_button(window, "textOrientationButton");
  if (rtl == nullptr || vertical == nullptr) {
    return;
  }
  CHECK(rtl->isVisible() && rtl->focusPolicy() == Qt::NoFocus);
  CHECK(rtl->text() == QStringLiteral("RTL"));
  select_row(window, QStringLiteral("Beta"));
  CHECK(rtl->isEnabled() && !rtl->isChecked());
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  rtl->click();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(direction_of(document, ids.beta) == QStringLiteral("rtl"));
  CHECK(rtl->isChecked());
  rtl->click();
  QApplication::processEvents();
  CHECK(direction_of(document, ids.beta) == QStringLiteral("ltr"));
  CHECK(!rtl->isChecked());
  // Type > Direction: Right to left checks it, Auto does not.
  require_action(window, "typeDirectionRightToLeftAction")->trigger();
  QApplication::processEvents();
  CHECK(rtl->isChecked());
  require_action(window, "typeDirectionAutoAction")->trigger();
  QApplication::processEvents();
  CHECK(!rtl->isChecked());

  bool drove = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textParagraphDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* direction = dialog->findChild<QComboBox*>(QStringLiteral("textDirectionCombo"));
    CHECK(direction != nullptr);
    if (direction != nullptr) {
      direction->setCurrentIndex(direction->findData(static_cast<int>(Qt::RightToLeft)));
      QApplication::processEvents();
      CHECK(rtl->isChecked());
      rtl->click();
      QApplication::processEvents();
      CHECK(direction->currentData().toInt() == static_cast<int>(Qt::LeftToRight));
      drove = true;
    }
    dialog->reject();
  }));
  bar_button(window, "textParagraphButton")->click();
  QApplication::processEvents();
  CHECK(drove);
  select_row(window, QStringLiteral("Alpha"));
  CHECK(!rtl->isChecked());
}

// The Smoothing list left the bar; the saved default (tools/textSmoothing) still seeds new text,
// and the Character panel's Anti-alias list and Type > Anti-Alias set it.
void ui_text_anti_alias_default_survives_without_the_bar_list() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  SettingsValueRestorer saved_text_smoothing(QStringLiteral("tools/textSmoothing"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/textSmoothing"), patchy::kTextAntiAliasSharp);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo")) == nullptr);
  CHECK(shown_text_anti_alias(window) == patchy::kTextAntiAliasSharp);

  const auto point = canvas->widget_position_for_document_point(QPoint(60, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == patchy::kTextAntiAliasSharp);

  // The Character panel shows it and changes it for the live text and the saved default.
  bool drove = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = dialog->findChild<QComboBox*>(QStringLiteral("textCharacterAntiAliasCombo"));
    CHECK(list != nullptr);
    if (list != nullptr) {
      CHECK(list->count() == 7);
      CHECK(list->currentData().toInt() == patchy::kTextAntiAliasSharp);
      save_widget_artifact("issue_673_character_panel", *dialog);
      const auto crisp = list->findData(patchy::kTextAntiAliasCrisp);
      list->setCurrentIndex(crisp);
      emit list->activated(crisp);
      QApplication::processEvents();
      CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == patchy::kTextAntiAliasCrisp);
      // Type > Anti-Alias moves the panel's list too.
      choose_text_anti_alias(window, patchy::kTextAntiAliasStrong);
      CHECK(list->currentData().toInt() == patchy::kTextAntiAliasStrong);
      drove = true;
    }
    dialog->reject();
  }));
  bar_button(window, "textCharacterButton")->click();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == patchy::kTextAntiAliasStrong);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/textSmoothing")).toInt() ==
        patchy::kTextAntiAliasStrong);
}

}  // namespace

std::vector<patchy::test::TestCase> text_options_bar_toggles_tests() {
  return {
      {"ui_text_options_bar_style_toggles_follow_the_character_panel",
       ui_text_options_bar_style_toggles_follow_the_character_panel},
      {"ui_text_options_bar_justify_toggle_sets_the_justify_modes",
       ui_text_options_bar_justify_toggle_sets_the_justify_modes},
      {"ui_text_options_bar_rtl_toggle_sets_the_paragraph_direction",
       ui_text_options_bar_rtl_toggle_sets_the_paragraph_direction},
      {"ui_text_anti_alias_default_survives_without_the_bar_list",
       ui_text_anti_alias_default_survives_without_the_bar_list},
  };
}
