// The text bar font picker's Recent section (GitHub issue 569), its Favorites (GitHub issue 630),
// its Style and Language filters (GitHub issue 570), the hidden-matches hint (GitHub issue 647) and
// plain family names without Qt's foundry suffix (GitHub issue 646 follow-up) and English names
// for families listed in another script (GitHub issue 691). See docs/font-picker.md.
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/font_display_names.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/font_family_filters.hpp"
#include "ui/font_picker.hpp"
#include "ui/main_window.hpp"
#include "ui/text_font_service.hpp"
#include "ui/theme_palette.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHoverEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QRawFont>
#include <QScrollBar>
#include <QSet>
#include <QStandardPaths>
#include <QStringListModel>
#include <QSignalBlocker>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::FontPickerRowButton;
using patchy::ui::FontPickerRowKind;

struct StandardPathsTestMode {
  StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(true); }
  ~StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(false); }
};

void reset_picker_settings() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey));
  settings.remove(QString::fromLatin1(patchy::ui::kFontPickerStyleFilterKey));
  settings.remove(QString::fromLatin1(patchy::ui::kFontPickerLanguageFilterKey));
  settings.remove(QString::fromLatin1(patchy::ui::kFontPickerFavoriteFamiliesKey));
  settings.remove(QString::fromLatin1(patchy::ui::kFontPickerFavoritesOnlyKey));
  settings.sync();
}

QWidget* open_picker_popup(patchy::ui::FontPickerCombo& picker) {
  picker.showPopup();
  QApplication::processEvents();
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QString::fromLatin1(patchy::ui::kFontPickerPopupObjectName) && widget->isVisible()) {
      return widget;
    }
  }
  return nullptr;
}

// The families the picker offers, each once: its combo keeps a row per foundry copy where Qt's
// Linux database lists a family several foundries install (GitHub issue 646 follow-up).
QStringList listed_families(const QComboBox& picker) {
  QStringList rows;
  for (int row = 0; row < picker.count(); ++row) {
    rows.append(picker.itemText(row));
  }
  return patchy::ui::font_families_without_foundries(rows);
}

FontPickerRowKind row_kind(const QModelIndex& index) {
  return static_cast<FontPickerRowKind>(index.data(patchy::ui::kFontPickerRowKindRole).toInt());
}

QStringList family_rows(const QListView& list, bool recent) {
  QStringList out;
  auto* model = list.model();
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto index = model->index(row, 0);
    if (row_kind(index) == FontPickerRowKind::family &&
        index.data(patchy::ui::kFontPickerRecentRowRole).toBool() == recent) {
      out.append(index.data(Qt::DisplayRole).toString());
    }
  }
  return out;
}

int header_rows(const QListView& list) {
  int headers = 0;
  for (int row = 0; row < list.model()->rowCount(); ++row) {
    headers += row_kind(list.model()->index(row, 0)) != FontPickerRowKind::family ? 1 : 0;
  }
  return headers;
}

patchy::ui::FontPickerCombo* type_tool_picker(patchy::ui::MainWindow& window) {
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  return window.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("textFontCombo"));
}

// Waits until no Style classification runs in the open popup.
bool wait_for_style_reading(QWidget& popup, QLabel& status, int timeout_ms) {
  QElapsedTimer clock;
  clock.start();
  while (status.isVisible() && clock.elapsed() < timeout_ms) {
    QTest::qWait(20);
  }
  Q_UNUSED(popup);
  return !status.isVisible();
}

void ui_font_picker_recent_section_orders_skips_and_persists() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  const auto families = listed_families(*picker);
  if (families.size() < 2) {
    patchy::test::skip("fewer than two font families on this machine");
  }
  // Any two installed families (the Linux runner has neither Verdana nor Arial).
  const auto verdana = families.at(1);
  const auto arial = families.at(0);

  // No recent picks yet: no section, no headers.
  {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    CHECK(list != nullptr);
    CHECK(header_rows(*list) == 0);
    popup->close();
    QApplication::processEvents();
  }

  // A popup pick records the family.
  const auto pick = [picker](const QString& family) {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    CHECK(search != nullptr && list != nullptr);
    QTest::keyClicks(search, family);
    QApplication::processEvents();
    CHECK(header_rows(*list) == 0);  // the Recent section hides while searching
    for (int row = 0; row < list->model()->rowCount(); ++row) {
      if (list->model()->index(row, 0).data(Qt::DisplayRole).toString() == family) {
        list->setCurrentIndex(list->model()->index(row, 0));
      }
    }
    QTest::keyClick(search, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(picker->currentText() == family);
  };
  pick(verdana);
  pick(arial);
  pick(verdana);
  CHECK(patchy::ui::stored_recent_font_families() == (QStringList{verdana, arial}));  // newest first, no repeat

  // The stored list caps at eight; a family no longer installed stays stored but never shows.
  const auto missing = QStringLiteral("Patchy Missing Recent Font 569");
  for (int n = 0; n < 9; ++n) {
    patchy::ui::record_recent_font_family(QStringLiteral("Patchy Filler Font %1").arg(n));
  }
  CHECK(patchy::ui::stored_recent_font_families().size() == patchy::ui::kMaxRecentFontFamilies);
  CHECK(!patchy::ui::stored_recent_font_families().contains(verdana));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey),
                      QStringList{verdana, missing, arial});
  }
  {
    const QSignalBlocker blocker(picker);
    picker->setCurrentFont(QFont(verdana));
  }

  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  CHECK(search != nullptr && list != nullptr);
  auto* model = list->model();
  CHECK(model->rowCount() > 4);
  CHECK(row_kind(model->index(0, 0)) == FontPickerRowKind::recent_header);
  CHECK(model->index(1, 0).data(Qt::DisplayRole).toString() == verdana);
  CHECK(model->index(2, 0).data(Qt::DisplayRole).toString() == arial);
  CHECK(row_kind(model->index(3, 0)) == FontPickerRowKind::all_header);
  CHECK(family_rows(*list, true) == (QStringList{verdana, arial}));
  CHECK(!family_rows(*list, false).contains(missing));
  CHECK(family_rows(*list, false).contains(verdana));  // the full list still has every family
  CHECK((model->flags(model->index(0, 0)) & Qt::ItemIsSelectable) == 0);
  CHECK((model->flags(model->index(3, 0)) & Qt::ItemIsSelectable) == 0);
  // Opens on the current family's Recent row, with the section in view.
  CHECK(list->currentIndex().row() == 1);
  save_widget_artifact("issue569_font_picker_recent", *popup);

  // Up from the first recent row stays there; Down from the last one skips the All Fonts header.
  QTest::keyClick(search, Qt::Key_Up);
  CHECK(list->currentIndex().row() == 1);
  QTest::keyClick(search, Qt::Key_Down);
  CHECK(list->currentIndex().row() == 2);
  QTest::keyClick(search, Qt::Key_Down);
  CHECK(list->currentIndex().row() == 4);
  CHECK(row_kind(list->currentIndex()) == FontPickerRowKind::family);
  QTest::keyClick(search, Qt::Key_Up);
  CHECK(list->currentIndex().row() == 2);
  // A page jump never rests on a header.
  QTest::keyClick(search, Qt::Key_PageUp);
  CHECK(row_kind(list->currentIndex()) == FontPickerRowKind::family);
  popup->close();
  QApplication::processEvents();
  reset_picker_settings();
}

