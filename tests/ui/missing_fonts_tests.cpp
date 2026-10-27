// Missing fonts: the Resolve Missing Fonts dialog (GitHub issue 169), the substitution prompt on
// hidden layers (177), the layer badge during a session (178) and the options-bar font new type
// starts in (180). See docs/font-resolution.md, "Missing fonts".
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/font_picker.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/qt_paths.hpp"
#include "ui/text_font_service.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QDialog>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

const QString kMissing = QStringLiteral("PatchyDefinitelyMissingFont123456");
const QString kOtherMissing = QStringLiteral("PatchyAnotherMissingFace98765");

patchy::Layer missing_font_text_layer(patchy::Document& document, const std::string& name, const QString& family,
                                      int top, bool visible = true) {
  auto pixels = solid_pixels(118, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 96, 30), QColor(20, 20, 20, 255));
  patchy::Layer layer(document.allocate_layer_id(), name, std::move(pixels));
  layer.set_bounds(patchy::Rect{40, top, 118, 36});
  layer.set_visible(visible);
  layer.metadata()[patchy::kLayerMetadataText] = name;
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextFont] = family.toStdString();
  layer.metadata()[patchy::kLayerMetadataTextSize] = "24";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  return layer;
}

patchy::Document white_document() {
  patchy::Document document(320, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(320, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  return document;
}

QString layer_font(MainWindow& window, patchy::LayerId id) {
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
  if (layer == nullptr) {
    return {};
  }
  const auto found = layer->metadata().find(patchy::kLayerMetadataTextFont);
  return found == layer->metadata().end() ? QString() : QString::fromStdString(found->second);
}

QLabel* content_thumbnail(QListWidget& layers, const QString& name) {
  auto* item = require_layer_item(layers, name);
  auto* row = item == nullptr ? nullptr : layers.itemWidget(item);
  return row == nullptr ? nullptr : row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
}

// Answers a missing-font message box once it appears (the substitution prompt opens from a
// deferred edit, so a single zero-delay timer can run before it exists). Gives up after
// `attempts` polls.
void answer_message_box(QString object_name, bool accept, std::shared_ptr<int> answered, int attempts = 100) {
  QTimer::singleShot(20, [object_name, accept, answered, attempts] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(object_name));
    if (dialog == nullptr) {
      if (attempts > 0) {
        answer_message_box(object_name, accept, answered, attempts - 1);
      }
      return;
    }
    ++*answered;
    if (!accept) {
      dialog->button(QMessageBox::Cancel)->click();
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
  });
}

void answer_missing_font_prompt(bool accept, std::shared_ptr<int> answered) {
  answer_message_box(QStringLiteral("missingPsdTextFontMessageBox"), accept, std::move(answered));
}

// Polls for the Missing Fonts dialog and hands it to `act` (which must close it).
void handle_missing_fonts_dialog(std::function<void(QDialog&)> act, std::shared_ptr<int> seen, int attempts = 100) {
  QTimer::singleShot(20, [act = std::move(act), seen, attempts]() mutable {
    auto* dialog = find_top_level_dialog(QStringLiteral("missingFontsDialog"));
    if (dialog == nullptr || !dialog->isVisible()) {
      if (attempts > 0) {
        handle_missing_fonts_dialog(std::move(act), seen, attempts - 1);
      }
      return;
    }
    ++*seen;
    act(*dialog);
  });
}

// The dialog row whose name label reads `family`: its index, or -1.
int missing_font_row(QDialog& dialog, const QString& family) {
  for (int index = 0; index < 32; ++index) {
    auto* name = dialog.findChild<QLabel*>(QStringLiteral("missingFontName%1").arg(index));
    if (name == nullptr) {
      return -1;
    }
    if (name->text() == family) {
      return index;
    }
  }
  return -1;
}

// A real type layer (a synthetic one carries no layout, so the writer stores no TySh) typed into a
// new "Source" document of `window` in the suite's Latin font, saved as PSD, then its family
// renamed in the bytes to one no machine has: same length, so every length prefix still holds.
// ASCII and UTF-16BE cover EngineData, descriptors and Txt2. Empty on failure.
std::vector<std::uint8_t> psd_with_missing_font(MainWindow& window, const QString& text) {
  window.add_document_session(white_document(), QStringLiteral("Source"));
  const auto family = patchy::test::visual_test_font().family();
  auto* combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(combo != nullptr);
  if (combo == nullptr) {
    return {};
  }
  combo->setCurrentFont(QFont(family));
  MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  QApplication::processEvents();
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return {};
  }
  QTest::keyClicks(editor, text);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window));
  const auto stem = family.section(QLatin1Char(' '), 0, 0);
  const auto renamed = QStringLiteral("Qzxqzxqzxqzxqzxqzx").left(stem.size());
  std::size_t replaced = 0;
  const auto replace_all = [&bytes, &replaced](const std::vector<std::uint8_t>& from, const std::vector<std::uint8_t>& to) {
    for (auto it = std::search(bytes.begin(), bytes.end(), from.begin(), from.end()); it != bytes.end();
         it = std::search(it + static_cast<std::ptrdiff_t>(to.size()), bytes.end(), from.begin(), from.end())) {
      std::copy(to.begin(), to.end(), it);
      ++replaced;
    }
  };
  std::vector<std::uint8_t> ascii_from, ascii_to, wide_from, wide_to;
  for (qsizetype index = 0; index < stem.size(); ++index) {
    ascii_from.push_back(static_cast<std::uint8_t>(stem[index].unicode()));
    ascii_to.push_back(static_cast<std::uint8_t>(renamed[index].unicode()));
    wide_from.insert(wide_from.end(), {0, static_cast<std::uint8_t>(stem[index].unicode())});
    wide_to.insert(wide_to.end(), {0, static_cast<std::uint8_t>(renamed[index].unicode())});
  }
  replace_all(ascii_from, ascii_to);
  replace_all(wide_from, wide_to);
  CHECK(replaced > 0);
  return bytes;
}

