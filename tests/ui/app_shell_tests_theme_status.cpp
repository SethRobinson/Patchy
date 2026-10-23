#include <QPushButton>
#include <QScrollBar>
#include <QToolBar>
#include <QVBoxLayout>



#include "ui/action_icons.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/palette_panel.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/zoom_status_bar.hpp"
#include "test_harness.hpp"
#include <patchy_version.hpp>
#include <QApplication>
#include <QPalette>
#include <QToolTip>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <vector>
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "app_shell_test_support.hpp"

namespace {
using namespace patchy::test::ui::app_shell_support;

[[nodiscard]] int channel_delta(const QColor& first, const QColor& second);

[[nodiscard]] double relative_luminance(const QColor& color);

[[nodiscard]] double contrast_ratio(const QColor& first, const QColor& second);

[[nodiscard]] int channel_delta(const QColor& first, const QColor& second) {
  return std::max({std::abs(first.red() - second.red()), std::abs(first.green() - second.green()),
                   std::abs(first.blue() - second.blue())});
}

[[nodiscard]] double relative_luminance(const QColor& color) {
  const auto channel = [](double value) {
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
         0.0722 * channel(color.blueF());
}

[[nodiscard]] double contrast_ratio(const QColor& first, const QColor& second) {
  const double a = relative_luminance(first);
  const double b = relative_luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// A role omitted from a palette's aggregate initializer default-constructs to an
// invalid QColor, which QSS then renders as a black fill rather than failing.
// Walking the role table is the only thing that catches it.
void ui_theme_palettes_define_every_role() {
  const auto roles = patchy::ui::theme_palette_roles();
  CHECK(!roles.empty());

  for (const auto& [name, member] : roles) {
    const auto dark = patchy::ui::dark_palette().*member;
    const auto light = patchy::ui::light_palette().*member;
    if (!dark.isValid() || !light.isValid()) {
      fprintf(stderr, "  role \"%s\" is missing a value\n", QString(name).toUtf8().constData());
    }
    CHECK(dark.isValid());
    CHECK(light.isValid());
  }

  // Role names are generated from the member spelling, so duplicates mean a
  // copy-paste in the table rather than a typo.
  QSet<QString> seen;
  for (const auto& [name, member] : roles) {
    CHECK(!seen.contains(QString(name)));
    seen.insert(QString(name));
  }
}

// Light is derived from Dark, so two roles that never touch in Dark can land on
// the same value in Light. That happened: title_bar_bg and canvas_backdrop both
// resolved to the same gray and a dialog floating over the canvas lost its title
// bar entirely. Encode the pairs that actually sit against each other on screen.
// Thresholds are set below what Dark already achieves, so this asserts "Light is
// at least as readable here as the scheme we have always shipped".
void ui_theme_adjacent_roles_stay_distinguishable() {
  struct RolePair {
    const char* description;
    QColor patchy::ui::ThemePalette::* first;
    QColor patchy::ui::ThemePalette::* second;
    int minimum_delta;
  };
  const std::array<RolePair, 5> pairs{{
      {"a dialog title bar floating over the canvas", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::canvas_backdrop, 24},
      {"the About dialog's secondary button on its body", &patchy::ui::ThemePalette::splash_button_bg,
       &patchy::ui::ThemePalette::splash_bg, 12},
      {"a dialog title bar against the dialog body it heads", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::window_bg, 12},
      {"the chrome bar against a docked panel", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::panel_bg, 16},
      {"the clipping badge on an unselected layer row", &patchy::ui::ThemePalette::layer_clip_badge,
       &patchy::ui::ThemePalette::layer_row_bg, 60},
  }};

  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const auto& palette = patchy::ui::theme(scheme);
    const char* label = scheme == patchy::ui::ColorScheme::Light ? "light" : "dark";
    for (const auto& pair : pairs) {
      const int measured = channel_delta(palette.*pair.first, palette.*pair.second);
      if (measured < pair.minimum_delta) {
        fprintf(stderr, "  %s: %s -> delta %d, want >= %d\n", label, pair.description, measured,
                pair.minimum_delta);
      }
      CHECK(measured >= pair.minimum_delta);
    }
  }
}

// Text roles pinned to one value across both schemes are the trap here:
// text_on_accent is correctly white over a saturated selection, and was also
// being used over neutral raised surfaces, where Light turns the background
// pale and the label vanishes. Every text/background pair has to hold up in
// both schemes, not just the one it was authored against.
void ui_theme_text_roles_contrast_with_their_backgrounds() {
  struct TextPair {
    const char* description;
    QColor patchy::ui::ThemePalette::* text;
    QColor patchy::ui::ThemePalette::* background;
  };
  const std::array<TextPair, 11> pairs{{
      {"the About dialog's body text", &patchy::ui::ThemePalette::splash_body_text,
       &patchy::ui::ThemePalette::splash_bg},
      {"a highlighted menu item", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::menu_item_selected_bg},
      {"a checked options-bar button", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::accent_checked_bg},
      {"the selected layer-style category", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::category_selected_bg},
      {"the About dialog's primary button", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::splash_primary_bg},
      {"the selected document tab", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::tab_selected_bg},
      {"the selected Preferences tab", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::dialog_tab_selected_bg},
      {"an Image Size panel label", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::dlg_raised_bg},
      {"body text on the window surface", &patchy::ui::ThemePalette::text_primary,
       &patchy::ui::ThemePalette::window_bg},
      {"the menu bar", &patchy::ui::ThemePalette::text_bright,
       &patchy::ui::ThemePalette::title_bar_bg},
      {"a selected layer row's name", &patchy::ui::ThemePalette::layer_row_name_text,
       &patchy::ui::ThemePalette::layer_row_selected_bg},
  }};

  // 3:1 is the floor for UI text and large type; every pair here clears it in
  // Dark, so this asserts Light is no worse than what already ships.
  constexpr double kMinimumContrast = 3.0;
  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const auto& palette = patchy::ui::theme(scheme);
    const char* label = scheme == patchy::ui::ColorScheme::Light ? "light" : "dark";
    for (const auto& pair : pairs) {
      const double ratio = contrast_ratio(palette.*pair.text, palette.*pair.background);
      if (ratio < kMinimumContrast) {
        fprintf(stderr, "  %s: %s -> contrast %.2f, want >= %.1f\n", label, pair.description, ratio,
                kMinimumContrast);
      }
      CHECK(ratio >= kMinimumContrast);
    }
  }
}

// The rendered counterpart to the pair above: the palette can be right and the
// chrome still wrong if a rule stops applying. Opens the real dialog over the
// real canvas in Light and looks at the pixels.
void ui_light_scheme_dialog_chrome_separates_from_canvas() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  bool sampled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* title_bar = dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar"));
    CHECK(title_bar != nullptr);
    if (title_bar != nullptr) {
      const auto shot = title_bar->grab().toImage();
      CHECK(!shot.isNull());
      if (!shot.isNull()) {
        // Mid-height, right of the badge and left of the close button.
        const auto bar = shot.pixelColor(shot.width() / 2, shot.height() / 2);
        const int against_canvas = channel_delta(bar, patchy::ui::theme().canvas_backdrop);
        const int against_body = channel_delta(bar, patchy::ui::theme().window_bg);
        if (against_canvas < 20 || against_body < 10) {
          fprintf(stderr, "  title bar %s: delta %d vs canvas, %d vs dialog body\n",
                  qPrintable(bar.name(QColor::HexRgb)), against_canvas, against_body);
        }
        CHECK(against_canvas >= 20);
        CHECK(against_body >= 10);
        sampled = true;
      }
    }
    save_widget_artifact("ui_light_scheme_preferences_dialog", *dialog);
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(sampled);
}

