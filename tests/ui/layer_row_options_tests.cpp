// Customize Layers Panel's row options (GitHub issue 680; docs/layer-panel-row-options.md):
// compact rows without the blend mode and opacity line, the row tooltip switch, the color label
// as a strip or a tint over the whole row, and the Thumbnail Size and Thumbnail Display combos
// that share their settings with the Layers gear's and the thumbnail menus. The same column's
// "Show Fill field" (issue 680 follow-up) hides the Fill field above the list.
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_row_options.hpp"
#include "ui/layer_thumbnail_size.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_palette.hpp"
#include "ui/tool_palette_customization.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <cstdio>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::LayerRowColorStyle;
using patchy::ui::LayerThumbnailSize;

// Every key the dialog's OK can write, restored after the test.
struct RowOptionSettingsGuard {
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  RowOptionSettingsGuard() {
    for (const auto* key :
         {"toolPalette/hiddenTools", "toolPalette/layout", "toolPalette/wheelScroll", "toolPalette/showCustomizeButton",
          "layersPanel/hiddenFooterButtons", "layersPanel/shownFooterButtons", "toolPalette/hiddenFooterItems",
          "layersPanel/footerButtonOrder", "preferences/toolbarIconSize", "preferences/toolFlyoutOpenMode",
          "layersPanel/footerIconSize", "toolPalette/ungroupTools", "toolPalette/customizeDialogSize",
          "toolPalette/hideExpandButton", "toolPalette/hideScrollArrows", patchy::ui::kLayerCompactRowsSettingsKey,
          patchy::ui::kLayerRowTooltipsSettingsKey, patchy::ui::kLayerRowColorSettingsKey,
          patchy::ui::kLayerShowFillSettingsKey, patchy::ui::kLayerThumbnailSizeSettingsKey,
          "view/zoomLayerThumbnailsToContent"}) {
      restorers.push_back(std::make_unique<SettingsValueRestorer>(QString::fromLatin1(key)));
      patchy::ui::app_settings().remove(QString::fromLatin1(key));
    }
  }
};

void settle() {
  QApplication::processEvents();
  process_events_for(40);
}

// Runs `act` on the Customize Tool Palette dialog once it is up (it runs its own loop).
void when_customize_dialog_opens(const std::function<void(QDialog&)>& act) {
  QTimer::singleShot(0, guarded([act] {
    QDialog* dialog = nullptr;
    process_events_until([&] {
      dialog = find_top_level_dialog(QStringLiteral("toolPaletteCustomizeDialog"));
      return dialog != nullptr && dialog->isVisible();
    });
    CHECK(dialog != nullptr);
    if (dialog != nullptr) {
      act(*dialog);
    }
  }));
}

void press_dialog_button(QDialog& dialog, QDialogButtonBox::StandardButton which) {
  auto* box = dialog.findChild<QDialogButtonBox*>(QStringLiteral("toolPaletteCustomizeButtonBox"));
  CHECK(box != nullptr);
  if (box != nullptr) {
    box->button(which)->click();
  }
}

template <typename T>
T* require_child(QWidget& parent, const char* name) {
  auto* child = parent.findChild<T*>(QLatin1String(name));
  CHECK(child != nullptr);
  return child;
}

void pick(QComboBox* combo, const QVariant& data) {
  CHECK(combo != nullptr);
  if (combo != nullptr) {
    const int index = combo->findData(data);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
  }
}

// Opens Customize Layers Panel, lets `act` set the Layers Panel tab's controls, presses OK.
void customize_rows(patchy::ui::MainWindow& window, const std::function<void(QDialog&)>& act) {
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tabs = require_child<QTabWidget>(dialog, "toolPaletteCustomizeTabs");
    CHECK(tabs != nullptr && tabs->currentIndex() == 1);
    act(dialog);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
}

// A document whose layers carry color labels, a lock, a mask and layer styles, so every row
// part (eye, thumbnails, name, badges) is there.
patchy::Document labeled_document() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paper", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(240, 236, 228)));
  struct Entry {
    const char* name;
    patchy::LayerColorLabel label;
    QColor color;
  };
  for (const auto& entry : std::array<Entry, 4>{{{"Sky", patchy::kLayerColorBlue, QColor(80, 140, 230)},
                                                 {"Sun", patchy::kLayerColorYellow, QColor(250, 200, 40)},
                                                 {"Hills", patchy::kLayerColorGreen, QColor(60, 150, 70)},
                                                 {"Notes", patchy::kLayerColorRed, QColor(200, 50, 40)}}}) {
    document.add_pixel_layer(entry.name, solid_pixels(96, 64, patchy::PixelFormat::rgba8(), entry.color));
    document.layers().back().set_color_label(entry.label);
  }
  auto& notes = document.layers().back();
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  notes.layer_style().drop_shadows.push_back(shadow);
  notes.set_lock_flags(patchy::kLayerLockPosition);
  patchy::PixelBuffer mask(96, 64, patchy::PixelFormat::gray8());
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 96; ++x) {
      *mask.pixel(x, y) = x < 48 ? 255 : 0;
    }
  }
  document.layers()[2].set_mask(patchy::LayerMask{patchy::Rect{0, 0, 96, 64}, std::move(mask), 0, false});
  document.set_active_layer(document.layers().front().id());
  return document;
}

QWidget* row_of(QListWidget& list, const char* name) {
  auto* row = list.itemWidget(require_layer_item(list, QString::fromLatin1(name)));
  CHECK(row != nullptr);
  return row;
}

QColor over(const QColor& base, const QColor& tint, int alpha) {
  const auto mix = [alpha](int below, int above) {
    return static_cast<int>(std::lround(below + (above - below) * (alpha / 255.0)));
  };
  return {mix(base.red(), tint.red()), mix(base.green(), tint.green()), mix(base.blue(), tint.blue())};
}

bool near(const QColor& a, const QColor& b, int tolerance = 2) {
  return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
         std::abs(a.blue() - b.blue()) <= tolerance;
}

// The row's background in its top-right corner, clear of every control and the bottom border.
QColor row_background(QWidget& row) {
  const auto image = row.grab().toImage();
  const auto ratio = image.devicePixelRatio();
  return image.pixelColor(QPoint(row.width() - 6, 1) * ratio);
}