void ui_font_picker_style_and_language_filters_persist() {
  register_test_fonts(TestFontRole::Verdana);
  const StandardPathsTestMode standard_paths_test_mode;  // the style cache goes to a test folder
  patchy::ui::FontStyleIndex::instance().clear();
  reset_picker_settings();
  patchy::ui::record_recent_font_family(QStringLiteral("Arial"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);

  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* style = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerStyleCombo"));
  auto* language = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerLanguageCombo"));
  auto* status = popup->findChild<QLabel*>(QStringLiteral("textFontPickerStatusLabel"));
  CHECK(list != nullptr && style != nullptr && language != nullptr && status != nullptr);
  CHECK(style->currentData().toString() == QStringLiteral("all"));
  CHECK(language->currentData().toString() == QStringLiteral("all"));
  CHECK(style->count() == 10);
  CHECK(language->count() > 1);
  CHECK(style->focusPolicy() == Qt::NoFocus && language->focusPolicy() == Qt::NoFocus);
  const auto all_rows = family_rows(*list, false);
  CHECK(!all_rows.isEmpty());

  // Monospace comes straight from the font database.
  style->setCurrentIndex(style->findData(QStringLiteral("monospace")));
  QApplication::processEvents();
  const auto mono_rows = family_rows(*list, false);
  CHECK(mono_rows.size() < all_rows.size());
  for (const auto& family : mono_rows) {
    CHECK(QFontDatabase::isFixedPitch(family));
  }
  CHECK(!family_rows(*list, true).contains(QStringLiteral("Arial")));  // the filter reaches Recent too

  // Sans Serif reads each family's OS/2 table in timer slices.
  QElapsedTimer clock;
  clock.start();
  style->setCurrentIndex(style->findData(QStringLiteral("sans-serif")));
  CHECK(wait_for_style_reading(*popup, *status, 120000));
  std::cout << "  note: classified " << all_rows.size() << " families in " << clock.elapsed() << " ms\n";
  const auto sans_rows = family_rows(*list, false);
  CHECK(!sans_rows.isEmpty());
  CHECK(sans_rows.size() < all_rows.size());
  auto& index = patchy::ui::FontStyleIndex::instance();
  for (const auto& family : sans_rows) {
    const auto traits = index.cached(family);
    CHECK(traits.has_value() && traits->category == patchy::FontStyleCategory::sans_serif);
  }
  for (const auto& family : {QStringLiteral("Arial"), QStringLiteral("Verdana")}) {
    if (all_rows.contains(family)) {
      CHECK(sans_rows.contains(family));
    }
  }
  // A recent family no longer installed (Arial on the Linux runner) never shows.
  CHECK(family_rows(*list, true) ==
        (all_rows.contains(QStringLiteral("Arial")) ? QStringList{QStringLiteral("Arial")} : QStringList{}));
  if (all_rows.contains(QStringLiteral("Times New Roman"))) {
    CHECK(!sans_rows.contains(QStringLiteral("Times New Roman")));
  }
  // A second pass reads nothing: every family is cached.
  style->setCurrentIndex(style->findData(QStringLiteral("serif")));
  QApplication::processEvents();
  CHECK(!status->isVisible());
  if (all_rows.contains(QStringLiteral("Times New Roman"))) {
    CHECK(family_rows(*list, false).contains(QStringLiteral("Times New Roman")));
  }

  // Language: only families that claim the writing system.
  style->setCurrentIndex(0);
  const auto latin = language->findData(QStringLiteral("Latin"));
  CHECK(latin > 0);
  language->setCurrentIndex(latin);
  QApplication::processEvents();
  const auto latin_families =
      patchy::ui::font_families_without_foundries(QFontDatabase::families(QFontDatabase::Latin));
  const auto latin_rows = family_rows(*list, false);
  CHECK(!latin_rows.isEmpty());
  for (const auto& family : latin_rows) {
    CHECK(latin_families.contains(family));
  }

  // Both choices persist across reopening.
  style->setCurrentIndex(style->findData(QStringLiteral("sans-serif")));
  QApplication::processEvents();
  save_widget_artifact("issue570_font_picker_filters", *popup);
  popup->close();
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kFontPickerStyleFilterKey)).toString() ==
        QStringLiteral("sans-serif"));
  CHECK(patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kFontPickerLanguageFilterKey)).toString() ==
        QStringLiteral("Latin"));
  auto* reopened = open_picker_popup(*picker);
  CHECK(reopened != nullptr);
  auto* reopened_style = reopened->findChild<QComboBox*>(QStringLiteral("textFontPickerStyleCombo"));
  auto* reopened_language = reopened->findChild<QComboBox*>(QStringLiteral("textFontPickerLanguageCombo"));
  CHECK(reopened_style->currentData().toString() == QStringLiteral("sans-serif"));
  CHECK(reopened_language->currentData().toString() == QStringLiteral("Latin"));
  reopened->close();
  QApplication::processEvents();

  // The style answers are written to the cache file.
  index.save();
  CHECK(QFileInfo::exists(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                              .filePath(QStringLiteral("font-style-cache.txt"))));
  index.clear();
  reset_picker_settings();
}

// The popup over its open filter list, composed like the screen shows it (offscreen, the list
// is its own top-level window).
QImage grab_popup_with_open_list(QWidget& popup, QComboBox& combo) {
  auto* list_window = combo.view()->window();
  const auto origin = popup.mapToGlobal(QPoint(0, 0));
  const auto list_origin = list_window->mapToGlobal(QPoint(0, 0)) - origin;
  const QRect bounds = QRect(QPoint(0, 0), popup.size()).united(QRect(list_origin, list_window->size()));
  QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.drawPixmap(-bounds.topLeft(), popup.grab());
  painter.drawPixmap(list_origin - bounds.topLeft(), list_window->grab());
  painter.end();
  return image;
}

// GitHub issue 569 follow-up: the rule above "All Fonts" uses field_border, which stands off the
// list background in both schemes (field_inset_border sat a shade off it in Dark and vanished).
void ui_font_picker_recent_separator_reads_in_both_schemes() {
  reset_picker_settings();
  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const auto& palette = patchy::ui::theme(scheme);
    const auto contrast = [&palette](const QColor& rule) {
      return std::abs(qGray(rule.rgb()) - qGray(palette.popup_list_bg.rgb()));
    };
    std::cout << "  note: rule contrast " << contrast(palette.field_border) << " (was "
              << contrast(palette.field_inset_border) << ")\n";
    CHECK(contrast(palette.field_border) >= 35);
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  const auto families = listed_families(*picker);
  if (families.size() < 2) {
    patchy::test::skip("fewer than two font families on this machine");
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey),
                      QStringList{families.at(1), families.at(0)});
  }
  {
    // A current family in the Recent section opens the list at the top, rule in view.
    const QSignalBlocker blocker(picker);
    picker->setCurrentFont(QFont(families.at(1)));
  }
  const auto capture = [picker](const char* name) {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    CHECK(list != nullptr);
    CHECK(row_kind(list->model()->index(3, 0)) == FontPickerRowKind::all_header);
    // The rule is drawn in field_border near the top of the All Fonts row (searched over a few
    // rows: the device pixel ratio and the style's row origin differ per platform).
    const auto row = list->visualRect(list->model()->index(3, 0));
    const auto image = list->viewport()->grab().toImage();
    const auto ratio = image.devicePixelRatio();
    const auto rule = patchy::ui::theme().field_border;
    bool found_rule = false;
    const int x = static_cast<int>(row.center().x() / 2 * ratio);
    for (int y = static_cast<int>(row.top() * ratio); y <= static_cast<int>((row.top() + 5) * ratio); ++y) {
      if (x < image.width() && y >= 0 && y < image.height()) {
        const auto pixel = image.pixelColor(x, y);
        found_rule = found_rule || (std::abs(pixel.red() - rule.red()) <= 6 && std::abs(pixel.green() - rule.green()) <= 6 &&
                                    std::abs(pixel.blue() - rule.blue()) <= 6);
      }
    }
    save_widget_artifact(name, *popup);
    CHECK(found_rule);
    popup->close();
    QApplication::processEvents();
  };
  capture("issue569_separator_dark");
  {
    ColorSchemeRestorer light(patchy::ui::ColorSchemePreference::Light);
    capture("issue569_separator_light");
  }
  reset_picker_settings();
}