// A floating dialog gets no window-manager shadow on wasm, and its title bar
// shares @title_bar_bg with the main window's own chrome bar, so the 1px
// @window_border outline is the only thing that can separate a dialog's edge
// from the chrome behind it. Assert the outline is actually the outermost
// pixel of the rendered dialog and that it reads against both the title-bar
// fill inside it and the chrome surface it typically floats over.
void ui_dialog_chrome_outline_reads_at_the_dialog_edge() {
  ColorSchemeRestorer restore_active;

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    const char* label = light ? "light" : "dark";

    QDialog dialog;
    auto* root = new QVBoxLayout(&dialog);
    auto* content =
        patchy::ui::install_dark_dialog_chrome(dialog, root, QStringLiteral("Outline Probe"));
    content->addWidget(new QLabel(QStringLiteral("Probe body"), &dialog));
    dialog.resize(300, 140);
    dialog.show();
    QApplication::processEvents();

    const auto shot = dialog.grab().toImage();
    CHECK(!shot.isNull());
    if (shot.isNull()) {
      return;
    }
    if (light) {
      save_widget_artifact("ui_light_scheme_dialog_chrome_outline", dialog);
    }

    // Mid-width avoids the badge and the close button; row 0 is the outline
    // if it is drawn at all, and row 17 is inside the 34px title bar's fill.
    const auto edge = shot.pixelColor(shot.width() / 2, 0);
    const auto title_fill = shot.pixelColor(shot.width() / 2, 17);
    const int edge_vs_title = channel_delta(edge, title_fill);
    const int edge_vs_chrome = channel_delta(edge, patchy::ui::theme().title_bar_bg);
    if (edge_vs_title < 40 || edge_vs_chrome < 40) {
      fprintf(stderr, "  %s: dialog edge %s vs title fill %s (%d), vs chrome bar %s (%d), want >= 40\n",
              label, qPrintable(edge.name(QColor::HexRgb)), qPrintable(title_fill.name(QColor::HexRgb)),
              edge_vs_title, qPrintable(patchy::ui::theme().title_bar_bg.name(QColor::HexRgb)),
              edge_vs_chrome);
    }
    CHECK(edge_vs_title >= 40);
    CHECK(edge_vs_chrome >= 40);
    // Where no window-manager shadow exists the outline is 2 px, and the chrome
    // content is inset by the same amount so the title bar never covers it.
    const int edge_px = patchy::ui::frameless_window_edge_width();
    CHECK(root->contentsMargins().left() == edge_px && root->contentsMargins().top() == edge_px);
    if (edge_px >= 2) {
      CHECK(channel_delta(shot.pixelColor(shot.width() / 2, 1), edge) < 8);
    }
  }
}