// Compact rows: no details line, shorter main rows at every thumbnail size, and every control
// still inside the row and the one under its own center (the hit zones). Stored, and a new
// window starts compact.
void ui_layer_rows_compact_hides_details_and_keeps_hit_zones() {
  RowOptionSettingsGuard settings_guard;
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    window.add_document_session(labeled_document(), QStringLiteral("Compact Rows"));
    settle();
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
    CHECK(layer_list != nullptr && layers_dock != nullptr);
    if (layer_list == nullptr || layers_dock == nullptr) {
      return;
    }
    auto* details = row_of(*layer_list, "Notes")->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
    CHECK(details != nullptr && details->isVisible());
    CHECK(require_layer_item(*layer_list, QStringLiteral("Notes"))->sizeHint().height() == 44 + 26);
    save_widget_artifact("issue680_layers_default", *layers_dock);

    customize_rows(window, [](QDialog& dialog) {
      auto* compact = require_child<QCheckBox>(dialog, "layerRowsCompactCheck");
      CHECK(compact != nullptr && !compact->isChecked());
      if (compact != nullptr) {
        compact->setChecked(true);
      }
    });
    CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerCompactRowsSettingsKey)).toBool());
    CHECK(patchy::ui::stored_layer_row_options().compact);

    struct Expected {
      const char* action;
      int height;
    };
    for (const auto& expected : std::array<Expected, 4>{{{"layerThumbnailSizeNoneAction", 26},
                                                         {"layerThumbnailSizeSmallAction", 26},
                                                         {"layerThumbnailSizeLargeAction", 54},
                                                         {"layerThumbnailSizeMediumAction", 34}}}) {
      require_action(window, expected.action)->trigger();
      settle();
      CHECK(patchy::ui::layer_main_row_height(
                patchy::ui::layer_thumbnail_size_from_token(
                    patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey)).toString()),
                true) == expected.height);
      for (const auto* name : {"Paper", "Sky", "Sun", "Hills"}) {
        auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
        CHECK(item->sizeHint().height() == expected.height);
        auto* main_row = row_of(*layer_list, name)->findChild<QWidget*>(QStringLiteral("layerMainRow"));
        CHECK(main_row != nullptr && main_row->height() == expected.height);
        CHECK(layer_list->visualItemRect(item).height() == expected.height);
      }
    }

    // Medium: the hidden details line and the controls of the busiest row.
    auto* notes_row = row_of(*layer_list, "Notes");
    CHECK(notes_row != nullptr);
    if (notes_row == nullptr) {
      return;
    }
    details = notes_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
    CHECK(details != nullptr && !details->isVisible());
    auto* main_row = notes_row->findChild<QWidget*>(QStringLiteral("layerMainRow"));
    CHECK(main_row != nullptr);
    if (main_row == nullptr) {
      return;
    }
    const QRect row_rect(main_row->mapTo(layer_list->viewport(), QPoint(0, 0)), main_row->size());
    struct Control {
      const char* name;
      QWidget* widget;
    };
    const std::array<Control, 6> controls{{
        {"eye", notes_row->findChild<QWidget*>(QStringLiteral("layerVisibilityCheck"))},
        {"thumbnail", notes_row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail"))},
        {"name", notes_row->findChild<QWidget*>(QStringLiteral("layerRowName"))},
        {"fx badge", notes_row->findChild<QWidget*>(QStringLiteral("layerFxBadgeButton"))},
        {"lock strip", notes_row->findChild<QWidget*>(QStringLiteral("layerLockStrip"))},
        {"mask", row_of(*layer_list, "Sun")->findChild<QWidget*>(QStringLiteral("layerMaskThumbnail"))},
    }};
    for (const auto& control : controls) {
      CHECK(control.widget != nullptr);
      if (control.widget == nullptr) {
        continue;
      }
      const QRect rect(control.widget->mapTo(layer_list->viewport(), QPoint(0, 0)), control.widget->size());
      const QRect own_row(control.widget->parentWidget()->mapTo(layer_list->viewport(), QPoint(0, 0)),
                          QSize(row_rect.width(), row_rect.height()));
      if (!(rect.top() >= own_row.top() && rect.bottom() <= own_row.bottom())) {
        std::printf("[INFO] %s %d..%d outside row %d..%d\n", control.name, rect.top(), rect.bottom(), own_row.top(),
                    own_row.bottom());
      }
      CHECK(rect.top() >= own_row.top() - 1 && rect.bottom() <= own_row.bottom() + 1);
      auto* hit = layer_list->viewport()->childAt(rect.center());
      CHECK(hit == control.widget || (hit != nullptr && control.widget->isAncestorOf(hit)));
    }
    // A row without badges packs its controls at the left like a full row: the eye in the first
    // column, the name right after the thumbnail (the details line's empty layout once spread them).
    for (const auto* name : {"Hills", "Sky", "Notes"}) {
      auto* row = row_of(*layer_list, name);
      auto* row_eye = row->findChild<QWidget*>(QStringLiteral("layerVisibilityCheck"));
      auto* thumbnail = row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail"));
      auto* label = row->findChild<QWidget*>(QStringLiteral("layerRowName"));
      CHECK(row_eye != nullptr && thumbnail != nullptr && label != nullptr);
      if (row_eye == nullptr || thumbnail == nullptr || label == nullptr) {
        continue;
      }
      const auto eye_x = row_eye->mapTo(row, QPoint(0, 0)).x();
      const auto gap = label->mapTo(row, QPoint(0, 0)).x() - thumbnail->mapTo(row, QPoint(thumbnail->width(), 0)).x();
      if (eye_x > 30 || gap > 10) {
        std::printf("[INFO] %s eye at %d, name %d px after the thumbnail\n", name, eye_x, gap);
      }
      CHECK(eye_x <= 30);
      CHECK(gap >= 0 && gap <= 10);
    }
    // The eye still toggles visibility from its own click.
    auto* eye = notes_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
    CHECK(eye != nullptr);
    if (eye != nullptr) {
      eye->click();
      settle();
      CHECK(require_layer_item(*layer_list, QStringLiteral("Notes"))->checkState() == Qt::Unchecked);
      eye = row_of(*layer_list, "Notes")->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
      CHECK(eye != nullptr);
      if (eye != nullptr) {
        eye->click();
        settle();
      }
      CHECK(require_layer_item(*layer_list, QStringLiteral("Notes"))->checkState() == Qt::Checked);
    }
    save_widget_artifact("issue680_layers_compact", *layers_dock);
  }

  // A new window starts with compact rows.
  auto&& [reopened, canvas] = ShownWindowFixture{};
  reopened.add_document_session(labeled_document(), QStringLiteral("Compact Rows Again"));
  settle();
  auto* layer_list = reopened.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list != nullptr) {
    CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->sizeHint().height() == 34);
  }
  // Unticked, the default stores no key.
  customize_rows(reopened, [](QDialog& dialog) {
    auto* compact = require_child<QCheckBox>(dialog, "layerRowsCompactCheck");
    CHECK(compact != nullptr && compact->isChecked());
    if (compact != nullptr) {
      compact->setChecked(false);
    }
  });
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerCompactRowsSettingsKey)));
  if (layer_list != nullptr) {
    CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->sizeHint().height() == 44);
  }
}