std::optional<patchy::LayerId> first_text_layer_id(const std::vector<patchy::Layer>& layers) {
  for (const auto& layer : layers) {
    if (patchy::layer_is_text(layer)) {
      return layer.id();
    }
    if (const auto child = first_text_layer_id(layer.children()); child.has_value()) {
      return child;
    }
  }
  return std::nullopt;
}

bool payload_names(const patchy::UnknownPsdBlock& block, const QString& name) {
  std::vector<std::uint8_t> needle;
  for (const auto ch : name) {
    needle.push_back(static_cast<std::uint8_t>(ch.unicode() >> 8));
    needle.push_back(static_cast<std::uint8_t>(ch.unicode() & 0xff));
  }
  return std::search(block.payload.begin(), block.payload.end(), needle.begin(), needle.end()) != block.payload.end();
}

// GitHub issue 180: a layer set in a missing font mirrors that family into the options bar,
// where the combo shows another row while currentFont() keeps the missing name. New type in
// another document then came out in the missing font instead of the one the bar showed.
void ui_new_text_uses_the_options_bar_font_after_a_missing_font_layer() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto first = white_document();
  const auto missing_id = first.add_layer(missing_font_text_layer(first, "Missing", kMissing, 40)).id();

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(first), QStringLiteral("Has Missing Font"));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(layer_list != nullptr && combo != nullptr);
  if (layer_list == nullptr || combo == nullptr) {
    return;
  }
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Missing")));
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::document(window).active_layer_id() == missing_id);
  // The bar mirrors the layer: the combo cannot show the missing family, so it parks elsewhere.
  CHECK(combo->currentFont().family() == kMissing);
  CHECK(combo->currentText() != kMissing);

  window.add_document_session(white_document(), QStringLiteral("Fresh"));
  QApplication::processEvents();
  const auto shown = combo->currentText();
  CHECK(!shown.isEmpty() && shown != kMissing);
  CHECK(combo->currentFont().family() == shown);

  MainWindowTestAccess::add_text_at(window, QPoint(60, 60));
  QApplication::processEvents();
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextFamily").toString() == shown);
  QTest::keyClicks(editor, QStringLiteral("New"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  const auto active = MainWindowTestAccess::document(window).active_layer_id();
  CHECK(active.has_value());
  if (active.has_value()) {
    CHECK(layer_font(window, *active) == shown);
  }
}

// GitHub issue 442: on Linux under a plain Qt theme (the Flatpak) the application font is Qt's
// generic "Sans Serif". Qt's Linux database lists that name as a family, but it draws in another
// face (DejaVu Sans on the test host); Windows does not list it at all. The first text layer of a
// session took the name and showed the missing-font badge although its text drew in an installed
// face. New type starts in the face the name resolves to, and a layer that stores a listed alias
// is not reported missing.
void ui_first_text_layer_in_a_generic_app_font_has_no_missing_font_badge() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto saved_app_font = QApplication::font();
  struct AppFontRestorer {
    QFont font;
    ~AppFontRestorer() { QApplication::setFont(font); }
  } restorer{saved_app_font};
  const auto generic = QStringLiteral("Sans Serif");
  const bool generic_listed = patchy::ui::available_text_family_match(generic).has_value();

  auto generic_font = saved_app_font;
  generic_font.setFamilies(QStringList{generic});
  QApplication::setFont(generic_font);

  MainWindow window;
  show_window(window);
  window.add_document_session(white_document(), QStringLiteral("Untitled-1"));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }

  const auto check_layer = [&] {
    const auto active = MainWindowTestAccess::document(window).active_layer_id();
    CHECK(active.has_value());
    if (!active.has_value()) {
      return;
    }
    const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(*active);
    CHECK(layer != nullptr && patchy::layer_is_text(*layer));
    if (layer == nullptr) {
      return;
    }
    const auto family = layer_font(window, *active);
    CHECK(!family.isEmpty() && family.compare(generic, Qt::CaseInsensitive) != 0);
    CHECK(patchy::ui::available_text_family_match(family).has_value());
    CHECK(patchy::ui::missing_text_families_for_layer(*layer).isEmpty());
    auto* thumbnail = content_thumbnail(*layer_list, QString::fromStdString(layer->name()));
    CHECK(thumbnail != nullptr);
    if (thumbnail != nullptr) {
      CHECK(!thumbnail->toolTip().contains(QStringLiteral("Font is missing on this computer")));
    }
  };

  auto* combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(combo != nullptr);
  if (combo == nullptr) {
    return;
  }
  const auto bar_family = combo->currentFont().family();
  // The provisional "Type" layer appears in the panel the moment the tool clicks.
  MainWindowTestAccess::add_text_at(window, QPoint(60, 60));
  QApplication::processEvents();
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  save_widget_artifact("ui_first_text_layer_generic_app_font_session", *layer_list);
  check_layer();
  QTest::keyClicks(editor, QStringLiteral("First"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  save_widget_artifact("ui_first_text_layer_generic_app_font_layers", *layer_list);
  check_layer();
  // The bar started on the installed face the generic name draws in.
  CHECK(bar_family.compare(generic, Qt::CaseInsensitive) != 0);
  CHECK(patchy::ui::available_text_family_match(bar_family).has_value());

  // A layer that already stores the generic name (an older Patchy, an XCF or KRA import): where
  // the database lists the alias it draws in a real face and is not missing; where it does not,
  // the layer still names a font this computer lacks.
  {
    auto scratch = white_document();
    const auto stored = missing_font_text_layer(scratch, "Generic", generic, 40);
    const auto missing = patchy::ui::missing_text_families_for_layer(stored);
    CHECK(generic_listed ? missing.isEmpty() : missing == QStringList{generic});
  }
}

// GitHub issue 177: editing a HIDDEN layer whose font is missing substituted it without the
// prompt a visible layer shows. Both go through the same question now; Cancel leaves the font.
void ui_hidden_missing_font_text_layer_still_warns_before_substitution() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto document = white_document();
  const auto hidden_id = document.add_layer(missing_font_text_layer(document, "Hidden", kMissing, 40, false)).id();

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Hidden Missing Font"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }

  // Double-click the layer icon, the reported path.
  const auto double_click_icon = [&] {
    click_layer_row_thumbnail(*layer_list, QStringLiteral("Hidden"), QStringLiteral("layerContentThumbnail"));
    auto* thumbnail = content_thumbnail(*layer_list, QStringLiteral("Hidden"));
    CHECK(thumbnail != nullptr);
    if (thumbnail != nullptr) {
      send_double_click(*thumbnail, thumbnail->rect().center());
    }
  };
  auto answered = std::make_shared<int>(0);
  answer_missing_font_prompt(false, answered);
  double_click_icon();
  process_events_for(400);
  CHECK(*answered == 1);
  CHECK(require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_font(window, hidden_id) == kMissing);

  // Continue substitutes, exactly as on a visible layer, and the layer stays hidden.
  answer_missing_font_prompt(true, answered);
  double_click_icon();
  process_events_for(400);
  CHECK(*answered == 2);
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor != nullptr) {
    CHECK(editor->property("patchy.documentTextFamily").toString() != kMissing);
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(layer_font(window, hidden_id) == kMissing);
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(hidden_id);
  CHECK(layer != nullptr && !layer->visible());
}