// Panel and list scroll bars are styled by the application sheet on every
// platform rather than left to the native style. They have to be: the global
// QWidget background rule reaches them anyway, and once QSS touches a scroll bar
// QStyleSheetStyle owns the whole complex control, so one without subcontrol
// rules fills its groove with the window background and lets the base style draw
// the rest on top. Against Dark's near-black surface that passed for native
// rendering, which is how it survived review. In Light the groove, the handle and
// the panel behind them all derived to the same near-white and the bar vanished,
// leaving two faint arrow glyphs as the only evidence it was there.
//
// A role-pair check cannot catch this, because the track is deliberately the same
// value as the surface in both schemes and the separation comes from the handle
// and the dither texture over it. So render a real bar and read it.
void ui_light_scheme_panel_scroll_bar_handle_reads_against_track() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  // Far more rows than fit, so the handle covers a small fraction of the groove
  // and the bare track below it is actually rendered.
  auto* list = new QListWidget(&window);
  list->setObjectName(QStringLiteral("lightScrollBarProbeList"));
  for (int row = 0; row < 400; ++row) {
    list->addItem(QStringLiteral("row %1").arg(row));
  }
  list->setGeometry(0, 0, 200, 160);
  list->show();
  QApplication::processEvents();

  auto* bar = list->verticalScrollBar();
  CHECK(bar != nullptr);
  if (bar == nullptr) {
    return;
  }
  const auto shot = bar->grab().toImage();
  CHECK(!shot.isNull());
  if (shot.isNull()) {
    return;
  }
  save_widget_artifact("ui_light_scheme_panel_scroll_bar", *list);

  // The list sits at row 0, so the handle is parked at the top of the groove and
  // bare track runs below it. Compare the two rows across the full width rather
  // than at one column: a handle is placed by its border as much as by its fill,
  // and the fill alone sits deliberately close to the track in a light scheme.
  // What has to hold is that the two rows do not render as the same thing, which
  // is exactly what failed here.
  const int handle_y = 6;
  const int track_y = shot.height() - 6;
  int separation = 0;
  int at_column = 0;
  for (int x = 0; x < shot.width(); ++x) {
    const int delta = channel_delta(shot.pixelColor(x, handle_y), shot.pixelColor(x, track_y));
    if (delta > separation) {
      separation = delta;
      at_column = x;
    }
  }
  if (separation < 40) {
    fprintf(stderr, "  light panel scroll bar: handle row %s vs track row %s, best delta %d at x=%d, want >= 40\n",
            qPrintable(shot.pixelColor(at_column, handle_y).name(QColor::HexRgb)),
            qPrintable(shot.pixelColor(at_column, track_y).name(QColor::HexRgb)), separation,
            at_column);
  }
  CHECK(separation >= 40);
}