// "Show row tooltips": on by default; off, rows carry no tooltip (badges keep theirs).
void ui_layer_rows_tooltip_option_turns_row_tips_off() {
  RowOptionSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(labeled_document(), QStringLiteral("Row Tips"));
  settle();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  auto* notes = require_layer_item(*layer_list, QStringLiteral("Notes"));
  CHECK(notes->toolTip().startsWith(QStringLiteral("Notes\n")));
  CHECK(notes->toolTip().contains(QStringLiteral("Locked: Position")));
  customize_rows(window, [](QDialog& dialog) {
    auto* tips = require_child<QCheckBox>(dialog, "layerRowsTooltipsCheck");
    CHECK(tips != nullptr && tips->isChecked());
    if (tips != nullptr) {
      tips->setChecked(false);
    }
  });
  CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerRowTooltipsSettingsKey)).toBool() == false);
  for (int index = 0; index < layer_list->count(); ++index) {
    CHECK(layer_list->item(index)->toolTip().isEmpty());
    CHECK(layer_list->item(index)->data(Qt::ToolTipRole).toString().isEmpty());
  }
  // A selection change re-reads the folder tooltips; they stay off.
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Sky")));
  settle();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->toolTip().isEmpty());
  auto* fx = row_of(*layer_list, "Notes")->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  CHECK(fx != nullptr && !fx->toolTip().isEmpty());
  {
    patchy::ui::MainWindow second;
    second.add_document_session(labeled_document(), QStringLiteral("Row Tips Again"));
    settle();
    auto* second_list = second.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(second_list != nullptr);
    if (second_list != nullptr) {
      CHECK(require_layer_item(*second_list, QStringLiteral("Notes"))->toolTip().isEmpty());
    }
  }
  customize_rows(window, [](QDialog& dialog) {
    auto* tips = require_child<QCheckBox>(dialog, "layerRowsTooltipsCheck");
    if (tips != nullptr) {
      tips->setChecked(true);
    }
  });
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerRowTooltipsSettingsKey)));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Notes"))->toolTip().contains(QStringLiteral("Locked: Position")));
}

// Row color "Whole row": the label's theme color laid over the row (Dark and Light), a selected
// row keeps the plain selection color, and the strip stays. "Strip" adds no tint.
void ui_layer_rows_whole_row_color_tints_rows() {
  RowOptionSettingsGuard settings_guard;
  ColorSchemeRestorer restore_scheme;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(labeled_document(), QStringLiteral("Row Colors"));
  settle();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(layer_list != nullptr && layers_dock != nullptr);
  if (layer_list == nullptr || layers_dock == nullptr) {
    return;
  }
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Paper")),
                             QItemSelectionModel::ClearAndSelect);
  settle();
  CHECK(row_of(*layer_list, "Sky")->findChild<QWidget*>(QStringLiteral("layerRowColorTint")) == nullptr);
  CHECK(row_background(*row_of(*layer_list, "Sky")).rgb() == patchy::ui::theme().layer_row_bg.rgb());

  customize_rows(window, [](QDialog& dialog) {
    auto* color = require_child<QComboBox>(dialog, "layerRowColorCombo");
    CHECK(color != nullptr && color->currentData().toString() == QStringLiteral("strip"));
    CHECK(color != nullptr && color->count() == 2);
    pick(color, QStringLiteral("whole_row"));
    auto* compact = require_child<QCheckBox>(dialog, "layerRowsCompactCheck");
    if (compact != nullptr) {
      compact->setChecked(true);
    }
  });
  CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerRowColorSettingsKey)).toString() ==
        QStringLiteral("whole_row"));
  CHECK(patchy::ui::stored_layer_row_options().color_style == LayerRowColorStyle::WholeRow);

  struct Labeled {
    const char* name;
    QColor patchy::ui::ThemePalette::* role;
  };
  const std::array<Labeled, 4> labeled{{{"Sky", &patchy::ui::ThemePalette::layer_color_blue},
                                        {"Sun", &patchy::ui::ThemePalette::layer_color_yellow},
                                        {"Hills", &patchy::ui::ThemePalette::layer_color_green},
                                        {"Notes", &patchy::ui::ThemePalette::layer_color_red}}};
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(scheme);
    settle();
    const auto& palette = patchy::ui::theme();
    // An unlabeled row keeps the plain background.
    const auto paper = row_background(*row_of(*layer_list, "Paper"));
    if (paper.rgb() != palette.layer_row_selected_bg.rgb()) {
      std::printf("[INFO] Paper painted %s, selected %s, row %s, current %s\n", qPrintable(paper.name()),
                  qPrintable(palette.layer_row_selected_bg.name()), qPrintable(palette.layer_row_bg.name()),
                  layer_list->currentItem() != nullptr ? qPrintable(layer_list->currentItem()->text()) : "none");
    }
    CHECK(paper.rgb() == palette.layer_row_selected_bg.rgb());
    for (const auto& entry : labeled) {
      auto* row = row_of(*layer_list, entry.name);
      CHECK(row->findChild<QWidget*>(QStringLiteral("layerRowColorTint")) != nullptr);
      CHECK(row->findChild<QWidget*>(QStringLiteral("layerRowColorStrip")) != nullptr);
      const auto painted = row_background(*row);
      const auto expected = over(palette.layer_row_bg, palette.*(entry.role), 56);
      if (!near(painted, expected)) {
        std::printf("[INFO] %s painted %s, expected %s\n", entry.name, qPrintable(painted.name()),
                    qPrintable(expected.name()));
      }
      CHECK(near(painted, expected));
      // Readable and apart from the selection color.
      CHECK(!near(painted, palette.layer_row_selected_bg, 12));
    }
    save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue680_layers_whole_row_dark"
                                                                  : "issue680_layers_whole_row_light",
                         *layers_dock);
    // Selected, a labeled row shows the selection color alone; the strip still names the label.
    layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Sun")),
                               QItemSelectionModel::ClearAndSelect);
    settle();
    CHECK(row_background(*row_of(*layer_list, "Sun")).rgb() == palette.layer_row_selected_bg.rgb());
    CHECK(near(row_background(*row_of(*layer_list, "Sky")), over(palette.layer_row_bg, palette.layer_color_blue, 56)));
    layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Paper")),
                               QItemSelectionModel::ClearAndSelect);
    settle();
  }
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  settle();

  // Back to the strip: no tint, no key.
  customize_rows(window, [](QDialog& dialog) { pick(require_child<QComboBox>(dialog, "layerRowColorCombo"),
                                                    QStringLiteral("strip")); });
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerRowColorSettingsKey)));
  CHECK(row_of(*layer_list, "Sky")->findChild<QWidget*>(QStringLiteral("layerRowColorTint")) == nullptr);
}

