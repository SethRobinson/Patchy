// Customize Layers Panel's row options (GitHub issue 680; docs/layer-panel-row-options.md):
// compact rows without the blend mode and opacity line, the row tooltip switch, the color label
// as a strip or a tint over the whole row, and the Thumbnail Size and Thumbnail Display combos
// that share their settings with the Layers gear's and the thumbnail menus.
#include "ui/app_settings.hpp"
#include "ui/layer_row_options.hpp"
#include "ui/layer_thumbnail_size.hpp"
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
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
          patchy::ui::kLayerThumbnailSizeSettingsKey, "view/zoomLayerThumbnailsToContent"}) {
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
    CHECK(require_child<QComboBox>(dialog, "layerRowColorCombo")->currentData().toString() == QStringLiteral("strip"));
    CHECK(require_child<QComboBox>(dialog, "layerThumbnailSizeCombo")->currentData().toString() ==
          QStringLiteral("medium"));
    CHECK(require_child<QComboBox>(dialog, "layerThumbnailDisplayCombo")->currentData().toBool());
  });
  CHECK(medium->isChecked() && layer_content->isChecked());
  CHECK(patchy::ui::stored_layer_row_options() == patchy::ui::LayerRowOptions{});
}

}  // namespace

std::vector<patchy::test::TestCase> layer_row_options_tests() {
  return {
      {"ui_layer_rows_compact_hides_details_and_keeps_hit_zones",
       ui_layer_rows_compact_hides_details_and_keeps_hit_zones},
      {"ui_layer_rows_tooltip_option_turns_row_tips_off", ui_layer_rows_tooltip_option_turns_row_tips_off},
      {"ui_layer_rows_whole_row_color_tints_rows", ui_layer_rows_whole_row_color_tints_rows},
      {"ui_layer_rows_thumbnail_combos_follow_the_menus", ui_layer_rows_thumbnail_combos_follow_the_menus},
  };
}