// The compact color picker styles its own tab bar because it wants more
// selected-tab contrast than the global theme gives, and borrowing roles to get
// it is what left it behind when the document tabs were corrected for Light. Its
// selected fill came from field_border, which sits lighter than its neighbours
// in Dark and darker than them in Light, so the current mode read as the
// inactive one. Reading the roles back would only restate whichever pairing the
// sheet happens to name, so render the bar and compare the tabs as drawn.
// Tooltips are ownerless QTipLabels, so the MainWindow stylesheet never reaches
// them; Qt painted the platform's yellow or white tip in Dark (the Linux smoke test
// caught it through the capture guard, October 2026). ThemeManager keeps QToolTip's
// application palette on the active scheme in Dark and Light.
void ui_tooltips_follow_the_active_theme() {
  ColorSchemeRestorer restore_active;
  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const auto& palette = patchy::ui::theme();
    CHECK(QToolTip::palette().color(QPalette::ToolTipBase).rgb() == palette.panel_bg.rgb());
    CHECK(QToolTip::palette().color(QPalette::ToolTipText).rgb() == palette.text_primary.rgb());
    // The tip's owner chain is the anchor's window: the app's windows carry the
    // sheet, so the themed parent stands in for a MainWindow here.
    auto themed_parent = make_themed_dialog_parent();
    themed_parent->resize(120, 40);
    themed_parent->show();
    auto& anchor_widget = *themed_parent;
    QApplication::processEvents();
    QToolTip::showText(anchor_widget.mapToGlobal(QPoint(4, 40)), QStringLiteral("Themed tip"), &anchor_widget);
    QApplication::processEvents();
    QWidget* tip = nullptr;
    for (auto* top : QApplication::topLevelWidgets()) {
      if (top->inherits("QTipLabel") && top->isVisible()) {
        tip = top;
      }
    }
    CHECK(tip != nullptr);
    if (tip != nullptr) {
      CHECK(tip->palette().color(QPalette::ToolTipBase).rgb() == palette.panel_bg.rgb());
      const auto mismatch = capture_theme_mismatch(*tip);
      if (!mismatch.isEmpty()) {
        std::cout << "  tooltip guard: " << mismatch.toStdString() << "\n";
      }
      CHECK(mismatch.isEmpty());
    }
    QToolTip::hideText();
    QApplication::processEvents();
  }
}

void ui_light_scheme_color_picker_selected_tab_reads_as_selected() {
  ColorSchemeRestorer restore_active;

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    const char* label = light ? "light" : "dark";

    patchy::ui::PatchyColorPicker picker(QColor(120, 60, 30));
    picker.show();
    QApplication::processEvents();

    auto* tabs = picker.findChild<QTabWidget*>(QStringLiteral("patchyColorPickerTabs"));
    CHECK(tabs != nullptr);
    if (tabs == nullptr) {
      return;
    }
    auto* bar = tabs->tabBar();
    CHECK(bar != nullptr);
    if (bar == nullptr || bar->count() < 2) {
      CHECK(false);
      return;
    }
    const auto shot = bar->grab().toImage();
    CHECK(!shot.isNull());
    if (shot.isNull()) {
      return;
    }
    if (light) {
      save_widget_artifact("ui_light_scheme_color_picker", picker);
    }

    // The picker reopens on whichever mode was used last, so read the current
    // tab rather than forcing one: setting it would write that mode back into
    // the shared QSettings store for every test after this one.
    const int selected_index = bar->currentIndex();
    const int other_index = selected_index == 0 ? 1 : 0;
    // Inside the left padding, clear of the border and of the label glyphs.
    const auto fill = [&shot, bar](int index) {
      const auto rect = bar->tabRect(index);
      return shot.pixelColor(rect.left() + 5, rect.center().y());
    };
    const auto selected = fill(selected_index);
    const auto unselected = fill(other_index);

    // Lightness carries the selection in both schemes: the current mode is the
    // raised tab, whichever end of the value range the scheme lives at. The
    // threshold is what Dark already ships (0x34 against 0x5a), so this asserts
    // Light separates them at least as well.
    const int separation = selected.lightness() - unselected.lightness();
    if (separation < 30) {
      fprintf(stderr, "  %s: picker selected tab %s vs unselected %s, lightness %+d, want >= +30\n",
              label, qPrintable(selected.name(QColor::HexRgb)),
              qPrintable(unselected.name(QColor::HexRgb)), separation);
    }
    CHECK(separation >= 30);
  }
}