// Thumbnail Size and Thumbnail Display on the Layers Panel tab: they show the current settings,
// follow the Layers gear's menu while the dialog is open, and OK sets them through the same
// settings, so the menus' checked entries follow. Cancel changes nothing; Reset to Default
// restores every row option.
void ui_layer_rows_thumbnail_combos_follow_the_menus() {
  RowOptionSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(labeled_document(), QStringLiteral("Thumbnail Combos"));
  settle();
  auto* large = require_action(window, "layerThumbnailSizeLargeAction");
  auto* small = require_action(window, "layerThumbnailSizeSmallAction");
  auto* medium = require_action(window, "layerThumbnailSizeMediumAction");
  auto* whole_document = require_action(window, "layerThumbnailBoundsDocumentAction");
  auto* layer_content = require_action(window, "layerThumbnailBoundsLayerAction");
  CHECK(medium->isChecked() && layer_content->isChecked());

  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* size = require_child<QComboBox>(dialog, "layerThumbnailSizeCombo");
    auto* display = require_child<QComboBox>(dialog, "layerThumbnailDisplayCombo");
    CHECK(size != nullptr && display != nullptr);
    if (size == nullptr || display == nullptr) {
      press_dialog_button(dialog, QDialogButtonBox::Cancel);
      return;
    }
    CHECK(size->count() == 4);
    CHECK(size->itemText(0) == QStringLiteral("None") && size->itemText(3) == QStringLiteral("Large"));
    CHECK(display->itemText(0) == QStringLiteral("Layer Content"));
    CHECK(display->itemText(1) == QStringLiteral("Whole Document"));
    CHECK(size->currentData().toString() == QStringLiteral("medium"));
    CHECK(display->currentData().toBool());
    (void)capture_widget_artifact("issue680_customize_layers_panel_tab", dialog);
    // The gear's menu changes them while the dialog is open: the combos follow.
    large->trigger();
    whole_document->trigger();
    settle();
    CHECK(size->currentData().toString() == QStringLiteral("large"));
    CHECK(!display->currentData().toBool());
    // A pick here waits for OK.
    pick(size, QStringLiteral("small"));
    pick(display, true);
    settle();
    CHECK(large->isChecked() && whole_document->isChecked());
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  CHECK(small->isChecked() && layer_content->isChecked());
  CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey)).toString() ==
        QStringLiteral("small"));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("view/zoomLayerThumbnailsToContent")).toBool());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list != nullptr) {
    CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->sizeHint().height() == 36);
  }

  // Cancel keeps everything as it was.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    pick(require_child<QComboBox>(dialog, "layerThumbnailSizeCombo"), QStringLiteral("none"));
    pick(require_child<QComboBox>(dialog, "layerRowColorCombo"), QStringLiteral("whole_row"));
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  CHECK(small->isChecked());
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerRowColorSettingsKey)));

  // Reset to Default restores the rows: Medium, Layer Content, strip, tooltips on, not compact.
  customize_rows(window, [&](QDialog& dialog) {
    require_child<QCheckBox>(dialog, "layerRowsCompactCheck")->setChecked(true);
    require_child<QCheckBox>(dialog, "layerRowsTooltipsCheck")->setChecked(false);
    require_child<QCheckBox>(dialog, "layerRowsShowFillCheck")->setChecked(false);
    pick(require_child<QComboBox>(dialog, "layerRowColorCombo"), QStringLiteral("whole_row"));
    pick(require_child<QComboBox>(dialog, "layerThumbnailDisplayCombo"), false);
    (void)capture_widget_artifact("issue680_customize_layers_panel_tab_changed", dialog);
    auto* reset = require_child<QPushButton>(dialog, "toolPaletteCustomizeResetButton");
    if (reset != nullptr) {
      reset->click();
    }
    settle();
    CHECK(!require_child<QCheckBox>(dialog, "layerRowsCompactCheck")->isChecked());
    CHECK(require_child<QCheckBox>(dialog, "layerRowsTooltipsCheck")->isChecked());
    CHECK(require_child<QCheckBox>(dialog, "layerRowsShowFillCheck")->isChecked());
    CHECK(require_child<QComboBox>(dialog, "layerRowColorCombo")->currentData().toString() == QStringLiteral("strip"));
    CHECK(require_child<QComboBox>(dialog, "layerThumbnailSizeCombo")->currentData().toString() ==
          QStringLiteral("medium"));
    CHECK(require_child<QComboBox>(dialog, "layerThumbnailDisplayCombo")->currentData().toBool());
  });
  CHECK(medium->isChecked() && layer_content->isChecked());
  CHECK(patchy::ui::stored_layer_row_options() == patchy::ui::LayerRowOptions{});
}

// The column width of a Layers panel narrowed to its footer buttons in the report (issue 680
// follow-up): under the opening width. The blend row with Fill wrapped there until the
// blend mode box learned to narrow (issue 693): it now holds one line by narrowing the box
// (ui_layer_blend_row_stays_on_one_line_in_narrow_panel), where its narrowest form fits.
constexpr int kFooterWidthColumn = 293;

void set_column_width(patchy::ui::MainWindow& window, int width) {
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, width);
  // Each wrapped row re-requests the layout once after the pass that wrapped or unwrapped it.
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();
  }
  settle();
}

struct BlendRowWidgets {
  QComboBox* combo{nullptr};
  QSpinBox* opacity{nullptr};
  QSpinBox* fill{nullptr};
  QLineEdit* filter{nullptr};
  QToolButton* filter_button{nullptr};
  QToolButton* lock_all{nullptr};