// GitHub issue 570 follow-up: both filter lists scroll at a fixed height, Enter in an open list
// only picks that entry (the font popup stays open), and Style offers Other for fonts whose
// files state no style.
void ui_font_picker_filter_lists_scroll_keep_enter_and_offer_other() {
  const StandardPathsTestMode standard_paths_test_mode;  // the style cache goes to a test folder
  patchy::ui::FontStyleIndex::instance().clear();
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* style = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerStyleCombo"));
  auto* language = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerLanguageCombo"));
  auto* status = popup->findChild<QLabel*>(QStringLiteral("textFontPickerStatusLabel"));
  CHECK(list != nullptr && style != nullptr && language != nullptr && status != nullptr);
  for (auto* combo : {style, language}) {
    CHECK(combo->maxVisibleItems() == patchy::ui::kLongListPopupRows);
    // The sheet hint that makes every style honour maxVisibleItems.
    CHECK(combo->styleSheet().contains(QStringLiteral("combobox-popup: 0")));
  }
  // Other is appended: the stored tokens of the older entries keep their places.
  CHECK(style->count() == 10);
  CHECK(style->itemData(9).toString() == QStringLiteral("other"));
  CHECK(style->itemData(8).toString() == QStringLiteral("variable"));

  const auto press_enter_in_open_list = [popup](QComboBox& combo, int row) {
    combo.showPopup();
    QApplication::processEvents();
    auto* view = combo.view();
    CHECK(view->isVisible());
    view->setCurrentIndex(view->model()->index(row, 0));
    auto* target = QApplication::focusWidget() != nullptr ? QApplication::focusWidget() : view;
    QTest::keyClick(target, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!view->isVisible());
    CHECK(popup->isVisible());  // the font popup stays open
    CHECK(combo.currentIndex() == row);
  };
  // The first entry (already current) and another one, in both lists.
  press_enter_in_open_list(*style, 0);
  press_enter_in_open_list(*language, 0);
  CHECK(language->count() > 2);
  press_enter_in_open_list(*language, 2);
  // Enter sent straight to the list view behaves the same.
  language->showPopup();
  QApplication::processEvents();
  language->view()->setCurrentIndex(language->view()->model()->index(0, 0));
  QTest::keyClick(language->view(), Qt::Key_Enter);
  QApplication::processEvents();
  CHECK(popup->isVisible());
  CHECK(language->currentIndex() == 0);

  // The Language list open over the popup, scrolling, with Other in the Style box.
  style->setCurrentIndex(style->findData(QStringLiteral("other")));
  CHECK(wait_for_style_reading(*popup, *status, 120000));
  language->showPopup();
  QApplication::processEvents();
  if (language->count() > patchy::ui::kLongListPopupRows) {
    CHECK(language->view()->verticalScrollBar()->maximum() > 0);  // scrolls, not screen-tall
  }
  save_image_artifact("issue570_language_list_and_other", grab_popup_with_open_list(*popup, *language));
  language->hidePopup();
  QApplication::processEvents();

  // Other lists exactly the families with no usable style class.
  auto& index = patchy::ui::FontStyleIndex::instance();
  const auto other_rows = family_rows(*list, false);
  for (const auto& family : other_rows) {
    const auto traits = index.cached(family);
    CHECK(traits.has_value() && traits->category == patchy::FontStyleCategory::unknown);
  }
  for (const auto& family : listed_families(*picker)) {
    const auto traits = index.cached(family);
    if (traits.has_value() && traits->category == patchy::FontStyleCategory::unknown) {
      CHECK(other_rows.contains(family));
    }
  }
  std::cout << "  note: " << other_rows.size() << " of " << picker->count() << " families under Other\n";
  popup->close();
  QApplication::processEvents();
  index.clear();
  reset_picker_settings();
}

bool popup_visible() {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QString::fromLatin1(patchy::ui::kFontPickerPopupObjectName) && widget->isVisible()) {
      return true;
    }
  }
  return false;
}

// The model row of `family` in the given section, or -1.
int family_row(const QListView& list, const QString& family, bool recent) {
  auto* model = list.model();
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto index = model->index(row, 0);
    if (row_kind(index) == FontPickerRowKind::family && index.data(Qt::DisplayRole).toString() == family &&
        index.data(patchy::ui::kFontPickerRecentRowRole).toBool() == recent) {
      return row;
    }
  }
  return -1;
}

// Hovers `position` in the list's viewport the way a real mouse does: the view's hover row (which
// shows the row buttons) and the mouse move the row buttons track.
void hover_list(QListView& list, const QPoint& position) {
  auto* viewport = list.viewport();
  const QPointF global = viewport->mapToGlobal(QPointF(position));
  QHoverEvent hover(QEvent::HoverMove, QPointF(position), global, QPointF(-1, -1));
  QApplication::sendEvent(viewport, &hover);
  QMouseEvent move(QEvent::MouseMove, QPointF(position), global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(viewport, &move);
  QApplication::processEvents();
}

QPoint row_button_center(const QListView& list, int row, FontPickerRowButton button) {
  const auto index = list.model()->index(row, 0);
  return patchy::ui::font_picker_row_button_rect(list.visualRect(index), button,
                                                 index.data(patchy::ui::kFontPickerRecentRowRole).toBool())
      .center();
}

void click_row_button(QListView& list, int row, FontPickerRowButton button) {
  const auto at = row_button_center(list, row, button);
  hover_list(list, at);
  QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier, at);
  QApplication::processEvents();  // the action runs queued
  QApplication::processEvents();
}

// GitHub issue 630: the star at the right of a row toggles a favorite without picking the font or
// closing the popup; a click elsewhere on the row still picks it.
void ui_font_picker_favorite_star_toggles_without_picking() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  const auto families = listed_families(*picker);
  if (families.size() < 3) {
    patchy::test::skip("fewer than three font families on this machine");
  }
  const auto current = families.at(0);
  {
    const QSignalBlocker blocker(picker);
    picker->setCurrentFont(QFont(current));
  }
  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  CHECK(list != nullptr);
  list->scrollToTop();
  QApplication::processEvents();
  const auto starred = list->model()->index(1, 0).data(Qt::DisplayRole).toString();
  CHECK(family_row(*list, starred, false) == 1);
  CHECK(!list->model()->index(1, 0).data(patchy::ui::kFontPickerFavoriteRole).toBool());

  click_row_button(*list, 1, FontPickerRowButton::favorite);
  CHECK(popup->isVisible());
  CHECK(picker->currentText() == current);
  CHECK(list->model()->index(1, 0).data(patchy::ui::kFontPickerFavoriteRole).toBool());
  CHECK(patchy::ui::stored_favorite_font_families() == QStringList{starred});
  // The star shows on the hovered row; the favorite's stays filled.
  hover_list(*list, row_button_center(*list, 2, FontPickerRowButton::favorite));
  save_widget_artifact("issue630_font_picker_stars", *popup);
  {
    // The star art and its theme roles in Light too.
    ColorSchemeRestorer light(patchy::ui::ColorSchemePreference::Light);
    QApplication::processEvents();
    CHECK(popup->isVisible());
    hover_list(*list, row_button_center(*list, 2, FontPickerRowButton::favorite));
    save_widget_artifact("issue630_font_picker_stars_light", *popup);
  }
  QApplication::processEvents();

  // A double click on a star toggles it twice, like any toggle button, and picks nothing.
  const auto at = row_button_center(*list, 2, FontPickerRowButton::favorite);
  QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(popup->isVisible());
  CHECK(picker->currentText() == current);

  // Clicking the star again removes the favorite.
  click_row_button(*list, 1, FontPickerRowButton::favorite);
  CHECK(!list->model()->index(1, 0).data(patchy::ui::kFontPickerFavoriteRole).toBool());
  CHECK(patchy::ui::stored_favorite_font_families().isEmpty());
  click_row_button(*list, 1, FontPickerRowButton::favorite);
  CHECK(patchy::ui::stored_favorite_font_families() == QStringList{starred});

  // The row outside the star still picks the font and closes the popup.
  const auto row_rect = list->visualRect(list->model()->index(1, 0));
  const QPoint name_point(row_rect.left() + 12, row_rect.center().y());
  hover_list(*list, name_point);
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, name_point);
  QApplication::processEvents();
  CHECK(picker->currentText() == starred);
  CHECK(!popup_visible());  // the pick closed the popup
  reset_picker_settings();
}