// The About dialog is painted from a family of near-blacks that wear a cool
// cast, and flip_for_light() reads that cast as chroma, so it clamped every one
// of them to a mid tone instead of lifting them into Light's surface band. The
// body, the secondary button and its hover all derived to within a couple of
// points of #93a4be and the dialog rendered as one flat periwinkle slab. The
// role table now says otherwise, but the other half of the bug was never in the
// table: the artwork is a plain QWidget, so the application's QWidget rule filled
// its slot with window_bg and drew a window-colored rectangle on the dialog
// surface. Only the rendered dialog shows both, so grab the real one.
void ui_light_scheme_about_dialog_reads_as_a_light_surface() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* artwork = dialog->findChild<QWidget*>(QStringLiteral("splashArtwork"));
    CHECK(artwork != nullptr);
    const auto shot = dialog->grab().toImage();
    CHECK(!shot.isNull());
    if (artwork == nullptr || shot.isNull()) {
      dialog->accept();
      return;
    }
    save_widget_artifact("ui_light_scheme_about_dialog", *dialog);

    // In the right margin below the button row: inside the 1px border, clear of
    // every child.
    const auto body = shot.pixelColor(shot.width() - 8, shot.height() - 8);
    if (body.lightness() < 210) {
      fprintf(stderr, "  light About dialog body %s, lightness %d, want >= 210\n",
              qPrintable(body.name(QColor::HexRgb)), body.lightness());
    }
    CHECK(body.lightness() >= 210);

    // A tint is allowed, a color is not. The failing body spread 43 between its
    // channels; the surface it replaced spreads single digits.
    const int spread = std::max({body.red(), body.green(), body.blue()}) -
                       std::min({body.red(), body.green(), body.blue()});
    if (spread > 16) {
      fprintf(stderr, "  light About dialog body %s spreads %d across channels, want <= 16\n",
              qPrintable(body.name(QColor::HexRgb)), spread);
    }
    CHECK(spread <= 16);

    // The artwork paints its card inside a margin, so its corner is dialog
    // surface and has to render as exactly that. Read it out of the dialog
    // image: grabbing the widget itself now returns an uninitialized pixmap,
    // which is the point of the rule under test.
    const auto slot_point = artwork->mapTo(dialog, QPoint(2, 2));
    const auto slot = shot.pixelColor(slot_point);
    const int against_body = channel_delta(slot, body);
    if (against_body > 4) {
      fprintf(stderr, "  About dialog artwork slot %s vs dialog body %s, delta %d, want <= 4\n",
              qPrintable(slot.name(QColor::HexRgb)), qPrintable(body.name(QColor::HexRgb)),
              against_body);
    }
    CHECK(against_body <= 4);

    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

// An unresolved @token makes Qt drop the entire declaration containing it,
// silently. Nothing downstream would notice, so assert no token survives and no
// raw hex was left behind in either scheme.
void ui_theme_qss_resolves_every_token() {
  const patchy::ui::ColorScheme entry_scheme = patchy::ui::active_color_scheme();

  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    patchy::ui::set_active_color_scheme(scheme);
    const auto style = patchy::ui::photoshop_style();
    CHECK(!style.isEmpty());
    CHECK(!style.contains(QLatin1Char('@')));
  }

  patchy::ui::set_active_color_scheme(entry_scheme);

  // The template is the thing that must stay token-only: a hex literal added
  // there would be invisible to the check above.
  const auto resolved = patchy::ui::apply_theme_tokens(QStringLiteral("a: @window_bg; b: @accent;"));
  CHECK(resolved ==
        QStringLiteral("a: %1; b: %2;")
            .arg(patchy::ui::theme().window_bg.name(QColor::HexRgb),
                 patchy::ui::theme().accent.name(QColor::HexRgb)));
}