  explicit BlendRowWidgets(patchy::ui::MainWindow& window)
      : combo(window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"))),
        opacity(window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"))),
        fill(window.findChild<QSpinBox*>(QStringLiteral("layerFillOpacitySpin"))),
        filter(window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"))),
        filter_button(window.findChild<QToolButton*>(QStringLiteral("layerFilterButton"))),
        lock_all(window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"))) {}
  [[nodiscard]] bool complete() const {
    return combo != nullptr && opacity != nullptr && fill != nullptr && filter != nullptr &&
           filter_button != nullptr && lock_all != nullptr;
  }
  // The row's room: from the blend mode box to the panel's right inset (the filter row's end).
  [[nodiscard]] int room() const { return filter_button->geometry().right() + 1 - combo->x(); }
  // The narrowest one-line row, the blend mode box at its narrowest: Opacity and Fill with their
  // "Opacity: " and "Fill: " labels or without (measured from the font, whichever state shows).
  [[nodiscard]] int one_line_minimum(bool labels, bool with_fill = true) const {
    return combo->minimumSizeHint().width() + 6 + patchy::ui::toolbar_spinbox_width_with_prefix(opacity, labels) +
           (with_fill ? 6 + patchy::ui::toolbar_spinbox_width_with_prefix(fill, labels) : 0);
  }
  // Whether the row with Fill fits one line here: with its labels, or once they are dropped
  // (issue 693 follow-up).
  [[nodiscard]] bool row_with_fill_fits_one_line() const { return one_line_minimum(false) <= room(); }
  [[nodiscard]] bool labels_dropped() const {
    return patchy::ui::toolbar_spinbox_prefix_dropped(opacity) && opacity->prefix().isEmpty();
  }
  // Opacity and Fill never disagree, Fill hidden or not.
  [[nodiscard]] bool labels_agree() const {
    return patchy::ui::toolbar_spinbox_prefix_dropped(opacity) == patchy::ui::toolbar_spinbox_prefix_dropped(fill) &&
           opacity->prefix().isEmpty() == fill->prefix().isEmpty();
  }
  // Opacity beside the blend mode, inside the panel's right inset (the filter row's end).
  [[nodiscard]] bool opacity_shares_the_combo_line() const {
    return std::abs(opacity->geometry().center().y() - combo->geometry().center().y()) <= 1 &&
           opacity->x() > combo->geometry().right() &&
           opacity->geometry().right() <= filter_button->geometry().right();
  }
  [[nodiscard]] bool opacity_is_under_the_combo() const { return opacity->y() > combo->geometry().bottom(); }
};

// "Show Fill field" (issue 680 follow-up): on by default. Off, the Fill field is hidden and out
// of the blend row, so at the width of the footer buttons the blend mode and Opacity share one
// line with the blend mode box wider than beside Fill (where the row with Fill wraps, the rows
// below move up instead); the layer's fill opacity is untouched. Stored as false only, a
// new window starts without the field, and ticking it again brings the field and the wrap back.
void ui_layer_rows_show_fill_option_hides_the_fill_field() {
  RowOptionSettingsGuard settings_guard;
  const auto fill_document = [] {
    auto document = labeled_document();
    document.layers().front().set_fill_opacity(0.4F);
    return document;
  };
  const auto active_fill = [](patchy::ui::MainWindow& window) {
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto active = document.active_layer_id();
    const auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
    return layer != nullptr ? layer->fill_opacity() : -1.0F;
  };
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    window.add_document_session(fill_document(), QStringLiteral("Fill Field"));
    settle();
    QWidget* column = patchy::ui::MainWindowTestAccess::panel_column(window);
    auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
    const BlendRowWidgets row(window);
    CHECK(column != nullptr && layers_dock != nullptr && row.complete());
    if (column == nullptr || layers_dock == nullptr || !row.complete()) {
      return;
    }
    CHECK(patchy::ui::stored_layer_row_options().show_fill);
    CHECK(row.fill->isVisible() && row.fill->value() == 40);
    const auto opening = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
    CHECK(kFooterWidthColumn < opening);

    // With Fill, the row fits the footer's width only with the blend mode box narrowed
    // (issue 693), then with the labels dropped (its follow-up); where even that does not
    // fit, Opacity and Fill go under the combo.
    set_column_width(window, kFooterWidthColumn);
    CHECK(column->width() == kFooterWidthColumn);
    const bool with_fill_on_one_line = row.row_with_fill_fits_one_line();
    if (with_fill_on_one_line) {
      CHECK(row.opacity_shares_the_combo_line());
      CHECK(row.combo->width() < row.combo->sizeHint().width());
      CHECK(row.fill->geometry().right() <= row.filter_button->geometry().right());
    } else {
      CHECK(row.opacity_is_under_the_combo());
    }
    CHECK(row.fill->geometry().center().y() == row.opacity->geometry().center().y());
    const auto with_fill_combo_width = row.combo->width();
    const auto wrapped_filter_y = row.filter->y();
    const auto wrapped_lock_y = row.lock_all->y();
    save_widget_artifact("issue680_fu_layers_fill_shown", *layers_dock);

    customize_rows(window, [](QDialog& dialog) {
      auto* show_fill = require_child<QCheckBox>(dialog, "layerRowsShowFillCheck");
      CHECK(show_fill != nullptr && show_fill->isChecked());
      if (show_fill != nullptr) {
        (void)capture_widget_artifact("issue680_fu_customize_layers_panel_tab", dialog);
        show_fill->setChecked(false);
      }
    });
    for (int pass = 0; pass < 4; ++pass) {
      QApplication::processEvents();
    }
    settle();
    CHECK(patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerShowFillSettingsKey)));
    CHECK(!patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerShowFillSettingsKey)).toBool());
    CHECK(!patchy::ui::stored_layer_row_options().show_fill);
    // Hidden and no part of the row: one line, and the Lock and filter row moves up by the line.
    CHECK(row.fill->isHidden() && !row.fill->isVisible());
    CHECK(row.combo->isVisible() && row.opacity->isVisible());
    CHECK(column->width() == kFooterWidthColumn);
    if (!row.opacity_shares_the_combo_line()) {
      std::printf("[INFO] combo %d..%d y %d, opacity %d..%d y %d, content right %d\n", row.combo->x(),
                  row.combo->geometry().right(), row.combo->y(), row.opacity->x(), row.opacity->geometry().right(),
                  row.opacity->y(), row.filter_button->geometry().right());
    }
    CHECK(row.opacity_shares_the_combo_line());
    if (with_fill_on_one_line) {
      // Already one line: the room Fill took goes to the blend mode box, nothing moves.
      CHECK(row.combo->width() > with_fill_combo_width);
      CHECK(row.filter->y() == wrapped_filter_y);
      CHECK(row.lock_all->y() == wrapped_lock_y);
    } else {
      CHECK(row.filter->y() < wrapped_filter_y);
      CHECK(row.lock_all->y() < wrapped_lock_y);
    }
    CHECK(row.filter->y() > row.combo->geometry().bottom());
    // Display only: the layer keeps its fill opacity, and the hidden field still follows it.
    CHECK(std::abs(active_fill(window) - 0.4F) < 0.001F);
    CHECK(row.fill->value() == 40);
    // The widths never grow without the field.
    CHECK(patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window) <= opening);
    save_widget_artifact("issue680_fu_layers_fill_hidden", *layers_dock);
  }

  // A new window starts without the field, on one line at the footer's width.
  auto&& [reopened, canvas] = ShownWindowFixture{};
  reopened.add_document_session(fill_document(), QStringLiteral("Fill Field Again"));
  settle();
  const BlendRowWidgets row(reopened);
  CHECK(row.complete());
  if (!row.complete()) {
    return;
  }
  CHECK(row.fill->isHidden());
  set_column_width(reopened, kFooterWidthColumn);
  CHECK(row.opacity_shares_the_combo_line());
  CHECK(std::abs(active_fill(reopened) - 0.4F) < 0.001F);

  // Ticked again: the default stores no key, the field is back and the row wraps again.
  customize_rows(reopened, [](QDialog& dialog) {
    auto* show_fill = require_child<QCheckBox>(dialog, "layerRowsShowFillCheck");
    CHECK(show_fill != nullptr && !show_fill->isChecked());
    if (show_fill != nullptr) {
      show_fill->setChecked(true);
    }
  });
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();
  }
  settle();
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(patchy::ui::kLayerShowFillSettingsKey)));
  CHECK(row.fill->isVisible() && row.fill->value() == 40);
  if (row.row_with_fill_fits_one_line()) {
    CHECK(row.opacity_shares_the_combo_line());
    CHECK(row.combo->width() < row.combo->sizeHint().width());
  } else {
    CHECK(row.opacity_is_under_the_combo());
  }
  CHECK(row.fill->geometry().center().y() == row.opacity->geometry().center().y());
  CHECK(row.fill->geometry().right() <= row.filter_button->geometry().right());
}