// GitHub issue 178: after Continue on the substitution prompt the session draws the layer in an
// installed face, but the panel kept the missing-font badge until the commit. It goes as the
// session opens, and an Escape (which keeps the missing font) brings it back.
void ui_missing_font_badge_clears_when_the_substitution_is_accepted() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto document = white_document();
  const auto id = document.add_layer(missing_font_text_layer(document, "Badge", kMissing, 40)).id();

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Badge"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  auto* before = content_thumbnail(*layer_list, QStringLiteral("Badge"));
  CHECK(before != nullptr);
  if (before == nullptr) {
    return;
  }
  CHECK(before->toolTip().contains(QStringLiteral("Font is missing on this computer")));
  const auto badged = before->pixmap().toImage();
  save_widget_artifact("ui_missing_font_badge_before_session", *layer_list);

  auto answered = std::make_shared<int>(0);
  answer_missing_font_prompt(true, answered);
  MainWindowTestAccess::edit_text_layer(window, id);
  process_events_for(300);
  CHECK(*answered == 1);
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  auto* during = content_thumbnail(*layer_list, QStringLiteral("Badge"));
  CHECK(during != nullptr);
  if (during != nullptr) {
    CHECK(!during->toolTip().contains(QStringLiteral("Font is missing on this computer")));
    CHECK(during->pixmap().toImage() != badged);
  }
  save_widget_artifact("ui_missing_font_badge_during_session", *layer_list);

  if (editor != nullptr) {
    send_key(*editor, Qt::Key_Escape);
  }
  process_events_for(150);
  CHECK(layer_font(window, id) == kMissing);
  auto* after = content_thumbnail(*layer_list, QStringLiteral("Badge"));
  CHECK(after != nullptr);
  if (after != nullptr) {
    CHECK(after->toolTip().contains(QStringLiteral("Font is missing on this computer")));
    CHECK(after->pixmap().toImage() == badged);
  }
}