void ui_status_bar_error_message_flashes_then_persists_until_replaced() {
  patchy::ui::ZoomStatusBar bar;
  bar.resize(600, 24);
  bar.show();
  QApplication::processEvents();

  bar.showMessage(QStringLiteral("Ready"));
  CHECK(!bar.error_message_active());

  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.currentMessage() == QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_message_active());
  CHECK(bar.error_flash_running());

  // The flash is finite (~1s); the red error presentation persists after it.
  CHECK(process_events_until([&bar] { return !bar.error_flash_running(); }));
  CHECK(bar.error_message_active());

  // Re-showing the SAME error restarts the flash even though QStatusBar
  // suppresses messageChanged for an identical string.
  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_flash_running());
  CHECK(bar.currentMessage() == QStringLiteral("Layer pixels are locked."));

  // Any different message ends the error presentation.
  bar.showMessage(QStringLiteral("Applied Levels"));
  CHECK(!bar.error_message_active());
  CHECK(!bar.error_flash_running());

  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_message_active());
  bar.clearMessage();
  CHECK(!bar.error_message_active());
}

void ui_blocking_refusal_shows_error_status_and_info_clears_it() {
  patchy::ui::MainWindow window;
  show_window(window);

  require_action(window, "filterConvertForSmartFiltersAction")->trigger();
  QApplication::processEvents();
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Rasterize the Smart Object before using Liquify"));
  auto* bar = qobject_cast<patchy::ui::ZoomStatusBar*>(window.statusBar());
  CHECK(bar != nullptr);
  CHECK(bar->error_message_active());
  save_widget_artifact("status_bar_error_flash", *bar);

  window.statusBar()->showMessage(QStringLiteral("Ready"));
  QApplication::processEvents();
  CHECK(!bar->error_message_active());
}