// GitHub issue 693 follow-up: between "the blend mode box narrows" (issue 693) and "the row
// wraps" (issues 96 and 199), Opacity and Fill drop their "Opacity: " and "Fill: " labels, both
// together, and take them back at the first width the labelled row fits again. The choice is a
// function of the row's width alone (both states are measured from the font), so one pixel
// either side of the threshold gives the same answer every time. Without their labels the
// fields are named by their tooltips, the field still scrubs and still takes typed values, and
// a language switch does not bring a dropped label back. With Fill hidden the rule applies to
// Opacity alone. A row too wide for one line even without labels wraps as before, labels shown.
void ui_layer_blend_row_drops_labels_before_wrapping() {
  RowOptionSettingsGuard settings_guard;
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto restore_language = qScopeGuard([&manager] { manager.set_language(QStringLiteral("en"), false); });
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(labeled_document(), QStringLiteral("Blend Row Labels"));
  settle();
  QWidget* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  auto* overflow =
      layers_dock != nullptr ? layers_dock->findChild<QScrollArea*>(QStringLiteral("rightDockOverflowScroll")) : nullptr;
  const BlendRowWidgets row(window);
  CHECK(column != nullptr && layers_dock != nullptr && overflow != nullptr && row.complete());
  if (column == nullptr || layers_dock == nullptr || overflow == nullptr || !row.complete()) {
    return;
  }
  auto* opacity_editor = row.opacity->findChild<QLineEdit*>();
  CHECK(opacity_editor != nullptr);
  if (opacity_editor == nullptr) {
    return;
  }
  const auto opacity_label = QStringLiteral("Opacity: ");
  const auto fill_label = QStringLiteral("Fill: ");
  // The tooltips name the fields at every width, labels shown or not.
  CHECK(row.opacity->toolTip() == QStringLiteral("Opacity"));
  CHECK(row.fill->toolTip() == QStringLiteral("Fill"));
  CHECK(patchy::ui::toolbar_spinbox_width_with_prefix(row.opacity, false) <
        patchy::ui::toolbar_spinbox_width_with_prefix(row.opacity, true));
  CHECK(patchy::ui::toolbar_spinbox_width_with_prefix(row.fill, false) <
        patchy::ui::toolbar_spinbox_width_with_prefix(row.fill, true));

  const auto compact = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window);
  constexpr int kWideColumn = 480;
  set_column_width(window, kWideColumn);
  CHECK(column->width() == kWideColumn);
  // What the column is wider than the row by, at every width down to the compact one.
  const auto chrome = column->width() - row.room();
  const auto column_for_room = [chrome](int room) { return room + chrome; };

  const auto check_one_line = [&](bool fill_shown) {
    CHECK(!overflow->horizontalScrollBar()->isVisible());
    CHECK(row.opacity_shares_the_combo_line());
    CHECK(row.opacity->x() == row.combo->geometry().right() + 1 + 6);
    CHECK(row.combo->width() >= row.combo->minimumSizeHint().width());
    CHECK(row.combo->width() <= row.combo->sizeHint().width());
    const auto edge = row.filter_button->geometry().right();
    if (fill_shown) {
      CHECK(row.fill->isVisible());
      CHECK(row.fill->geometry().center().y() == row.opacity->geometry().center().y());
      CHECK(row.fill->x() == row.opacity->geometry().right() + 1 + 6);
      CHECK(row.fill->geometry().right() == edge);
    } else {
      CHECK(row.fill->isHidden());
      CHECK(row.opacity->geometry().right() == edge);
    }
    CHECK(row.filter->y() > row.combo->geometry().bottom());
  };
  const auto check_labels_shown = [&](const QString& opacity_text, const QString& fill_text) {
    CHECK(!row.labels_dropped() && row.labels_agree());
    CHECK(row.opacity->prefix() == opacity_text);
    CHECK(row.fill->prefix() == fill_text);
    CHECK(row.opacity->width() >= patchy::ui::toolbar_spinbox_width_with_prefix(row.opacity, true));
  };
  const auto check_labels_dropped = [&] {
    CHECK(row.labels_dropped() && row.labels_agree());
    CHECK(row.opacity->prefix().isEmpty() && row.fill->prefix().isEmpty());
    CHECK(row.opacity->text() == row.opacity->locale().toString(row.opacity->value()) + row.opacity->suffix());
    CHECK(row.opacity->width() >= patchy::ui::toolbar_spinbox_width_with_prefix(row.opacity, false));
    CHECK(row.opacity->toolTip() == QStringLiteral("Opacity"));
    CHECK(row.fill->toolTip() == QStringLiteral("Fill"));
    // The whole value shows in the narrower field, clear of the popup chevron.
    const auto margins = opacity_editor->textMargins();
    CHECK(opacity_editor->fontMetrics().horizontalAdvance(opacity_editor->text()) <=
          opacity_editor->contentsRect().width() - margins.left() - margins.right() - 4);
  };

  // --- Fill shown, the stock English labels.
  check_labels_shown(opacity_label, fill_label);
  check_one_line(true);
  const auto labelled = row.one_line_minimum(true);
  const auto bare = row.one_line_minimum(false);
  CHECK(bare < labelled);
  // Whether a column can get narrow enough for the labels to drop before it reaches the compact
  // width, where the panel scrolls sideways instead. Pinned where the release fonts are.
  const bool drop_reachable = column_for_room(labelled - 1) >= compact;
  std::printf("[INFO] fill shown: one-line row %d px with labels, %d px without; column chrome %d px, compact "
              "column %d px\n",
              labelled, bare, chrome, compact);
#ifdef Q_OS_WIN
  CHECK(drop_reachable);