// GitHub issue 169: Type > Resolve Missing Fonts lists every missing family once and replaces
// the chosen ones in every layer that uses them (hidden and grouped layers too) as one undo
// step. A family left unchecked keeps its name. The replacement reaches TySh and Txt2 on save.
void ui_resolve_missing_fonts_replaces_the_font_in_every_layer() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  // The shipped default, so the saved dialog image shows what a user sees.
  SettingsValueRestorer restorer(QStringLiteral("imports/showMissingFontsDialog"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showMissingFontsDialog"), true);
  const auto replacement = patchy::test::visual_test_font().family();
  CHECK(QFontDatabase::families().contains(replacement));
  auto document = white_document();
  const auto top_id = document.add_layer(missing_font_text_layer(document, "Top", kMissing, 20)).id();
  const auto hidden_id = document.add_layer(missing_font_text_layer(document, "Hidden", kMissing, 70, false)).id();
  const auto other_id = document.add_layer(missing_font_text_layer(document, "Other", kOtherMissing, 120)).id();
  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  auto child = missing_font_text_layer(document, "Grouped", kMissing, 170);
  const auto grouped_id = child.id();
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Resolve"));
  QApplication::processEvents();

  auto seen = std::make_shared<int>(0);
  handle_missing_fonts_dialog(
      [replacement](QDialog& dialog) {
        const auto missing_row = missing_font_row(dialog, kMissing);
        const auto other_row = missing_font_row(dialog, kOtherMissing);
        CHECK(missing_row >= 0 && other_row >= 0 && missing_row != other_row);
        CHECK(dialog.findChild<QLabel*>(QStringLiteral("missingFontName2")) == nullptr);
        auto* resolve = dialog.findChild<QPushButton*>(QStringLiteral("missingFontsResolveButton"));
        CHECK(resolve != nullptr);
        // Nothing is replaced until a row is ticked: the button is always enabled and reads
        // "Don't Resolve" until then (issue 169 follow-up).
        CHECK(resolve != nullptr && resolve->isEnabled() && resolve->text() == QStringLiteral("Don't Resolve"));
        // One divider line between the two rows, no "Not installed." prefix on the details, and
        // the "Don't show" label names the way back.
        CHECK(dialog.findChildren<QFrame*>(QStringLiteral("missingFontsRowDivider")).size() == 1);
        const auto details = dialog.findChildren<QLabel*>(QStringLiteral("missingFontDetail"));
        CHECK(details.size() == 2);
        for (const auto* detail : details) {
          CHECK(detail->text().startsWith(QStringLiteral("Used in ")));
        }
        auto* intro = dialog.findChild<QLabel*>(QStringLiteral("missingFontsIntroLabel"));
        CHECK(intro != nullptr && intro->text().contains(QStringLiteral("missing on this computer")));
        auto* dont_show = dialog.findChild<QCheckBox*>(QStringLiteral("missingFontsDontShowCheck"));
        CHECK(dont_show != nullptr && dont_show->text().contains(QStringLiteral("Preferences > Application")));
        auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("missingFontReplaceCheck%1").arg(missing_row));
        auto* picker = dialog.findChild<patchy::ui::FontPickerCombo*>(
            QStringLiteral("missingFontReplacementCombo%1").arg(missing_row));
        CHECK(check != nullptr && picker != nullptr);
        if (check == nullptr || picker == nullptr || resolve == nullptr) {
          dialog.reject();
          return;
        }
        CHECK(!picker->isEnabled());
        save_widget_artifact("ui_resolve_missing_fonts_dialog_none_ticked", dialog);
        check->setChecked(true);
        CHECK(picker->isEnabled() && resolve->isEnabled() && resolve->text() == QStringLiteral("Resolve Fonts"));
        picker->setCurrentFont(QFont(replacement));
        save_widget_artifact("ui_resolve_missing_fonts_dialog", dialog);
        resolve->click();
      },
      seen);
  auto* action = window.findChild<QAction*>(QStringLiteral("typeResolveMissingFontsAction"));
  CHECK(action != nullptr);
  if (action == nullptr) {
    return;
  }
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  action->trigger();
  process_events_for(200);
  CHECK(*seen == 1);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

  for (const auto id : {top_id, hidden_id, grouped_id}) {
    CHECK(layer_font(window, id) == replacement);
  }
  CHECK(layer_font(window, other_id) == kOtherMissing);
  save_widget_artifact("ui_resolve_missing_fonts_after", window);
  const auto* hidden = std::as_const(MainWindowTestAccess::document(window)).find_layer(hidden_id);
  CHECK(hidden != nullptr && !hidden->visible());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list != nullptr) {
    auto* top = content_thumbnail(*layer_list, QStringLiteral("Top"));
    CHECK(top != nullptr && !top->toolTip().contains(QStringLiteral("Font is missing on this computer")));
    auto* other = content_thumbnail(*layer_list, QStringLiteral("Other"));
    CHECK(other != nullptr && other->toolTip().contains(QStringLiteral("Font is missing on this computer")));
  }

  // Saved: the TySh of each resolved layer and the document's Txt2 name the replacement and no
  // longer the missing family.
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window));
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  const auto replacement_stem = replacement.left(5);
  std::function<void(const std::vector<patchy::Layer>&)> check_layers = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      check_layers(layer.children());
      if (layer.name() != "Top" && layer.name() != "Hidden" && layer.name() != "Grouped") {
        continue;
      }
      const auto& blocks = layer.unknown_psd_blocks();
      CHECK(std::ranges::any_of(blocks, [&](const auto& block) {
        return block.key == "TySh" && payload_names(block, replacement_stem) && !payload_names(block, kMissing);
      }));
    }
  };
  check_layers(reopened.layers());
  const auto& globals = reopened.metadata().unknown_psd_resources;
  const auto txt2 = std::ranges::find_if(globals, [](const auto& block) { return block.key == "Txt2"; });
  CHECK(txt2 != globals.end());
  if (txt2 != globals.end()) {
    CHECK(payload_names(*txt2, replacement_stem));
    CHECK(!payload_names(*txt2, kMissing));
  }

  // One undo step puts every replaced layer back on the missing family.
  MainWindowTestAccess::undo(window);
  process_events_for(100);
  for (const auto id : {top_id, hidden_id, grouped_id}) {
    CHECK(layer_font(window, id) == kMissing);
  }
}

