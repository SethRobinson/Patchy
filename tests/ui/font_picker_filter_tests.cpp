// The text bar font picker's Recent section (GitHub issue 569), its Favorites (GitHub issue 630)
// and its Style and Language filters (GitHub issue 570). See docs/font-picker.md.
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/font_family_filters.hpp"
#include "ui/font_picker.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_palette.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
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
#include <QScrollBar>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QTest>
#include <QToolButton>

#include <cstdlib>
#include <iostream>

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
  if (picker->count() < 2) {
    patchy::test::skip("fewer than two font families on this machine");
  }
  // Any two installed families (the Linux runner has neither Verdana nor Arial).
  const auto verdana = picker->itemText(1);
  const auto arial = picker->itemText(0);

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
  const auto latin_families = QFontDatabase::families(QFontDatabase::Latin);
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
  if (picker->count() < 2) {
    patchy::test::skip("fewer than two font families on this machine");
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QString::fromLatin1(patchy::ui::kFontPickerRecentFamiliesKey),
                      QStringList{picker->itemText(1), picker->itemText(0)});
  }
  {
    // A current family in the Recent section opens the list at the top, rule in view.
    const QSignalBlocker blocker(picker);
    picker->setCurrentFont(QFont(picker->itemText(1)));
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
  for (int row = 0; row < picker->count(); ++row) {
    const auto family = picker->itemText(row);
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
  if (picker->count() < 3) {
    patchy::test::skip("fewer than three font families on this machine");
  }
  const auto current = picker->itemText(0);
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
  for (int row = 0; row < picker->count() && (mono.isEmpty() || proportional.isEmpty()); ++row) {
    const auto family = picker->itemText(row);
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
  CHECK(family_rows(*reopened_list, false).size() == picker->count());
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
  if (picker->count() < 3) {
    patchy::test::skip("fewer than three font families on this machine");
  }
  const auto first = picker->itemText(1);
  const auto second = picker->itemText(2);
  const auto current = picker->itemText(0);
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

}  // namespace

std::vector<patchy::test::TestCase> font_picker_filter_tests() {
  return {
      {"ui_font_picker_recent_section_orders_skips_and_persists",
       ui_font_picker_recent_section_orders_skips_and_persists},
      {"ui_font_picker_style_and_language_filters_persist", ui_font_picker_style_and_language_filters_persist},
      {"ui_font_picker_recent_separator_reads_in_both_schemes", ui_font_picker_recent_separator_reads_in_both_schemes},
      {"ui_font_picker_filter_lists_scroll_keep_enter_and_offer_other",
       ui_font_picker_filter_lists_scroll_keep_enter_and_offer_other},
      {"ui_font_picker_favorite_star_toggles_without_picking", ui_font_picker_favorite_star_toggles_without_picking},
      {"ui_font_picker_favorites_view_filters_and_persists", ui_font_picker_favorites_view_filters_and_persists},
      {"ui_font_picker_recent_remove_button_keeps_popup_open", ui_font_picker_recent_remove_button_keeps_popup_open},
  };
}