#endif
  if (drop_reachable) {
    // (a) The narrowest row that still holds the labels: the blend mode box at its minimum.
    set_column_width(window, column_for_room(labelled));
    CHECK(row.room() == labelled);
    check_labels_shown(opacity_label, fill_label);
    check_one_line(true);
    CHECK(row.combo->width() == row.combo->minimumSizeHint().width());
    save_widget_artifact("issue693_fu_blend_row_labels_shown", *layers_dock);

    // (b) One pixel narrower: both labels go, the row stays one line, and the room the labels
    // took goes back to the blend mode box.
    set_column_width(window, column_for_room(labelled - 1));
    CHECK(row.room() == labelled - 1);
    check_labels_dropped();
    check_one_line(true);
    CHECK(row.combo->width() > row.combo->minimumSizeHint().width());
    save_widget_artifact("issue693_fu_blend_row_labels_dropped", *layers_dock);

    // The field still scrubs: a drag that starts on its left part changes the value, a pixel a
    // step, and gives the focus back (issue 46).
    const int drag_start = QApplication::startDragDistance();
    const QPoint left_part(opacity_editor->textMargins().left() + 4, opacity_editor->height() / 2);
    CHECK(row.opacity->value() == 100);
    send_mouse(*opacity_editor, QEvent::MouseButtonPress, left_part, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseMove, left_part + QPoint(-(drag_start + 30), 0), Qt::NoButton,
               Qt::LeftButton);
    CHECK(row.opacity->value() == 100 - (drag_start + 30));
    send_mouse(*opacity_editor, QEvent::MouseMove, left_part + QPoint(-30, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseButtonRelease, left_part + QPoint(-30, 0), Qt::LeftButton, Qt::NoButton);
    settle();
    CHECK(row.opacity->value() == 70);
    CHECK(!row.opacity->hasFocus());
    // A click enters editing with the number selected, and typing replaces it.
    send_mouse(*opacity_editor, QEvent::MouseButtonPress, left_part, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseButtonRelease, left_part, Qt::LeftButton, Qt::NoButton);
    settle();
    CHECK(row.opacity->hasFocus());
    CHECK(opacity_editor->hasSelectedText());
    for (int digit = 0; digit < 2; ++digit) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
      QApplication::sendEvent(row.opacity, &press);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
      QApplication::sendEvent(row.opacity, &release);
    }
    send_key(*row.opacity, Qt::Key_Return);
    settle();
    CHECK(row.opacity->value() == 55);
    check_labels_dropped();
    row.opacity->clearFocus();
    row.opacity->setValue(100);
    QMetaObject::invokeMethod(row.opacity, "editingFinished", Qt::DirectConnection);
    settle();

    // A language switch keeps the labels dropped (German's are longer) and renames the tooltips;
    // wide again, the German labels show.
    CHECK(manager.set_language(QStringLiteral("de"), false));
    set_column_width(window, column_for_room(labelled - 1));
    CHECK(row.labels_dropped() && row.labels_agree());
    CHECK(row.opacity->prefix().isEmpty() && row.fill->prefix().isEmpty());
    CHECK(row.opacity->toolTip() == QStringLiteral("Deckkraft"));
    CHECK(row.fill->toolTip() == QString::fromUtf8("Fl\xc3\xa4" "che"));
    check_one_line(true);
    set_column_width(window, kWideColumn);
    check_labels_shown(QStringLiteral("Deckkraft: "), QString::fromUtf8("Fl\xc3\xa4" "che: "));
    CHECK(manager.set_language(QStringLiteral("en"), false));
    set_column_width(window, kWideColumn);
    check_labels_shown(opacity_label, fill_label);

    // The compact width, the narrowest the panel gets: still one line without the labels.
    set_column_width(window, compact);
    CHECK(bare <= row.room());
    check_labels_dropped();
    check_one_line(true);
    save_widget_artifact("issue693_fu_blend_row_compact_width", *layers_dock);

    // Back at the first width the labelled row fits: the labels return at once.
    set_column_width(window, column_for_room(labelled));
    check_labels_shown(opacity_label, fill_label);
    check_one_line(true);
  }

  // --- Fill hidden: the rule applies to Opacity alone, and the hidden Fill follows it.
  customize_rows(window, [](QDialog& dialog) {
    auto* show_fill = require_child<QCheckBox>(dialog, "layerRowsShowFillCheck");
    CHECK(show_fill != nullptr);
    if (show_fill != nullptr) {
      show_fill->setChecked(false);
    }
  });
  set_column_width(window, compact);
  CHECK(row.fill->isHidden());
  if (row.one_line_minimum(true, false) <= row.room()) {
    // English: the blend mode and the labelled Opacity fit the narrowest panel, so the label stays.
    check_labels_shown(opacity_label, fill_label);
    check_one_line(false);
  }
  // A long label, as a long translation gives: set the way a language switch sets it.
  const auto relabel = [&](const QString& opacity_text, const QString& fill_text, int floor) {
    patchy::ui::set_toolbar_spinbox_prefix(row.opacity, opacity_text);
    patchy::ui::set_toolbar_spinbox_prefix(row.fill, fill_text);
    patchy::ui::configure_toolbar_spinbox(row.opacity, floor);
    patchy::ui::configure_toolbar_spinbox(row.fill, floor);
    row.opacity->updateGeometry();
    settle();
  };
  const auto long_opacity = QStringLiteral("Opacity of the whole layer: ");
  const auto long_fill = QStringLiteral("Fill of the layer's pixels: ");
  set_column_width(window, kWideColumn);
  relabel(long_opacity, long_fill, 52);
  set_column_width(window, kWideColumn);
  const auto long_labelled = row.one_line_minimum(true, false);
  const auto long_bare = row.one_line_minimum(false, false);
  std::printf("[INFO] fill hidden, long label: one-line row %d px with the label, %d px without\n", long_labelled,
              long_bare);
  CHECK(column_for_room(long_labelled - 1) > compact);
  CHECK(column_for_room(long_labelled) <= kWideColumn);
  check_labels_shown(long_opacity, long_fill);
  check_one_line(false);
  set_column_width(window, column_for_room(long_labelled));
  check_labels_shown(long_opacity, long_fill);
  check_one_line(false);
  set_column_width(window, column_for_room(long_labelled - 1));
  check_labels_dropped();
  check_one_line(false);
  // The blend mode box is back at its full width here, so Opacity takes the spare width: the
  // number sits at its right end with empty room left of it.
  CHECK(row.combo->width() == row.combo->sizeHint().width());
  CHECK(row.opacity->width() > patchy::ui::toolbar_spinbox_width_with_prefix(row.opacity, false) + 30);
  save_widget_artifact("issue693_fu_blend_row_fill_hidden_label_dropped", *layers_dock);
  {
    // While the field is being edited, that empty room still scrubs (the prefix's place);
    // the number itself selects text then.
    const int drag_start = QApplication::startDragDistance();
    const QPoint empty_room(opacity_editor->textMargins().left() + 6, opacity_editor->height() / 2);
    CHECK(opacity_editor->cursorPositionAt(empty_room) == 0);
    send_mouse(*opacity_editor, QEvent::MouseButtonPress, empty_room, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseButtonRelease, empty_room, Qt::LeftButton, Qt::NoButton);
    settle();
    CHECK(opacity_editor->hasSelectedText());
    CHECK(row.opacity->value() == 100);
    // Being edited: a drag on the number selects text and changes nothing.
    opacity_editor->deselect();
    QPoint number_point(-1, opacity_editor->height() / 2);
    for (int x = 0; x < opacity_editor->width() && number_point.x() < 0; ++x) {
      if (opacity_editor->cursorPositionAt(QPoint(x, number_point.y())) == 1) {
        number_point.setX(x + 2);
      }
    }
    CHECK(number_point.x() > empty_room.x() + 30);
    send_mouse(*opacity_editor, QEvent::MouseButtonPress, number_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseMove, number_point + QPoint(drag_start + 20, 0), Qt::NoButton,
               Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseButtonRelease, number_point + QPoint(drag_start + 20, 0), Qt::LeftButton,
               Qt::NoButton);
    settle();
    CHECK(row.opacity->value() == 100);
    CHECK(opacity_editor->hasSelectedText());
    // The empty room scrubs all the same.
    send_mouse(*opacity_editor, QEvent::MouseButtonPress, empty_room, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*opacity_editor, QEvent::MouseMove, empty_room + QPoint(-(drag_start + 20), 0), Qt::NoButton,
               Qt::LeftButton);
    CHECK(row.opacity->value() == 100 - (drag_start + 20));
    send_mouse(*opacity_editor, QEvent::MouseButtonRelease, empty_room + QPoint(-(drag_start + 20), 0), Qt::LeftButton,
               Qt::NoButton);
    settle();
    row.opacity->clearFocus();
    row.opacity->setValue(100);
    QMetaObject::invokeMethod(row.opacity, "editingFinished", Qt::DirectConnection);
    settle();
  }
  set_column_width(window, compact);
  check_labels_dropped();
  check_one_line(false);
  set_column_width(window, column_for_room(long_labelled));
  check_labels_shown(long_opacity, long_fill);

  // --- (c) Too wide for one line even without the labels (fields with a wide floor): the row
  // wraps as before, Opacity under the blend mode box, and the labels show, since dropping
  // them would not bring the row back to one line.
  relabel(long_opacity, long_fill, 200);
  set_column_width(window, compact);
  CHECK(row.one_line_minimum(false, false) > row.room());
  CHECK(row.opacity_is_under_the_combo());
  CHECK(row.opacity->x() == row.combo->x());
  check_labels_shown(long_opacity, long_fill);
  CHECK(row.opacity->geometry().right() <= row.filter_button->geometry().right());
  CHECK(row.filter->y() > row.opacity->geometry().bottom());
  save_widget_artifact("issue693_fu_blend_row_wrapped_fill_hidden", *layers_dock);

  // The same with Fill shown again: Opacity under the box, Fill beside or under Opacity.
  customize_rows(window, [](QDialog& dialog) {
    auto* show_fill = require_child<QCheckBox>(dialog, "layerRowsShowFillCheck");
    CHECK(show_fill != nullptr);
    if (show_fill != nullptr) {
      show_fill->setChecked(true);
    }
  });
  set_column_width(window, compact);
  CHECK(row.fill->isVisible());
  CHECK(row.opacity_is_under_the_combo());
  CHECK(row.fill->y() >= row.opacity->y());
  check_labels_shown(long_opacity, long_fill);
  CHECK(row.fill->geometry().right() <= row.filter_button->geometry().right());
  CHECK(row.filter->y() > row.fill->geometry().bottom());
  save_widget_artifact("issue693_fu_blend_row_wrapped_fill_shown", *layers_dock);

  // The stock labels and floor again: one line with the labels at the wide width.
  relabel(opacity_label, fill_label, 52);
  set_column_width(window, kWideColumn);
  check_labels_shown(opacity_label, fill_label);
  check_one_line(true);
}