// Opening a document with missing fonts shows the dialog (Photoshop does); "Don't show when
// opening documents" turns that off, and Type > Resolve Missing Fonts still opens it.
void ui_missing_fonts_dialog_shows_on_open_until_turned_off() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  SettingsValueRestorer restorer(QStringLiteral("imports/showMissingFontsDialog"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showMissingFontsDialog"), true);
  ensure_artifact_dir();
  const auto path = QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/missing_fonts_on_open.psd"));

  MainWindow window;
  show_window(window);
  {
    const auto bytes = psd_with_missing_font(window, QStringLiteral("Missing"));
    CHECK(!bytes.empty());
    if (bytes.empty()) {
      return;
    }
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    file.close();
  }

  auto seen = std::make_shared<int>(0);
  handle_missing_fonts_dialog(
      [](QDialog& dialog) {
        CHECK(dialog.findChild<QLabel*>(QStringLiteral("missingFontName0")) != nullptr);
        auto* dont_show = dialog.findChild<QCheckBox*>(QStringLiteral("missingFontsDontShowCheck"));
        CHECK(dont_show != nullptr && !dont_show->isChecked());
        if (dont_show != nullptr) {
          dont_show->setChecked(true);
        }
        dialog.reject();
      },
      seen);
  MainWindowTestAccess::open_document_path(window, path);
  process_events_for(100);
  CHECK(*seen == 1);
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("imports/showMissingFontsDialog"), true).toBool());

  // Off: a second open stays quiet (the poller would count a dialog and close it).
  handle_missing_fonts_dialog([](QDialog& dialog) { dialog.reject(); }, seen, 10);
  MainWindowTestAccess::open_document_path(window, path);
  process_events_for(400);
  CHECK(*seen == 1);

  // The menu entry still offers it.
  handle_missing_fonts_dialog([](QDialog& dialog) { dialog.reject(); }, seen);
  window.findChild<QAction*>(QStringLiteral("typeResolveMissingFontsAction"))->trigger();
  process_events_for(100);
  CHECK(*seen == 2);
}

// Issue 169 follow-up: with no row ticked the button reads "Don't Resolve"; pressing it closes the
// dialog, changes no layer, adds no undo step, and still saves the "Don't show" box.
void ui_missing_fonts_dont_resolve_keeps_fonts_and_saves_dont_show() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  SettingsValueRestorer restorer(QStringLiteral("imports/showMissingFontsDialog"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showMissingFontsDialog"), true);
  auto document = white_document();
  const auto layer_id = document.add_layer(missing_font_text_layer(document, "Only", kMissing, 20)).id();

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Dont Resolve"));
  QApplication::processEvents();

  auto seen = std::make_shared<int>(0);
  handle_missing_fonts_dialog(
      [](QDialog& dialog) {
        auto* resolve = dialog.findChild<QPushButton*>(QStringLiteral("missingFontsResolveButton"));
        auto* dont_show = dialog.findChild<QCheckBox*>(QStringLiteral("missingFontsDontShowCheck"));
        CHECK(resolve != nullptr && dont_show != nullptr);
        if (resolve == nullptr || dont_show == nullptr) {
          dialog.reject();
          return;
        }
        // A single row: no divider.
        CHECK(dialog.findChildren<QFrame*>(QStringLiteral("missingFontsRowDivider")).isEmpty());
        CHECK(resolve->isEnabled() && resolve->text() == QStringLiteral("Don't Resolve"));
        dont_show->setChecked(true);
        resolve->click();
      },
      seen);
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  window.findChild<QAction*>(QStringLiteral("typeResolveMissingFontsAction"))->trigger();
  process_events_for(200);
  CHECK(*seen == 1);
  CHECK(find_top_level_dialog(QStringLiteral("missingFontsDialog")) == nullptr ||
        !find_top_level_dialog(QStringLiteral("missingFontsDialog"))->isVisible());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before);
  CHECK(layer_font(window, layer_id) == kMissing);
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("imports/showMissingFontsDialog"), true).toBool());
}