// GitHub issue 630: the star toggle beside the filters shows only the Favorites section; the
// search and the Style and Language filters still apply inside it; it and the favorites persist.
void ui_font_picker_favorites_view_filters_and_persists() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  QString mono;
  QString proportional;
  for (const auto& family : listed_families(*picker)) {
    if (!mono.isEmpty() && !proportional.isEmpty()) {
      break;
    }
    auto& slot = QFontDatabase::isFixedPitch(family) ? mono : proportional;
    if (slot.isEmpty()) {
      slot = family;
    }
  }
  if (mono.isEmpty() || proportional.isEmpty()) {
    patchy::test::skip("needs a monospace and a proportional font family");
  }
  const auto missing = QStringLiteral("Patchy Missing Favorite Font 630");
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerFavoriteFamiliesKey),
                      QStringList{mono, missing, proportional});
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey), QStringList{proportional});
  }

  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* style = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerStyleCombo"));
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  auto* favorites = popup->findChild<QToolButton*>(QStringLiteral("textFontPickerFavoritesButton"));
  CHECK(list != nullptr && style != nullptr && search != nullptr && favorites != nullptr);
  CHECK(favorites->isCheckable() && !favorites->isChecked());
  CHECK(favorites->focusPolicy() == Qt::NoFocus);
  CHECK(!favorites->toolTip().isEmpty());
  CHECK(row_kind(list->model()->index(0, 0)) == FontPickerRowKind::recent_header);

  favorites->click();
  QApplication::processEvents();
  auto* model = list->model();
  CHECK(row_kind(model->index(0, 0)) == FontPickerRowKind::favorites_header);
  CHECK(header_rows(*list) == 1);  // no Recent or All Fonts section
  CHECK(family_rows(*list, true).isEmpty());
  auto expected = QStringList{mono, proportional};
  expected.sort(Qt::CaseInsensitive);
  auto shown = family_rows(*list, false);
  shown.sort(Qt::CaseInsensitive);
  CHECK(shown == expected);  // the missing family stays stored but never shows
  for (int row = 1; row < model->rowCount(); ++row) {
    CHECK(model->index(row, 0).data(patchy::ui::kFontPickerFavoriteRole).toBool());
  }
  CHECK(row_kind(list->currentIndex()) == FontPickerRowKind::family);
  save_widget_artifact("issue630_font_picker_favorites_view", *popup);

  // The Style filter applies inside Favorites.
  style->setCurrentIndex(style->findData(QStringLiteral("monospace")));
  QApplication::processEvents();
  CHECK(family_rows(*list, false) == QStringList{mono});
  // So does the search; nothing passing shows the empty note.
  QTest::keyClicks(search, QStringLiteral("zzzz-no-such-font"));
  QApplication::processEvents();
  CHECK(model->rowCount() == 2);
  CHECK(row_kind(model->index(1, 0)) == FontPickerRowKind::empty_note);
  CHECK((model->flags(model->index(1, 0)) & Qt::ItemIsSelectable) == 0);
  // Enter with nothing to pick keeps the popup open and the font unchanged.
  const auto before_enter = picker->currentText();
  QTest::keyClick(search, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(popup->isVisible());
  CHECK(picker->currentText() == before_enter);
  search->clear();
  style->setCurrentIndex(0);
  QApplication::processEvents();

  // Unstarring in the Favorites view takes the row out at once; the popup stays open.
  const int mono_row = family_row(*list, mono, false);
  CHECK(mono_row > 0);
  click_row_button(*list, mono_row, FontPickerRowButton::favorite);
  CHECK(popup->isVisible());
  CHECK(family_rows(*list, false) == QStringList{proportional});
  CHECK(patchy::ui::stored_favorite_font_families() == (QStringList{missing, proportional}));

  // The toggle persists across reopening.
  popup->close();
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kFontPickerFavoritesOnlyKey)).toBool());
  auto* reopened = open_picker_popup(*picker);
  CHECK(reopened != nullptr);
  auto* reopened_list = reopened->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* reopened_favorites = reopened->findChild<QToolButton*>(QStringLiteral("textFontPickerFavoritesButton"));
  CHECK(reopened_list != nullptr && reopened_favorites != nullptr);
  CHECK(reopened_favorites->isChecked());
  CHECK(row_kind(reopened_list->model()->index(0, 0)) == FontPickerRowKind::favorites_header);
  CHECK(family_rows(*reopened_list, false) == QStringList{proportional});

  // No favorites at all: the note says how to add one.
  click_row_button(*reopened_list, family_row(*reopened_list, proportional, false), FontPickerRowButton::favorite);
  CHECK(reopened_list->model()->rowCount() == 2);
  CHECK(row_kind(reopened_list->model()->index(1, 0)) == FontPickerRowKind::empty_note);
  save_widget_artifact("issue630_font_picker_favorites_empty", *reopened);

  // Off again: Recent and All Fonts are back.
  reopened_favorites->click();
  QApplication::processEvents();
  CHECK(row_kind(reopened_list->model()->index(0, 0)) == FontPickerRowKind::recent_header);
  CHECK(family_rows(*reopened_list, false).size() == listed_families(*picker).size());
  CHECK(!patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kFontPickerFavoritesOnlyKey)).toBool());
  reopened->close();
  QApplication::processEvents();
  reset_picker_settings();
}

// GitHub issue 569 follow-up: the x on a Recent row takes the font out of the saved Recent list
// without picking it or closing the popup; the section hides once it is empty.
void ui_font_picker_recent_remove_button_keeps_popup_open() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);
  const auto families = listed_families(*picker);
  if (families.size() < 3) {
    patchy::test::skip("fewer than three font families on this machine");
  }
  const auto first = families.at(1);
  const auto second = families.at(2);
  const auto current = families.at(0);
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey), QStringList{first, second});
  }
  {
    const QSignalBlocker blocker(picker);
    picker->setCurrentFont(QFont(current));
  }
  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  CHECK(list != nullptr);
  list->scrollToTop();
  QApplication::processEvents();
  CHECK(family_rows(*list, true) == (QStringList{first, second}));
  // Only Recent rows have the remove button.
  const int all_row = family_row(*list, first, false);
  CHECK(all_row > 0);
  CHECK(patchy::ui::font_picker_row_button_rect(list->visualRect(list->model()->index(all_row, 0)),
                                                FontPickerRowButton::remove_recent, false)
            .isEmpty());
  // The x and the star sit side by side inside the row.
  const auto recent_rect = list->visualRect(list->model()->index(1, 0));
  const auto x_rect =
      patchy::ui::font_picker_row_button_rect(recent_rect, FontPickerRowButton::remove_recent, true);
  const auto star_rect = patchy::ui::font_picker_row_button_rect(recent_rect, FontPickerRowButton::favorite, true);
  CHECK(recent_rect.contains(x_rect) && recent_rect.contains(star_rect));
  CHECK(!x_rect.intersects(star_rect) && star_rect.right() < x_rect.left());
  hover_list(*list, x_rect.center());
  save_widget_artifact("issue569_font_picker_recent_remove", *popup);

  click_row_button(*list, 1, FontPickerRowButton::remove_recent);
  CHECK(popup->isVisible());
  CHECK(picker->currentText() == current);
  CHECK(patchy::ui::stored_recent_font_families() == QStringList{second});
  CHECK(family_rows(*list, true) == QStringList{second});
  CHECK(family_rows(*list, false).contains(first));  // still in All Fonts

  click_row_button(*list, 1, FontPickerRowButton::remove_recent);
  CHECK(popup->isVisible());
  CHECK(patchy::ui::stored_recent_font_families().isEmpty());
  CHECK(header_rows(*list) == 0);  // the section is gone
  // Keyboard navigation still works.
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  CHECK(search != nullptr);
  QTest::keyClick(search, Qt::Key_Down);
  CHECK(row_kind(list->currentIndex()) == FontPickerRowKind::family);
  popup->close();
  QApplication::processEvents();
  reset_picker_settings();
}