// Transform, Crop and Type sessions share one Apply/Cancel pair (GitHub issue
// 182). Their tints come from icon_success and icon_danger at paint time, so the
// same buttons have to recolor on a live scheme switch and read on the options
// bar in both schemes. Cancel is a round "stop" sign of its own: the square
// "clear" glyph it used to borrow is also Layer > Clear, which erases pixels.
void ui_session_commit_cancel_icons_read_in_both_schemes() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1800, 900);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 125)));
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // Captures of the Transform bar's Apply/Cancel pair in both schemes for the
  // issue thread, from the warp toggle to just past Cancel. Taken before the
  // checks so a failing run still leaves them.
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* warp_mode = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  auto* transform_cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  CHECK(options_bar != nullptr && warp_mode != nullptr && transform_cancel != nullptr);
  if (options_bar != nullptr && warp_mode != nullptr && transform_cancel != nullptr) {
    for (const auto preference :
         {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
      ColorSchemeRestorer::apply(preference);
      CHECK(transform_cancel->isVisible());
      const auto left = warp_mode->mapTo(options_bar, QPoint(0, 0)).x() - 10;
      const auto right = transform_cancel->mapTo(options_bar, QPoint(transform_cancel->width(), 0)).x() + 10;
      const auto shot = options_bar->grab(QRect(left, 0, right - left, options_bar->height())).toImage();
      save_image_artifact(preference == patchy::ui::ColorSchemePreference::Light
                              ? "issue182-transform-session-buttons-light"
                              : "issue182-transform-session-buttons-dark",
                          shot);
    }
  }
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  struct SessionPair {
    const char* label;
    const char* apply;
    const char* cancel;
  };
  const std::array<SessionPair, 3> pairs{{
      {"transform", "freeTransformApplyButton", "freeTransformCancelButton"},
      {"crop", "cropApplyButton", "cropCancelButton"},
      {"text", "textApplyButton", "textCancelButton"},
  }};
  constexpr QSize kIconSize(20, 20);
  const auto render = [&](const QIcon& icon) {
    return icon.pixmap(kIconSize, 1.0, QIcon::Normal).toImage().convertToFormat(QImage::Format_ARGB32);
  };
  // Every well-covered pixel of the glyph is the role's color (antialiased edges
  // aside), and there is enough of it to read.
  const auto check_glyph_ink = [&](const QImage& glyph, const QColor& expected, const char* what) {
    int inked = 0;
    int off_color = 0;
    for (int y = 0; y < glyph.height(); ++y) {
      for (int x = 0; x < glyph.width(); ++x) {
        const auto pixel = glyph.pixelColor(x, y);
        if (pixel.alpha() < 230) {
          continue;
        }
        ++inked;
        if (channel_delta(pixel, expected) > 12) {
          ++off_color;
        }
      }
    }
    if (inked < 20 || off_color != 0) {
      fprintf(stderr, "  %s: %d inked pixels, %d not %s\n", what, inked, off_color,
              qPrintable(expected.name(QColor::HexRgb)));
    }
    CHECK(inked >= 20);
    CHECK(off_color == 0);
  };

  const auto clear_glyph = [&] { return render(patchy::ui::simple_icon(QStringLiteral("clear"))); };
  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    // No setIcon after the switch: the icons built in Dark must repaint themselves.
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    const char* scheme = light ? "light" : "dark";
    const auto& palette = patchy::ui::theme();
    for (const auto* role : {&palette.icon_success, &palette.icon_danger}) {
      const double ratio = contrast_ratio(*role, palette.options_button_bg);
      if (ratio < 3.0) {
        fprintf(stderr, "  %s: %s on the options-bar button -> contrast %.2f, want >= 3.0\n", scheme,
                qPrintable(role->name(QColor::HexRgb)), ratio);
      }
      CHECK(ratio >= 3.0);
    }

    for (const auto& pair : pairs) {
      auto* apply = window.findChild<QPushButton*>(QString::fromLatin1(pair.apply));
      auto* cancel = window.findChild<QPushButton*>(QString::fromLatin1(pair.cancel));
      CHECK(apply != nullptr && cancel != nullptr);
      if (apply == nullptr || cancel == nullptr) {
        continue;
      }
      CHECK(apply->iconSize() == kIconSize && cancel->iconSize() == kIconSize);
      const auto apply_glyph = render(apply->icon());
      const auto cancel_glyph = render(cancel->icon());
      const auto what = [&](const char* button) {
        return QStringLiteral("%1 %2 %3").arg(QString::fromLatin1(scheme), QString::fromLatin1(pair.label),
                                              QString::fromLatin1(button));
      };
      check_glyph_ink(apply_glyph, palette.icon_success, qPrintable(what("apply")));
      check_glyph_ink(cancel_glyph, palette.icon_danger, qPrintable(what("cancel")));
      // A round sign: no ink in the corners, where the square "clear" glyph has it
      // (both fill the same 29/32 box since GitHub issue 338).
      for (const QPoint corner : {QPoint(2, 2), QPoint(17, 2), QPoint(2, 17), QPoint(17, 17)}) {
        CHECK(cancel_glyph.pixelColor(corner).alpha() < 40);
      }
      CHECK(cancel_glyph != clear_glyph());
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests_part4() {
  return {
      {"ui_theme_palettes_define_every_role", ui_theme_palettes_define_every_role},
      {"ui_theme_adjacent_roles_stay_distinguishable", ui_theme_adjacent_roles_stay_distinguishable},
      {"ui_theme_text_roles_contrast_with_their_backgrounds", ui_theme_text_roles_contrast_with_their_backgrounds},
      {"ui_light_scheme_dialog_chrome_separates_from_canvas", ui_light_scheme_dialog_chrome_separates_from_canvas},
      {"ui_dialog_chrome_outline_reads_at_the_dialog_edge", ui_dialog_chrome_outline_reads_at_the_dialog_edge},
      {"ui_light_scheme_panel_scroll_bar_handle_reads_against_track", ui_light_scheme_panel_scroll_bar_handle_reads_against_track},
      {"ui_tooltips_follow_the_active_theme", ui_tooltips_follow_the_active_theme},
      {"ui_light_scheme_color_picker_selected_tab_reads_as_selected", ui_light_scheme_color_picker_selected_tab_reads_as_selected},
      {"ui_light_scheme_about_dialog_reads_as_a_light_surface", ui_light_scheme_about_dialog_reads_as_a_light_surface},
      {"ui_theme_qss_resolves_every_token", ui_theme_qss_resolves_every_token},
      {"ui_status_bar_error_message_flashes_then_persists_until_replaced", ui_status_bar_error_message_flashes_then_persists_until_replaced},
      {"ui_blocking_refusal_shows_error_status_and_info_clears_it", ui_blocking_refusal_shows_error_status_and_info_clears_it},
      {"ui_session_commit_cancel_icons_read_in_both_schemes", ui_session_commit_cancel_icons_read_in_both_schemes},
  };
}