// GitHub issue 185: Free Transform on imported type whose font is missing gave no warning.
// Photoshop asks and then transforms the saved pixels; so does Patchy now. Cancel leaves the layer
// as it was; Continue scales its pixels and keeps the font. Patchy-made type whose font is
// substituted is untouched by this: it re-renders in the substitute without a question, by design
// (on Linux every Arial layer is one of those).
void ui_transforming_missing_font_text_warns_and_keeps_its_pixels() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  SettingsValueRestorer restorer(QStringLiteral("text/warnMissingFontsOnTransform"));
  patchy::ui::app_settings().setValue(QStringLiteral("text/warnMissingFontsOnTransform"), true);
  MainWindow window;
  show_window(window);
  const auto bytes = psd_with_missing_font(window, QStringLiteral("Scale me"));
  CHECK(!bytes.empty());
  if (bytes.empty()) {
    return;
  }
  auto imported = patchy::psd::DocumentIo::read(bytes);
  const auto id = first_text_layer_id(std::as_const(imported).layers());
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  imported.set_active_layer(*id);
  window.add_document_session(std::move(imported), QStringLiteral("Imported"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(*id);
  CHECK(layer != nullptr && !patchy::ui::missing_text_families_for_layer(*layer).isEmpty());
  CHECK(layer != nullptr && layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform));
  if (layer == nullptr) {
    return;
  }
  const auto font_before = layer_font(window, *id);
  const auto size_before = layer->metadata().at(patchy::kLayerMetadataTextSize);
  const auto scale_and_commit = [&](int grow) {
    const auto rect = canvas->active_layer_document_rect();
    CHECK(rect.has_value());
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    if (rect.has_value()) {
      const auto corner = canvas->widget_position_for_document_point(rect->bottomRight() + QPoint(1, 1));
      drag(*canvas, corner, corner + QPoint(grow, grow / 3));
    }
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    process_events_for(150);
  };
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());

  auto answered = std::make_shared<int>(0);
  answer_message_box(QStringLiteral("missingFontTransformMessageBox"), false, answered);
  scale_and_commit(120);
  CHECK(*answered == 1);
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);
  CHECK(layer_font(window, *id) == font_before);

  answer_message_box(QStringLiteral("missingFontTransformMessageBox"), true, answered);
  scale_and_commit(120);
  CHECK(*answered == 2);
  const auto after = canvas->active_layer_document_rect();
  CHECK(before.has_value() && after.has_value() && after->width() > before->width() + 40);
  // Not re-rendered in a substitute: the font is still the one the file names, and the size was
  // not folded into a new point size (the scale rides the transform).
  CHECK(layer_font(window, *id) == font_before);
  layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(*id);
  CHECK(layer != nullptr && layer->metadata().at(patchy::kLayerMetadataTextSize) == size_before);
  save_widget_artifact("ui_transforming_missing_font_text_after", window);

  // Automation never sees the question (a script or connector run would hang on it): the
  // transform goes ahead as if Continue were pressed. The poller only closes a stray prompt.
  window.set_cli_automation_mode(true);
  const auto automated_before = canvas->active_layer_document_rect();
  answer_message_box(QStringLiteral("missingFontTransformMessageBox"), true, answered, 20);
  scale_and_commit(60);
  process_events_for(450);
  CHECK(*answered == 2);
  CHECK(!canvas->free_transform_active());
  const auto automated_after = canvas->active_layer_document_rect();
  CHECK(automated_before.has_value() && automated_after.has_value() &&
        automated_after->width() > automated_before->width());
  CHECK(layer_font(window, *id) == font_before);
  window.set_cli_automation_mode(false);

  // Patchy-made type whose font is substituted (here: renamed in place) keeps re-rendering through
  // the transform as before: no question, and the scale folds into the point size.
  window.add_document_session(white_document(), QStringLiteral("Patchy type"));
  const auto family = patchy::test::visual_test_font().family();
  if (auto* combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo")); combo != nullptr) {
    combo->setCurrentFont(QFont(family));
  }
  MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  QApplication::processEvents();
  auto* patchy_canvas = require_canvas(window);
  auto* editor = patchy_canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  QTest::keyClicks(editor, QStringLiteral("Scale me"));
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  const auto patchy_id = MainWindowTestAccess::document(window).active_layer_id();
  CHECK(patchy_id.has_value());
  if (!patchy_id.has_value()) {
    return;
  }
  {
    auto* patchy_layer = MainWindowTestAccess::document(window).find_layer(*patchy_id);
    CHECK(patchy_layer != nullptr);
    if (patchy_layer == nullptr) {
      return;
    }
    auto& metadata = patchy_layer->metadata();
    metadata[patchy::kLayerMetadataTextFont] = kMissing.toStdString();
    if (const auto runs = metadata.find(patchy::kLayerMetadataTextRuns); runs != metadata.end()) {
      runs->second = QString::fromStdString(runs->second)
                         .replace(QString::fromUtf8(QUrl::toPercentEncoding(family)), kMissing)
                         .replace(family, kMissing)
                         .toStdString();
    }
  }
  MainWindowTestAccess::refresh_layer_ui(window);
  const auto* patchy_layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(*patchy_id);
  CHECK(patchy_layer != nullptr && !patchy::ui::missing_text_families_for_layer(*patchy_layer).isEmpty());
  CHECK(patchy_layer != nullptr && !patchy_layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform));
  if (patchy_layer == nullptr) {
    return;
  }
  const auto patchy_size_before = patchy_layer->metadata().at(patchy::kLayerMetadataTextSize);
  canvas = patchy_canvas;
  answer_message_box(QStringLiteral("missingFontTransformMessageBox"), false, answered, 20);
  scale_and_commit(120);
  process_events_for(450);
  CHECK(*answered == 2);
  CHECK(!patchy_canvas->free_transform_active());
  patchy_layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(*patchy_id);
  CHECK(patchy_layer != nullptr && patchy_layer->metadata().at(patchy::kLayerMetadataTextSize) != patchy_size_before);
}

}  // namespace