// GitHub issue 647: while searching, a line under the list counts the matches the Style or
// Language filter hides; clicking it sets both filters to All.
void ui_font_picker_hidden_matches_hint_counts_and_resets() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = type_tool_picker(window);
  CHECK(picker != nullptr);

  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* style = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerStyleCombo"));
  auto* language = popup->findChild<QComboBox*>(QStringLiteral("textFontPickerLanguageCombo"));
  auto* hint = popup->findChild<QToolButton*>(QStringLiteral("textFontPickerHiddenMatchesButton"));
  CHECK(search != nullptr && list != nullptr && style != nullptr && language != nullptr && hint != nullptr);
  CHECK(hint->isHidden());
  CHECK(hint->focusPolicy() == Qt::NoFocus);

  // Monospace reads no font tables, so the expected count is known up front.
  auto& index = patchy::ui::FontStyleIndex::instance();
  const auto families = listed_families(*picker);
  const auto hidden_by_monospace = [&](const QString& text) {
    int hidden = 0;
    for (const auto& family : families) {
      if (family.contains(text, Qt::CaseInsensitive) &&
          !index.matches(patchy::ui::FontStyleFilter::monospace, family, {})) {
        ++hidden;
      }
    }
    return hidden;
  };
  // A family that is not monospace: searching its whole name finds at least that one.
  QString proportional;
  for (const auto& family : families) {
    if (!index.matches(patchy::ui::FontStyleFilter::monospace, family, {})) {
      proportional = family;
      break;
    }
  }
  if (proportional.isEmpty()) {
    patchy::test::skip("no proportional font family on this machine");
  }

  // All Styles hides nothing, so no hint.
  QTest::keyClicks(search, proportional);
  QApplication::processEvents();
  CHECK(hint->isHidden());
  CHECK(family_rows(*list, false).contains(proportional));

  // Monospace hides it: the hint counts every hidden match.
  style->setCurrentIndex(style->findData(QStringLiteral("monospace")));
  QApplication::processEvents();
  CHECK(!family_rows(*list, false).contains(proportional));
  const int expected = hidden_by_monospace(proportional);
  CHECK(expected >= 1);
  CHECK(!hint->isHidden());
  CHECK(hint->text().contains(QString::number(expected)));
  for (const auto& family : families) {
    std::cout << "  note: family \"" << family.toStdString() << "\" monospace "
              << index.matches(patchy::ui::FontStyleFilter::monospace, family, {}) << "\n";
  }
  std::cout << "  note: \"" << proportional.toStdString() << "\" hint: " << hint->text().toStdString() << "\n";

  // No search, no hint, even with a filter on.
  search->clear();
  QApplication::processEvents();
  CHECK(hint->isHidden());
  // A search nothing matches: no hint.
  QTest::keyClicks(search, QStringLiteral("zz no such font 647"));
  QApplication::processEvents();
  CHECK(hint->isHidden());

  // A short search, as in the report, for the screenshot: the first letter that both lists
  // monospace families and hides others.
  QString letter;
  for (char c = 'a'; c <= 'z' && letter.isEmpty(); ++c) {
    const auto candidate = QString(QLatin1Char(c));
    int shown = 0;
    for (const auto& family : families) {
      shown += family.contains(candidate, Qt::CaseInsensitive) &&
                       index.matches(patchy::ui::FontStyleFilter::monospace, family, {})
                   ? 1
                   : 0;
    }
    if (shown > 0 && hidden_by_monospace(candidate) > 0) {
      letter = candidate;
    }
  }
  if (!letter.isEmpty()) {
    search->clear();
    QTest::keyClicks(search, letter);
    QApplication::processEvents();
    CHECK(!family_rows(*list, false).isEmpty());
    CHECK(!hint->isHidden());
    CHECK(hint->text().contains(QString::number(hidden_by_monospace(letter))));
    save_widget_artifact("issue647_font_picker_hidden_matches", *popup);
  } else {
    std::cout << "  note: no search letter both lists and hides fonts here; no screenshot\n";
  }

  // Clicking the hint sets Style and Language to All and lists the hidden matches.
  search->clear();
  QTest::keyClicks(search, proportional);
  const auto latin = language->findData(QStringLiteral("Latin"));
  if (latin > 0) {
    language->setCurrentIndex(latin);
  }
  QApplication::processEvents();
  CHECK(!hint->isHidden());
  QTest::mouseClick(hint, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(style->currentData().toString() == QStringLiteral("all"));
  CHECK(language->currentData().toString() == QStringLiteral("all"));
  CHECK(hint->isHidden());
  CHECK(family_rows(*list, false).contains(proportional));
  CHECK(search->text() == proportional);
  CHECK(popup->isVisible());
  popup->close();
  QApplication::processEvents();
  reset_picker_settings();
}

// Qt's Linux font database lists a family two foundries install as "Arial [TMC ]" and
// "Arial [Mono]" (GitHub issue 646 follow-up); the helper the picker, the family lookup and the
// stored Recent and Favorites lists share drops that suffix. Synthetic names: no Linux fonts needed.
void ui_font_family_without_foundry_strips_qt_suffix() {
  using patchy::ui::font_family_without_foundry;
  CHECK(font_family_without_foundry(QStringLiteral("Arial [TMC ]")) == QStringLiteral("Arial"));
  CHECK(font_family_without_foundry(QStringLiteral("Arial Black [MONO]")) == QStringLiteral("Arial Black"));
  CHECK(font_family_without_foundry(QStringLiteral("C059 [UKWN]")) == QStringLiteral("C059"));
  CHECK(font_family_without_foundry(QStringLiteral("Nimbus Mono PS [urw]")) == QStringLiteral("Nimbus Mono PS"));
  CHECK(font_family_without_foundry(QStringLiteral("Roboto Mono [GOOG] ")) == QStringLiteral("Roboto Mono"));
  // Only a trailing bracketed part after a space is a foundry.
  CHECK(font_family_without_foundry(QStringLiteral("Foo [Bar] Baz")) == QStringLiteral("Foo [Bar] Baz"));
  CHECK(font_family_without_foundry(QStringLiteral("[Foo]")) == QStringLiteral("[Foo]"));
  CHECK(font_family_without_foundry(QStringLiteral("Foo[Bar]")) == QStringLiteral("Foo[Bar]"));
  CHECK(font_family_without_foundry(QStringLiteral("Arial")) == QStringLiteral("Arial"));
  CHECK(font_family_without_foundry(QString()).isEmpty());
  // A family whose own name ends in brackets loses them, as in Qt's own matching.
  CHECK(font_family_without_foundry(QStringLiteral("Foo [Bar]")) == QStringLiteral("Foo"));

  const QStringList listed{QStringLiteral("Arial [TMC ]"), QStringLiteral("Arial [Mono]"),
                           QStringLiteral("Arial Black [TMC ]"), QStringLiteral("Arial Black [MONO]"),
                           QStringLiteral("C059 [UKWN]"), QStringLiteral("C059 [urw]"), QStringLiteral("Courier New"),
                           QString(), QStringLiteral("courier new")};
  CHECK(patchy::ui::font_families_without_foundries(listed) ==
        (QStringList{QStringLiteral("Arial"), QStringLiteral("Arial Black"), QStringLiteral("C059"),
                     QStringLiteral("Courier New")}));
}

