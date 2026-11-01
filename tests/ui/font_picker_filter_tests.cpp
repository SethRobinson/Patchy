// The text bar font picker's Recent section (GitHub issue 569) and its Style and Language
// filters (GitHub issue 570). See docs/font-picker.md.
#include "ui/app_settings.hpp"
#include "ui/font_family_filters.hpp"
#include "ui/font_picker.hpp"
#include "ui/main_window.hpp"
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
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QTest>

#include <iostream>

using namespace patchy::test::ui;

namespace {

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
  CHECK(style->count() == 9);
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

}  // namespace

std::vector<patchy::test::TestCase> font_picker_filter_tests() {
  return {
      {"ui_font_picker_recent_section_orders_skips_and_persists",
       ui_font_picker_recent_section_orders_skips_and_persists},
      {"ui_font_picker_style_and_language_filters_persist", ui_font_picker_style_and_language_filters_persist},
  };
}