// Scripts, the connector and --headless runs never see the Missing Fonts dialog on open,
// whatever the preference says: both automation forms (app-wide CLI/connector mode and an
// unattended script run) open the document and leave no modal behind. The perf harness,
// which drives MainWindow without a script, sat in this dialog for hours on October 25,
// 2026; this pins the paths real automation takes.
void ui_unattended_open_skips_the_missing_fonts_dialog() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  SettingsValueRestorer restorer(QStringLiteral("imports/showMissingFontsDialog"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showMissingFontsDialog"), true);
  ensure_artifact_dir();
  const auto path = QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/missing_fonts_unattended.psd"));

  MainWindow window;
  show_window(window);
  {
    const auto bytes = psd_with_missing_font(window, QStringLiteral("Missing"));
    CHECK(!bytes.empty());
    if (bytes.empty()) {
      return;
    }
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    file.close();
  }

  // The poller only closes a stray dialog so a regression fails instead of hanging.
  auto seen = std::make_shared<int>(0);
  handle_missing_fonts_dialog([](QDialog& dialog) { dialog.reject(); }, seen, 40);

  // CLI automation mode (--headless, --run-script, the invisible connector).
  window.set_cli_automation_mode(true);
  const auto tabs_before = MainWindowTestAccess::document_tab_count(window);
  MainWindowTestAccess::open_document_path(window, path);
  process_events_for(300);
  CHECK(*seen == 0);
  CHECK(MainWindowTestAccess::document_tab_count(window) == tabs_before + 1);
  window.set_cli_automation_mode(false);

  // An unattended script run (every connector request and --run-script execution) in an
  // otherwise attended window: app.open goes through the same gate.
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("unattended-open");
  options.unattended = true;
  const auto tabs_before_script = MainWindowTestAccess::document_tab_count(window);
  CHECK(host.run_source(QStringLiteral("app.open('%1');").arg(path), std::move(options)));
  for (int waited = 0; host.run_active() && waited < 10000; waited += 20) {
    process_events_for(20);
  }
  process_events_for(300);
  CHECK(!host.run_active());
  CHECK(*seen == 0);
  CHECK(MainWindowTestAccess::document_tab_count(window) == tabs_before_script + 1);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imports/showMissingFontsDialog"), true).toBool());
}

// The suite's Latin font as the substitute table's stand-in for Helvetica (Arial on Windows and
// macOS, Liberation Sans on Linux; GitHub issue 463). Skips on a host that has Helvetica itself or
// registered none of its substitutes.
patchy::ui::TextFontSubstitute helvetica_substitute_or_skip() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  if (patchy::ui::available_text_family_match(QStringLiteral("Helvetica")).has_value()) {
    patchy::test::skip("Helvetica is installed on this host");
  }
  const auto substitutes = patchy::ui::installed_text_font_substitutes(QStringLiteral("Helvetica"),
                                                                       QStringLiteral("Headline"));
  if (substitutes.empty()) {
    patchy::test::skip("no Helvetica substitute is registered on this host");
  }
  return substitutes.front();
}