// Issue 680 follow-up 2: the blend row's Opacity and Fill fields take the spare width of their
// line, so each line holding one ends at the right edge of the filter row and the layer list,
// Fill shown or hidden, on one line or wrapped. Both fields share it equally; the blend mode
// box keeps its natural width (narrowed only to hold one line, issue 693), and neither field
// gets narrower than its measured width.
void ui_layer_blend_row_fields_reach_the_panel_edge() {
  RowOptionSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(labeled_document(), QStringLiteral("Blend Row Edge"));
  settle();
  QWidget* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  const BlendRowWidgets row(window);
  CHECK(column != nullptr && layers_dock != nullptr && list != nullptr && row.complete());
  if (column == nullptr || layers_dock == nullptr || list == nullptr || !row.complete()) {
    return;
  }
  const auto combo_natural = row.combo->sizeHint().width();
  const auto opening = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  const auto compact = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window);
  const auto check_widths = [&](bool fill_shown, const char* label) {
    // 341 px: the reporter's column with Fill hidden.
    for (const int width : {compact, kFooterWidthColumn, 341, opening, opening + 120}) {
      set_column_width(window, width);
      const auto edge = row.filter_button->geometry().right();
      const auto list_edge = list->geometry().right();
      std::printf("[INFO] %s, column %d: combo %d..%d, opacity %d..%d y %d, fill %d..%d y %d (shown %d), edge %d, "
                  "list %d\n",
                  label, width, row.combo->x(), row.combo->geometry().right(), row.opacity->x(),
                  row.opacity->geometry().right(), row.opacity->y(), row.fill->x(), row.fill->geometry().right(),
                  row.fill->y(), fill_shown ? 1 : 0, edge, list_edge);
      CHECK(std::abs(edge - list_edge) <= 2);
      CHECK(row.opacity->width() >= row.opacity->minimumWidth());
      CHECK(row.combo->width() <= combo_natural);
      const bool fill_beside_opacity = fill_shown && row.fill->y() == row.opacity->y();
      // The last field of each line ends at the edge.
      if (fill_shown) {
        CHECK(row.fill->isVisible());
        CHECK(row.fill->width() >= row.fill->minimumWidth());
        CHECK(row.fill->geometry().right() == edge);
        if (!fill_beside_opacity) {
          // Fill on a line of its own under Opacity: Opacity's line ends at the edge too.
          CHECK(row.fill->y() > row.opacity->geometry().bottom());
          CHECK(row.opacity->geometry().right() == edge);
        } else {
          CHECK(row.fill->x() == row.opacity->geometry().right() + 1 + 6);
          // An equal share of the spare width each, the remainder pixel on Fill.
          const auto opacity_extra = row.opacity->width() - row.opacity->minimumWidth();
          const auto fill_extra = row.fill->width() - row.fill->minimumWidth();
          CHECK(fill_extra - opacity_extra >= 0 && fill_extra - opacity_extra <= 1);
        }
      } else {
        CHECK(row.fill->isHidden());
        CHECK(row.opacity->geometry().right() == edge);
      }
      if (row.opacity_shares_the_combo_line()) {
        CHECK(row.opacity->x() == row.combo->geometry().right() + 1 + 6);
        // Spare width goes to the fields only once the box is back at its natural width.
        if (row.opacity->width() > row.opacity->minimumWidth()) {
          CHECK(row.combo->width() == combo_natural);
        }
      } else {
        // Wrapped: the blend mode box keeps its own width on its line, Opacity starts the next.
        CHECK(row.opacity_is_under_the_combo());
        CHECK(row.opacity->x() == row.combo->x());
        CHECK(row.combo->width() == std::min(combo_natural, edge + 1 - row.combo->x()));
      }
      CHECK(row.filter->y() > std::max(row.opacity->geometry().bottom(),
                                       fill_shown ? row.fill->geometry().bottom() : 0));
      if (width == kFooterWidthColumn || width == 341) {
        save_widget_artifact(std::string("issue680_fu2_blend_row_") + label + "_" + std::to_string(width),
                             *layers_dock);
      }
    }
  };
  check_widths(true, "fill_shown");
  customize_rows(window, [](QDialog& dialog) {
    auto* show_fill = require_child<QCheckBox>(dialog, "layerRowsShowFillCheck");
    CHECK(show_fill != nullptr);
    if (show_fill != nullptr) {
      show_fill->setChecked(false);
    }
  });
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();
  }
  settle();
  check_widths(false, "fill_hidden");
}

}  // namespace

std::vector<patchy::test::TestCase> layer_row_options_tests() {
  return {
      {"ui_layer_rows_compact_hides_details_and_keeps_hit_zones",
       ui_layer_rows_compact_hides_details_and_keeps_hit_zones},
      {"ui_layer_rows_tooltip_option_turns_row_tips_off", ui_layer_rows_tooltip_option_turns_row_tips_off},
      {"ui_layer_rows_whole_row_color_tints_rows", ui_layer_rows_whole_row_color_tints_rows},
      {"ui_layer_rows_thumbnail_combos_follow_the_menus", ui_layer_rows_thumbnail_combos_follow_the_menus},
      {"ui_layer_rows_show_fill_option_hides_the_fill_field", ui_layer_rows_show_fill_option_hides_the_fill_field},
      {"ui_layer_blend_row_drops_labels_before_wrapping", ui_layer_blend_row_drops_labels_before_wrapping},
      {"ui_layer_blend_row_fields_reach_the_panel_edge", ui_layer_blend_row_fields_reach_the_panel_edge},
  };
}