// The picker lists a family several foundries install once, under its plain name; picking it
// reports the plain family; Recent and Favorites saved with a suffix read back plain; and a
// document's suffixed family resolves to the installed plain one.
void ui_font_picker_lists_plain_family_names() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  QWidget host;
  auto* picker = new patchy::ui::FontPickerCombo(&host);
  host.resize(300, 60);
  host.show();
  QApplication::processEvents();
  const auto families = listed_families(*picker);
  if (families.size() < 2) {
    patchy::test::skip("fewer than two font families on this machine");
  }
  const auto first = families.at(0);
  const auto second = families.at(1);
  CHECK(!first.isEmpty() && !second.isEmpty() && first != second);

  // The database as Qt's Linux FreeType database lists it when two foundries install `first`.
  auto* model = qobject_cast<QStringListModel*>(picker->model());
  CHECK(model != nullptr);
  model->setStringList({first + QStringLiteral(" [TMC ]"), first + QStringLiteral(" [Mono]"), second});
  CHECK(picker->itemText(0) == first);
  CHECK(picker->itemText(1) == first);
  CHECK(picker->findText(first, Qt::MatchExactly | Qt::MatchCaseSensitive) == 0);
  // The second copy is disabled: the closed control's arrow keys and wheel step over it.
  CHECK(model->flags(model->index(0, 0)).testFlag(Qt::ItemIsEnabled));
  CHECK(!model->flags(model->index(1, 0)).testFlag(Qt::ItemIsEnabled));
  CHECK(model->flags(model->index(2, 0)).testFlag(Qt::ItemIsEnabled));

  {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    CHECK(list != nullptr);
    CHECK(family_rows(*list, false) == (QStringList{first, second}));
    // Picking the plain row reports the plain family.
    for (int row = 0; row < list->model()->rowCount(); ++row) {
      if (list->model()->index(row, 0).data(Qt::DisplayRole).toString() == first) {
        list->setCurrentIndex(list->model()->index(row, 0));
      }
    }
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
    CHECK(search != nullptr);
    QTest::keyClick(search, Qt::Key_Return);
    QApplication::processEvents();
  }
  CHECK(picker->currentText() == first);
  CHECK(picker->currentFont().families().value(0) == first);
  CHECK(patchy::ui::stored_recent_font_families() == QStringList{first});

  // Lists saved with Qt's suffix (before the fix) map to the plain names, once each.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey),
                      QStringList{second + QStringLiteral(" [urw]"), first + QStringLiteral(" [TMC ]"), first});
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerFavoriteFamiliesKey),
                      QStringList{first + QStringLiteral(" [Mono]"), first + QStringLiteral(" [TMC ]")});
    settings.sync();
  }
  CHECK(patchy::ui::stored_recent_font_families() == (QStringList{second, first}));
  CHECK(patchy::ui::stored_favorite_font_families() == QStringList{first});
  patchy::ui::remove_recent_font_family(second + QStringLiteral(" [urw]"));
  CHECK(patchy::ui::stored_recent_font_families() == QStringList{first});
  CHECK(!patchy::ui::set_favorite_font_family(first + QStringLiteral(" [TMC ]"), false));
  CHECK(patchy::ui::stored_favorite_font_families().isEmpty());

  // A document or preset that stored the suffixed name resolves to the installed family.
  CHECK(patchy::ui::available_text_family_match(first + QStringLiteral(" [TMC ]")) == std::optional<QString>(first));
  CHECK(patchy::ui::canonical_text_display_family(first + QStringLiteral(" [TMC ]")) == first);
  reset_picker_settings();
}

// The same on this machine's real font database, where it lists a family two foundries install
// with Qt's suffix (Linux: "C059 [UKWN]", "C059 [urw]"). Saves the popup for review as
// test-artifacts/font_picker_foundry_duplicates.png.
void ui_font_picker_foundry_duplicates_review_capture() {
  QStringList suffixed;
  for (const auto& family : QFontDatabase::families()) {
    if (patchy::ui::font_family_without_foundry(family) != family) {
      suffixed.append(family);
    }
  }
  if (suffixed.isEmpty()) {
    patchy::test::skip("no font family on this machine is installed from two foundries");
  }
  reset_picker_settings();
  QWidget host;
  auto* picker = new patchy::ui::FontPickerCombo(&host);
  host.resize(300, 60);
  host.show();
  QApplication::processEvents();
  for (int row = 0; row < picker->count(); ++row) {
    CHECK(patchy::ui::font_family_without_foundry(picker->itemText(row)) == picker->itemText(row));
  }
  QString search_text = patchy::ui::font_family_without_foundry(suffixed.front()).section(QLatin1Char(' '), 0, 0);
  for (const auto& family : suffixed) {
    if (family.startsWith(QStringLiteral("Nimbus"))) {
      search_text = QStringLiteral("Nimbus");
      break;
    }
  }
  auto* popup = open_picker_popup(*picker);
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  CHECK(list != nullptr && search != nullptr);
  QTest::keyClicks(search, search_text);
  QApplication::processEvents();
  const auto rows = family_rows(*list, false);
  CHECK(!rows.isEmpty());
  CHECK(rows.size() == QSet<QString>(rows.cbegin(), rows.cend()).size());
  for (const auto& family : rows) {
    CHECK(!family.contains(QLatin1Char('[')));
  }
  QTest::qWait(50);
  std::filesystem::create_directories("test-artifacts");
  CHECK(popup->grab().save(QStringLiteral("test-artifacts/font_picker_foundry_duplicates.png")));
  popup->close();
  QApplication::processEvents();
  reset_picker_settings();
}

// "Show font names in English" (GitHub issue 691). Turns the option off again however a test
// ends: the shared name table is cached per process.
struct EnglishFontNamesOff {
  EnglishFontNamesOff() { patchy::ui::set_stored_show_font_names_in_english(false); }
  ~EnglishFontNamesOff() {
    patchy::ui::set_stored_show_font_names_in_english(false);
    auto settings = patchy::ui::app_settings();
    settings.remove(QString::fromLatin1(patchy::ui::kShowFontNamesInEnglishKey));
    settings.sync();
  }
};

struct NameRecord {
  quint16 platform;
  quint16 encoding;
  quint16 language;
  quint16 name_id;
  QString text;
};

// An OpenType `name` table holding `records` (Macintosh strings as Latin-1, the rest UTF-16BE).
QByteArray name_table_with(const std::vector<NameRecord>& records) {
  QByteArray strings;
  QByteArray table;
  const auto put_u16 = [](QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value >> 8));
    out.append(static_cast<char>(value & 0xff));
  };
  put_u16(table, 0);
  put_u16(table, static_cast<quint16>(records.size()));
  put_u16(table, static_cast<quint16>(6 + 12 * records.size()));
  for (const auto& record : records) {
    QByteArray encoded;
    if (record.platform == 1) {
      encoded = record.text.toLatin1();
    } else {
      for (const auto ch : record.text) {
        put_u16(encoded, ch.unicode());
      }
    }
    put_u16(table, record.platform);
    put_u16(table, record.encoding);
    put_u16(table, record.language);
    put_u16(table, record.name_id);
    put_u16(table, static_cast<quint16>(encoded.size()));
    put_u16(table, static_cast<quint16>(strings.size()));
    strings.append(encoded);
  }
  return table + strings;
}