// GitHub issue 463: Resolve Missing Fonts starts a listed font's picker on the installed
// substitute and says how close it is; the row stays unticked, and a font the table does not list
// keeps today's fallback with no suggestion line.
void ui_missing_fonts_dialog_offers_the_installed_substitute_first() {
  const auto substitute = helvetica_substitute_or_skip();
  CHECK(substitute.metric);
  const auto helvetica = QStringLiteral("Helvetica");
  auto document = white_document();
  const auto id = document.add_layer(missing_font_text_layer(document, "Headline", helvetica, 20)).id();
  document.add_layer(missing_font_text_layer(document, "Other", kMissing, 80));

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Substitutes"));
  QApplication::processEvents();

  auto seen = std::make_shared<int>(0);
  handle_missing_fonts_dialog(
      [&](QDialog& dialog) {
        const auto row = missing_font_row(dialog, helvetica);
        const auto other_row = missing_font_row(dialog, kMissing);
        CHECK(row >= 0 && other_row >= 0);
        auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("missingFontReplaceCheck%1").arg(row));
        auto* picker =
            dialog.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("missingFontReplacementCombo%1").arg(row));
        auto* suggestion = dialog.findChild<QLabel*>(QStringLiteral("missingFontSuggestion%1").arg(row));
        CHECK(check != nullptr && !check->isChecked());
        CHECK(picker != nullptr && picker->currentFont().family() == substitute.family);
        CHECK(suggestion != nullptr &&
              suggestion->text() == QStringLiteral("Suggested: %1 (same character widths)").arg(substitute.family));
        CHECK(dialog.findChild<QLabel*>(QStringLiteral("missingFontSuggestion%1").arg(other_row)) == nullptr);
        save_widget_artifact("ui_missing_fonts_dialog_suggests_substitute", dialog);
        dialog.reject();
      },
      seen);
  auto* action = window.findChild<QAction*>(QStringLiteral("typeResolveMissingFontsAction"));
  CHECK(action != nullptr);
  if (action == nullptr) {
    return;
  }
  action->trigger();
  process_events_for(200);
  CHECK(*seen == 1);
  CHECK(layer_font(window, id) == helvetica);
}

// GitHub issue 463: editing imported type whose font is missing works in the table's substitute,
// the prompt says so, and the layer keeps naming the original font until the edit is committed.
void ui_missing_font_edit_session_uses_the_table_substitute() {
  const auto substitute = helvetica_substitute_or_skip();
  const auto helvetica = QStringLiteral("Helvetica");
  auto document = white_document();
  const auto id = document.add_layer(missing_font_text_layer(document, "Headline", helvetica, 40)).id();

  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Substitute Edit"));
  QApplication::processEvents();

  auto informative = std::make_shared<QString>();
  auto answered = std::make_shared<int>(0);
  const std::function<void(int)> answer = [informative, answered, &answer](int attempts) {
    QTimer::singleShot(20, [informative, answered, attempts, &answer] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
      if (box == nullptr) {
        if (attempts > 0) {
          answer(attempts - 1);
        }
        return;
      }
      ++*answered;
      *informative = box->informativeText();
      for (auto* button : box->findChildren<QPushButton*>()) {
        if (box->buttonRole(button) == QMessageBox::AcceptRole) {
          button->click();
          return;
        }
      }
    });
  };
  answer(100);
  MainWindowTestAccess::edit_text_layer(window, id);
  process_events_for(300);
  CHECK(*answered == 1);
  CHECK(*informative == QStringLiteral("Helvetica will be replaced with %1.").arg(substitute.face_family));
  auto* editor = require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextFamily").toString() == substitute.face_family);
  CHECK(layer_font(window, id) == helvetica);  // nothing is stored until the commit
  send_key(*editor, Qt::Key_End);
  QTest::keyClicks(editor, QStringLiteral("s"));
  send_key(*editor, Qt::Key_Enter, Qt::KeypadModifier);
  process_events_for(200);
  CHECK(require_canvas(window)->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_font(window, id) == substitute.face_family);
}

std::vector<patchy::test::TestCase> missing_fonts_tests() {
  return {
      {"ui_missing_fonts_dialog_offers_the_installed_substitute_first",
       ui_missing_fonts_dialog_offers_the_installed_substitute_first},
      {"ui_missing_font_edit_session_uses_the_table_substitute", ui_missing_font_edit_session_uses_the_table_substitute},
      {"ui_new_text_uses_the_options_bar_font_after_a_missing_font_layer",
       ui_new_text_uses_the_options_bar_font_after_a_missing_font_layer},
      {"ui_first_text_layer_in_a_generic_app_font_has_no_missing_font_badge",
       ui_first_text_layer_in_a_generic_app_font_has_no_missing_font_badge},
      {"ui_hidden_missing_font_text_layer_still_warns_before_substitution",
       ui_hidden_missing_font_text_layer_still_warns_before_substitution},
      {"ui_missing_font_badge_clears_when_the_substitution_is_accepted",
       ui_missing_font_badge_clears_when_the_substitution_is_accepted},
      {"ui_resolve_missing_fonts_replaces_the_font_in_every_layer",
       ui_resolve_missing_fonts_replaces_the_font_in_every_layer},
      {"ui_missing_fonts_dialog_shows_on_open_until_turned_off", ui_missing_fonts_dialog_shows_on_open_until_turned_off},
      {"ui_missing_fonts_dont_resolve_keeps_fonts_and_saves_dont_show",
       ui_missing_fonts_dont_resolve_keeps_fonts_and_saves_dont_show},
      {"ui_unattended_open_skips_the_missing_fonts_dialog", ui_unattended_open_skips_the_missing_fonts_dialog},
      {"ui_transforming_missing_font_text_warns_and_keeps_its_pixels",
       ui_transforming_missing_font_text_warns_and_keeps_its_pixels},
  };
}