// The English name a face's own name table gives for the family the database lists: the record
// kind the family was listed under answers first, native names filed as English are passed over,
// and the PostScript name is the last resort.
void ui_font_english_name_comes_from_the_name_table() {
  using patchy::ui::english_family_name_from_name_table;
  using patchy::ui::font_name_is_latin;
  const auto yahei = QStringLiteral("\u5fae\u8f6f\u96c5\u9ed1");                    // Microsoft YaHei, zh-CN
  const auto yu_gothic = QStringLiteral("\u6e38\u30b4\u30b7\u30c3\u30af");          // Yu Gothic, ja
  const auto yu_gothic_medium = yu_gothic + QStringLiteral(" Medium");
  const auto lanting = QStringLiteral("\u65b9\u6b63\u5170\u4ead\u9ed1");
  const auto hiragino = QStringLiteral("\u30d2\u30e9\u30ae\u30ce\u89d2\u30b4\u30b7\u30c3\u30af");

  CHECK(font_name_is_latin(QString()));
  CHECK(font_name_is_latin(QStringLiteral("Arial")));
  CHECK(font_name_is_latin(QStringLiteral("Noto Sans CJK JP")));
  CHECK(font_name_is_latin(QStringLiteral("\u010ce\u0161tina Sans 2.0")));
  CHECK(!font_name_is_latin(yahei));
  CHECK(!font_name_is_latin(yu_gothic_medium));
  CHECK(!font_name_is_latin(QStringLiteral("\u0420\u0443\u0441\u0441\u043a\u0438\u0439 Sans")));

  // The Windows en-US family record.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0804, 1, yahei},
            {3, 1, 0x0409, 1, QStringLiteral("Microsoft YaHei")},
            {3, 1, 0x0409, 6, QStringLiteral("MicrosoftYaHei")},
        }), yahei) == QStringLiteral("Microsoft YaHei"));

  // A family listed by its GDI family (id 1) keeps that level; the typographic family (id 16)
  // answers for a family listed by it.
  const auto yu_table = name_table_with({
      {3, 1, 0x0411, 1, yu_gothic_medium},
      {3, 1, 0x0409, 1, QStringLiteral("Yu Gothic Medium")},
      {3, 1, 0x0411, 16, yu_gothic},
      {3, 1, 0x0409, 16, QStringLiteral("Yu Gothic")},
      {3, 1, 0x0409, 6, QStringLiteral("YuGothic-Medium")},
  });
  CHECK(english_family_name_from_name_table(yu_table, yu_gothic_medium) == QStringLiteral("Yu Gothic Medium"));
  CHECK(english_family_name_from_name_table(yu_table, yu_gothic) == QStringLiteral("Yu Gothic"));

  // No family record in Latin script: the PostScript name.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0804, 1, lanting},
            {3, 1, 0x0409, 6, QStringLiteral("FZLTHK--GBK1-0")},
        }), lanting) == QStringLiteral("FZLTHK--GBK1-0"));

  // A native name filed under en-US is no English name.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0409, 1, lanting},
            {3, 1, 0x0804, 1, lanting},
            {3, 1, 0x0409, 6, QStringLiteral("FZLanTingHei")},
        }), lanting) == QStringLiteral("FZLanTingHei"));

  // A Latin name under another language, and a Macintosh English record.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0804, 1, lanting},
            {3, 1, 0x0407, 1, QStringLiteral("FZ LanTing Hei")},
        }), lanting) == QStringLiteral("FZ LanTing Hei"));
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0411, 1, hiragino},
            {1, 0, 0, 1, QStringLiteral("Hiragino Sans")},
        }), hiragino) == QStringLiteral("Hiragino Sans"));
  // A Macintosh record in a Japanese encoding is not read as Latin-1.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0411, 1, hiragino},
            {1, 1, 11, 1, QStringLiteral("\u00e3\u00e9\u00ee")},
        }), hiragino).isEmpty());

  // A Windows record in a legacy code page (PRC here) is not UTF-16 and is passed over.
  CHECK(english_family_name_from_name_table(name_table_with({
            {3, 1, 0x0804, 1, yahei},
            {3, 3, 0x0409, 1, QStringLiteral("Wrong Bytes")},
        }), yahei).isEmpty());

  // Nothing to offer: only the listed name, or no table at all.
  CHECK(english_family_name_from_name_table(name_table_with({{3, 1, 0x0804, 1, yahei}}), yahei).isEmpty());
  CHECK(english_family_name_from_name_table(QByteArray(), yahei).isEmpty());
  CHECK(english_family_name_from_name_table(QByteArray("\x00\x00\x00\x09", 4), yahei).isEmpty());
}

// The same on a real font: a Japanese system face lists its family in Japanese and in English.
void ui_font_english_name_reads_a_system_font_if_available() {
  register_test_fonts(TestFontRole::JapaneseGothic);
  for (const auto& family : QFontDatabase::families(QFontDatabase::Japanese)) {
    const auto raw = QRawFont::fromFont(QFont(family));
    if (!raw.isValid()) {
      continue;
    }
    const auto table = raw.fontTable("name");
    QString native;
    QString english;
    for (const auto& record : patchy::ui::opentype_name_records(table)) {
      if (record.platform != 3 || record.name_id != 1) {
        continue;
      }
      if (record.language == 0x0409) {
        english = record.text;
      } else if (!patchy::ui::font_name_is_latin(record.text)) {
        native = record.text;
      }
    }
    if (native.isEmpty() || english.isEmpty() || !patchy::ui::font_name_is_latin(english)) {
      continue;
    }
    CHECK(patchy::ui::english_family_name_from_name_table(table, native) == english);
    std::cout << "  note: " << family.toStdString() << " lists a native family name and \"" << english.toStdString()
              << "\"\n";
    return;
  }
  patchy::test::skip("no registered font carries a family name in two scripts");
}

// The table behind the option: a shown name leads back to exactly one listed family, a name
// another family goes by is not used, search reads both names, and the list is ordered by the
// names shown.
void ui_font_display_names_round_trip_to_the_listed_family() {
  const auto yahei = QStringLiteral("\u5fae\u8f6f\u96c5\u9ed1");
  const auto simsun = QStringLiteral("\u5b8b\u4f53");
  const auto simhei = QStringLiteral("\u9ed1\u4f53");
  const auto yu_medium = QStringLiteral("\u6e38\u30b4\u30b7\u30c3\u30af Medium");
  const QStringList families{QStringLiteral("Arial"), QStringLiteral("Tahoma"), QStringLiteral("Yu Gothic"),
                             simsun, yahei, yu_medium, simhei};
  const QHash<QString, QString> english{
      {QStringLiteral("Arial"), QStringLiteral("Arial")},  // its own name: nothing to show
      {yahei, QStringLiteral("Microsoft YaHei")},
      {simsun, QStringLiteral("SimSun")},
      {yu_medium, QStringLiteral("yu gothic")},             // a listed family already has that name
      {simhei, QStringLiteral("SIMSUN")},                   // another family is already shown as that
  };
  const auto names = patchy::ui::FontDisplayNames::build(
      families, [&english](const QString& family) { return english.value(family); });
  CHECK(!names.empty());
  CHECK(names.display_name(yahei) == QStringLiteral("Microsoft YaHei"));
  CHECK(names.display_name(simsun) == QStringLiteral("SimSun"));
  CHECK(names.display_name(QStringLiteral("Arial")) == QStringLiteral("Arial"));
  CHECK(names.display_name(yu_medium) == yu_medium);
  CHECK(names.display_name(simhei) == simhei);
  CHECK(names.display_name(QStringLiteral("Not Listed")) == QStringLiteral("Not Listed"));

  // Every shown name maps back to the family the database lists, whatever its case.
  for (const auto& family : families) {
    CHECK(names.family_for_display_name(names.display_name(family)) == family);
  }
  CHECK(names.family_for_display_name(QStringLiteral("microsoft  yahei ")) == yahei);
  CHECK(names.family_for_display_name(QStringLiteral("SIMSUN")) == simsun);
  CHECK(names.family_for_display_name(QStringLiteral("Yu Gothic")) == QStringLiteral("Yu Gothic"));
  CHECK(names.family_for_display_name(QStringLiteral("Unknown")) == QStringLiteral("Unknown"));

  CHECK(names.matches_search(yahei, QStringLiteral("yahei")));
  CHECK(names.matches_search(yahei, yahei.left(2)));
  CHECK(!names.matches_search(yahei, QStringLiteral("simsun")));
  CHECK(names.matches_search(QStringLiteral("Tahoma"), QStringLiteral("aho")));
  CHECK(!names.matches_search(simhei, QStringLiteral("simsun")));  // the name it did not get

  CHECK(names.sorted(families) == (QStringList{QStringLiteral("Arial"), yahei, simsun, QStringLiteral("Tahoma"),
                                               QStringLiteral("Yu Gothic"), yu_medium, simhei}));

  // Nothing to show: every answer is the family itself and the order is untouched.
  const patchy::ui::FontDisplayNames none;
  CHECK(none.empty());
  CHECK(none.display_name(yahei) == yahei);
  CHECK(none.family_for_display_name(QStringLiteral("Microsoft YaHei")) == QStringLiteral("Microsoft YaHei"));
  CHECK(none.sorted(families) == families);
  CHECK(!none.matches_search(yahei, QStringLiteral("yahei")));
}

// The picker with the option off and on: off changes nothing; on, the closed control, the row and
// the preview show the English name while currentFont(), Recent and the row's own data keep the
// listed family; search finds the family by either name; an English name typed into the closed
// control picks its family; and toggling refreshes an open control with no restart.
void ui_font_picker_shows_english_names_when_enabled() {
  register_test_fonts(TestFontRole::Verdana);
  reset_picker_settings();
  const EnglishFontNamesOff option_off;
  QWidget host;
  auto* picker = new patchy::ui::FontPickerCombo(&host);
  // Away from the pointer: a popup closed with the pointer over the combo swallows the next open.
  host.setGeometry(320, 240, 300, 60);
  host.show();
  QApplication::processEvents();
  const auto families = listed_families(*picker);
  if (families.size() < 3) {
    patchy::test::skip("fewer than three font families on this machine");
  }
  // Any listed family stands in for one the database names in another script.
  const auto family = families.at(0);
  const auto other = families.at(1);
  const auto english = QStringLiteral("Zz Patchy English 691");
  const patchy::ui::ScopedFontDisplayNameLookup lookup(
      [family, english](const QString& listed) { return listed == family ? english : QString(); });
  const int rows_before = picker->count();
  const auto shown_rows = [](const QListView& list) {
    QStringList out;
    for (int row = 0; row < list.model()->rowCount(); ++row) {
      const auto index = list.model()->index(row, 0);
      if (row_kind(index) == FontPickerRowKind::family) {
        out.append(index.data(patchy::ui::kFontPickerShownNameRole).toString());
      }
    }
    return out;
  };

  // Off (the default): names as the database lists them, and the English name finds nothing.
  CHECK(!patchy::ui::stored_show_font_names_in_english());
  picker->setCurrentFont(QFont(family));
  CHECK(picker->currentText() == family);
  {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
    CHECK(list != nullptr && search != nullptr);
    CHECK(family_rows(*list, false) == families);
    CHECK(shown_rows(*list) == families);
    QTest::keyClicks(search, QStringLiteral("patchy english"));
    QApplication::processEvents();
    CHECK(family_rows(*list, false).isEmpty());
    popup->close();
    QApplication::processEvents();
  }

  // On: the open control changes at once; the font it reports does not.
  patchy::ui::set_stored_show_font_names_in_english(true);
  CHECK(picker->currentText() == english);
  CHECK(picker->row_family() == family);
  CHECK(picker->currentFont().families().value(0) == family);
  CHECK(picker->count() == rows_before);
  {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
    auto* preview = popup->findChild<patchy::ui::FontPreviewPane*>(QStringLiteral("textFontPickerPreview"));
    CHECK(list != nullptr && search != nullptr && preview != nullptr);
    // Ordered by the shown names; each row still carries the listed family.
    const auto ordered = patchy::ui::font_display_names().sorted(families);
    CHECK(ordered != families);
    CHECK(family_rows(*list, false) == ordered);
    CHECK(shown_rows(*list).count(english) == 1);
    CHECK(!shown_rows(*list).contains(family));
    CHECK(list->currentIndex().data(Qt::DisplayRole).toString() == family);  // opens on the current family
    CHECK(preview->family() == family);
    CHECK(preview->line_texts().contains(english));
    save_widget_artifact("issue691_font_picker_english_name", *popup);

    // Search by the English name, then by the listed one.
    QTest::keyClicks(search, QStringLiteral("PATCHY english"));
    QApplication::processEvents();
    CHECK(family_rows(*list, false) == QStringList{family});
    search->clear();
    QTest::keyClicks(search, family);
    QApplication::processEvents();
    CHECK(family_rows(*list, false).contains(family));
    popup->close();
    QApplication::processEvents();
  }

  // A popup pick of the English row reports and records the listed family.
  picker->setCurrentFont(QFont(other));
  CHECK(picker->currentText() == other);
  {
    auto* popup = open_picker_popup(*picker);
    CHECK(popup != nullptr);
    auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
    CHECK(list != nullptr && search != nullptr);
    QTest::keyClicks(search, english);
    QApplication::processEvents();
    CHECK(list->currentIndex().data(Qt::DisplayRole).toString() == family);
    QTest::keyClick(search, Qt::Key_Return);
    QApplication::processEvents();
  }
  CHECK(picker->currentText() == english);
  CHECK(picker->currentFont().families().value(0) == family);
  CHECK(patchy::ui::stored_recent_font_families() == QStringList{family});

  // The English name typed into the closed control, in any case, picks the same family and adds
  // no row of its own.
  picker->setCurrentFont(QFont(other));
  CHECK(picker->currentFont().families().value(0) == other);
  CHECK(picker->lineEdit() != nullptr);
  picker->lineEdit()->selectAll();
  QTest::keyClicks(picker->lineEdit(), english.toLower());
  QTest::keyClick(picker->lineEdit(), Qt::Key_Return);
  QApplication::processEvents();
  CHECK(picker->count() == rows_before);
  CHECK(picker->currentFont().families().value(0) == family);
  CHECK(picker->row_family() == family);
  CHECK(picker->currentText() == english);

  // A family with no English name is shown as listed.
  picker->setCurrentFont(QFont(other));
  CHECK(picker->currentText() == other);
  CHECK(picker->row_family() == other);

  // Off again: the listed name returns at once.
  picker->setCurrentFont(QFont(family));
  CHECK(picker->currentText() == english);
  patchy::ui::set_stored_show_font_names_in_english(false);
  CHECK(picker->currentText() == family);
  CHECK(picker->currentFont().families().value(0) == family);
  reset_picker_settings();
}

// The Preferences checkbox: off by default, and OK stores ui/showFontNamesInEnglish. Saves a crop
// of the Application page around it for the issue card.
void ui_preferences_show_font_names_in_english_checkbox() {
  ensure_artifact_dir();
  const EnglishFontNamesOff option_off;
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QString::fromLatin1(patchy::ui::kShowFontNamesInEnglishKey));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) return;
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowFontNamesInEnglishCheck"));
    CHECK(check != nullptr);
    if (check != nullptr) {
      CHECK(!check->isChecked());
      CHECK(check->text() == QStringLiteral("Show font names in English"));
      CHECK(!check->toolTip().isEmpty());
      check->setChecked(true);
      QApplication::processEvents();
      if (auto* group = check->parentWidget(); group != nullptr) {
        const auto whole = capture_widget_artifact("issue691_preferences_application_group", *group);
        const qreal ratio = whole.devicePixelRatio();
        const auto area = check->geometry().adjusted(-8, -54, 8, 54).intersected(group->rect());
        const QRect pixels(qRound(area.x() * ratio), qRound(area.y() * ratio), qRound(area.width() * ratio),
                           qRound(area.height() * ratio));
        if (!whole.isNull()) {
          save_image_artifact("issue691_preferences_show_font_names_in_english", whole.copy(pixels));
        }
      }
    }
    saw_dialog = true;
    dialog->accept();
  }));
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::stored_show_font_names_in_english());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("ui/showFontNamesInEnglish"), false).toBool());
}

}  // namespace

std::vector<patchy::test::TestCase> font_picker_filter_tests() {
  return {
      {"ui_font_english_name_comes_from_the_name_table", ui_font_english_name_comes_from_the_name_table},
      {"ui_font_english_name_reads_a_system_font_if_available", ui_font_english_name_reads_a_system_font_if_available},
      {"ui_font_display_names_round_trip_to_the_listed_family", ui_font_display_names_round_trip_to_the_listed_family},
      {"ui_font_picker_shows_english_names_when_enabled", ui_font_picker_shows_english_names_when_enabled},
      {"ui_preferences_show_font_names_in_english_checkbox", ui_preferences_show_font_names_in_english_checkbox},
      {"ui_font_family_without_foundry_strips_qt_suffix", ui_font_family_without_foundry_strips_qt_suffix},
      {"ui_font_picker_lists_plain_family_names", ui_font_picker_lists_plain_family_names},
      {"ui_font_picker_foundry_duplicates_review_capture", ui_font_picker_foundry_duplicates_review_capture},
      {"ui_font_picker_recent_section_orders_skips_and_persists",
       ui_font_picker_recent_section_orders_skips_and_persists},
      {"ui_font_picker_style_and_language_filters_persist", ui_font_picker_style_and_language_filters_persist},
      {"ui_font_picker_recent_separator_reads_in_both_schemes", ui_font_picker_recent_separator_reads_in_both_schemes},
      {"ui_font_picker_filter_lists_scroll_keep_enter_and_offer_other",
       ui_font_picker_filter_lists_scroll_keep_enter_and_offer_other},
      {"ui_font_picker_favorite_star_toggles_without_picking", ui_font_picker_favorite_star_toggles_without_picking},
      {"ui_font_picker_favorites_view_filters_and_persists", ui_font_picker_favorites_view_filters_and_persists},
      {"ui_font_picker_recent_remove_button_keeps_popup_open", ui_font_picker_recent_remove_button_keeps_popup_open},
      {"ui_font_picker_hidden_matches_hint_counts_and_resets", ui_font_picker_hidden_matches_hint_counts_and_resets},
  };
}
