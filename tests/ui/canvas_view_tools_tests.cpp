#include <QActionGroup>

#include "ui/canvas_widget.hpp"
#include "ai/segmentation_engine.hpp"
#include "ui/main_window_shared.hpp"
#include "core/palette.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/curved_slider.hpp"
#include "ui/gradient_library.hpp"
#include "ui/main_window.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/options_flow_layout.hpp"
#include "ui/app_settings.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "ui/status_document_info.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_api.hpp"
#include "color/color_management.hpp"
#include "core/pixel_depth.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "core/pattern_resource.hpp"
#include "core/style_presets.hpp"
#include "ui/image_document_io.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QColorSpace>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontComboBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QPixmap>
#include <QAbstractButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSplitter>
#include <QScrollBar>
#include <QSlider>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <list>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "scripting_test_support.hpp"
#if PATCHY_HAVE_QT_DBUS
#include "ui/portal_color_pick.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServer>
#include <QDir>
#include <initializer_list>
#endif

namespace {

using namespace patchy::test::ui;

// The Brush and Eraser bars come back from the settings store (GitHub issue 254; the
// gradient keys always did), with the defaults for the keys the store lacks (Flow here).
void ui_startup_restores_stored_brush_and_gradient_settings() {
  SettingsValueRestorer saved_brush_preset(QStringLiteral("tools/brushPreset"));
  SettingsValueRestorer saved_brush_size(QStringLiteral("tools/brushSize"));
  SettingsValueRestorer saved_brush_opacity(QStringLiteral("tools/brushOpacity"));
  SettingsValueRestorer saved_brush_flow(QStringLiteral("tools/brushFlow"));
  SettingsValueRestorer saved_brush_softness(QStringLiteral("tools/brushSoftness"));
  SettingsValueRestorer saved_brush_airbrush(QStringLiteral("tools/brushAirbrush"));
  SettingsValueRestorer saved_brush_tip(QStringLiteral("tools/brushTip"));
  SettingsValueRestorer saved_active_tool(QStringLiteral("tools/activeTool"));
  SettingsValueRestorer saved_eraser_size(QStringLiteral("tools/eraserSize"));
  SettingsValueRestorer saved_eraser_opacity(QStringLiteral("tools/eraserOpacity"));
  SettingsValueRestorer saved_eraser_softness(QStringLiteral("tools/eraserSoftness"));
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  {
    auto settings = patchy::ui::app_settings();
    // The brush state an earlier session left behind: the next launch shows it.
    settings.remove(QStringLiteral("tools/brushFlow"));
    settings.remove(QStringLiteral("tools/brushTip"));
    settings.remove(QStringLiteral("tools/activeTool"));
    settings.setValue(QStringLiteral("tools/brushPreset"), QStringLiteral("airbrush"));
    settings.setValue(QStringLiteral("tools/brushSize"), 56);
    settings.setValue(QStringLiteral("tools/brushOpacity"), 12);
    settings.setValue(QStringLiteral("tools/brushSoftness"), 100);
    settings.setValue(QStringLiteral("tools/brushAirbrush"), true);
    settings.setValue(QStringLiteral("tools/eraserSize"), 77);
    settings.setValue(QStringLiteral("tools/eraserOpacity"), 15);
    settings.setValue(QStringLiteral("tools/eraserSoftness"), 95);
    settings.setValue(QStringLiteral("tools/gradientMethod"), static_cast<int>(patchy::GradientMethod::Radial));
    settings.setValue(QStringLiteral("tools/gradientReverse"), true);
    settings.setValue(QStringLiteral("tools/gradientOpacity"), 66);
    settings.setValue(QStringLiteral("tools/gradientUseCustomStops"), true);
    settings.setValue(QStringLiteral("tools/gradientStops"),
                      QStringLiteral("[{\"location\":0,\"r\":10,\"g\":20,\"b\":30,\"a\":255},"
                                     "{\"location\":1,\"r\":40,\"g\":50,\"b\":60,\"a\":128}]"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* brush_airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_flow != nullptr);
  CHECK(brush_airbrush != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Brush);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("airbrush"));
  CHECK(brush_size->value() == 56);
  CHECK(brush_opacity->value() == 12);
  CHECK(brush_flow->value() == 100);  // no stored key: the Round preset's default
  CHECK(brush_airbrush->isChecked());
  CHECK(brush_softness->value() == 100);
  CHECK(canvas->brush_size() == 56);
  CHECK(canvas->brush_opacity() == 12);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 100);
  CHECK(canvas->brush_build_up());
  // The eraser keeps its own size, opacity and Soft; it has no Flow or Airbrush.
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  CHECK(canvas->brush_size() == 77);
  CHECK(canvas->brush_opacity() == 15);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 95);
  CHECK(!canvas->brush_build_up());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  CHECK(canvas->brush_size() == 56);
  CHECK(canvas->brush_opacity() == 12);
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_reverse());
  CHECK(canvas->gradient_opacity() == 66);
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 2);
  CHECK(gradient_method->currentData().toInt() == static_cast<int>(patchy::GradientMethod::Radial));
  CHECK(gradient_opacity->value() == 66);
  CHECK(gradient_reverse->isChecked());
}

void ui_options_bar_spinboxes_fit_widest_value() {
  // "100%" in the brush Opacity box rendered as "100": the 52px fixed width left
  // no room for the suffix once the popup chevron claimed its 14px text margin.
  // configure_toolbar_spinbox now treats the requested width as a minimum and
  // grows the box to fit its widest value text; require chevron + box chrome
  // clearance (14 + 14 in dialog_utils.cpp) beyond the min/max text on every
  // options-bar spin box.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(toolbar != nullptr);
  const auto require_fits = [](const QWidget* spin, const QString& text) {
    const int required = spin->fontMetrics().horizontalAdvance(text) + 28;
    if (spin->minimumWidth() < required) {
      std::fprintf(stderr, "  %s: width %d < %d needed for \"%s\"\n",
                   qPrintable(spin->objectName()), spin->minimumWidth(), required,
                   qPrintable(text));
    }
    CHECK(spin->minimumWidth() >= required);
  };
  int checked = 0;
  for (const auto* spin : toolbar->findChildren<QSpinBox*>()) {
    if (!spin->property("patchy.numericPopupInstalled").toBool()) {
      continue;
    }
    const auto locale = spin->locale();
    require_fits(spin, spin->prefix() + locale.toString(spin->minimum()) + spin->suffix());
    require_fits(spin, spin->prefix() + locale.toString(spin->maximum()) + spin->suffix());
    ++checked;
  }
  for (const auto* spin : toolbar->findChildren<QDoubleSpinBox*>()) {
    if (!spin->property("patchy.numericPopupInstalled").toBool()) {
      continue;
    }
    const auto locale = spin->locale();
    // A box sized from a sample value (Text Size drops trailing zeros, so its max shows
    // as "10000 pt") must fit that sample and the text it really shows at the extremes.
    if (const auto sample = spin->property(patchy::ui::kToolbarSpinboxWidthSampleProperty); sample.isValid()) {
      require_fits(spin, spin->prefix() + locale.toString(sample.toDouble(), 'f', spin->decimals()) +
                             spin->suffix());
      require_fits(spin, spin->prefix() + spin->textFromValue(spin->minimum()) + spin->suffix());
      require_fits(spin, spin->prefix() + spin->textFromValue(spin->maximum()) + spin->suffix());
      ++checked;
      continue;
    }
    require_fits(spin, spin->prefix() + locale.toString(spin->minimum(), 'f', spin->decimals()) +
                           spin->suffix());
    require_fits(spin, spin->prefix() + locale.toString(spin->maximum(), 'f', spin->decimals()) +
                           spin->suffix());
    ++checked;
  }
  CHECK(checked >= 10);
}

void ui_options_bar_spinboxes_show_their_extremes_unclipped() {
  // "255" in the Fill tool's Tol box rendered as a clipped "55" on screen (September 2026).
  // ui_options_bar_spinboxes_fit_widest_value only checks the requested width against a
  // constant; this measures the live editor once the tool's row is shown, which is what
  // loses the box's QSS padding and borders, the popup chevron's text margin, and
  // QLineEdit's own horizontal margins before any text is drawn. It runs on the real
  // windows platform too (DirectWrite advances are wider than offscreen FreeType's):
  //   patchy_ui_visual_tests.exe ui_options_bar_spinboxes_show   (no QT_QPA_PLATFORM)
  patchy::ui::MainWindow window;
  show_window(window);
  auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(toolbar != nullptr);
  int checked = 0;
  for (const auto* action_name : {"toolFillAction", "toolMagicWandAction", "toolBrushAction"}) {
    require_action(window, action_name)->trigger();
    QApplication::processEvents();
    for (const auto* spin : toolbar->findChildren<QSpinBox*>()) {
      if (!spin->isVisible() || !spin->property("patchy.numericPopupInstalled").toBool()) {
        continue;
      }
      const auto* editor = spin->findChild<QLineEdit*>();
      CHECK(editor != nullptr);
      if (editor == nullptr) {
        continue;
      }
      // The editor sits inside the box's padding and borders, so a laid-out one is narrower.
      CHECK(editor->width() < spin->width());
      // QLineEdit keeps a 2px horizontal margin on each side of its text area.
      const int available = editor->width() - editor->textMargins().left() -
                            editor->textMargins().right() - 4;
      const auto locale = spin->locale();
      for (const auto& text : {spin->prefix() + locale.toString(spin->minimum()) + spin->suffix(),
                               spin->prefix() + locale.toString(spin->maximum()) + spin->suffix()}) {
        // The caret sits after the widest value while it is edited, so it needs room too.
        const int needed = editor->fontMetrics().horizontalAdvance(text) + 2;
        if (available < needed) {
          std::fprintf(stderr, "  %s: editor shows %d px but \"%s\" needs %d px (box %d px)\n",
                       qPrintable(spin->objectName()), available, qPrintable(text), needed,
                       spin->width());
        }
        CHECK(available >= needed);
      }
      ++checked;
    }
  }
  CHECK(checked >= 4);
}

// Every tool action in the palette, in CanvasTool order. Object Selection is left out:
// choosing it can start the model download offer.
std::vector<QAction*> options_bar_test_tool_actions(patchy::ui::MainWindow& window) {
  auto* brush = require_action(window, "toolBrushAction");
  std::vector<QAction*> tools;
  if (brush->actionGroup() == nullptr) {
    return tools;
  }
  for (auto* action : brush->actionGroup()->actions()) {
    if (action->data().toInt() != static_cast<int>(patchy::ui::CanvasTool::ObjectSelect)) {
      tools.push_back(action);
    }
  }
  std::sort(tools.begin(), tools.end(),
            [](QAction* a, QAction* b) { return a->data().toInt() < b->data().toInt(); });
  return tools;
}

// The right edge of the active tool's controls and how many lines they take.
std::pair<int, int> options_row_extent(QWidget& options) {
  int right = 0;
  std::vector<int> tops;
  for (auto* child : options.findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
    if (!child->isVisible()) {
      continue;
    }
    right = std::max(right, child->geometry().right() + 1);
    const int top = child->geometry().top();
    if (std::none_of(tops.begin(), tops.end(), [top](int other) { return std::abs(other - top) < 8; })) {
      tops.push_back(top);
    }
  }
  return {right, static_cast<int>(tops.size())};
}

void ui_options_bar_keeps_one_height_across_tools() {
  // GitHub issue 91: on a 1366 px laptop screen the Brush and Text rows wrapped onto a
  // second line, so every switch to or from them moved the tool palette, the canvas and
  // the docks. The bar now takes the tallest tool row's height at the current width, and
  // at 1366 px every row fits on one line.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options_bar != nullptr && options != nullptr);
  const auto tools = options_bar_test_tool_actions(window);
  CHECK(tools.size() >= 30);

  const auto settle = [] {
    QApplication::processEvents();
    process_events_for(20);
  };
  window.resize(2600, 800);
  settle();
  int single_row_height = 0;
  int widest_row = 0;
  for (auto* action : tools) {
    action->trigger();
    settle();
    const auto [right, lines] = options_row_extent(*options);
    std::printf("  %-28s natural row %4d px\n", qPrintable(action->objectName()), right);
    CHECK(lines <= 1);
    widest_row = std::max(widest_row, right);
    if (single_row_height == 0) {
      single_row_height = options_bar->height();
    }
    CHECK(options_bar->height() == single_row_height);
  }
  std::printf("  widest row %d px; single-row bar %d px\n", widest_row, single_row_height);

  // At a width where some tool needs two rows, the bar grows the first time such a
  // tool is picked and then stays, so switching back never moves the canvas; a
  // tool that fits never adds an empty second row on its own (the empty-row
  // complaint on issue 91). A resize forgets the reservation.
  int wrapped_rows = 0;
  for (const int width : {1366, 1024, 800}) {
    require_action(window, "toolMoveAction")->trigger();
    window.resize(width, 738);
    settle();
    int common_height = options_bar->height();
    // Move fits one row at 1366 px; with the Align and Distribute buttons (GitHub
    // issue 172) it wraps in narrower windows like the Brush row.
    if (width == 1366) {
      CHECK(common_height == single_row_height);
    }
    for (auto* action : tools) {
      action->trigger();
      settle();
      if (options_bar->height() < common_height) {
        std::fprintf(stderr, "  %d px: %s shrank the bar to %d px from %d px\n", width,
                     qPrintable(action->objectName()), options_bar->height(), common_height);
      }
      CHECK(options_bar->height() >= common_height);
      common_height = std::max(common_height, options_bar->height());
      if (width == 1366) {
        const auto [right, lines] = options_row_extent(*options);
        if (lines > 1) {
          // Name every control in the row so the overrun is easy to budget.
          std::fprintf(stderr, "  1366 px: %s wraps onto %d lines (container %d px):",
                       qPrintable(action->objectName()), lines, options->width());
          for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (child->isVisible()) {
              const auto* label = qobject_cast<QLabel*>(child);
              const auto name = !child->objectName().isEmpty() ? child->objectName()
                                : label != nullptr            ? QStringLiteral("\"%1\"").arg(label->text())
                                                              : QString::fromLatin1(child->metaObject()->className());
              std::fprintf(stderr, " %s=%d", qPrintable(name), child->width());
            }
          }
          std::fprintf(stderr, "\n");
          ++wrapped_rows;
        }
      }
    }
    std::printf("  %4d px window: bar %d px\n", width, common_height);
    if (width == 1366) {
      CHECK(wrapped_rows == 0);
      CHECK(common_height == single_row_height);
    }
  }
}

// The active tool's controls left to right, one token each: "|" for a separator, a
// label's text, a mode button's action name, otherwise the widget's object name.
std::vector<QString> options_row_tokens(QWidget& options) {
  std::vector<QWidget*> visible;
  for (auto* child : options.findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
    if (child->isVisible()) {
      visible.push_back(child);
    }
  }
  std::sort(visible.begin(), visible.end(), [](QWidget* a, QWidget* b) {
    const auto key = [](QWidget* widget) { return std::make_pair(widget->geometry().center().y() / 20, widget->x()); };
    return key(a) < key(b);
  });
  std::vector<QString> tokens;
  for (auto* child : visible) {
    const auto* label = qobject_cast<QLabel*>(child);
    const auto* button = qobject_cast<QToolButton*>(child);
    if (child->objectName() == QLatin1String("optionSeparator")) {
      tokens.push_back(QStringLiteral("|"));
    } else if (!child->objectName().isEmpty()) {
      tokens.push_back(child->objectName());
    } else if (label != nullptr) {
      tokens.push_back(label->text());
    } else if (button != nullptr && button->defaultAction() != nullptr) {
      tokens.push_back(button->defaultAction()->objectName());
    } else {
      tokens.push_back(QString::fromLatin1(child->metaObject()->className()));
    }
  }
  return tokens;
}

// GitHub issues 286, 290, 291 and 292: separators group the selection tools' options, the
// Magic Wand spells out Tolerance, Quick Select's Size comes first, and the marquee pairs get
// a swap button for Fixed Ratio and Fixed Size. Every one of these bars still fits one row at
// 1366 px. The artifacts are the issue shots, in both schemes.
void ui_selection_options_bars_group_their_controls() {
  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-none");
  patchy::ui::configure_automatic_selection_models();
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
    CHECK(options_bar != nullptr && options != nullptr && style != nullptr);
    if (options_bar == nullptr || options == nullptr || style == nullptr) {
      return;
    }
    const QString separator = QStringLiteral("|");
    const auto row = [&](const char* what) {
      QApplication::processEvents();
      process_events_for(20);
      auto tokens = options_row_tokens(*options);
      std::printf("  %-34s", what);
      for (const auto& token : tokens) {
        std::printf(" %s", qPrintable(token));
      }
      std::printf("\n");
      const auto [right, lines] = options_row_extent(*options);
      if (lines != 1) {
        std::fprintf(stderr, "  %s takes %d lines at 1366 px (row %d px)\n", what, lines, right);
      }
      CHECK(lines == 1);
      return tokens;
    };
    const auto at = [](const std::vector<QString>& tokens, const QString& token) {
      const auto found = std::find(tokens.begin(), tokens.end(), token);
      return found == tokens.end() ? -1 : static_cast<int>(found - tokens.begin());
    };
    const auto next_to = [&at](const std::vector<QString>& tokens, const QString& left, const QString& right) {
      const int index = at(tokens, left);
      return index >= 0 && index + 1 < static_cast<int>(tokens.size()) && tokens[index + 1] == right;
    };
    const auto separated_before = [&at, &separator](const std::vector<QString>& tokens, const QString& token) {
      const int index = at(tokens, token);
      return index > 0 && tokens[index - 1] == separator;
    };
    const auto shot = [&](const std::string& name) { save_widget_artifact(name + suffix, *options_bar); };

    // Marquee and Elliptical Marquee: Feather and Anti-alias | Radius and Style | the pair.
    const std::array<const char*, 3> style_names{"normal", "fixed_ratio", "fixed_size"};
    for (const char* tool : {"toolMarqueeAction", "toolEllipticalMarqueeAction"}) {
      require_action(window, tool)->trigger();
      for (int index = 0; index < 3; ++index) {
        style->setCurrentIndex(index);
        const auto tokens = row(tool);
        CHECK(next_to(tokens, QStringLiteral("selectionAntiAliasCheck"), separator));
        const bool rectangular = std::string_view(tool) == "toolMarqueeAction";
        CHECK(separated_before(tokens, rectangular ? QStringLiteral("Radius") : QStringLiteral("Style")));
        CHECK(separated_before(tokens, QStringLiteral("Width")));
        auto* ratio_swap = window.findChild<QAbstractButton*>(QStringLiteral("selectionRatioSwapButton"));
        auto* size_swap = window.findChild<QAbstractButton*>(QStringLiteral("selectionFixedSizeSwapButton"));
        CHECK(ratio_swap != nullptr && size_swap != nullptr);
        CHECK((at(tokens, QStringLiteral("selectionRatioSwapButton")) >= 0) == (index == 1));
        CHECK((at(tokens, QStringLiteral("selectionFixedSizeSwapButton")) >= 0) == (index == 2));
        if (index == 1) {
          CHECK(next_to(tokens, QStringLiteral("selectionRatioWidthSpin"), QStringLiteral("selectionRatioSwapButton")));
          CHECK(next_to(tokens, QStringLiteral("selectionRatioSwapButton"), QStringLiteral("Height")));
        }
        if (index == 2) {
          CHECK(next_to(tokens, QStringLiteral("selectionFixedWidthSpin"),
                        QStringLiteral("selectionFixedSizeSwapButton")));
          CHECK(next_to(tokens, QStringLiteral("selectionFixedSizeSwapButton"), QStringLiteral("Height")));
        }
        if (rectangular) {
          shot(std::string("issue_286_marquee_options_bar_") + style_names[static_cast<std::size_t>(index)]);
        } else if (index == 2) {
          shot("issue_286_elliptical_marquee_options_bar_fixed_size");
        }
      }
    }
    style->setCurrentIndex(0);

    // Magic Wand: Tolerance spelled out and set apart; Contiguous keeps its name.
    require_action(window, "toolMagicWandAction")->trigger();
    {
      const auto tokens = row("toolMagicWandAction");
      CHECK(next_to(tokens, QStringLiteral("Tolerance"), QStringLiteral("wandToleranceSpin")));
      CHECK(next_to(tokens, QStringLiteral("wandToleranceSpin"), separator));
      auto* contiguous = window.findChild<QCheckBox*>(QStringLiteral("wandContiguousCheck"));
      CHECK(contiguous != nullptr && contiguous->text() == QStringLiteral("Contiguous"));
      shot("issue_290_magic_wand_options_bar");
    }

    // Quick Select: Size | Feather | the checkboxes (no Anti-alias, as in Photoshop).
    require_action(window, "toolQuickSelectAction")->trigger();
    {
      const auto tokens = row("toolQuickSelectAction");
      const int size = at(tokens, QStringLiteral("Size"));
      const int feather = at(tokens, QStringLiteral("selectionFeatherLabel"));
      CHECK(size >= 0 && feather > size);
      CHECK(next_to(tokens, QStringLiteral("Size"), QStringLiteral("quickSelectSizeSpin")));
      CHECK(separated_before(tokens, QStringLiteral("selectionFeatherLabel")));
      CHECK(next_to(tokens, QStringLiteral("selectionFeatherSpin"), separator));
      CHECK(at(tokens, QStringLiteral("selectionAntiAliasCheck")) < 0);
      shot("issue_291_quick_select_options_bar");
    }

    // Object Selection: Feather, Anti-alias, Sample All Layers | Model: Anti-alias stays beside
    // Feather as on the other selection bars, and the checkboxes stay together.
    if (patchy::ai::segmentation_available()) {
      require_action(window, "toolObjectSelectionAction")->trigger();
      const auto tokens = row("toolObjectSelectionAction");
      CHECK(next_to(tokens, QStringLiteral("selectionFeatherSpin"), QStringLiteral("selectionAntiAliasCheck")));
      CHECK(next_to(tokens, QStringLiteral("selectionAntiAliasCheck"), QStringLiteral("objectSelectSampleAllLayersCheck")));
#if !defined(Q_OS_WASM)
      CHECK(separated_before(tokens, QStringLiteral("Model")));
#endif
      shot("issue_292_object_selection_options_bar");
    }
  }
}

// GitHub issue 311: the shape bars follow the grouping rules (docs/options-bar.md). In Shape
// mode each reads mode | Fill | Stroke, its swatch and width | W/H | Combine | the tool's own
// settings | Appearance, Line's Weight now follows Combine beside the arrowheads, the Fill and
// Stroke swatches are framed buttons with a wide chip, and the keep-proportions toggle sits
// evenly between the W field and the H label. Every bar fits one row at 1366 px. The
// artifacts are the issue shots, in both schemes.
void ui_shape_options_bars_group_their_controls() {
  struct ShapeBar {
    const char* tool;
    const char* name;
    const char* own;  // first control of the tool's own group, or nullptr
  };
  const std::array<ShapeBar, 5> bars{{
      {"toolRectAction", "rectangle", "Radius"},
      {"toolEllipseAction", "ellipse", nullptr},
      {"toolPolygonAction", "polygon", "Sides"},
      {"toolLineAction", "line", "Weight"},
      {"toolCustomShapeAction", "custom_shape", "Shape"},
  }};
  std::vector<std::string> problems;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
    CHECK(options_bar != nullptr && options != nullptr && mode != nullptr);
    if (options_bar == nullptr || options == nullptr || mode == nullptr) {
      return;
    }
    const QString separator = QStringLiteral("|");
    const auto at = [](const std::vector<QString>& tokens, const QString& token) {
      const auto found = std::find(tokens.begin(), tokens.end(), token);
      return found == tokens.end() ? -1 : static_cast<int>(found - tokens.begin());
    };
    for (const auto& bar : bars) {
      require_action(window, bar.tool)->trigger();
      mode->setCurrentIndex(0);  // Shape
      QApplication::processEvents();
      process_events_for(20);
      save_widget_artifact(std::string("issue_311_") + bar.name + "_options_bar" + suffix, *options_bar);
      const auto tokens = options_row_tokens(*options);
      std::printf("  %-22s", (bar.name + suffix).c_str());
      for (const auto& token : tokens) {
        std::printf(" %s", qPrintable(token));
      }
      std::printf("\n");
      const auto problem = [&](const std::string& what) { problems.push_back(bar.name + suffix + ": " + what); };
      const auto expect_separator_before = [&](const QString& token) {
        const int index = at(tokens, token);
        if (index <= 0 || tokens[static_cast<std::size_t>(index) - 1] != separator) {
          problem("no separator before " + token.toStdString());
        }
      };
      const auto expect_next_to = [&](const QString& left, const QString& right) {
        const int index = at(tokens, left);
        if (index < 0 || index + 1 >= static_cast<int>(tokens.size()) ||
            tokens[static_cast<std::size_t>(index) + 1] != right) {
          problem(left.toStdString() + " is not followed by " + right.toStdString());
        }
      };
      for (const char* token : {"Fill", "vectorStrokeCheck", "W", "Combine", "vectorAppearanceButton"}) {
        expect_separator_before(QLatin1String(token));
      }
      if (bar.own != nullptr) {
        expect_separator_before(QLatin1String(bar.own));
        if (at(tokens, QLatin1String(bar.own)) < at(tokens, QStringLiteral("vectorCombineCombo"))) {
          problem(std::string(bar.own) + " comes before Combine");
        }
      } else {
        expect_next_to(QStringLiteral("vectorCombineCombo"), separator);
      }
      // One group each: Fill and its swatch; the Stroke checkbox, swatch and width.
      expect_next_to(QStringLiteral("Fill"), QStringLiteral("vectorFillSwatchButton"));
      expect_next_to(QStringLiteral("vectorFillSwatchButton"), separator);
      expect_next_to(QStringLiteral("vectorStrokeCheck"), QStringLiteral("vectorStrokeSwatchButton"));
      expect_next_to(QStringLiteral("vectorStrokeSwatchButton"), QStringLiteral("vectorStrokeWidthLabel"));
      if (std::string_view(bar.name) == "line") {
        expect_next_to(QStringLiteral("Weight"), QStringLiteral("vectorLineWeightSpin"));
        expect_next_to(QStringLiteral("vectorLineWeightSpin"), QStringLiteral("lineArrowStartButton"));
        expect_next_to(QStringLiteral("lineArrowStartButton"), QStringLiteral("lineArrowEndButton"));
        expect_next_to(QStringLiteral("lineArrowEndButton"), separator);
      }
      if (const auto [right, lines] = options_row_extent(*options); lines != 1) {
        problem("takes " + std::to_string(lines) + " rows at 1366 px (row " + std::to_string(right) + " px)");
      }

      const QImage image = options->grab().toImage();
      // The swatches are framed buttons with a wide chip: the frame differs from the bar beside it.
      for (const char* name : {"vectorFillSwatchButton", "vectorStrokeSwatchButton"}) {
        auto* swatch = window.findChild<QToolButton*>(QLatin1String(name));
        if (swatch == nullptr || !swatch->isVisible()) {
          problem(std::string(name) + " not shown");
          continue;
        }
        const QRect box = swatch->geometry();
        if (box.width() < 36 || box.height() < 22 || swatch->iconSize().width() < 28 ||
            swatch->iconSize().height() < 14) {
          problem(std::string(name) + " is too small to read as a button");
        }
        const QColor bar_color = image.pixelColor(box.left() - 2, box.center().y());
        const QColor frame = image.pixelColor(box.left(), box.center().y() - 3);
        const QColor face = image.pixelColor(box.left() + 2, box.center().y());
        if (frame == bar_color && face == bar_color) {
          problem(std::string(name) + " has no button frame");
        }
      }
      // The keep-proportions toggle: as far from the W field as from the H label's text.
      auto* width_spin = window.findChild<QWidget*>(QStringLiteral("vectorShapeWidthSpin"));
      auto* link = window.findChild<QWidget*>(QStringLiteral("vectorShapeLinkSizeButton"));
      auto* height_label = window.findChild<QWidget*>(QStringLiteral("vectorShapeHeightLabel"));
      if (width_spin == nullptr || link == nullptr || height_label == nullptr || !link->isVisible()) {
        problem("W/H pair not found");
        continue;
      }
      const QRect field = width_spin->geometry();
      const QRect button = link->geometry();
      const QRect label = height_label->geometry();
      const QColor background = image.pixelColor(field.right() + 2, button.center().y());
      int text_left = -1;
      for (int x = label.left(); x <= label.right() && text_left < 0; ++x) {
        for (int y = label.top(); y <= label.bottom(); ++y) {
          const QColor color = image.pixelColor(x, y);
          if (std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
                  std::abs(color.blue() - background.blue()) > 90) {
            text_left = x;
            break;
          }
        }
      }
      const int left_gap = button.left() - (field.right() + 1);
      const int right_gap = text_left - (button.right() + 1);
      std::printf("  %s%s: field to toggle %d px, toggle to H %d px\n", bar.name, suffix.c_str(), left_gap,
                  right_gap);
      if (text_left < 0 || std::abs(left_gap - right_gap) > 1) {
        problem("uneven space around the keep-proportions toggle: " + std::to_string(left_gap) + " and " +
                std::to_string(right_gap));
      }
    }
  }
  for (const auto& problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  CHECK(problems.empty());
}

// GitHub issue 324: the Crop bar follows the grouping rules (docs/options-bar.md) in both
// styles: Style | the preset, W : H pair and Clear (Ratio) or the size fields (Size) | the two
// Delete checkboxes | Apply and Cancel. Clear stays beside the pair it clears (rule 2). Both
// styles fit one row at 1366 px. The artifacts are the issue shots, in both schemes.
void ui_crop_options_bar_groups_its_controls() {
  std::vector<std::string> problems;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* style = window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
    CHECK(options_bar != nullptr && options != nullptr && style != nullptr);
    if (options_bar == nullptr || options == nullptr || style == nullptr) {
      return;
    }
    const QString separator = QStringLiteral("|");
    require_action(window, "toolCropAction")->trigger();
    const int original = style->currentIndex();
    for (int index = 0; index < 2; ++index) {
      style->setCurrentIndex(index);
      QApplication::processEvents();
      process_events_for(20);
      const std::string name = std::string(index == 0 ? "ratio" : "size") + suffix;
      save_widget_artifact("issue_324_crop_options_bar_" + name, *options_bar);
      const auto tokens = options_row_tokens(*options);
      std::printf("  %-12s", name.c_str());
      for (const auto& token : tokens) {
        std::printf(" %s", qPrintable(token));
      }
      std::printf("\n");
      const auto problem = [&](const std::string& what) { problems.push_back(name + ": " + what); };
      const auto at = [&tokens](const QString& token) {
        const auto found = std::find(tokens.begin(), tokens.end(), token);
        return found == tokens.end() ? -1 : static_cast<int>(found - tokens.begin());
      };
      const auto expect_next_to = [&](const QString& left, const QString& right) {
        const int i = at(left);
        if (i < 0 || i + 1 >= static_cast<int>(tokens.size()) || tokens[static_cast<std::size_t>(i) + 1] != right) {
          problem(left.toStdString() + " is not followed by " + right.toStdString());
        }
      };
      expect_next_to(QStringLiteral("cropStyleCombo"), separator);
      if (index == 0) {
        expect_next_to(QStringLiteral("cropRatioHeightSpin"), QStringLiteral("cropRatioClearButton"));
        expect_next_to(QStringLiteral("cropRatioClearButton"), separator);
      } else {
        expect_next_to(QStringLiteral("cropHeightSpin"), separator);
      }
      expect_next_to(QStringLiteral("cropDeletePixelsCheck"), QStringLiteral("cropDeleteLayersCheck"));
      expect_next_to(QStringLiteral("cropDeleteLayersCheck"), separator);
      expect_next_to(QStringLiteral("cropApplyButton"), QStringLiteral("cropCancelButton"));
      const int pixels = at(QStringLiteral("cropDeletePixelsCheck"));
      const int apply = at(QStringLiteral("cropApplyButton"));
      if (pixels <= 0 || tokens[static_cast<std::size_t>(pixels) - 1] != separator) {
        problem("no separator before the Delete checkboxes");
      }
      if (apply <= 0 || tokens[static_cast<std::size_t>(apply) - 1] != separator) {
        problem("no separator before Apply");
      }
      if (tokens.empty() || tokens.back() != QStringLiteral("cropCancelButton")) {
        problem("Cancel is not last");
      }
      if (const auto count = std::count(tokens.begin(), tokens.end(), separator); count != 3) {
        problem(std::to_string(count) + " separators, expected 3");
      }
      if (const auto [right, lines] = options_row_extent(*options); lines != 1) {
        problem("takes " + std::to_string(lines) + " rows at 1366 px (row " + std::to_string(right) + " px)");
      } else {
        std::printf("  %s: one row, %d px\n", name.c_str(), right);
      }
    }
    style->setCurrentIndex(original);
  }
  for (const auto& problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  CHECK(problems.empty());
}

// GitHub issue 286 follow-up: the swap width/height buttons on the Artboard and marquee bars
// show one monochrome glyph in the theme's icon ink, at the bar's icon size, filling its box,
// and dim when disabled. The bars still fit one row at 1366 px. Each bar is captured before
// the checks, so these artifacts are the issue shots in both schemes.
void ui_swap_dimension_buttons_show_a_themed_full_size_glyph() {
  struct SwapCase {
    const char* tool;
    int marquee_style;  // -1: not a marquee
    const char* button;
    const char* shot;
  };
  const std::array<SwapCase, 4> cases{{
      {"toolArtboardAction", -1, "artboardSwapSizeButton", "artboard"},
      {"toolMarqueeAction", 1, "selectionRatioSwapButton", "marquee_fixed_ratio"},
      {"toolMarqueeAction", 2, "selectionFixedSizeSwapButton", "marquee_fixed_size"},
      {"toolEllipticalMarqueeAction", 2, "selectionFixedSizeSwapButton", "elliptical_marquee_fixed_size"},
  }};
  std::vector<std::string> problems;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
    CHECK(options_bar != nullptr && options != nullptr && style != nullptr);
    if (options_bar == nullptr || options == nullptr || style == nullptr) {
      return;
    }
    const QColor ink = patchy::ui::theme().icon_ink;
    const auto expect = [&problems, &suffix](bool ok, const char* button, const char* what) {
      if (!ok) {
        problems.push_back(std::string(button) + suffix + ": " + what);
      }
    };
    for (const auto& swap : cases) {
      require_action(window, swap.tool)->trigger();
      if (swap.marquee_style >= 0) {
        style->setCurrentIndex(swap.marquee_style);
      }
      QApplication::processEvents();
      process_events_for(20);
      save_widget_artifact(std::string("issue_286_swap_") + swap.shot + suffix, *options_bar);
      const auto [right, lines] = options_row_extent(*options);
      expect(lines == 1, swap.button, "the bar wraps at 1366 px");

      auto* widget = window.findChild<QWidget*>(QLatin1String(swap.button));
      expect(widget != nullptr && widget->isVisible(), swap.button, "not shown");
      expect(qobject_cast<QToolButton*>(widget) != nullptr, swap.button,
             "not an options-bar icon button (QToolButton)");
      auto* button = qobject_cast<QAbstractButton*>(widget);
      if (button == nullptr) {
        continue;
      }
      expect(button->toolTip().contains(QStringLiteral("Swap width and height")), swap.button, "tooltip");
      const QSize size = button->iconSize();
      expect(size == options_bar->iconSize(), swap.button, "icon size differs from the bar's");
      const QImage normal = button->icon().pixmap(size, 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
      const QImage disabled =
          button->icon().pixmap(size, 1.0, QIcon::Disabled).toImage().convertToFormat(QImage::Format_ARGB32);
      QRect ink_box;
      int foreign = 0;
      for (int y = 0; y < normal.height(); ++y) {
        for (int x = 0; x < normal.width(); ++x) {
          const QColor pixel = normal.pixelColor(x, y);
          if (pixel.alpha() > 64) {
            ink_box = ink_box.united(QRect(x, y, 1, 1));
          }
          // Solid pixels carry the scheme's icon ink and nothing else.
          if (pixel.alpha() > 200 && std::abs(pixel.red() - ink.red()) + std::abs(pixel.green() - ink.green()) +
                                             std::abs(pixel.blue() - ink.blue()) > 30) {
            ++foreign;
          }
        }
      }
      std::printf("  %s%s: icon %dx%d, ink %dx%d, %d pixels off the icon ink\n", swap.button, suffix.c_str(),
                  size.width(), size.height(), ink_box.width(), ink_box.height(), foreign);
      expect(foreign == 0, swap.button, "colors other than the icon ink");
      expect(ink_box.width() >= size.width() * 3 / 4 && ink_box.height() >= size.height() / 2, swap.button,
             "glyph does not fill its box");
      expect(disabled != normal, swap.button, "does not dim when disabled");
    }
    style->setCurrentIndex(0);

    // The New Document dialog's swap button shows the same glyph.
    auto* bar_swap = window.findChild<QAbstractButton*>(QStringLiteral("artboardSwapSizeButton"));
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
      auto* swap = dialog != nullptr
                       ? dialog->findChild<QToolButton*>(QStringLiteral("newDocumentSwapDimensionsButton"))
                       : nullptr;
      if (dialog != nullptr && swap != nullptr) {
        inspected = true;
        save_window_popup_artifact("issue_286_swap_new_document" + suffix, *dialog, *swap, *swap);
        const QSize size(18, 18);
        expect(bar_swap != nullptr &&
                   swap->icon().pixmap(size, 1.0).toImage() == bar_swap->icon().pixmap(size, 1.0).toImage(),
               "newDocumentSwapDimensionsButton", "not the options bar's swap glyph");
      }
      if (dialog != nullptr) {
        dialog->reject();
      }
    });
    static_cast<void>(patchy::ui::request_new_document_settings(&window));
    expect(inspected, "newDocumentSwapDimensionsButton", "New Document dialog not found");
  }
  for (const auto& problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  CHECK(problems.empty());
}

// GitHub issue 286 follow-up: a swap button sits evenly between the Width field and the
// Height label, so the space from the field's edge to the glyph matches the space from the
// glyph to the label's text (within 1 px), on the Artboard bar and both marquee pairs.
void ui_swap_dimension_buttons_sit_evenly_between_width_and_height() {
  struct SwapCase {
    const char* tool;
    int marquee_style;  // -1: not a marquee
    const char* button;
  };
  const std::array<SwapCase, 3> cases{{
      {"toolArtboardAction", -1, "artboardSwapSizeButton"},
      {"toolMarqueeAction", 1, "selectionRatioSwapButton"},
      {"toolMarqueeAction", 2, "selectionFixedSizeSwapButton"},
  }};
  std::vector<std::string> problems;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
    CHECK(options != nullptr && style != nullptr);
    if (options == nullptr || style == nullptr) {
      return;
    }
    for (const auto& swap : cases) {
      require_action(window, swap.tool)->trigger();
      if (swap.marquee_style >= 0) {
        style->setCurrentIndex(swap.marquee_style);
      }
      QApplication::processEvents();
      process_events_for(20);
      std::vector<QWidget*> row;
      for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (child->isVisible()) {
          row.push_back(child);
        }
      }
      std::sort(row.begin(), row.end(), [](const QWidget* a, const QWidget* b) { return a->x() < b->x(); });
      const auto found = std::find_if(row.begin(), row.end(), [&swap](const QWidget* widget) {
        return widget->objectName() == QLatin1String(swap.button);
      });
      if (found == row.end() || found == row.begin() || found + 1 == row.end() ||
          qobject_cast<QLabel*>(*(found + 1)) == nullptr) {
        problems.push_back(std::string(swap.button) + suffix + ": not between a field and a label");
        continue;
      }
      const QRect field = (*(found - 1))->geometry();
      const QRect button = (*found)->geometry();
      const QRect label = (*(found + 1))->geometry();
      const QImage image = options->grab().toImage();
      if (image.devicePixelRatio() != 1.0) {
        problems.push_back(std::string(swap.button) + suffix + ": grab is not at 1x");
        continue;
      }
      // The bar's own color, between the field and the button.
      const QColor background = image.pixelColor(field.right() + 2, button.center().y());
      const auto ink_column = [&image, &background, &button](int x) {
        for (int y = button.top(); y <= button.bottom(); ++y) {
          const QColor color = image.pixelColor(x, y);
          if (std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
                  std::abs(color.blue() - background.blue()) > 90) {
            return true;
          }
        }
        return false;
      };
      int glyph_left = -1;
      int glyph_right = -1;
      for (int x = button.left(); x <= button.right(); ++x) {
        if (ink_column(x)) {
          glyph_left = glyph_left < 0 ? x : glyph_left;
          glyph_right = x;
        }
      }
      int text_left = -1;
      for (int x = label.left(); x <= label.right() && text_left < 0; ++x) {
        if (ink_column(x)) {
          text_left = x;
        }
      }
      if (glyph_left < 0 || text_left < 0) {
        problems.push_back(std::string(swap.button) + suffix + ": glyph or label text not found");
        continue;
      }
      const int left_gap = glyph_left - (field.right() + 1);
      const int right_gap = text_left - (glyph_right + 1);
      std::printf("  %s%s: field to glyph %d px, glyph to label text %d px\n", swap.button, suffix.c_str(), left_gap,
                  right_gap);
      if (std::abs(left_gap - right_gap) > 1) {
        problems.push_back(std::string(swap.button) + suffix + ": uneven gaps " + std::to_string(left_gap) + " and " +
                           std::to_string(right_gap));
      }
    }
    style->setCurrentIndex(0);
  }
  for (const auto& problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  CHECK(problems.empty());
}

// The options-bar separator rules (docs/options-bar.md, "Grouping rules"): on every tool's
// bar, and on the variants a style or mode combo switches to, no separator comes first or
// last or right after another one, and the bar fits one row at 1366 px, in both schemes.
// Every bar is captured (options_bar_guide_<tool>[_variant]_<scheme>) before the checks.
void ui_options_bars_follow_the_separator_rules() {
  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-none");
  patchy::ui::configure_automatic_selection_models();
  std::vector<std::string> problems;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1366, 738);
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    CHECK(options_bar != nullptr && options != nullptr);
    if (options_bar == nullptr || options == nullptr) {
      return;
    }
    const auto settle = [] {
      QApplication::processEvents();
      process_events_for(20);
    };
    const auto inspect = [&](const std::string& name) {
      settle();
      const auto tokens = options_row_tokens(*options);
      std::printf("  %-44s", (name + suffix).c_str());
      for (const auto& token : tokens) {
        std::printf(" %s", qPrintable(token));
      }
      std::printf("\n");
      save_widget_artifact("options_bar_guide_" + name + suffix, *options_bar);
      const auto problem = [&](const std::string& what) { problems.push_back(name + suffix + ": " + what); };
      if (!tokens.empty() && tokens.front() == QStringLiteral("|")) {
        problem("starts with a separator");
      }
      if (!tokens.empty() && tokens.back() == QStringLiteral("|")) {
        problem("ends with a separator");
      }
      for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == QStringLiteral("|") && tokens[i - 1] == QStringLiteral("|")) {
          problem("two separators with nothing between them");
        }
      }
      if (const auto [right, lines] = options_row_extent(*options); lines > 1) {
        problem("wraps onto " + std::to_string(lines) + " rows at 1366 px");
      }
    };
    auto tools = options_bar_test_tool_actions(window);
    if (patchy::ai::segmentation_available()) {
      tools.push_back(require_action(window, "toolObjectSelectionAction"));
    }
    auto* marquee_style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
    auto* crop_style = window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
    auto* vector_mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
    for (auto* action : tools) {
      action->trigger();
      const auto tool = static_cast<patchy::ui::CanvasTool>(action->data().toInt());
      const auto name = action->objectName().toStdString();
      // The variants a combo on the bar switches between change what the bar holds.
      QComboBox* variants = nullptr;
      if (tool == patchy::ui::CanvasTool::Marquee || tool == patchy::ui::CanvasTool::EllipticalMarquee) {
        variants = marquee_style;
      } else if (tool == patchy::ui::CanvasTool::Crop) {
        variants = crop_style;
      } else if (vector_mode != nullptr) {
        settle();
        variants = vector_mode->isVisible() ? vector_mode : nullptr;
      }
      if (variants == nullptr) {
        inspect(name);
        continue;
      }
      const int original = variants->currentIndex();
      for (int index = 0; index < variants->count(); ++index) {
        if (auto* model = qobject_cast<QStandardItemModel*>(variants->model());
            model != nullptr && model->item(index) != nullptr && !model->item(index)->isEnabled()) {
          continue;
        }
        variants->setCurrentIndex(index);
        inspect(name + "_" + std::to_string(index));
      }
      variants->setCurrentIndex(original);
    }
  }
  for (const auto& problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  CHECK(problems.empty());
}

void ui_options_bar_drops_duplicate_sliders_before_wrapping() {
  // GitHub issue 91: the inline Size slider repeats the spin box beside it (popup slider
  // and scrub handles), so it is the first thing a narrow bar drops. Opacity and Soft
  // have no inline slider at all any more.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* gradient_slider = window.findChild<QSlider*>(QStringLiteral("gradientOpacitySlider"));
  CHECK(size_spin != nullptr && size_slider != nullptr && gradient_slider != nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider")) == nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider")) == nullptr);
  for (const auto* slider : {size_slider, gradient_slider}) {
    CHECK(slider->property(patchy::ui::kOptionsBarCollapsibleProperty).toBool());
  }
  const auto settle = [] {
    QApplication::processEvents();
    process_events_for(20);
  };
  require_action(window, "toolBrushAction")->trigger();
  window.resize(2600, 800);
  settle();
  CHECK(size_spin->isVisible() && size_slider->isVisible());

  window.resize(1000, 800);
  settle();
  CHECK(size_spin->isVisible());
  CHECK(!size_slider->isVisible());
  // The decision covers every tool at this width, not just the active one.
  require_action(window, "toolGradientAction")->trigger();
  settle();
  CHECK(!gradient_slider->isVisible());
  // The spin box still drives the value.
  require_action(window, "toolBrushAction")->trigger();
  settle();
  size_spin->setValue(40);
  CHECK(patchy::ui::slider_value(*size_slider) == 40);

  window.resize(2600, 800);
  settle();
  CHECK(size_slider->isVisible());
}

void ui_options_bar_checkboxes_paint_without_a_frame() {
  // GitHub issue 167: Auto-Select (Move) and Anti-alias (selection tools) painted a field
  // frame behind their glyph and text, so they read as buttons beside the plain Show
  // Transform Controls and Snap boxes. Every options-bar checkbox now shows the bar's own
  // surface at its corners, the same pixel as the gap beside it.
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1600, 800);
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr);
  const auto settle = [] {
    QApplication::processEvents();
    process_events_for(20);
  };
  const auto check_plain = [&](const char* tool, const char* check_name) {
    require_action(window, tool)->trigger();
    settle();
    auto* check = window.findChild<QCheckBox*>(QString::fromLatin1(check_name));
    CHECK(check != nullptr);
    if (check == nullptr) {
      return;
    }
    CHECK(check->isVisible());
    CHECK(check->parentWidget() == options);
    const QImage bar = options->grab().toImage();
    const QRect box = check->geometry();
    // The flow puts 4 px between controls; 2 px left of the box is bar surface.
    const QColor gap = bar.pixelColor(box.left() - 2, box.center().y());
    for (const QPoint corner : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()}) {
      const QColor pixel = bar.pixelColor(corner);
      if (pixel != gap) {
        std::fprintf(stderr, "  %s corner (%d,%d) is %s, bar is %s\n", check_name, corner.x(), corner.y(),
                     qPrintable(pixel.name()), qPrintable(gap.name()));
      }
      CHECK(pixel == gap);
    }
  };
  check_plain("toolMoveAction", "moveAutoSelectCheck");
  check_plain("toolMoveAction", "moveShowTransformControlsCheck");
  check_plain("toolMarqueeAction", "selectionAntiAliasCheck");
  check_plain("toolCloneAction", "cloneAlignedCheck");
  // The Move row as the reporter saw it, for the issue reply.
  require_action(window, "toolMoveAction")->trigger();
  settle();
  auto* auto_select = window.findChild<QCheckBox*>(QStringLiteral("moveAutoSelectCheck"));
  auto* snap = window.findChild<QCheckBox*>(QStringLiteral("moveSnapCheck"));
  if (auto_select != nullptr && snap != nullptr) {
    const QRect strip(0, 0, snap->geometry().right() + 8, options->height());
    save_image_artifact("issue167_move_checkboxes", options->grab(strip).toImage());
  }
}

void ui_line_options_bar_fits_one_row_with_room_to_spare() {
  // GitHub issue 174: the Line tool's Shape-mode row was about 1330 px with nothing that
  // collapses, so a desktop with slightly wider fonts than the offscreen ones wrapped
  // Appearance... onto a second line at 1366 px. The arrowheads are glyph toggles now and
  // the row keeps 100 px of slack under that width.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(options != nullptr && mode != nullptr);
  const auto settle = [] {
    QApplication::processEvents();
    process_events_for(20);
  };
  window.resize(2600, 800);
  require_action(window, "toolLineAction")->trigger();
  settle();
  mode->setCurrentIndex(0);  // Shape, the widest Line row
  settle();
  const auto [right, lines] = options_row_extent(*options);
  std::printf("  Line (Shape) natural row %d px\n", right);
  CHECK(lines == 1);
  CHECK(right <= 1366 - 100);

  // Same height as the bar's other push buttons (the QSS pins them), such as the W/H link.
  auto* link = window.findChild<QPushButton*>(QStringLiteral("vectorShapeLinkSizeButton"));
  CHECK(link != nullptr && link->isVisible());
  for (const auto* name : {"lineArrowStartButton", "lineArrowEndButton"}) {
    auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
    CHECK(button != nullptr);
    if (button == nullptr || link == nullptr) {
      continue;
    }
    CHECK(button->isVisible() && button->isCheckable());
    CHECK(button->text().isEmpty() && !button->icon().isNull() && !button->toolTip().isEmpty());
    CHECK(button->height() == link->height());
    CHECK(button->geometry().center().y() == link->geometry().center().y());
  }

  window.resize(1366, 738);
  settle();
  auto* appearance = window.findChild<QPushButton*>(QStringLiteral("vectorAppearanceButton"));
  CHECK(appearance != nullptr && appearance->isVisible());
  CHECK(options_row_extent(*options).second == 1);
  if (appearance != nullptr) {
    CHECK(std::abs(appearance->geometry().center().y() - mode->geometry().center().y()) <= 2);
    const QRect strip(0, 0, appearance->geometry().right() + 8, options->height());
    save_image_artifact("issue174_line_row_1366", options->grab(strip).toImage());
  }
}

void ui_text_options_bar_size_shows_photoshop_precision() {
  // Photoshop's type size shows at most two decimals and drops trailing zeros. Patchy
  // stores whole pixels, so the third decimal ("14.446 pt") was false precision.
  patchy::ui::MainWindow window;
  show_window(window);
  require_action(window, "toolTypeAction")->trigger();
  QApplication::processEvents();
  auto* size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(size != nullptr);
  CHECK(size->decimals() == 2);
  const QSignalBlocker blocker(size);
  size->setValue(12.0);
  CHECK(size->text() == QStringLiteral("12 pt"));
  size->setValue(14.446);
  CHECK(std::abs(size->value() - 14.45) < 1e-9);
  CHECK(size->text() == QStringLiteral("14.45 pt"));
  size->setValue(13.5);
  CHECK(size->text() == QStringLiteral("13.5 pt"));
  // Typed values with trailing zeros still parse.
  auto* editor = size->findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  editor->setText(QStringLiteral("18.50 pt"));
  size->interpretText();
  CHECK(std::abs(size->value() - 18.5) < 1e-9);
  CHECK(size->text() == QStringLiteral("18.5 pt"));

  // The rest of the GitHub issue 91 trims: glyph align buttons instead of L / C / R behind an
  // "Align:" label, no "Color:" label, and paragraph direction moved to the Paragraph panel.
  for (const auto* name : {"textAlignLeftButton", "textAlignCenterButton", "textAlignRightButton"}) {
    auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
    CHECK(button != nullptr && button->text().isEmpty() && !button->icon().isNull() && !button->toolTip().isEmpty());
  }
  CHECK(window.findChild<QComboBox*>(QStringLiteral("textDirectionCombo")) == nullptr);
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr);
  for (auto* label : options->findChildren<QLabel*>()) {
    if (label->isVisible()) {
      CHECK(label->text() != QStringLiteral("Color:") && label->text() != QStringLiteral("Align:") &&
            label->text() != QStringLiteral("Direction:"));
    }
  }
}

void ui_canvas_wheel_matches_photoshop_navigation() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  // This test exercises the Photoshop-style pan navigation (wheel zoom off).
  canvas->set_wheel_zooms(false);

  const auto initial_zoom = canvas->zoom();
  const auto initial_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  // Photoshop's axes: a plain wheel scrolls vertically, Ctrl or Shift horizontally.
  send_wheel(*canvas, QPoint(300, 240), 120);
  const auto vertical_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(vertical_pan_origin.y() != initial_origin.y());
  CHECK(vertical_pan_origin.x() == initial_origin.x());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ControlModifier);
  const auto horizontal_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(horizontal_pan_origin.x() != vertical_pan_origin.x());
  CHECK(horizontal_pan_origin.y() == vertical_pan_origin.y());

  send_wheel(*canvas, QPoint(300, 240), -120, Qt::ShiftModifier);
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == vertical_pan_origin);

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
  CHECK(canvas->zoom() > initial_zoom);
  save_widget_artifact("ui_canvas_wheel_navigation", *canvas);
}

void ui_canvas_wheel_zoom_mode_zooms_at_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  // A plain wheel zooms by default on every platform (MainWindow::kWheelZoomsDefault).
  CHECK(patchy::ui::MainWindow::kWheelZoomsDefault);
  CHECK(canvas->wheel_zooms() == patchy::ui::MainWindow::kWheelZoomsDefault);
  canvas->set_wheel_zooms(true);

  const auto initial_zoom = canvas->zoom();
  send_wheel(*canvas, QPoint(300, 240), 120);
  CHECK(canvas->zoom() > initial_zoom);

  send_wheel(*canvas, QPoint(300, 240), -120);
  send_wheel(*canvas, QPoint(300, 240), -120);
  CHECK(canvas->zoom() < initial_zoom);

  const auto zoom_before_pan = canvas->zoom();
  const auto origin_before_pan = canvas->widget_position_for_document_point(QPoint(0, 0));
  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ShiftModifier);
  CHECK(canvas->zoom() == zoom_before_pan);
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)).x() != origin_before_pan.x());
}

void ui_canvas_trackpad_scroll_pans_both_axes() {
  // GitHub issue 44: a two-finger scroll carries both axes and a scroll phase. It pans
  // freely at finger speed in either wheel mode; a stepped wheel keeps the mode.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  const QPoint at(300, 240);
  // Zoom in so the pan has room on both axes.
  canvas->zoom_at_widget_point(QPointF(at), 8.0);
  const auto origin = [canvas] { return canvas->widget_position_for_document_point(QPoint(0, 0)); };

  for (const bool wheel_zooms : {false, true}) {
    canvas->set_wheel_zooms(wheel_zooms);
    const auto zoom = canvas->zoom();
    auto before = origin();
    send_scroll(*canvas, at, QPoint(0, 0), Qt::ScrollBegin);
    send_scroll(*canvas, at, QPoint(0, -30));
    CHECK(origin() == before + QPoint(0, -30));
    send_scroll(*canvas, at, QPoint(-20, 1));
    CHECK(origin() == before + QPoint(-20, -29));
    send_scroll(*canvas, at, QPoint(7, 9));
    CHECK(origin() == before + QPoint(-13, -20));
    send_scroll(*canvas, at, QPoint(0, 0), Qt::ScrollEnd);
    send_scroll(*canvas, at, QPoint(5, 0), Qt::ScrollMomentum);
    CHECK(origin() == before + QPoint(-8, -20));
    CHECK(canvas->zoom() == zoom);
  }

  // Alt zooms about the pointer in proportion to travel, not one wheel step per event.
  const auto zoom = canvas->zoom();
  const auto anchor = canvas->document_point_for_widget_position(QPointF(at));
  send_scroll(*canvas, at, QPoint(0, 10), Qt::ScrollUpdate, Qt::AltModifier);
  CHECK(canvas->zoom() > zoom);
  CHECK(canvas->zoom() < zoom * 1.06);
  const auto anchor_after = canvas->document_point_for_widget_position(QPointF(at));
  CHECK(std::abs(anchor_after.x() - anchor.x()) < 0.01);
  CHECK(std::abs(anchor_after.y() - anchor.y()) < 0.01);
  send_scroll(*canvas, at, QPoint(0, -10), Qt::ScrollUpdate, Qt::AltModifier);
  CHECK(std::abs(canvas->zoom() - zoom) < zoom * 1e-9);

  // A pen Scroll button on macOS (Wacom driver) sends phased events that are still
  // wheel notches: a whole 120 beside a small pixelDelta. They follow the wheel mode.
  canvas->set_wheel_zooms(true);
  {
    const auto zoom_before_notch = canvas->zoom();
    QWheelEvent notch(QPointF(at), QPointF(canvas->mapToGlobal(at)), QPoint(0, 3), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::ScrollUpdate, false);
    CHECK(!patchy::ui::CanvasWidget::wheel_event_is_continuous_scroll(notch));
    QApplication::sendEvent(canvas, &notch);
    CHECK(std::abs(canvas->zoom() - zoom_before_notch * 1.1) < zoom_before_notch * 1e-9);
    canvas->zoom_at_widget_point(QPointF(at), 1.0 / 1.1);
    // A finger scroll that happens to travel 60 px also reports 120, and stays a pan.
    QWheelEvent finger(QPointF(at), QPointF(canvas->mapToGlobal(at)), QPoint(0, 60), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::ScrollUpdate, false);
    CHECK(patchy::ui::CanvasWidget::wheel_event_is_continuous_scroll(finger));
  }

  // A sideways-only stepped wheel pans horizontally even in wheel-zoom mode.
  canvas->set_wheel_zooms(true);
  const auto before = origin();
  const auto zoom_before_tilt = canvas->zoom();
  QWheelEvent tilt(QPointF(at), QPointF(canvas->mapToGlobal(at)), QPoint(), QPoint(-120, 0), Qt::NoButton,
                   Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &tilt);
  CHECK(canvas->zoom() == zoom_before_tilt);
  CHECK(origin().x() < before.x());
  CHECK(origin().y() == before.y());
}

void ui_canvas_trackpad_scroll_ignored_during_pointer_gesture() {
  // A palm on the trackpad mid-stroke must not slide the document under the brush, and
  // momentum from an earlier flick stops at the next press.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  const QPoint at(300, 240);
  canvas->zoom_at_widget_point(QPointF(at), 8.0);
  const auto origin = [canvas] { return canvas->widget_position_for_document_point(QPoint(0, 0)); };

  const auto before = origin();
  send_scroll(*canvas, at, QPoint(0, 0), Qt::ScrollBegin);
  send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, at + QPoint(6, 0), Qt::NoButton, Qt::LeftButton);
  send_scroll(*canvas, at, QPoint(12, 12));
  CHECK(origin() == before);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at + QPoint(6, 0), Qt::LeftButton, Qt::NoButton);

  // Leftover momentum after the press is dropped; a fresh scroll works again.
  send_scroll(*canvas, at, QPoint(12, 0), Qt::ScrollMomentum);
  CHECK(origin() == before);
  send_scroll(*canvas, at, QPoint(0, 0), Qt::ScrollBegin);
  send_scroll(*canvas, at, QPoint(12, 0));
  CHECK(origin() == before + QPoint(12, 0));
  send_scroll(*canvas, at, QPoint(4, 0), Qt::ScrollMomentum);
  CHECK(origin() == before + QPoint(16, 0));
}

#if PATCHY_HAVE_QT_DBUS
// GitHub issue 128: the Wayland "Pick Screen Color" subscribes to the portal
// request's Response before calling PickColor, at the path the portal derives
// from the caller's unique bus name and handle token.
void ui_portal_color_pick_request_path_follows_the_portal_convention() {
  CHECK(patchy::ui::portal_request_path(QStringLiteral(":1.42"), QStringLiteral("patchy_pick_1")) ==
        QStringLiteral("/org/freedesktop/portal/desktop/request/1_42/patchy_pick_1"));
  CHECK(patchy::ui::portal_request_path(QStringLiteral(":1.0"), QStringLiteral("t")) ==
        QStringLiteral("/org/freedesktop/portal/desktop/request/1_0/t"));
  // Offscreen is not Wayland: the overlay picker stays in charge here.
  CHECK(!patchy::ui::portal_color_pick_preferred());
  // A finished request reports once and never again.
  QObject context;
  int reports = 0;
  std::optional<QColor> reported;
  auto* request = new patchy::ui::PortalColorPickRequest(&context, [&](std::optional<QColor> color) {
    ++reports;
    reported = color;
  });
  request->response(1, {});  // cancelled
  request->response(0, {});
  CHECK(reports == 1);
  CHECK(!reported.has_value());
  QApplication::processEvents();  // the request deletes itself
}

// GitHub issue 128 follow-up: a real PickColor answer arrives demarshalled, the color
// a read-only QDBusArgument holding a (ddd) struct. Reading it through the non-const
// (marshalling) beginStructure left the iterator on the struct, and libdbus aborted the
// whole process on the next read (the AppImage and the Flatpak both died on the pick).
// A peer-to-peer connection delivers the Response the way the session bus does, with
// no bus daemon or portal.
void ui_portal_color_pick_reads_a_delivered_response() {
  QDBusServer server(QStringLiteral("unix:tmpdir=%1").arg(QDir::tempPath()));
  CHECK(server.isConnected());
  std::optional<QDBusConnection> portal_side;
  QObject::connect(&server, &QDBusServer::newConnection,
                   [&portal_side](const QDBusConnection& connection) { portal_side = connection; });
  const auto client_name = QStringLiteral("patchy_portal_color_pick_test");
  auto client = QDBusConnection::connectToPeer(server.address(), client_name);
  CHECK(client.isConnected());
  CHECK(process_events_until([&portal_side] { return portal_side.has_value(); }));

  const auto request_path = QStringLiteral("/org/freedesktop/portal/desktop/request/1_1/patchy_pick_test");
  const auto request_interface = QStringLiteral("org.freedesktop.portal.Request");
  const auto deliver = [&](const QVariant& color) -> std::optional<QColor> {
    QObject context;
    int reports = 0;
    std::optional<QColor> reported;
    auto* request = new patchy::ui::PortalColorPickRequest(&context, [&](std::optional<QColor> picked) {
      ++reports;
      reported = picked;
    });
    CHECK(client.connect(QString(), request_path, request_interface, QStringLiteral("Response"), request,
                         SLOT(response(uint, QVariantMap))));
    QVariantMap results;
    results.insert(QStringLiteral("color"), color);
    auto signal = QDBusMessage::createSignal(request_path, request_interface, QStringLiteral("Response"));
    signal << 0U << results;
    CHECK(portal_side->send(signal));
    CHECK(process_events_until([&reports] { return reports > 0; }));
    CHECK(reports == 1);
    return reported;
  };
  const auto structure = [](std::initializer_list<double> components) {
    QDBusArgument argument;
    argument.beginStructure();
    for (const double component : components) {
      argument << component;
    }
    argument.endStructure();
    return QVariant::fromValue(argument);
  };

  // GNOME's and KDE's answer: sRGB components in 0..1.
  const auto picked = deliver(structure({1.0, 0.5, 0.25}));
  CHECK(picked.has_value());
  CHECK(picked.has_value() && picked->rgb() == QColor::fromRgbF(1.0F, 0.5F, 0.25F).rgb());
  // Out-of-range components clamp.
  const auto clamped = deliver(structure({1.5, -0.25, 0.0}));
  CHECK(clamped.has_value() && clamped->rgb() == qRgb(255, 0, 0));
  // A malformed answer reports no color instead of reading past the struct.
  CHECK(!deliver(structure({0.5, 0.5})).has_value());
  CHECK(!deliver(QVariant(QStringLiteral("#ff8000"))).has_value());

  QDBusConnection::disconnectFromPeer(client_name);
}
#endif

void ui_own_window_color_sample_reads_widget_without_screen_grab() {
  // The macOS eyedropper samples Patchy's own windows by rendering them, so a pick on
  // the pasteboard or a panel never needs the Screen Recording permission.
  QWidget swatch;
  swatch.setWindowFlag(Qt::FramelessWindowHint);
  swatch.setAutoFillBackground(true);
  QPalette palette = swatch.palette();
  palette.setColor(QPalette::Window, QColor(12, 200, 90));
  swatch.setPalette(palette);
  swatch.setGeometry(40, 40, 120, 80);
  swatch.show();
  QApplication::processEvents();

  const auto picked = patchy::ui::own_window_color_at_global_position(swatch.mapToGlobal(QPoint(60, 40)));
  CHECK(picked.has_value());
  CHECK(*picked == QColor(12, 200, 90));
  CHECK(!patchy::ui::own_window_color_at_global_position(QPoint(-20000, -20000)).has_value());
}

void ui_status_bar_zoom_percent_box_edits_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto* zoom_edit = window.statusBar()->findChild<patchy::ui::ZoomPercentEdit*>(QStringLiteral("statusZoomEdit"));
  CHECK(zoom_edit != nullptr);
  CHECK(zoom_edit->isEnabled());
  CHECK(zoom_edit->text() == patchy::ui::ZoomPercentEdit::format_zoom_text(canvas->zoom()));

  // The box sits at the far left and must stay visible while a persistent status
  // message shows (QStatusBar hides normal left-side widgets whenever one does).
  window.statusBar()->showMessage(QStringLiteral("Something informative"));
  QApplication::processEvents();
  CHECK(zoom_edit->isVisible());
  CHECK(zoom_edit->x() < 20);

  // Typing a percentage and pressing Enter applies it.
  zoom_edit->setFocus();
  QApplication::processEvents();
  zoom_edit->setText(QStringLiteral("300"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 3.0) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("300%"));

  // Out-of-range values clamp to the canvas zoom limit (12800%).
  zoom_edit->setFocus();
  zoom_edit->setText(QStringLiteral("999999"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 128.0) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("12800%"));

  // Fractional percentages apply and display with two decimals.
  zoom_edit->setFocus();
  zoom_edit->setText(QStringLiteral("33.33"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 0.3333) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("33.33%"));

  // Escape reverts a pending edit without changing the zoom.
  zoom_edit->setFocus();
  QApplication::processEvents();
  zoom_edit->setText(QStringLiteral("55"));
  send_key(*zoom_edit, Qt::Key_Escape);
  CHECK(zoom_edit->text() == QStringLiteral("33.33%"));
  CHECK(std::abs(canvas->zoom() - 0.3333) < 1e-6);

  // Zooming by any other means keeps the box in sync.
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  CHECK(zoom_edit->text() == QStringLiteral("100%"));

  save_widget_artifact("ui_status_zoom_percent_box", *window.statusBar());
}

// Values of 1000% and up lost their leading digits (GitHub issue 237). The box now
// drops decimals as the value grows (display only) and is as wide as its widest
// value needs in the current font, its text running up to the presets chevron.
void ui_status_bar_zoom_box_fits_large_values() {
  using patchy::ui::ZoomPercentEdit;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* bar = window.statusBar();
  auto* zoom_edit = bar->findChild<ZoomPercentEdit*>(QStringLiteral("statusZoomEdit"));
  auto* controls = bar->findChild<QWidget*>(QStringLiteral("statusZoomControls"));
  CHECK(zoom_edit != nullptr && controls != nullptr);
  if (zoom_edit == nullptr || controls == nullptr) {
    return;
  }

  // Typed decimals still apply exactly; the box shows the value rounded.
  zoom_edit->setFocus();
  QApplication::processEvents();
  zoom_edit->setText(QStringLiteral("1521.88"));
  send_key(*zoom_edit, Qt::Key_Return);
  QApplication::processEvents();
  save_widget_artifact("ui_status_zoom_box_1521_88", *controls);
  CHECK(std::abs(canvas->view_zoom() - 15.2188) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("1522%"));
  // Enter on the shown value keeps the exact zoom it rounds.
  canvas->set_view_zoom_centered(15.218834);
  QApplication::processEvents();
  const auto exact = canvas->view_zoom();
  CHECK(zoom_edit->text() == QStringLiteral("1522%"));
  zoom_edit->setFocus();
  QApplication::processEvents();
  send_key(*zoom_edit, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->view_zoom() == exact);

  CHECK(ZoomPercentEdit::format_zoom_text(0.3333) == QStringLiteral("33.33%"));
  CHECK(ZoomPercentEdit::format_zoom_text(0.125) == QStringLiteral("12.5%"));
  CHECK(ZoomPercentEdit::format_zoom_text(1.505) == QStringLiteral("150.5%"));
  CHECK(ZoomPercentEdit::format_zoom_text(15.2188) == QStringLiteral("1522%"));
  CHECK(ZoomPercentEdit::format_zoom_text(0.99996) == QStringLiteral("100%"));
  CHECK(ZoomPercentEdit::format_zoom_text(9.9996) == QStringLiteral("1000%"));
  CHECK(ZoomPercentEdit::format_zoom_text(0.0001) == QStringLiteral("0.01%"));

  QToolButton* chevron = nullptr;
  for (auto* button : zoom_edit->findChildren<QToolButton*>()) {
    if (button->isVisible()) {
      chevron = button;
    }
  }
  CHECK(chevron != nullptr);
  if (chevron == nullptr) {
    return;
  }
  // QLineEdit scrolls text that overflows its room so the cursor (at the end after
  // setText) stays visible, shifting every character left. A text fits when its
  // first character sits where it does in a two-character text that cannot
  // overflow, and its end stays clear of the chevron's button.
  const auto mid_y = zoom_edit->height() / 2;
  const auto first_x_reaching = [&](const QString& text, int position) {
    zoom_edit->setText(text);
    (void)zoom_edit->grab();  // painting settles the horizontal scroll
    int x = 0;
    while (x < zoom_edit->width() && zoom_edit->cursorPositionAt(QPoint(x, mid_y)) < position) {
      ++x;
    }
    return x;  // the middle of character position - 1
  };
  const auto fits = [&](const QString& text) {
    const auto unscrolled = first_x_reaching(text.left(1) + QLatin1Char('%'), 1);
    if (first_x_reaching(text, 1) != unscrolled) {
      return false;
    }
    const auto half_last = (zoom_edit->fontMetrics().horizontalAdvance(text.right(1)) + 1) / 2;
    return first_x_reaching(text, static_cast<int>(text.size())) + half_last <= chevron->x();
  };
  canvas->set_view_zoom_centered(128.0);  // the zoom limit
  QApplication::processEvents();
  const auto at_limit = zoom_edit->text();
  CHECK(at_limit == QStringLiteral("12800%"));
  CHECK(fits(at_limit));
  CHECK(fits(QStringLiteral("999.9%")));
  CHECK(fits(QStringLiteral("99.99%")));
  // No wider than it needs: one more digit no longer fits.
  CHECK(!fits(QStringLiteral("128000%")));
  zoom_edit->setText(at_limit);
}

// The status zoom box works like an options-bar field (GitHub issue 159): the
// wheel and Up/Down step the zoom ladder, a sideways drag from the unfocused box
// scrubs the zoom while a plain click still focuses it for typing, the chevron
// inside it opens the zoom presets (800% down to 12.5%, current one checked), and
// Fit fits the view.
void ui_status_bar_zoom_box_wheel_scrub_and_buttons() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* zoom_edit = window.statusBar()->findChild<patchy::ui::ZoomPercentEdit*>(QStringLiteral("statusZoomEdit"));
  auto* controls = window.statusBar()->findChild<QWidget*>(QStringLiteral("statusZoomControls"));
  auto* fit = window.statusBar()->findChild<QToolButton*>(QStringLiteral("statusZoomFitButton"));
  CHECK(zoom_edit != nullptr && controls != nullptr && fit != nullptr);
  CHECK(window.statusBar()->findChild<QToolButton*>(QStringLiteral("statusZoomActualPixelsButton")) == nullptr);
  QAction* presets = nullptr;
  for (auto* action : zoom_edit->actions()) {
    if (action->objectName() == QStringLiteral("statusZoomPresetsAction")) {
      presets = action;
    }
  }
  auto* presets_menu = zoom_edit->findChild<QMenu*>(QStringLiteral("statusZoomPresetsMenu"));
  CHECK(presets != nullptr && presets_menu != nullptr);

  // The chevron sits inside the box, at its right end; Fit follows the box, and
  // both stay visible under a status message.
  window.statusBar()->showMessage(QStringLiteral("Something informative"));
  QApplication::processEvents();
  CHECK(fit->isVisible() && fit->text() == QStringLiteral("Fit"));
  QToolButton* chevron = nullptr;
  for (auto* button : zoom_edit->findChildren<QToolButton*>()) {
    if (button->isVisible() && button->defaultAction() == presets) {
      chevron = button;
    }
  }
  CHECK(chevron != nullptr);
  CHECK(zoom_edit->rect().contains(chevron->geometry()));
  CHECK(chevron->geometry().center().x() > zoom_edit->width() / 2);
  CHECK(fit->mapTo(controls, QPoint()).x() > zoom_edit->geometry().right());
  QStringList preset_texts;
  for (auto* action : presets_menu->actions()) {
    preset_texts << action->text();
  }
  CHECK(preset_texts == (QStringList{QStringLiteral("800%"), QStringLiteral("400%"), QStringLiteral("200%"),
                                     QStringLiteral("100%"), QStringLiteral("50%"), QStringLiteral("25%"),
                                     QStringLiteral("12.5%")}));
  CHECK(controls->height() <= window.statusBar()->height());

  // Wheel: one notch is one ladder rung each way.
  canvas->set_view_zoom_centered(1.0);
  QApplication::processEvents();
  send_wheel(*zoom_edit, zoom_edit->rect().center(), 120);
  QApplication::processEvents();
  const double up_one = patchy::ui::next_zoom_ladder_step(1.0, true);
  CHECK(std::abs(canvas->view_zoom() - up_one) < 1e-6);
  CHECK(zoom_edit->text() == patchy::ui::ZoomPercentEdit::format_zoom_text(up_one));
  send_wheel(*zoom_edit, zoom_edit->rect().center(), -120);
  send_wheel(*zoom_edit, zoom_edit->rect().center(), -120);
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - patchy::ui::next_zoom_ladder_step(1.0, false)) < 1e-6);

  // A sideways drag from the unfocused box scrubs, right = in, and leaves it unfocused.
  canvas->set_view_zoom_centered(1.0);
  QApplication::processEvents();
  CHECK(!zoom_edit->hasFocus());
  CHECK(zoom_edit->cursor().shape() == Qt::SizeHorCursor);
  const QPoint press = zoom_edit->rect().center();
  const int travel = QApplication::startDragDistance() + 30;
  send_mouse(*zoom_edit, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*zoom_edit, QEvent::MouseMove, press + QPoint(travel, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*zoom_edit, QEvent::MouseButtonRelease, press + QPoint(travel, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const double scrubbed = std::pow(patchy::ui::ZoomPercentEdit::kScrubFactorPerPixel, travel);
  CHECK(std::abs(canvas->view_zoom() - scrubbed) < 1e-6);
  CHECK(!zoom_edit->hasFocus());
  send_mouse(*zoom_edit, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*zoom_edit, QEvent::MouseMove, press - QPoint(travel, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*zoom_edit, QEvent::MouseButtonRelease, press - QPoint(travel, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - 1.0) < 1e-6);

  // A click without travel focuses the box for typing; Up/Down then step the ladder.
  window.activateWindow();
  send_mouse(*zoom_edit, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*zoom_edit, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(zoom_edit->hasFocus());
  CHECK(zoom_edit->cursor().shape() == Qt::IBeamCursor);
  CHECK(std::abs(canvas->view_zoom() - 1.0) < 1e-6);
  send_key(*zoom_edit, Qt::Key_Up);
  CHECK(std::abs(canvas->view_zoom() - up_one) < 1e-6);
  CHECK(zoom_edit->text() == patchy::ui::ZoomPercentEdit::format_zoom_text(up_one));
  send_key(*zoom_edit, Qt::Key_Down);
  CHECK(std::abs(canvas->view_zoom() - 1.0) < 1e-6);
  send_key(*zoom_edit, Qt::Key_Escape);
  QApplication::processEvents();

  // Fit matches View > Fit on Screen; a preset sets that zoom, and the menu
  // checks the preset the view is at.
  canvas->fit_to_view();
  const double fit_zoom = canvas->view_zoom();
  canvas->set_view_zoom_centered(3.0);
  fit->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - fit_zoom) < 1e-6);
  presets_menu->actions()[4]->trigger();  // 50%
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - 0.5) < 1e-6);
  presets_menu->actions()[6]->trigger();  // 12.5%
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - 0.125) < 1e-6);
  presets_menu->actions()[3]->trigger();  // 100%
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_zoom() - 1.0) < 1e-6);
  // The chevron opens the list above the box, the 100% row checked.
  presets->trigger();
  QApplication::processEvents();
  CHECK(presets_menu->isVisible());
  CHECK(presets_menu->geometry().bottom() <= zoom_edit->mapToGlobal(QPoint(0, 0)).y());
  CHECK(presets_menu->actions()[3]->isChecked());
  CHECK(!presets_menu->actions()[4]->isChecked());
  save_widget_artifact("ui_status_zoom_presets_menu_open", *presets_menu);
  presets_menu->close();
  save_widget_artifact("ui_status_zoom_controls", *window.statusBar());
  presets_menu->ensurePolished();
  presets_menu->adjustSize();
  save_widget_artifact("ui_status_zoom_presets_menu", *presets_menu);

  // With no document open the whole group is disabled.
  patchy::ui::MainWindow empty;
  show_window_empty(empty);
  auto* empty_controls = empty.statusBar()->findChild<QWidget*>(QStringLiteral("statusZoomControls"));
  CHECK(empty_controls != nullptr && !empty_controls->isEnabled());
}

// A 1920 x 1080 RGB document at 72 ppi with one full-canvas pixel layer, the
// known document the status-bar document info tests read (issues 202 and 206).
patchy::Document status_info_test_document(int width = 1920, int height = 1080, double ppi = 72.0) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = ppi;
  document.print_settings().vertical_ppi = ppi;
  document.add_pixel_layer("Background", patchy::PixelBuffer(width, height, patchy::PixelFormat::rgba8()));
  return document;
}

patchy::ui::StatusDocumentInfoLabel* require_status_document_info(patchy::ui::MainWindow& window) {
  auto* info = window.statusBar()->findChild<patchy::ui::StatusDocumentInfoLabel*>(
      QStringLiteral("statusDocumentInfoLabel"));
  CHECK(info != nullptr);
  return info;
}

QString status_info_memory(std::uint64_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

// Grabs the tooltip Qt shows for `widget` (its QTipLabel), or a null image.
QImage grab_status_tooltip(QWidget& widget) {
  QToolTip::showText(widget.mapToGlobal(QPoint(4, widget.height())), widget.toolTip(), &widget);
  QApplication::processEvents();
  QImage image;
  for (auto* top : QApplication::topLevelWidgets()) {
    if (top->inherits("QTipLabel") && top->isVisible()) {
      image = top->grab().toImage();
    }
  }
  QToolTip::hideText();
  QApplication::processEvents();
  return image;
}

// The status-bar document info field (issues 202 and 206) shows the dimensions,
// resolution, color mode and bit depth of a known document, follows depth and mode
// changes, and keeps the profile and memory use in its tooltip.
void ui_status_bar_document_info_shows_document_facts() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  using patchy::ui::StatusDocumentInfoLabel;
  const auto dot = StatusDocumentInfoLabel::separator();
  const QString dims = QStringLiteral("1920 × 1080 px");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    window.add_document_session(status_info_test_document(), QStringLiteral("Status Info"));
    window.resize(1366, 768);
    QApplication::processEvents();
    auto* info = require_status_document_info(window);
    if (info == nullptr) {
      return;
    }
    CHECK(!info->isHidden());
    CHECK(info->text() == dims + dot + QStringLiteral("72 ppi") + dot + QStringLiteral("RGB 8-bit"));
    const auto memory = QStringLiteral("Memory: ") + status_info_memory(1920ULL * 1080ULL * 4ULL);
    CHECK(patchy::ui::document_layer_bytes(patchy::ui::MainWindowTestAccess::document(window)) ==
          1920ULL * 1080ULL * 4ULL);
    CHECK(info->toolTip() == (QStringList{info->text(), QStringLiteral("Color profile: None"), memory}
                                  .join(QLatin1Char('\n'))));

    // An embedded profile shows in the tooltip by its description.
    const auto srgb = QColorSpace(QColorSpace::SRgb).iccProfile();
    const auto* srgb_bytes = reinterpret_cast<const std::uint8_t*>(srgb.constData());
    const std::vector<std::uint8_t> icc(srgb_bytes, srgb_bytes + srgb.size());
    const auto summary = patchy::metadata::summarize_icc(icc);
    CHECK(summary.has_value() && !summary->description.empty());
    patchy::ui::MainWindowTestAccess::document(window).color_state().embedded_icc_profile = icc;
    patchy::ui::MainWindowTestAccess::refresh_document_info(window);
    const auto profile_name =
        summary.has_value() ? QString::fromStdString(summary->description).trimmed() : QString();
    CHECK(info->toolTip().contains(QStringLiteral("Color profile: ") + profile_name));

    // 16 bits a channel: the field and the memory figure follow.
    require_action(window, "imageMode16BitAction")->trigger();
    QApplication::processEvents();
    CHECK(info->text() == dims + dot + QStringLiteral("72 ppi") + dot + QStringLiteral("RGB 16-bit"));
    CHECK(info->toolTip().endsWith(QStringLiteral("Memory: ") + status_info_memory(1920ULL * 1080ULL * 8ULL)));
    window.statusBar()->showMessage(QStringLiteral("Brush Tool: drag to paint. Shift-click paints a straight line."));
    QApplication::processEvents();
    save_widget_artifact("ui_status_document_info_16bit", *window.statusBar());
    const auto tip = grab_status_tooltip(*info);
    if (!tip.isNull()) {
      CHECK(tip.save(QStringLiteral("test-artifacts/ui_status_document_info_tooltip.png")));
    }

    // CMYK Proof names the working CMYK profile it shows through.
    if (patchy::working_cmyk_space()->has_profile()) {
      require_action(window, "imageModeCmykProofAction")->trigger();
      QApplication::processEvents();
      CHECK(info->text() == dims + dot + QStringLiteral("72 ppi") + dot + QStringLiteral("CMYK Proof 16-bit"));
      CHECK(info->toolTip().contains(QStringLiteral("Color profile: ") +
                                     QString::fromStdString(patchy::working_cmyk_space()->profile_description())));
      require_action(window, "imageModeRgbAction")->trigger();
      QApplication::processEvents();
      CHECK(info->text().endsWith(QStringLiteral("RGB 16-bit")));
    }
  }

  // Indexed documents, px/cm resolutions and per-axis resolutions.
  const StatusDocumentInfoLabel::ColorContext rgb;
  auto indexed = status_info_test_document(640, 480, 72.0);
  indexed.palette_editing() = patchy::DocumentPaletteEditing{};
  CHECK(StatusDocumentInfoLabel::describe(indexed, rgb).parts.last() == QStringLiteral("Indexed 8-bit"));
  auto per_cm = status_info_test_document(400, 300, 72.0);
  per_cm.print_settings().horizontal_resolution_display_unit = 2;
  CHECK(StatusDocumentInfoLabel::describe(per_cm, rgb).parts.at(1) ==
        QLocale().toString(28.35, 'f', 2) + QStringLiteral(" px/cm"));
  auto anisotropic = status_info_test_document(400, 300, 300.0);
  anisotropic.print_settings().vertical_ppi = 150.0;
  CHECK(StatusDocumentInfoLabel::describe(anisotropic, rgb).parts.at(1) == QStringLiteral("300 × 150 ppi"));
}

// When the bar is narrow the trailing parts drop off whole, never mid-text, and the
// dimensions always stay.
void ui_status_bar_document_info_drops_parts_when_narrow() {
  using patchy::ui::StatusDocumentInfoLabel;
  const auto dot = StatusDocumentInfoLabel::separator();
  StatusDocumentInfoLabel field;
  auto info = StatusDocumentInfoLabel::describe(status_info_test_document(), {});
  field.set_info(info);
  CHECK(!field.isHidden());
  const auto full = info.parts.join(dot);
  const auto two = info.parts.at(0) + dot + info.parts.at(1);
  const auto metrics = field.fontMetrics();
  field.set_width_budget(metrics.horizontalAdvance(full));
  CHECK(field.text() == full);
  field.set_width_budget(metrics.horizontalAdvance(full) - 1);
  CHECK(field.text() == two);
  field.set_width_budget(metrics.horizontalAdvance(two) - 1);
  CHECK(field.text() == info.parts.at(0));
  field.set_width_budget(4);
  CHECK(field.text() == info.parts.at(0));
  CHECK(field.toolTip().startsWith(full));
  field.set_info({});
  CHECK(field.isHidden());

  // In the window: a narrower bar shows a shorter prefix of the same line.
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(status_info_test_document(), QStringLiteral("Status Info"));
  window.resize(1366, 768);
  QApplication::processEvents();
  auto* label = require_status_document_info(window);
  if (label == nullptr) {
    return;
  }
  CHECK(label->text() == full);
  // Sized directly so the window's own minimum width cannot hide the case.
  window.statusBar()->resize(600, window.statusBar()->height());
  CHECK(full.startsWith(label->text()));
  CHECK(label->text().startsWith(info.parts.at(0)));
  CHECK(label->text().size() < full.size());
  window.resize(1366, 768);
  QApplication::processEvents();
  CHECK(label->text() == full);
}

// The field is hidden with no document, then follows the active document and Image Size.
void ui_status_bar_document_info_follows_active_document() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* info = require_status_document_info(window);
  if (info == nullptr) {
    return;
  }
  CHECK(info->isHidden());
  window.add_document_session(status_info_test_document(1920, 1080, 72.0), QStringLiteral("Wide"));
  window.add_document_session(status_info_test_document(800, 600, 300.0), QStringLiteral("Small"));
  QApplication::processEvents();
  const auto dot = patchy::ui::StatusDocumentInfoLabel::separator();
  CHECK(!info->isHidden());
  CHECK(info->parts().at(0) == QStringLiteral("800 × 600 px") && info->parts().at(1) == QStringLiteral("300 ppi"));
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr && tabs->count() == 2);
  if (tabs == nullptr) {
    return;
  }
  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(info->text().startsWith(QStringLiteral("1920 × 1080 px") + dot + QStringLiteral("72 ppi")));
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(info->text().startsWith(QStringLiteral("800 × 600 px")));

  // Image Size (here through the script API, the dialog's own resize path).
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("Resize");
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral("app.activeDocument.resizeImage(400, 300);"), options));
  CHECK(process_events_until([&] { return !host.run_active(); }));
  CHECK(!host.last_run_had_error());
  QApplication::processEvents();
  CHECK(info->text().startsWith(QStringLiteral("400 × 300 px")));

  // Closing the unchanged document leaves the resized one shown. (The resized one
  // is not closed here: it has unsaved changes, so closing it would ask to save.)
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, 0));
  QApplication::processEvents();
  CHECK(info->text().startsWith(QStringLiteral("400 × 300 px")));
}

// The status bar at a 1366 px window keeps the zoom box and the document info
// field on its one line, with room left for the status message.
void ui_status_bar_document_info_fits_at_1366() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(status_info_test_document(), QStringLiteral("Status Info"));
  window.resize(1366, 768);
  QApplication::processEvents();
  auto* bar = window.statusBar();
  bar->showMessage(QStringLiteral("Brush Tool: drag to paint. Shift-click paints a straight line."));
  QApplication::processEvents();
  save_widget_artifact("ui_status_bar_document_info_1366", *bar);

  CHECK(window.width() == 1366);
  auto* controls = bar->findChild<QWidget*>(QStringLiteral("statusZoomControls"));
  auto* zoom_edit = bar->findChild<QWidget*>(QStringLiteral("statusZoomEdit"));
  auto* fit = bar->findChild<QToolButton*>(QStringLiteral("statusZoomFitButton"));
  auto* info = bar->findChild<QLabel*>(QStringLiteral("statusDocumentInfoLabel"));
  CHECK(controls != nullptr && zoom_edit != nullptr && fit != nullptr && info != nullptr);
  if (controls == nullptr || zoom_edit == nullptr || fit == nullptr || info == nullptr) {
    return;
  }
  CHECK(info->isVisible());
  const auto dot = patchy::ui::StatusDocumentInfoLabel::separator();
  CHECK(info->text() == QStringLiteral("1920 × 1080 px") + dot + QStringLiteral("72 ppi") + dot +
                            QStringLiteral("RGB 8-bit"));
  // Zoom box, Fit and the field sit close together on the one bar line.
  const auto fit_rect = QRect(fit->mapTo(bar, QPoint()), fit->size());
  const auto info_rect = QRect(info->mapTo(bar, QPoint()), info->size());
  const auto box_right = zoom_edit->mapTo(bar, QPoint(zoom_edit->width(), 0)).x();
  // Fit's text sits centered with the same gap to the box as to the field.
  const auto gap_before = fit_rect.left() - box_right;
  const auto gap_after = info_rect.left() - (fit_rect.right() + 1);
  CHECK(gap_before >= 0 && gap_before <= 4);
  CHECK(std::abs(gap_after - gap_before) <= 1);
  CHECK(fit->width() <= fit->fontMetrics().horizontalAdvance(fit->text()) +
                            2 * fit->fontMetrics().horizontalAdvance(QLatin1Char(' ')) + 8);
  CHECK(info_rect.top() >= 0 && info_rect.bottom() < bar->height());
  CHECK(controls->height() <= bar->height());
  CHECK(bar->minimumSizeHint().width() <= 1366);
  // The message keeps most of the bar.
  CHECK(info_rect.right() < bar->width() / 3);
  // One line: the field adds no height to the bar a window without it has.
  patchy::ui::MainWindow empty;
  show_window_empty(empty);
  empty.resize(1366, 768);
  QApplication::processEvents();
  CHECK(bar->height() == empty.statusBar()->height());
}

void ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // Center the view on the middle of the default 1024x768 document, then zoom to 300%.
  canvas->fit_to_view();
  canvas->set_zoom_centered(3.0);
  CHECK(std::abs(canvas->zoom() - 3.0) < 1e-6);
  const QPoint document_center(512, 384);
  const QPoint widget_center(canvas->width() / 2, canvas->height() / 2);
  const auto before = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(before.x() - widget_center.x()) <= 2);
  CHECK(std::abs(before.y() - widget_center.y()) <= 2);

  // Double-clicking the Zoom tool resets to 100% and must keep the viewport anchored:
  // the document point that was at the viewport center stays there instead of the
  // canvas panning mostly off screen.
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(zoom_button != nullptr);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 1e-6);
  const auto after = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(after.x() - widget_center.x()) <= 2);
  CHECK(std::abs(after.y() - widget_center.y()) <= 2);
}

void ui_image_resize_recenters_view_and_zoom_double_click_shows_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();

  // Image > Image Size to half keeps the zoom; the view must recenter on the
  // smaller document instead of leaving it shrunk toward the top-left with the
  // viewport center over grey margin.
  accept_image_size_dialog(512, 384);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  const QPoint widget_center(canvas->width() / 2, canvas->height() / 2);
  const QPoint document_center(256, 192);
  const auto after_resize = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(after_resize.x() - widget_center.x()) <= 2);
  CHECK(std::abs(after_resize.y() - widget_center.y()) <= 2);

  // Double-clicking the Zoom tool jumps to 100% and must leave the document
  // centered; anchoring the stale viewport-center point used to push it almost
  // entirely off screen.
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(zoom_button != nullptr);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 1e-6);
  const auto after_zoom = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(after_zoom.x() - widget_center.x()) <= 2);
  CHECK(std::abs(after_zoom.y() - widget_center.y()) <= 2);
}

void ui_size_dialogs_open_with_width_focused_and_selected() {
  patchy::ui::MainWindow window;  // default document: 1024x768
  show_window(window);

  // Both size dialogs pre-set focus and selection on the Width spin before
  // exec() so a new width can be typed immediately (Photoshop behavior). The
  // wasm build must repair that focus after create-time window activation
  // (WasmDialogInitialFocusGuard, dialog_utils.cpp); this pins the intended
  // state where Qt gets it right natively.
  const auto check_dialog = [](const QString& dialog_name, const QString& spin_name) {
    QTimer::singleShot(0, [dialog_name, spin_name] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() != dialog_name) {
          continue;
        }
        auto* dialog = qobject_cast<QDialog*>(widget);
        auto* width = dialog->findChild<QAbstractSpinBox*>(spin_name);
        CHECK(width != nullptr);
        auto* focus = dialog->focusWidget();
        CHECK(focus != nullptr && (focus == width || width->isAncestorOf(focus)));
        auto* edit = width->findChild<QLineEdit*>();
        CHECK(edit != nullptr);
        CHECK(!edit->text().isEmpty());
        CHECK(edit->selectedText() == edit->text());
        dialog->grab().save(QStringLiteral("test-artifacts/%1_initial_focus.png").arg(dialog_name));
        dialog->reject();
        return;
      }
      CHECK(false);
    });
  };
  check_dialog(QStringLiteral("patchyImageSizeDialog"), QStringLiteral("imageSizeWidthSpin"));
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  check_dialog(QStringLiteral("patchyCanvasSizeDialog"), QStringLiteral("canvasSizeWidthSpin"));
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
}

void ui_zoom_preset_recovers_parked_view() {
  patchy::Document document(200, 150, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(200, 150, patchy::PixelFormat::rgba8(), QColor(90, 120, 200)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(400, 300);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  // Park the half-zoom document almost entirely past the top-left edge with
  // the hand-pan API; the 10%-visible pan rule allows it, so the viewport
  // center ends up over grey margin.
  canvas.set_zoom(0.5);
  CHECK(canvas.begin_pan_at_global_position(canvas.mapToGlobal(QPoint(200, 150))));
  CHECK(canvas.pan_to_global_position(canvas.mapToGlobal(QPoint(-400, -300))));
  CHECK(canvas.end_pan());
  const auto parked_origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  CHECK(parked_origin.x() < 0);
  CHECK(parked_origin.y() < 0);

  // A preset zoom where the document overflows the viewport clamps
  // Photoshop-style: the anchor pins to the document, and no grey shows past
  // the document edges.
  canvas.set_zoom_centered(4.0);
  CHECK(std::abs(canvas.zoom() - 4.0) < 1e-6);
  const auto overflow_origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto overflow_corner = canvas.widget_position_for_document_point(QPoint(200, 150));
  CHECK(overflow_origin.x() <= 0);
  CHECK(overflow_origin.y() <= 0);
  CHECK(overflow_corner.x() >= canvas.width());
  CHECK(overflow_corner.y() >= canvas.height());

  // Back at 100% the document fits the viewport, so the preset recenters it.
  canvas.set_zoom_centered(1.0);
  CHECK(std::abs(canvas.zoom() - 1.0) < 1e-6);
  const auto center = canvas.widget_position_for_document_point(QPoint(100, 75));
  CHECK(std::abs(center.x() - 200) <= 2);
  CHECK(std::abs(center.y() - 150) <= 2);
}

// GitHub issue 241: a viewport resize keeps a fitting document centered, keeps a fitting
// document the user parked off center at its offset from the center, and keeps the
// document point under the view center in place when the document overflows.
// GitHub issue 247: a document opens at 100% view zoom, centered, unless it is larger than
// the view; only then is it fitted. View > Fit on Screen still zooms a small one in. The
// checks read the canvas's own device pixel ratio, so the same test covers a 150% display
// when the suite runs under QT_SCALE_FACTOR=1.5.
void ui_open_shows_small_images_at_actual_size_and_fits_large_ones() {
  ensure_artifact_dir();
  const auto folder = std::filesystem::absolute(std::filesystem::path("test-artifacts") / "issue247_open");
  std::filesystem::create_directories(folder);
  const auto write_png = [&folder](const char* name, int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(200, 120, 40));
    const auto path = QString::fromStdWString((folder / name).wstring());
    CHECK(image.save(path));
    return path;
  };
  const auto small_path = write_png("small.png", 120, 80);
  const auto large_path = write_png("large.png", 5000, 3000);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto canvas = [&] { return patchy::ui::MainWindowTestAccess::canvas(window); };
  const auto check_opening_view = [&](int width, int height) {
    auto* view = canvas();
    CHECK(view != nullptr);
    if (view == nullptr) {
      return;
    }
    std::cout << "issue 247: " << width << " x " << height << " opens at " << view->view_zoom() * 100.0
              << "% (device pixel ratio " << view->devicePixelRatioF() << ", view " << view->width() << " x "
              << view->height() << ")" << std::endl;
    const auto top_left = view->widget_position_for_document_point(QPoint(0, 0));
    const auto bottom_right = view->widget_position_for_document_point(QPoint(width, height));
    const bool larger_than_view = width * (1.0 / view->devicePixelRatioF()) > view->width() - 80 ||
                                  height * (1.0 / view->devicePixelRatioF()) > view->height() - 80;
    if (larger_than_view) {
      CHECK(view->view_zoom() < 1.0);
      CHECK(top_left.x() >= 0 && top_left.y() >= 0);
      CHECK(bottom_right.x() <= view->width() && bottom_right.y() <= view->height());
    } else {
      CHECK(std::abs(view->view_zoom() - 1.0) < 1e-9);
    }
    // Centered either way.
    CHECK(std::abs(top_left.x() + bottom_right.x() - view->width()) <= 2);
    CHECK(std::abs(top_left.y() + bottom_right.y() - view->height()) <= 2);
  };

  patchy::ui::MainWindowTestAccess::open_document_path(window, small_path);
  QApplication::processEvents();
  check_opening_view(120, 80);
  save_widget_artifact("issue247_small_image_opens_at_100", window);
  // View > Fit on Screen is the explicit zoom-in.
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas()->view_zoom() > 1.0);

  patchy::ui::MainWindowTestAccess::open_document_path(window, large_path);
  QApplication::processEvents();
  check_opening_view(5000, 3000);

  // Opening a folder activates one document and adds the rest in the background; those
  // take the same view the first time they are shown.
  const auto opened = patchy::ui::MainWindowTestAccess::open_folder_path(
      window, QString::fromStdWString(folder.wstring()));
  CHECK(opened == 2);
  const auto count = patchy::ui::MainWindowTestAccess::session_count(window);
  for (std::size_t index = count - 2U; index < count; ++index) {
    patchy::ui::MainWindowTestAccess::activate_session(window, index);
    process_events_for(50);
    const auto& document = patchy::ui::MainWindowTestAccess::session_document(window, index);
    check_opening_view(document.width(), document.height());
  }

}

void ui_canvas_resize_keeps_document_centered_and_view_center_stable() {
  patchy::Document document(200, 150, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(200, 150, patchy::PixelFormat::rgba8(), QColor(90, 120, 200)));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(400, 300);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  const auto offset_from_center = [&canvas](QPointF document_point) {
    return canvas.widget_position_f(document_point) - QPointF(canvas.width() / 2.0, canvas.height() / 2.0);
  };
  const auto close_to = [](QPointF a, QPointF b) { return std::abs(a.x() - b.x()) <= 1.0 && std::abs(a.y() - b.y()) <= 1.0; };

  canvas.set_zoom_centered(1.0);
  CHECK(close_to(offset_from_center({100.0, 75.0}), {0.0, 0.0}));
  canvas.resize(700, 520);
  QApplication::processEvents();
  CHECK(close_to(offset_from_center({100.0, 75.0}), {0.0, 0.0}));
  canvas.resize(320, 240);
  QApplication::processEvents();
  CHECK(close_to(offset_from_center({100.0, 75.0}), {0.0, 0.0}));

  canvas.resize(600, 450);
  QApplication::processEvents();
  CHECK(canvas.begin_pan_at_global_position(canvas.mapToGlobal(QPoint(300, 225))));
  CHECK(canvas.pan_to_global_position(canvas.mapToGlobal(QPoint(360, 185))));
  CHECK(canvas.end_pan());
  const auto parked = offset_from_center({100.0, 75.0});
  CHECK(close_to(parked, {60.0, -40.0}));
  canvas.resize(800, 600);
  QApplication::processEvents();
  CHECK(close_to(offset_from_center({100.0, 75.0}), parked));

  // Zoomed in past the viewport and panned: the point under the center stays there.
  canvas.set_zoom_centered(6.0);
  CHECK(canvas.begin_pan_at_global_position(canvas.mapToGlobal(QPoint(400, 300))));
  CHECK(canvas.pan_to_global_position(canvas.mapToGlobal(QPoint(350, 330))));
  CHECK(canvas.end_pan());
  const auto probe = canvas.document_point_for_widget_position(QPointF(400.0, 300.0));
  CHECK(probe.x() > 10.0 && probe.x() < 190.0 && probe.y() > 10.0 && probe.y() < 140.0);
  for (const auto size : {QSize(520, 380), QSize(900, 560), QSize(640, 700)}) {
    canvas.resize(size);
    QApplication::processEvents();
    CHECK(canvas.width() == size.width() && canvas.height() == size.height());
    CHECK(close_to(offset_from_center(probe), {0.0, 0.0}));
  }
}

// The issue's window-level cases: a maximized (larger) window, the panel column closing
// and opening, a document in a background tab, and a resized float window.
void ui_window_resize_panels_tabs_and_floats_keep_small_document_centered() {
  patchy::ui::MainWindow window;
  window.resize(900, 640);
  window.show();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::create_document(window, 300, 200);
  QApplication::processEvents();
  auto* first = require_canvas(window);
  first->set_view_zoom_centered(1.0);
  const auto centered = [](patchy::ui::CanvasWidget* canvas, QPointF document_center) {
    const auto position = canvas->widget_position_f(document_center);
    return std::abs(position.x() - canvas->width() / 2.0) <= 1.0 &&
           std::abs(position.y() - canvas->height() / 2.0) <= 1.0;
  };
  CHECK(centered(first, {150.0, 100.0}));
  patchy::ui::MainWindowTestAccess::create_document(window, 260, 180);
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != first);
  second->set_view_zoom_centered(1.0);
  const auto old_width = second->width();
  window.resize(1500, 1000);
  QApplication::processEvents();
  window.grab().save(QStringLiteral("test-artifacts/issue_241_window_after_resize.png"));
  CHECK(second->width() > old_width);
  CHECK(centered(second, {130.0, 90.0}));

  auto* splitter = window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter"));
  CHECK(splitter != nullptr && splitter->count() >= 2);
  const auto with_panels = second->width();
  splitter->widget(1)->hide();
  QApplication::processEvents();
  CHECK(second->width() > with_panels);
  CHECK(centered(second, {130.0, 90.0}));
  splitter->widget(1)->show();
  QApplication::processEvents();
  CHECK(second->width() == with_panels);
  CHECK(centered(second, {130.0, 90.0}));

  patchy::ui::MainWindowTestAccess::activate_session(window, 0);
  QApplication::processEvents();
  CHECK(require_canvas(window) == first);
  CHECK(first->width() == second->width());
  CHECK(centered(first, {150.0, 100.0}));

  patchy::ui::MainWindowTestAccess::float_active_document(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_floated(window));
  auto* float_window = first->window();
  CHECK(float_window != &window);
  const auto floated_width = first->width();
  float_window->resize(float_window->width() + 260, float_window->height() + 140);
  QApplication::processEvents();
  CHECK(first->width() > floated_width);
  CHECK(centered(first, {150.0, 100.0}));
}

void ui_canvas_focus_in_restores_tool_cursor() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(120, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(20);
  canvas.show();
  QApplication::processEvents();

  // Simulate the OS (Windows) resetting the cursor to an arrow on re-activation.
  canvas.setCursor(Qt::ArrowCursor);
  CHECK(canvas.cursor().shape() == Qt::ArrowCursor);

  // Regaining focus / re-entry must restore the brush (custom pixmap) cursor.
  canvas.refresh_tool_cursor();
  CHECK(canvas.cursor().shape() == Qt::BitmapCursor);
}

void ui_max_brush_uses_overlay_cursor() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(120, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_brush_size(patchy::ui::kMaxBrushSize);
  canvas.show();
  QApplication::processEvents();

  CHECK(canvas.cursor().shape() == Qt::CrossCursor);
  CHECK(canvas.cursor().pixmap().isNull());

  // Every retouch tool with the round footprint cursor switches to the overlay
  // crosshair at large sizes too (browsers reject the giant pixmap cursor in
  // the wasm build) and returns to a pixmap cursor when small.
  const patchy::ui::CanvasTool footprint_tools[] = {
      patchy::ui::CanvasTool::Clone,        patchy::ui::CanvasTool::Healing,
      patchy::ui::CanvasTool::SpotHealing,  patchy::ui::CanvasTool::Smudge,
      patchy::ui::CanvasTool::Dodge,        patchy::ui::CanvasTool::Burn,
      patchy::ui::CanvasTool::Sponge,       patchy::ui::CanvasTool::BlurBrush,
      patchy::ui::CanvasTool::SharpenBrush};
  for (const auto tool : footprint_tools) {
    canvas.set_tool(tool);
    canvas.set_brush_size(patchy::ui::kMaxBrushSize);
    CHECK(canvas.cursor().shape() == Qt::CrossCursor);
    CHECK(canvas.cursor().pixmap().isNull());
    canvas.set_brush_size(48);
    CHECK(canvas.cursor().shape() == Qt::BitmapCursor);
  }
}

void ui_canvas_pan_keeps_document_partly_visible() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Pan);
  canvas.show();
  QApplication::processEvents();

  const auto minimum_visible = [](int viewport_span, int document_span) {
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(std::min(viewport_span, document_span)) * 0.10)));
  };
  const auto visible_document_rect = [&] {
    const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
    return QRect(top_left, QSize(document.width(), document.height())).intersected(canvas.rect());
  };
  const auto check_minimum_visible = [&] {
    const auto visible = visible_document_rect();
    CHECK(visible.width() >= minimum_visible(canvas.width(), document.width()));
    CHECK(visible.height() >= minimum_visible(canvas.height(), document.height()));
  };

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(5000, 4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(5000, 4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(-5000, -4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(-5000, -4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();
}

std::pair<QScrollBar*, QScrollBar*> require_canvas_scroll_bars(patchy::ui::CanvasWidget& canvas) {
  auto* horizontal = canvas.findChild<QScrollBar*>(QStringLiteral("canvasHorizontalScrollBar"));
  auto* vertical = canvas.findChild<QScrollBar*>(QStringLiteral("canvasVerticalScrollBar"));
  CHECK(horizontal != nullptr);
  CHECK(vertical != nullptr);
  return {horizontal, vertical};
}

void ui_canvas_scroll_bars_reflect_pan_range() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.center_document_in_view();
  canvas.show();
  QApplication::processEvents();

  const auto [horizontal, vertical] = require_canvas_scroll_bars(canvas);
  CHECK(horizontal->isVisible());
  CHECK(vertical->isVisible());

  // Flush against the bottom/right edges, each shortened by the other's
  // thickness so the corner square stays free (never assert pixel thickness;
  // the offscreen style's extent differs from the app style's).
  CHECK(horizontal->geometry().left() == 0);
  CHECK(horizontal->geometry().bottom() == canvas.height() - 1);
  CHECK(horizontal->width() == canvas.width() - vertical->width());
  CHECK(vertical->geometry().top() == 0);
  CHECK(vertical->geometry().right() == canvas.width() - 1);
  CHECK(vertical->height() == canvas.height() - horizontal->height());

  // The range mirrors the pan clamp (>= 10% of the document, or of the viewport
  // if smaller, stays visible): pan spans
  // [minimum_visible - document_span, viewport_span - minimum_visible], and the
  // bar value counts down from the pan maximum (value 0 = top/left extreme).
  const auto expected_maximum_and_centered_value = [](int viewport_span, int document_span) {
    const auto minimum_visible =
        std::max(1.0, static_cast<double>(std::min(viewport_span, document_span)) * 0.10);
    const auto pan_minimum = minimum_visible - static_cast<double>(document_span);
    const auto pan_maximum = static_cast<double>(viewport_span) - minimum_visible;
    const auto centered_pan =
        (static_cast<double>(viewport_span) - static_cast<double>(document_span)) / 2.0;
    return std::pair(static_cast<int>(std::lround(pan_maximum - pan_minimum)),
                     static_cast<int>(std::lround(pan_maximum - centered_pan)));
  };
  const auto [h_maximum, h_value] = expected_maximum_and_centered_value(500, 100);  // 580, 290
  const auto [v_maximum, v_value] = expected_maximum_and_centered_value(400, 80);   // 464, 232
  CHECK(horizontal->minimum() == 0);
  CHECK(horizontal->maximum() == h_maximum);
  CHECK(horizontal->value() == h_value);
  CHECK(horizontal->pageStep() == 500);
  CHECK(vertical->minimum() == 0);
  CHECK(vertical->maximum() == v_maximum);
  CHECK(vertical->value() == v_value);
  CHECK(vertical->pageStep() == 400);

  canvas.set_document(nullptr);
  CHECK(!horizontal->isVisible());
  CHECK(!vertical->isVisible());
}

void ui_canvas_hand_pan_updates_scroll_bars() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.center_document_in_view();
  canvas.set_tool(patchy::ui::CanvasTool::Pan);
  canvas.show();
  QApplication::processEvents();

  const auto [horizontal, vertical] = require_canvas_scroll_bars(canvas);
  const auto h_before = horizontal->value();
  const auto v_before = vertical->value();

  // Dragging the document left/up by (100, 80) scrolls the view right/down by
  // the same amount.
  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(150, 120), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(150, 120), Qt::LeftButton, Qt::NoButton);

  CHECK(std::abs(horizontal->value() - (h_before + 100)) <= 1);
  CHECK(std::abs(vertical->value() - (v_before + 80)) <= 1);
}

void ui_canvas_scroll_bar_scrolls_view() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.center_document_in_view();
  canvas.show();
  QApplication::processEvents();

  const auto [horizontal, vertical] = require_canvas_scroll_bars(canvas);
  int view_changes = 0;
  canvas.set_view_changed_callback([&view_changes] { ++view_changes; });

  const auto origin_before = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto h_target = horizontal->value() + 50;
  horizontal->setValue(h_target);
  auto origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  CHECK(origin.x() == origin_before.x() - 50);
  CHECK(origin.y() == origin_before.y());
  CHECK(horizontal->value() == h_target);  // the resync echo must not fight the user's value
  CHECK(view_changes == 1);

  const auto v_target = vertical->value() + 30;
  vertical->setValue(v_target);
  origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  CHECK(origin.x() == origin_before.x() - 50);
  CHECK(origin.y() == origin_before.y() - 30);
  CHECK(vertical->value() == v_target);
  CHECK(view_changes == 2);
}

void ui_canvas_scroll_bars_follow_zoom_and_resize() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.center_document_in_view();
  canvas.show();
  QApplication::processEvents();

  const auto [horizontal, vertical] = require_canvas_scroll_bars(canvas);

  canvas.set_zoom(2.0);
  // Document span doubles to 200x160, so minimum_visible becomes 20/16 and the
  // range widens: maximum = (viewport - min_vis) - (min_vis - span).
  CHECK(horizontal->maximum() == 660);  // (500 - 20) - (20 - 200)
  CHECK(vertical->maximum() == 528);    // (400 - 16) - (16 - 160)
  CHECK(horizontal->pageStep() == 500);

  canvas.resize(600, 500);
  QApplication::processEvents();
  CHECK(horizontal->pageStep() == 600);
  CHECK(vertical->pageStep() == 500);
  CHECK(horizontal->maximum() == 760);  // (600 - 20) - (20 - 200)
  CHECK(vertical->maximum() == 628);    // (500 - 16) - (16 - 160)
  CHECK(horizontal->geometry().bottom() == canvas.height() - 1);
  CHECK(vertical->geometry().right() == canvas.width() - 1);
}

// With the Zoom tool, a right-click anywhere on the canvas opens the View menu's
// own zoom actions (GitHub issue 300): Zoom In, Zoom Out, then Fit on Screen,
// Actual Pixels and 200%, ahead of the selection and pasteboard sections, and
// each entry changes the view as its View menu twin does.
void ui_zoom_tool_context_menu_offers_view_zoom_commands() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto visible_context_menu = [](QWidget& widget) -> QMenu* {
    for (auto* menu : widget.findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
      if (menu->isVisible()) {
        return menu;
      }
    }
    return nullptr;
  };
  const auto right_click = [&](QPoint point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
    QApplication::processEvents();
    return visible_context_menu(*canvas);
  };
  const auto pick = [](QMenu& menu, QAction* action) {
    CHECK(menu.actions().contains(action));
    menu.hide();
    action->trigger();
    QApplication::processEvents();
  };
  const auto close_to = [](double actual, double expected) { return std::abs(actual - expected) < 0.001; };
  const QList<QAction*> zoom_actions{
      require_action(window, "viewZoomInAction"), require_action(window, "viewZoomOutAction"),
      require_action(window, "viewFitOnScreenAction"), require_action(window, "viewActualPixelsAction"),
      require_action(window, "viewZoom200Action")};
  for (auto* action : zoom_actions) {
    if (action == nullptr) {
      return;
    }
  }
  CHECK(!zoom_actions[0]->shortcut().isEmpty());

  // A painting tool still opens nothing inside the document.
  require_action(window, "toolBrushAction")->trigger();
  canvas->set_view_zoom(0.5);
  QApplication::processEvents();
  const auto inside = canvas->widget_position_for_document_point(QPoint(40, 40));
  CHECK(canvas->rect().contains(inside));
  CHECK(right_click(inside) == nullptr);

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  auto* menu = right_click(inside);
  if (menu == nullptr) {
    save_widget_artifact("ui_zoom_tool_context_menu", window);
  }
  CHECK(menu != nullptr);
  QList<QAction*> entries;
  for (auto* action : menu->actions()) {
    if (!action->isSeparator()) {
      entries.push_back(action);
    }
  }
  CHECK(entries == zoom_actions);
  save_window_popup_artifact("ui_zoom_tool_context_menu", window, *canvas, *menu);

  pick(*menu, zoom_actions[4]);
  CHECK(close_to(canvas->view_zoom(), 2.0));
  menu = right_click(inside);
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    pick(*menu, zoom_actions[1]);
    CHECK(canvas->view_zoom() < 2.0 - 0.001);
  }
  menu = right_click(canvas->rect().center());
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    pick(*menu, zoom_actions[0]);
    CHECK(close_to(canvas->view_zoom(), 2.0));
  }
  menu = right_click(canvas->rect().center());
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    pick(*menu, zoom_actions[3]);
    CHECK(close_to(canvas->view_zoom(), 1.0));
  }
  zoom_actions[2]->trigger();
  QApplication::processEvents();
  const auto fit_zoom = canvas->view_zoom();
  canvas->set_view_zoom(1.0);
  QApplication::processEvents();
  menu = right_click(canvas->rect().center());
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    pick(*menu, zoom_actions[2]);
    CHECK(close_to(canvas->view_zoom(), fit_zoom));
  }

  // Off the document the pasteboard colors follow the zoom section; inside a
  // selection its commands do.
  canvas->set_zoom_centered(0.25);
  QApplication::processEvents();
  const auto backdrop = canvas->widget_position_for_document_point(QPoint(-300, -300));
  CHECK(canvas->rect().contains(backdrop));
  menu = right_click(backdrop);
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    CHECK(menu->actions().value(0) == zoom_actions[0]);
    CHECK(menu->findChild<QAction*>(QStringLiteral("canvasBackdropDefaultAction")) != nullptr);
    menu->hide();
  }
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  menu = right_click(canvas->widget_position_for_document_point(QPoint(40, 40)));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    CHECK(menu->actions().value(0) == zoom_actions[0]);
    CHECK(menu->actions().contains(require_action(window, "editDeselectAction")));
    menu->hide();
  }
}

// Right-click on the pasteboard offers Photoshop's backdrop presets (GitHub
// issue 47): the pick repaints the pasteboard, persists as
// view/canvasBackdropColor so a new window starts from it, and Default drops
// the key and returns to the theme role. Inside the document nothing changes.
void ui_canvas_backdrop_context_menu_sets_color() {
  SettingsValueRestorer restore_backdrop(QStringLiteral("view/canvasBackdropColor"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("view/canvasBackdropColor"));
    settings.sync();
  }
  const auto visible_context_menu = [](QWidget& canvas) -> QMenu* {
    for (auto* menu : canvas.findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
      if (menu->isVisible()) {
        return menu;
      }
    }
    return nullptr;
  };
  const auto right_click = [&](patchy::ui::CanvasWidget& canvas, QPoint point) {
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
    QApplication::processEvents();
    return visible_context_menu(canvas);
  };
  const auto find_entry = [](QMenu& menu, const char* name) {
    auto* action = menu.findChild<QAction*>(QString::fromLatin1(name));
    CHECK(action != nullptr);
    return action;
  };
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    canvas->set_zoom_centered(0.25);
    QApplication::processEvents();
    const auto backdrop_point = canvas->widget_position_for_document_point(QPoint(-300, -300));
    CHECK(canvas->rect().contains(backdrop_point));
    CHECK(!canvas->backdrop_color_override().has_value());
    CHECK(canvas->backdrop_color() == patchy::ui::theme().canvas_backdrop);

    // A painting tool inside the document still has no menu.
    CHECK(right_click(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100))) == nullptr);

    auto* menu = right_click(*canvas, backdrop_point);
    CHECK(menu != nullptr);
    CHECK(find_entry(*menu, "canvasBackdropDefaultAction")->isChecked());
    CHECK(!find_entry(*menu, "canvasBackdropWhiteAction")->isChecked());
    CHECK(!find_entry(*menu, "canvasBackdropCustomAction")->isChecked());
    find_entry(*menu, "canvasBackdropWhiteAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->backdrop_color() == QColor(255, 255, 255));
    CHECK(canvas->backdrop_color_override().has_value());
    CHECK(canvas->grab().toImage().pixelColor(backdrop_point) == QColor(255, 255, 255));
    CHECK(patchy::ui::app_settings().value(QStringLiteral("view/canvasBackdropColor")).value<QColor>() ==
          QColor(255, 255, 255));

    menu = right_click(*canvas, backdrop_point);
    CHECK(menu != nullptr);
    CHECK(find_entry(*menu, "canvasBackdropWhiteAction")->isChecked());
    CHECK(!find_entry(*menu, "canvasBackdropDefaultAction")->isChecked());
    find_entry(*menu, "canvasBackdropDarkGrayAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->backdrop_color() == QColor(0x35, 0x35, 0x35));
  }
  {
    // A new window starts from the saved color; Default clears the key.
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(canvas->backdrop_color() == QColor(0x35, 0x35, 0x35));
    canvas->set_zoom_centered(0.25);
    QApplication::processEvents();
    auto* menu = right_click(*canvas, canvas->widget_position_for_document_point(QPoint(-300, -300)));
    CHECK(menu != nullptr);
    CHECK(find_entry(*menu, "canvasBackdropDarkGrayAction")->isChecked());
    find_entry(*menu, "canvasBackdropDefaultAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->backdrop_color() == patchy::ui::theme().canvas_backdrop);
    CHECK(!canvas->backdrop_color_override().has_value());
    CHECK(!patchy::ui::app_settings().contains(QStringLiteral("view/canvasBackdropColor")));
  }
}

void ui_canvas_fractional_zoom_paints_to_document_edge() {
  patchy::Document document(1024, 768, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(1024, 768, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 80;
      px[2] = 40;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Opaque", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(915, 706);
  canvas.set_document(&document);
  canvas.fit_to_view();
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QPoint right_edge_sample(bottom_right.x() - 1, (top_left.y() + bottom_right.y()) / 2);
  CHECK(preview.rect().contains(right_edge_sample));
  CHECK(!color_close(preview.pixelColor(right_edge_sample), QColor(36, 38, 41), 4));
}

void ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_zoom(5.5);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

void ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  // The sample row is a pixel-grid line row; this pins the pixels, not the grid.
  canvas.set_pixel_grid_visible(false);
  canvas.set_zoom(12.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

// The display interpolation rule (GitHub issue 210): fractional zooms between 100% and
// 400% interpolate, whole zooms and high zooms show exact pixel blocks.
void ui_canvas_display_smoothing_rule_pins_zoom_bands() {
  using patchy::ui::uses_smooth_display_scaling;
  for (const double zoom : {0.125, 0.5, 0.9, 1.25, 1.5, 1.75, 2.5, 3.333, 3.99}) {
    if (!uses_smooth_display_scaling(zoom, false)) {
      std::cerr << "expected smooth display scaling at " << zoom << '\n';
      CHECK(false);
    }
  }
  // 1.0000001 and 2.9999999 are a whole zoom reached through floating-point drift
  // (view zoom = logical zoom x device pixel ratio).
  for (const double zoom : {1.0, 1.0000001, 2.0, 2.9999999, 3.0, 4.0, 4.5, 5.5, 7.5}) {
    if (uses_smooth_display_scaling(zoom, false)) {
      std::cerr << "expected exact pixels at " << zoom << '\n';
      CHECK(false);
    }
  }
  // The deep-zoom pixel renderer always draws exact blocks.
  CHECK(!uses_smooth_display_scaling(8.0, true));
  CHECK(!uses_smooth_display_scaling(12.5, true));
}

// One-pixel black and white columns: any interpolated value on screen is smoothing.
patchy::Document display_smoothing_stripes_document() {
  patchy::Document document(40, 24, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(40, 24, patchy::PixelFormat::rgb8(), Qt::white);
  for (int x = 0; x < 40; x += 2) {
    fill_pixel_rect(pixels, QRect(x, 0, 1, 24), Qt::black);
  }
  document.add_pixel_layer("Stripes", std::move(pixels));
  return document;
}

// Count of on-screen document pixels (one pixel inside the document edge) whose value is
// neither black nor white.
int interpolated_canvas_pixels(patchy::ui::CanvasWidget& canvas, const patchy::Document& document,
                               const QImage& image) {
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto inside = QRect(top_left, bottom_right).adjusted(1, 1, -2, -2).intersected(canvas_content_rect(canvas));
  int interpolated = 0;
  for (int y = inside.top(); y <= inside.bottom(); ++y) {
    for (int x = inside.left(); x <= inside.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (!color_close(color, QColor(Qt::black), 2) && !color_close(color, QColor(Qt::white), 2)) {
        ++interpolated;
      }
    }
  }
  return interpolated;
}

// The rule reaches the screen: fractional zooms between 100% and 400% interpolate, 100%,
// whole zooms and high zooms show exact pixels, and a bounded repaint after an edit at a
// smooth zoom matches a full repaint (the blend reaches half a document pixel past the
// edited rect).
void ui_canvas_fractional_zoom_smooths_between_whole_zooms() {
  auto document = display_smoothing_stripes_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(240, 180);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  for (const double zoom : {1.5, 1.75, 2.5, 3.5}) {
    canvas.set_view_zoom_centered(zoom);
    QApplication::processEvents();
    const auto image = render_widget_image(canvas);
    if (interpolated_canvas_pixels(canvas, document, image) == 0) {
      std::cerr << "no interpolated pixels at " << zoom << '\n';
      CHECK(false);
    }
  }
  for (const double zoom : {1.0, 2.0, 3.0, 4.5}) {
    canvas.set_view_zoom_centered(zoom);
    QApplication::processEvents();
    const auto image = render_widget_image(canvas);
    if (const auto interpolated = interpolated_canvas_pixels(canvas, document, image); interpolated != 0) {
      std::cerr << interpolated << " interpolated pixels at " << zoom << '\n';
      CHECK(false);
    }
  }

  for (const double zoom : {1.5, 2.5}) {
    canvas.set_view_zoom_centered(zoom);
    QApplication::processEvents();
    canvas.force_refresh();
    QApplication::processEvents();
    auto* layer = document.find_layer(document.layers().front().id());
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      return;
    }
    const QRect edited(zoom < 2.0 ? 13 : 21, 7, 5, 6);
    fill_pixel_rect(layer->pixels(), edited, QColor(220, 30, 40));
    canvas.document_changed(edited);
    QApplication::processEvents();
    const auto backing = grab_widget_window_image(canvas);
    const auto full = render_widget_image(canvas);
    const auto content = canvas_content_rect(canvas);
    const auto matches = images_equal_rgba(backing.copy(content), full.copy(content));
    if (!matches) {
      save_image_artifact("ui_canvas_fractional_zoom_partial_backing", backing);
      save_image_artifact("ui_canvas_fractional_zoom_partial_full", full);
      if (const auto mismatch = image_mismatch_bounds_rgba(backing.copy(content), full.copy(content));
          mismatch.has_value()) {
        std::cerr << "partial repaint mismatch at " << zoom << ": " << mismatch->x() << "," << mismatch->y()
                  << "," << mismatch->width() << "," << mismatch->height() << '\n';
      }
    }
    CHECK(matches);
  }
}

// GitHub issue 210's scene: text with the Slate Etched style at 150%. Nearest-neighbor
// scaling repeats every other screen column at 150% (the stair-steps in the report);
// interpolation leaves no such repeat. Saves the 100%, 150% and 200% captures as
// artifacts for the issue's before/after images.
void ui_canvas_styled_text_at_150_percent_has_no_repeated_columns() {
  QImage text_image(300, 96, QImage::Format_ARGB32_Premultiplied);
  text_image.fill(Qt::transparent);
  {
    QPainter painter(&text_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    auto font = visual_test_font();
    font.setPixelSize(64);
    painter.setFont(font);
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(text_image.rect(), Qt::AlignCenter, QStringLiteral("Multiple"));
  }
  auto text_document = patchy::ui::document_from_qimage(text_image, "Text");
  CHECK(!text_document.layers().empty());
  if (text_document.layers().empty()) {
    return;
  }

  constexpr const char* kSlateEtched = "57a1e500-0021-4c6d-8f2a-9b3d4e55c021";
  patchy::Document document(300, 96, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(300, 96, patchy::PixelFormat::rgba8(), QColor(38, 38, 38)));
  patchy::Layer text(document.allocate_layer_id(), "Text", text_document.layers().front().pixels());
  text.layer_style() = patchy::builtin_style_preset_style(kSlateEtched);
  std::vector<std::string> pattern_ids;
  patchy::collect_referenced_pattern_ids(text.layer_style(), pattern_ids);
  CHECK(!pattern_ids.empty());
  for (const auto& id : pattern_ids) {
    const auto pattern = patchy::ui::bundled_pattern_resource(id);
    CHECK(pattern.has_value());
    if (pattern.has_value()) {
      document.metadata().patterns.adopt(*pattern);
    }
  }
  document.add_layer(std::move(text));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 220);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  const auto capture = [&](double zoom) {
    canvas.set_view_zoom_centered(zoom);
    QApplication::processEvents();
    const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
    const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
    return render_widget_image(canvas).copy(QRect(top_left, bottom_right).intersected(canvas_content_rect(canvas)));
  };
  // The largest share of identical neighbouring columns at one phase of `period`, among
  // columns that are not flat background. Nearest-neighbor at 150% repeats one column in
  // every three; at 200% one in every two.
  const auto repeated_column_share = [](const QImage& image, int period) {
    const auto column = [&image](int x) {
      std::vector<QRgb> values;
      for (int y = 0; y < image.height(); ++y) {
        values.push_back(image.pixel(x, y));
      }
      return values;
    };
    std::vector<int> repeats(static_cast<std::size_t>(period), 0);
    std::vector<int> totals(static_cast<std::size_t>(period), 0);
    auto current = column(0);
    for (int x = 0; x + 1 < image.width(); ++x) {
      auto next = column(x + 1);
      const bool flat = std::all_of(current.begin(), current.end(), [&](QRgb value) { return value == current[0]; });
      if (!flat) {
        ++totals[static_cast<std::size_t>(x % period)];
        if (current == next) {
          ++repeats[static_cast<std::size_t>(x % period)];
        }
      }
      current = std::move(next);
    }
    double share = 0.0;
    for (int phase = 0; phase < period; ++phase) {
      if (totals[static_cast<std::size_t>(phase)] > 0) {
        share = std::max(share, static_cast<double>(repeats[static_cast<std::size_t>(phase)]) /
                                    static_cast<double>(totals[static_cast<std::size_t>(phase)]));
      }
    }
    return share;
  };

  const auto at_100 = capture(1.0);
  const auto at_150 = capture(1.5);
  const auto at_200 = capture(2.0);
  save_image_artifact("ui_styled_text_slate_etched_100", at_100);
  save_image_artifact("ui_styled_text_slate_etched_150", at_150);
  save_image_artifact("ui_styled_text_slate_etched_200", at_200);
  // Inset past the capture's last row and column (one past the document) and the 1 px
  // document frame, so margin columns read as flat background.
  const auto inset = [](const QImage& image) { return image.copy(image.rect().adjusted(1, 1, -2, -2)); };
  const auto share_150 = repeated_column_share(inset(at_150), 3);
  const auto share_200 = repeated_column_share(inset(at_200), 2);
  std::cerr << "repeated column share: 150% " << share_150 << ", 200% " << share_200 << '\n';
  CHECK(share_150 < 0.2);
  CHECK(share_200 > 0.95);
}

// 8 x 6 pixels, each a different color: the left half light, the right half dark, none
// equal to any pixel_grid_line_color of another, so a grid line shows as a color that is
// not in the document.
patchy::Document pixel_grid_document() {
  patchy::Document document(8, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(8, 6, patchy::PixelFormat::rgb8(), Qt::white);
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 8; ++x) {
      const auto color = x < 4 ? QColor(230 - 3 * x, 225 - 5 * y, 240) : QColor(20 + 3 * x, 30 + 5 * y, 10);
      fill_pixel_rect(pixels, QRect(x, y, 1, 1), color);
    }
  }
  document.add_pixel_layer("Pixels", std::move(pixels));
  return document;
}

struct PixelGridCheck {
  int mismatches{0};
  int line_pixels{0};
};

// Compares every device pixel of every document cell (inside the document's frame, one
// logical pixel wide) against the expected deep-zoom output: document pixel (x, y) covers device
// columns [qRound(left(x) * ratio), qRound(left(x + 1) * ratio)) with left() the widget
// position, and, when `grid`, its first device column (x > 0) and row (y > 0) carry
// pixel_grid_line_color.
PixelGridCheck check_pixel_grid_cells(patchy::ui::CanvasWidget& canvas, const patchy::Document& document,
                                      bool grid) {
  QApplication::processEvents();
  const auto image = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const auto ratio = canvas.devicePixelRatioF();
  const auto device_x = [&](int x) {
    return qRound(canvas.widget_position_for_document_point(QPoint(x, 0)).x() * ratio);
  };
  const auto device_y = [&](int y) {
    return qRound(canvas.widget_position_for_document_point(QPoint(0, y)).y() * ratio);
  };
  const auto content = canvas_content_rect(canvas);
  const QRect device_content(qRound(content.left() * ratio), qRound(content.top() * ratio),
                             qRound(content.width() * ratio), qRound(content.height() * ratio));
  const auto edge = static_cast<int>(std::ceil(ratio));
  const QRect frame(device_x(0) + edge, device_y(0) + edge, device_x(document.width()) - device_x(0) - 2 * edge,
                    device_y(document.height()) - device_y(0) - 2 * edge);
  const auto inside = frame.intersected(device_content).intersected(image.rect());
  const auto& pixels = document.layers().front().pixels();
  PixelGridCheck result;
  for (int y = 0; y < document.height(); ++y) {
    for (int x = 0; x < document.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      const auto pixel = qRgb(px[0], px[1], px[2]);
      const auto line = patchy::ui::pixel_grid_line_color(pixel);
      for (int dy = device_y(y); dy < device_y(y + 1); ++dy) {
        for (int dx = device_x(x); dx < device_x(x + 1); ++dx) {
          if (!inside.contains(dx, dy)) {
            continue;
          }
          const bool on_line = grid && ((dx == device_x(x) && x > 0) || (dy == device_y(y) && y > 0));
          const auto shown = image.pixel(dx, dy) | 0xff000000U;
          if (on_line) {
            ++result.line_pixels;
          }
          if (shown != (on_line ? line : pixel)) {
            ++result.mismatches;
          }
        }
      }
    }
  }
  return result;
}

// Device pixels inside the document that show none of its pixel colors (grid lines).
int non_document_colors(patchy::ui::CanvasWidget& canvas, const patchy::Document& document) {
  QApplication::processEvents();
  const auto image = canvas.grab().toImage();
  const auto ratio = canvas.devicePixelRatioF();
  std::set<QRgb> colors;
  const auto& pixels = document.layers().front().pixels();
  for (int y = 0; y < document.height(); ++y) {
    for (int x = 0; x < document.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      colors.insert(qRgb(px[0], px[1], px[2]));
    }
  }
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto edge = static_cast<int>(std::ceil(ratio));
  const QRect inside(qRound(top_left.x() * ratio) + edge, qRound(top_left.y() * ratio) + edge,
                     qRound((bottom_right.x() - top_left.x()) * ratio) - 2 * edge,
                     qRound((bottom_right.y() - top_left.y()) * ratio) - 2 * edge);
  int count = 0;
  for (int y = inside.top(); y <= inside.bottom(); ++y) {
    for (int x = inside.left(); x <= inside.right(); ++x) {
      if (image.rect().contains(x, y) && !colors.contains(image.pixel(x, y) | 0xff000000U)) {
        ++count;
      }
    }
  }
  return count;
}

// The pixel grid (GitHub issue 239) appears at 600% and not below, tints each line from
// its own pixel (lighter over dark pixels, darker over light ones), and View > Pixel
// Grid off removes it.
void ui_canvas_pixel_grid_shows_from_600_percent() {
  using patchy::ui::pixel_grid_line_color;
  CHECK(pixel_grid_line_color(qRgb(255, 255, 255)) == qRgb(200, 200, 200));
  CHECK(pixel_grid_line_color(qRgb(0, 0, 0)) == qRgb(63, 63, 63));
  CHECK(qGray(pixel_grid_line_color(qRgb(240, 200, 60))) < qGray(qRgb(240, 200, 60)));
  CHECK(qGray(pixel_grid_line_color(qRgb(30, 40, 90))) > qGray(qRgb(30, 40, 90)));
  // A transparent pixel reads over the light checkerboard, so its line is darker gray.
  CHECK(qGray(pixel_grid_line_color(qRgba(0, 0, 0, 0))) < 224);

  auto document = pixel_grid_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 240);
  canvas.set_document(&document);
  canvas.show();
  CHECK(canvas.pixel_grid_visible());

  for (const double zoom : {4.0, 5.0, 5.9}) {
    canvas.set_view_zoom_centered(zoom);
    if (const auto lines = non_document_colors(canvas, document); lines != 0) {
      std::cerr << lines << " grid pixels at " << zoom << '\n';
      CHECK(false);
    }
  }
  for (const double zoom : {6.0, 6.5, 8.0, 12.0}) {
    canvas.set_view_zoom_centered(zoom);
    const auto check = check_pixel_grid_cells(canvas, document, true);
    if (check.mismatches != 0 || check.line_pixels == 0) {
      std::cerr << "pixel grid at " << zoom << ": " << check.mismatches << " mismatches, " << check.line_pixels
                << " line pixels\n";
      save_image_artifact("ui_canvas_pixel_grid_threshold", canvas.grab().toImage());
      CHECK(false);
    }
  }
  canvas.set_view_zoom_centered(8.0);
  canvas.set_pixel_grid_visible(false);
  CHECK(non_document_colors(canvas, document) == 0);
  CHECK(check_pixel_grid_cells(canvas, document, false).mismatches == 0);
}

// Grid lines sit exactly on the first device column and row of each pixel at 800% and
// 1600%, at device pixel ratio 1 here and 1.5 in a child run of this test
// (QT_SCALE_FACTOR=1.5).
void ui_canvas_pixel_grid_lands_on_pixel_boundaries() {
  const bool child = qEnvironmentVariableIsSet("PATCHY_PIXEL_GRID_DPR_CHILD");
  auto document = pixel_grid_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 240);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  const auto ratio = canvas.devicePixelRatioF();
  std::cout << "pixel grid device pixel ratio " << ratio << '\n';
  CHECK(std::abs(ratio - (child ? 1.5 : 1.0)) < 0.001);
  for (const double zoom : {8.0, 16.0}) {
    canvas.set_view_zoom_centered(zoom);
    const auto check = check_pixel_grid_cells(canvas, document, true);
    if (check.mismatches != 0 || check.line_pixels == 0) {
      std::cerr << "pixel grid at " << zoom << " ratio " << ratio << ": " << check.mismatches << " mismatches, "
                << check.line_pixels << " line pixels\n";
      save_image_artifact(child ? "ui_canvas_pixel_grid_boundaries_150" : "ui_canvas_pixel_grid_boundaries_100",
                          canvas.grab().toImage());
      CHECK(false);
    }
  }
  if (child) {
    return;
  }
  QProcess scaled;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_SCALE_FACTOR"), QStringLiteral("1.5"));
  environment.insert(QStringLiteral("PATCHY_PIXEL_GRID_DPR_CHILD"), QStringLiteral("1"));
  environment.remove(QStringLiteral("PATCHY_UI_TEST_FILTER"));
  scaled.setProcessEnvironment(environment);
  scaled.setProcessChannelMode(QProcess::MergedChannels);
  scaled.setProgram(QCoreApplication::applicationFilePath());
  scaled.setArguments({QStringLiteral("ui_canvas_pixel_grid_lands_on_pixel_boundaries")});
  scaled.start();
  CHECK(scaled.waitForStarted(30000));
  CHECK(scaled.waitForFinished(180000));
  const auto output = QString::fromUtf8(scaled.readAll());
  const bool passed = scaled.exitStatus() == QProcess::NormalExit && scaled.exitCode() == 0 &&
                      output.contains(QStringLiteral("pixel grid device pixel ratio 1.5")) &&
                      output.contains(QStringLiteral("[PASS] ui_canvas_pixel_grid_lands_on_pixel_boundaries")) &&
                      !output.contains(QStringLiteral("[FAIL]"));
  if (!passed) {
    std::cerr << "device pixel ratio 1.5 child run:\n" << output.toStdString() << '\n';
  }
  CHECK(passed);
}

// Issue 239's before/after images: a light and a dark area of a varied image at 800%, with
// View > Pixel Grid off (what Patchy drew before) and on.
void ui_canvas_pixel_grid_capture_light_and_dark_areas() {
  patchy::Document document(32, 12, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(32, 12, patchy::PixelFormat::rgb8(), Qt::white);
  std::uint64_t state = 0x239239239ULL;
  const auto next = [&state] {
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<int>((z ^ (z >> 31)) % 24U);
  };
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 32; ++x) {
      const auto color = x < 16 ? QColor(214 + next() / 2, 222 + next() / 3, 236 + next() / 3 - y)
                                : QColor(46 + next() + y, 32 + next(), 24 + next() / 2);
      fill_pixel_rect(pixels, QRect(x, y, 1, 1), color);
    }
  }
  document.add_pixel_layer("Photo", std::move(pixels));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 240);
  canvas.set_document(&document);
  canvas.show();
  const auto capture = [&](QPoint center) {
    canvas.set_view_zoom_centered(8.0);
    QApplication::processEvents();
    // Center the 12 x 12 pixel area around `center` (8 px per pixel = 96 x 96 screen px).
    const auto at = canvas.widget_position_for_document_point(center);
    const QRect area(at.x() - 48, at.y() - 48, 96, 96);
    return render_widget_image(canvas).copy(area.intersected(canvas_content_rect(canvas)));
  };
  // The view shows 40 x 30 document pixels at 800%, so the whole 32 x 12 image fits.
  for (const bool grid : {false, true}) {
    canvas.set_pixel_grid_visible(grid);
    const auto light = capture(QPoint(8, 6));
    const auto dark = capture(QPoint(24, 6));
    save_image_artifact(grid ? "ui_pixel_grid_800_light_after" : "ui_pixel_grid_800_light_before", light);
    save_image_artifact(grid ? "ui_pixel_grid_800_dark_after" : "ui_pixel_grid_800_dark_before", dark);
  }
  CHECK(non_document_colors(canvas, document) > 0);
}

// View > Pixel Grid is on by default, saved as view/pixelGrid, read by a new window, and
// reachable from scripts (patchy.ui.pixelGrid and the view.pixel_grid command).
void ui_pixel_grid_toggle_persists_and_scripts() {
  using patchy::test::ui::scripting_support::backlog_contains;
  using patchy::test::ui::scripting_support::run_script;
  SettingsValueRestorer saved(QStringLiteral("view/pixelGrid"));
  patchy::ui::app_settings().remove(QStringLiteral("view/pixelGrid"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* action = window.findChild<QAction*>(QStringLiteral("viewTogglePixelGridAction"));
    CHECK(action != nullptr);
    if (action == nullptr) {
      return;
    }
    auto* canvas = require_canvas(window);
    CHECK(action->isCheckable() && action->isChecked());
    CHECK(action->menuRole() == QAction::NoRole);
    CHECK(canvas->pixel_grid_visible());
    action->trigger();
    QApplication::processEvents();
    CHECK(!canvas->pixel_grid_visible());
    CHECK(!patchy::ui::app_settings().value(QStringLiteral("view/pixelGrid"), true).toBool());
    CHECK(run_script(window, QStringLiteral(
                                 "console.log('grid=' + patchy.ui.pixelGrid); patchy.ui.pixelGrid = true;"
                                 "console.log('grid now=' + patchy.ui.pixelGrid);")));
    CHECK(backlog_contains(window, QStringLiteral("grid=false")));
    CHECK(backlog_contains(window, QStringLiteral("grid now=true")));
    CHECK(action->isChecked() && canvas->pixel_grid_visible());
    CHECK(patchy::ui::app_settings().value(QStringLiteral("view/pixelGrid"), false).toBool());
    CHECK(run_script(window, QStringLiteral("console.log('command=' + app.runCommand('view.pixel_grid'));")));
    CHECK(backlog_contains(window, QStringLiteral("command=true")));
    CHECK(!action->isChecked() && !canvas->pixel_grid_visible());
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* action = window.findChild<QAction*>(QStringLiteral("viewTogglePixelGridAction"));
    CHECK(action != nullptr && !action->isChecked());
    CHECK(!require_canvas(window)->pixel_grid_visible());
  }
}

void ui_zoomed_out_canvas_uses_downsampled_display_mip() {
  patchy::Document document(256, 256, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(256, 256, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto value = ((x + y) % 2 == 0) ? 0 : 255;
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(value);
      px[1] = static_cast<std::uint8_t>(value);
      px[2] = static_cast<std::uint8_t>(value);
    }
  }
  document.add_pixel_layer("Checker", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 180);
  canvas.set_document(&document);
  canvas.set_zoom(0.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QRect target_rect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
  const auto sample_rect = target_rect.adjusted(4, 4, -4, -4).intersected(preview.rect());
  CHECK(!sample_rect.isEmpty());

  int midtone_samples = 0;
  int source_tone_samples = 0;
  for (int y = sample_rect.top(); y <= sample_rect.bottom(); y += 3) {
    for (int x = sample_rect.left(); x <= sample_rect.right(); x += 3) {
      const auto color = preview.pixelColor(x, y);
      const auto value = (color.red() + color.green() + color.blue()) / 3;
      if (value >= 96 && value <= 160) {
        ++midtone_samples;
      }
      if (value <= 24 || value >= 231) {
        ++source_tone_samples;
      }
    }
  }

  CHECK(midtone_samples > 0);
  CHECK(midtone_samples > source_tone_samples * 4);
}

void ui_stamp_and_gradient_flyouts_swap_tools() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto* stamp_button = window.findChild<QToolButton*>(QStringLiteral("stampToolButton"));
  CHECK(stamp_button != nullptr);
  CHECK(stamp_button->menu() != nullptr);
  CHECK(stamp_button->menu()->actions().size() == 4);  // two tools, separator, Cycle
  CHECK(stamp_button->defaultAction() == require_action(window, "toolCloneAction"));
  require_action(window, "toolPatternStampAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PatternStamp);
  CHECK(stamp_button->defaultAction() == require_action(window, "toolPatternStampAction"));

  auto* gradient_button = window.findChild<QToolButton*>(QStringLiteral("gradientToolButton"));
  CHECK(gradient_button != nullptr);
  CHECK(gradient_button->menu() != nullptr);
  CHECK(gradient_button->menu()->actions().size() == 4);
  CHECK(gradient_button->defaultAction() == require_action(window, "toolGradientAction"));
  require_action(window, "toolFillAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Fill);
  CHECK(gradient_button->defaultAction() == require_action(window, "toolFillAction"));

  auto* healing_button = window.findChild<QToolButton*>(QStringLiteral("healingToolButton"));
  CHECK(healing_button != nullptr);
  CHECK(healing_button->menu() != nullptr);
  CHECK(healing_button->menu()->actions().size() == 5);
  CHECK(healing_button->defaultAction() == require_action(window, "toolHealingBrushAction"));
  require_action(window, "toolSpotHealingAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::SpotHealing);
  CHECK(healing_button->defaultAction() == require_action(window, "toolSpotHealingAction"));
  require_action(window, "toolPatchAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PatchTool);
  CHECK(healing_button->defaultAction() == require_action(window, "toolPatchAction"));

  auto* pen_button = window.findChild<QToolButton*>(QStringLiteral("penToolButton"));
  CHECK(pen_button != nullptr);
  CHECK(pen_button->menu() != nullptr);
  CHECK(pen_button->menu()->actions().size() == 6);
  CHECK(pen_button->defaultAction() == require_action(window, "toolPenAction"));
  require_action(window, "toolConvertPointAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::ConvertPoint);
  CHECK(pen_button->defaultAction() == require_action(window, "toolConvertPointAction"));

  // Every flyout button carries the QSS corner-marker property, including the
  // smudge/dodge buttons that historically lacked the indicator.
  for (const auto* name : {"marqueeToolButton", "lassoToolButton", "wandToolButton", "gradientToolButton",
                           "stampToolButton", "healingToolButton", "detailToolButton", "toneToolButton",
                           "shapeToolButton", "pathSelectToolButton", "penToolButton"}) {
    auto* button = window.findChild<QToolButton*>(QString::fromLatin1(name));
    CHECK(button != nullptr);
    CHECK(button->property("toolFlyout").toBool());
  }
}

// Shift+<letter> walks a flyout in menu order from the active tool, wrapping,
// and from the button's shown tool when the active tool is elsewhere (GitHub
// issue 45, Photoshop's Shift+key convention). The plain letter still selects
// the group's primary tool.
void ui_tool_cycle_hotkeys_walk_each_flyout() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Brush);

  auto* healing_button = window.findChild<QToolButton*>(QStringLiteral("healingToolButton"));
  CHECK(healing_button != nullptr);
  auto* cycle_healing = require_action(window, "toolCycleHealingAction");
  CHECK(healing_button->menu()->actions().contains(cycle_healing));
  CHECK(!cycle_healing->isCheckable());
  CHECK(cycle_healing->isEnabled());

  // From an unrelated tool the first press steps past the shown member.
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::SpotHealing);
  CHECK(healing_button->defaultAction() == require_action(window, "toolSpotHealingAction"));
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PatchTool);
  CHECK(healing_button->defaultAction() == require_action(window, "toolPatchAction"));
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);
  CHECK(healing_button->defaultAction() == require_action(window, "toolHealingBrushAction"));
  require_action(window, "toolPatchAction")->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_J);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);

  // A two-member group toggles; the cycle command drives the flyout button too.
  auto* gradient_button = window.findChild<QToolButton*>(QStringLiteral("gradientToolButton"));
  CHECK(gradient_button != nullptr);
  CHECK(gradient_button->defaultAction() == require_action(window, "toolGradientAction"));
  QTest::keyClick(canvas, Qt::Key_G, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Fill);
  CHECK(gradient_button->defaultAction() == require_action(window, "toolFillAction"));
  QTest::keyClick(canvas, Qt::Key_G, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Gradient);
  require_action(window, "toolCycleFillAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Fill);

  // Shift+L walks Lasso, Polygonal Lasso, Magnetic Lasso (GitHub issue 287). The new member
  // keeps its own persistent id and ships unbound, like every member past the first.
  auto* lasso_button = window.findChild<QToolButton*>(QStringLiteral("lassoToolButton"));
  CHECK(lasso_button != nullptr);
  auto* polygon_lasso = require_action(window, "toolPolygonalLassoAction");
  CHECK(lasso_button->menu()->actions().indexOf(polygon_lasso) ==
        lasso_button->menu()->actions().indexOf(require_action(window, "toolLassoAction")) + 1);
  QTest::keyClick(canvas, Qt::Key_L, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(patchy::ui::tool_hotkey_id(canvas->tool()) == QStringLiteral("tools.polygonal_lasso"));
  CHECK(lasso_button->defaultAction() == polygon_lasso);
  QTest::keyClick(canvas, Qt::Key_L, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagneticLasso);
  QTest::keyClick(canvas, Qt::Key_L, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Lasso);
  QTest::keyClick(canvas, Qt::Key_L, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(lasso_button->defaultAction() == polygon_lasso);
  const auto* polygon_command = window.hotkey_registry().find_command(QStringLiteral("tools.polygonal_lasso"));
  CHECK(polygon_command != nullptr);
  CHECK(polygon_command->action == polygon_lasso);
  CHECK(polygon_command->category == QStringLiteral("tools"));
  CHECK(polygon_lasso->shortcut().isEmpty());

  // Every flyout owns one, in the tools category, listed after a separator.
  const auto& registry = window.hotkey_registry();
  const std::pair<const char*, const char*> cycles[] = {
      {"toolCycleMarqueeAction", "tools.cycle.marquee"}, {"toolCycleLassoAction", "tools.cycle.lasso"},
      {"toolCycleWandAction", "tools.cycle.wand"},       {"toolCycleFillAction", "tools.cycle.gradient"},
      {"toolCycleStampAction", "tools.cycle.stamp"},     {"toolCycleHealingAction", "tools.cycle.healing"},
      {"toolCycleDetailAction", "tools.cycle.detail"},   {"toolCycleToningAction", "tools.cycle.tone"},
      {"toolCyclePenAction", "tools.cycle.pen"},         {"toolCyclePathAction", "tools.cycle.path_select"},
      {"toolCycleShapeAction", "tools.cycle.shape"},     {"toolCycleMoveAction", "tools.cycle.move"},
  };
  for (const auto& [object_name, id] : cycles) {
    auto* action = require_action(window, object_name);
    CHECK(!action->shortcut().isEmpty());
    CHECK(action->menuRole() == QAction::NoRole);
    const auto* command = registry.find_command(QString::fromLatin1(id));
    CHECK(command != nullptr);
    CHECK(command->action == action);
    CHECK(command->category == QStringLiteral("tools"));
  }
}

// GitHub issue 308: the Shape flyout leads with its lettered default, Rect (U), and Line
// sits below Polygon. Shift+U walks the same order; U still selects Rect. The artifact is
// the open flyout, the issue's AFTER shot.
void ui_shape_flyout_leads_with_rect_and_cycles_in_menu_order() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(button != nullptr && button->menu() != nullptr);
  if (button == nullptr || button->menu() == nullptr) {
    return;
  }
  const std::pair<const char*, patchy::ui::CanvasTool> order[] = {
      {"toolRectAction", patchy::ui::CanvasTool::Rectangle},
      {"toolEllipseAction", patchy::ui::CanvasTool::Ellipse},
      {"toolPolygonAction", patchy::ui::CanvasTool::Polygon},
      {"toolLineAction", patchy::ui::CanvasTool::Line},
      {"toolCustomShapeAction", patchy::ui::CanvasTool::CustomShape},
  };
  const auto entries = button->menu()->actions();
  CHECK(entries.size() == 7);  // five tools, separator, Cycle Shape Tools
  for (std::size_t i = 0; i < std::size(order); ++i) {
    CHECK(entries.value(static_cast<int>(i)) == require_action(window, order[i].first));
  }
  CHECK(entries.value(5) != nullptr && entries.value(5)->isSeparator());
  CHECK(entries.value(6) == require_action(window, "toolCycleShapeAction"));
  CHECK(button->defaultAction() == require_action(window, "toolRectAction"));
  CHECK(require_action(window, "toolRectAction")->shortcut() == QKeySequence(Qt::Key_U));

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_U);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  for (std::size_t step = 1; step <= std::size(order); ++step) {
    QTest::keyClick(canvas, Qt::Key_U, Qt::ShiftModifier);
    QApplication::processEvents();
    const auto& expected = order[step % std::size(order)];
    CHECK(canvas->tool() == expected.second);
    CHECK(button->defaultAction() == require_action(window, expected.first));
  }

  auto* menu = button->menu();
  bool shown = false;
  QTimer::singleShot(0, [&] {
    shown = menu->isVisible();
    save_window_popup_artifact("issue_308_shape_flyout", window, *button, *menu);
    menu->close();
  });
  button->showMenu();
  QApplication::processEvents();
  CHECK(shown);
}

void ui_tool_flyout_double_click_opens_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(button != nullptr);
  auto* menu = button->menu();
  CHECK(menu != nullptr);
  const QPoint center(button->width() / 2, button->height() / 2);

  // A plain click selects the default tool and never shows the flyout.
  click_widget_like_a_user(*button, center);
  process_events_for(20);
  CHECK(!menu->isVisible());
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Marquee);

  // The second press of a double-click arrives as MouseButtonDblClick; the
  // flyout filter opens the menu through showMenu(), which blocks in exec(),
  // so a queued timer records the visible menu and closes it.
  bool shown = false;
  QTimer::singleShot(0, [&] {
    shown = menu->isVisible();
    menu->close();
  });
  send_double_click(*button, center);
  CHECK(shown);
  CHECK(!menu->isVisible());
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Marquee);
  CHECK(button->defaultAction() == require_action(window, "toolMarqueeAction"));
}

void ui_tool_flyout_right_click_opens_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(button != nullptr);
  auto* menu = button->menu();
  CHECK(menu != nullptr);

  bool shown = false;
  QTimer::singleShot(0, [&] {
    shown = menu->isVisible();
    menu->close();
  });
  send_mouse(*button, QEvent::MouseButtonPress, button->rect().center(), Qt::RightButton, Qt::RightButton);
  QApplication::processEvents();
  CHECK(shown);
  CHECK(!menu->isVisible());
}

// Where a flyout row's icon box starts (GitHub issue 284). The current tool's marker (issue
// 284 follow-up) sits in the margin before it.
constexpr int kFlyoutIconLeft = 14;

// GitHub issue 284: a flyout opens beside its button rather than over its right edge, and
// draws each tool's icon at the palette's own size, centered between the menu edge and the
// tool names. The artifacts are the issue shots, in both schemes.
void ui_tool_flyout_opens_beside_its_button_with_palette_size_icons() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    auto* palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
    auto* button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
    CHECK(palette != nullptr && button != nullptr && button->menu() != nullptr);
    if (palette == nullptr || button == nullptr || button->menu() == nullptr) {
      return;
    }
    auto* menu = button->menu();
    const int icon_extent = palette->iconSize().width();
    CHECK(menu->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, menu) == icon_extent);

    QRect menu_geometry;
    QImage menu_image;
    QTimer::singleShot(0, [&] {
      menu_geometry = menu->geometry();
      menu_image = menu->grab().toImage();
      save_window_popup_artifact("issue_284_marquee_flyout" + suffix, window, *button, *menu);
      menu->close();
    });
    button->showMenu();
    QApplication::processEvents();
    CHECK(!menu->isVisible());

    // Beside the button, covering none of it, with at most a small gap.
    const QPoint beside = button->mapToGlobal(QPoint(button->width(), 0));
    std::printf("  button right edge %d, top %d; menu at %d, %d\n", beside.x(), beside.y(), menu_geometry.left(),
                menu_geometry.top());
    CHECK(menu_geometry.left() >= beside.x());
    CHECK(menu_geometry.left() <= beside.x() + 4);
    CHECK(std::abs(menu_geometry.top() - beside.y()) <= 1);

    CHECK(!menu_image.isNull() && menu_image.devicePixelRatio() == 1.0);
    if (menu_image.isNull() || menu_image.devicePixelRatio() != 1.0) {
      return;
    }
    // Ink columns of a row: pixels that differ clearly from the row's background.
    const auto ink_columns = [&menu_image](const QRect& row) {
      const QColor background = menu_image.pixelColor(row.left() + 1, row.top() + 1);
      std::vector<bool> ink(static_cast<std::size_t>(row.width()), false);
      for (int x = row.left(); x <= row.right(); ++x) {
        for (int y = row.top(); y <= row.bottom(); ++y) {
          const QColor color = menu_image.pixelColor(x, y);
          const int delta = std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
                            std::abs(color.blue() - background.blue());
          if (delta > 60) {
            ink[static_cast<std::size_t>(x - row.left())] = true;
            break;
          }
        }
      }
      return ink;
    };
    for (const char* name : {"toolMarqueeAction", "toolEllipticalMarqueeAction"}) {
      auto* action = require_action(window, name);
      const QRect row = menu->actionGeometry(action);
      CHECK(row.isValid() && menu_image.rect().contains(row));
      if (!row.isValid() || !menu_image.rect().contains(row)) {
        continue;
      }
      // The same icon at the palette's size: its ink extent and where the ink sits in its box.
      const QImage reference = action->icon().pixmap(QSize(icon_extent, icon_extent), 1.0).toImage();
      int reference_left = reference.width();
      int reference_right = -1;
      for (int y = 0; y < reference.height(); ++y) {
        for (int x = 0; x < reference.width(); ++x) {
          if (qAlpha(reference.pixel(x, y)) > 28) {
            reference_left = std::min(reference_left, x);
            reference_right = std::max(reference_right, x);
          }
        }
      }
      CHECK(reference_right >= reference_left);
      const auto ink = ink_columns(row);
      int icon_left = -1;
      int icon_right = -1;
      int text_left = -1;
      for (int x = kFlyoutIconLeft; x < static_cast<int>(ink.size()); ++x) {
        if (!ink[static_cast<std::size_t>(x)]) {
          continue;
        }
        if (icon_left < 0) {
          icon_left = x;
          icon_right = x;
        } else if (text_left < 0 && x - icon_right <= 3) {
          icon_right = x;
        } else if (text_left < 0) {
          text_left = x;
        }
      }
      CHECK(icon_left >= 0 && text_left > icon_right);
      const int measured = icon_right - icon_left + 1;
      const int expected = reference_right - reference_left + 1;
      const int box_left = icon_left - reference_left;
      const int box_right = box_left + icon_extent;
      std::printf("  %s: icon ink %d px (palette %d px), %d px after the menu edge, %d px before the name\n", name,
                  measured, expected, box_left, text_left - box_right);
      CHECK(std::abs(measured - expected) <= 1);
      CHECK(std::abs(box_left - (text_left - box_right)) <= 2);
    }
  }
}

// GitHub issue 284 follow-up: the flyout marks the tool its button shows with a small
// accent square a few pixels before its icon, and the mark moves when another tool is
// picked. Marked and unmarked rows keep their icons and names at the same place. The
// artifacts are the issue shots, in both schemes.
void ui_tool_flyout_marks_the_tool_its_button_shows() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    auto* palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
    auto* button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
    CHECK(palette != nullptr && button != nullptr && button->menu() != nullptr);
    if (palette == nullptr || button == nullptr || button->menu() == nullptr) {
      return;
    }
    auto* menu = button->menu();
    auto* marquee = require_action(window, "toolMarqueeAction");
    auto* ellipse = require_action(window, "toolEllipticalMarqueeAction");
    const int icon_extent = palette->iconSize().width();
    const QColor accent = patchy::ui::theme().accent;
    const auto open_flyout = [&](const std::string& shot) {
      QImage image;
      QTimer::singleShot(0, [&] {
        image = menu->grab().toImage();
        save_window_popup_artifact(shot + suffix, window, *button, *menu);
        menu->close();
      });
      button->showMenu();
      QApplication::processEvents();
      return image;
    };
    // Accent pixels in the margin before a row's icon box, and the rightmost one's column
    // (row-relative; -1 when there is none).
    const auto marker_scan = [&](const QImage& image, QAction* action) {
      const QRect row = menu->actionGeometry(action);
      int count = 0;
      int right = -1;
      for (int y = row.top(); y <= row.bottom(); ++y) {
        for (int x = row.left(); x < row.left() + kFlyoutIconLeft; ++x) {
          const QColor color = image.pixelColor(x, y);
          if (std::abs(color.red() - accent.red()) + std::abs(color.green() - accent.green()) +
                  std::abs(color.blue() - accent.blue()) <
              40) {
            ++count;
            right = std::max(right, x - row.left());
          }
        }
      }
      return std::make_pair(count, right);
    };
    const auto marker_pixels = [&](const QImage& image, QAction* action) { return marker_scan(image, action).first; };
    // Where a row's icon box and name start, past the marker.
    const auto icon_and_name = [&](const QImage& image, QAction* action) {
      const QRect row = menu->actionGeometry(action);
      const QColor background = image.pixelColor(row.right() - 2, row.top() + 1);
      const QImage reference = action->icon().pixmap(QSize(icon_extent, icon_extent), 1.0).toImage();
      int reference_left = reference.width();
      for (int y = 0; y < reference.height(); ++y) {
        for (int x = 0; x < reference.width(); ++x) {
          if (qAlpha(reference.pixel(x, y)) > 28) {
            reference_left = std::min(reference_left, x);
          }
        }
      }
      int icon_left = -1;
      int icon_right = -1;
      int name_left = -1;
      for (int x = row.left() + kFlyoutIconLeft; x <= row.right() && name_left < 0; ++x) {
        bool ink = false;
        for (int y = row.top(); y <= row.bottom() && !ink; ++y) {
          const QColor color = image.pixelColor(x, y);
          ink = std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
                    std::abs(color.blue() - background.blue()) > 60;
        }
        if (!ink) {
          continue;
        }
        if (icon_left < 0) {
          icon_left = icon_right = x;
        } else if (x - icon_right <= 3) {
          icon_right = x;
        } else {
          name_left = x;
        }
      }
      return std::make_pair(icon_left - reference_left - row.left(), name_left - row.left());
    };

    const QImage first = open_flyout("issue_284_flyout_marks_marquee");
    CHECK(!first.isNull() && first.devicePixelRatio() == 1.0);
    if (first.isNull() || first.devicePixelRatio() != 1.0) {
      return;
    }
    CHECK(button->defaultAction() == marquee);
    std::printf("  %s: marker pixels marquee %d, ellipse %d\n", suffix.c_str(), marker_pixels(first, marquee),
                marker_pixels(first, ellipse));
    CHECK(marker_pixels(first, marquee) >= 16);
    CHECK(marker_pixels(first, ellipse) == 0);
    const auto marquee_marked = icon_and_name(first, marquee);
    const auto ellipse_plain = icon_and_name(first, ellipse);
    // The marker sits close to its icon: a few pixels between them, no more.
    const int marker_right = marker_scan(first, marquee).second;
    std::printf("  %s: marker ends at %d, icon box starts at %d\n", suffix.c_str(), marker_right,
                marquee_marked.first);
    CHECK(marker_right >= 0);
    CHECK(marquee_marked.first - marker_right - 1 >= 2);
    CHECK(marquee_marked.first - marker_right - 1 <= 4);
    CHECK(std::abs(marquee_marked.first - ellipse_plain.first) <= 1);
    CHECK(std::abs(marquee_marked.second - ellipse_plain.second) <= 1);

    ellipse->trigger();
    QApplication::processEvents();
    CHECK(button->defaultAction() == ellipse);
    const QImage second = open_flyout("issue_284_flyout_marks_ellipse");
    CHECK(!second.isNull());
    if (second.isNull()) {
      return;
    }
    CHECK(marker_pixels(second, ellipse) >= 16);
    CHECK(marker_pixels(second, marquee) == 0);
    const auto marquee_plain = icon_and_name(second, marquee);
    const auto ellipse_marked = icon_and_name(second, ellipse);
    CHECK(std::abs(marquee_plain.first - marquee_marked.first) <= 1);
    CHECK(std::abs(marquee_plain.second - marquee_marked.second) <= 1);
    CHECK(std::abs(ellipse_marked.first - ellipse_plain.first) <= 1);
    CHECK(std::abs(ellipse_marked.second - ellipse_plain.second) <= 1);
  }
}

void ui_shape_flyout_and_zoom_tool_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(zoom_button != nullptr);

  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Elliptical Marquee")));

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Ellipse);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Ellipse")));

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Zoom);
  canvas->set_zoom(0.25);
  const auto before_zoom = canvas->zoom();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(420, 320)));
  CHECK(canvas->zoom() > before_zoom);

  canvas->set_zoom(2.0);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  save_widget_artifact("ui_shape_flyout_zoom_tool", window);
}

// Scrubby Zoom (GitHub issue 51): the Zoom tool's options-bar checkbox is off by
// default, persists as tools/zoomScrubby, reaches the active canvas, and follows
// the window into a second document.
void ui_zoom_tool_scrubby_option_persists_and_reaches_canvas() {
  SettingsValueRestorer saved_scrubby(QStringLiteral("tools/zoomScrubby"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/zoomScrubby"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* scrubby = window.findChild<QCheckBox*>(QStringLiteral("zoomScrubbyCheck"));
  CHECK(scrubby != nullptr);
  if (scrubby == nullptr) {
    return;
  }
  CHECK(!scrubby->isChecked());
  CHECK(!canvas->zoom_scrubby());

  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  CHECK(!scrubby->isVisible());
  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Zoom);
  CHECK(scrubby->isVisible());
  CHECK(scrubby->isEnabled());

  scrubby->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->zoom_scrubby());
  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/zoomScrubby"), false).toBool());
  }

  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != canvas);
  CHECK(second->zoom_scrubby());
  CHECK(scrubby->isChecked());
}

// With Scrubby Zoom on, a Zoom tool drag zooms live from horizontal travel
// (1.01 per pixel, right = in) about the press point, which stays put; a press
// without travel is still the fixed-factor click, Alt+drag scrubs, vertical
// travel does nothing, and turning the option off restores the marquee.
void ui_zoom_tool_scrubby_drag_zooms_live_around_press_point() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(200, 200, 200, 255)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(400, 300);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Zoom);
  canvas.set_zoom_scrubby(true);
  canvas.show();
  QApplication::processEvents();

  const QPoint anchor_document(40, 30);
  const auto press = [&canvas](QPoint at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
  };
  const auto move = [&canvas](QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
  };
  const auto release = [&canvas](QPoint at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
  };

  // (a) Drag right in steps: the zoom follows 1.01^dx and the pressed document
  // point stays under the press position.
  canvas.set_zoom(1.0);
  const auto start = canvas.widget_position_for_document_point(anchor_document);
  press(start);
  move(start + QPoint(20, 0));
  move(start + QPoint(60, 0));
  move(start + QPoint(100, 0));
  release(start + QPoint(100, 0));
  const auto scrubbed_in = canvas.zoom();
  CHECK(std::abs(scrubbed_in - std::pow(1.01, 100)) < 0.05);
  const auto anchor_after = canvas.widget_position_for_document_point(anchor_document);
  CHECK((anchor_after - start).manhattanLength() <= 2);

  // (b) Drag left the same distance: back to the starting zoom.
  const auto left_start = canvas.widget_position_for_document_point(anchor_document);
  press(left_start);
  move(left_start + QPoint(-50, 0));
  move(left_start + QPoint(-100, 0));
  release(left_start + QPoint(-100, 0));
  CHECK(std::abs(canvas.zoom() - 1.0) < 0.05);

  // (c) A press without travel is still a click: 2x, and 0.5x with Alt.
  canvas.set_zoom(1.0);
  const auto click_at = canvas.widget_position_for_document_point(anchor_document);
  press(click_at);
  release(click_at);
  CHECK(std::abs(canvas.zoom() - 2.0) < 0.001);
  const auto alt_click_at = canvas.widget_position_for_document_point(anchor_document);
  press(alt_click_at, Qt::AltModifier);
  release(alt_click_at, Qt::AltModifier);
  CHECK(std::abs(canvas.zoom() - 1.0) < 0.001);

  // (d) Alt only matters to a click: an Alt drag to the right still zooms in.
  const auto alt_drag_at = canvas.widget_position_for_document_point(anchor_document);
  press(alt_drag_at, Qt::AltModifier);
  move(alt_drag_at + QPoint(50, 0), Qt::AltModifier);
  release(alt_drag_at + QPoint(50, 0), Qt::AltModifier);
  CHECK(canvas.zoom() > 1.5);

  // (e) Vertical travel past the click slop is neither a click nor a zoom.
  canvas.set_zoom(1.0);
  const auto vertical_at = canvas.widget_position_for_document_point(anchor_document);
  press(vertical_at);
  move(vertical_at + QPoint(0, 40));
  release(vertical_at + QPoint(0, 40));
  CHECK(std::abs(canvas.zoom() - 1.0) < 0.001);

  // (f) Option off: the same drag is a marquee fit, not a scrub.
  canvas.set_zoom_scrubby(false);
  canvas.set_zoom(1.0);
  const auto marquee_at = canvas.widget_position_for_document_point(anchor_document);
  press(marquee_at);
  move(marquee_at + QPoint(60, 30));
  release(marquee_at + QPoint(60, 30));
  CHECK(std::abs(canvas.zoom() - std::pow(1.01, 60)) > 0.05);
  CHECK(canvas.zoom() > 1.0);
}

// The Zoom tool's Zoom In / Zoom Out toggle sets the click direction (Alt
// inverts it), persists as tools/zoomToolZoomsOut, and follows the window into
// a second document.
void ui_zoom_tool_direction_buttons_set_click_direction() {
  SettingsValueRestorer saved_direction(QStringLiteral("tools/zoomToolZoomsOut"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/zoomToolZoomsOut"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* zoom_in_mode = window.findChild<QAction*>(QStringLiteral("zoomInModeAction"));
  auto* zoom_out_mode = window.findChild<QAction*>(QStringLiteral("zoomOutModeAction"));
  CHECK(zoom_in_mode != nullptr);
  CHECK(zoom_out_mode != nullptr);
  if (zoom_in_mode == nullptr || zoom_out_mode == nullptr) {
    return;
  }
  CHECK(zoom_in_mode->isCheckable());
  CHECK(zoom_in_mode->isChecked());
  CHECK(!zoom_out_mode->isChecked());
  CHECK(!canvas->zoom_tool_zooms_out());

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Zoom);
  const auto click = [canvas](Qt::KeyboardModifiers modifiers) {
    const auto at = canvas->widget_position_for_document_point(QPoint(10, 10));
    send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
  };
  canvas->set_zoom(1.0);
  click(Qt::NoModifier);
  CHECK(std::abs(canvas->zoom() - 2.0) < 0.001);

  zoom_out_mode->trigger();
  QApplication::processEvents();
  CHECK(zoom_out_mode->isChecked());
  CHECK(!zoom_in_mode->isChecked());
  CHECK(canvas->zoom_tool_zooms_out());
  click(Qt::NoModifier);
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  click(Qt::AltModifier);
  CHECK(std::abs(canvas->zoom() - 2.0) < 0.001);

  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/zoomToolZoomsOut"), false).toBool());
  }
  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != canvas);
  CHECK(second->zoom_tool_zooms_out());
  CHECK(zoom_out_mode->isChecked());

  zoom_in_mode->trigger();
  QApplication::processEvents();
  CHECK(!second->zoom_tool_zooms_out());
}

// GitHub issue 77: Zoom In/Out and the Zoom tool click walk Photoshop's zoom
// ladder, so an off-ladder view lands on the next rung instead of a multiple.
void ui_zoom_steps_follow_photoshop_ladder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* zoom_in = require_action(window, "viewZoomInAction");
  auto* zoom_out = require_action(window, "viewZoomOutAction");
  CHECK(zoom_in != nullptr && zoom_out != nullptr);
  if (zoom_in == nullptr || zoom_out == nullptr) {
    return;
  }
  const auto close_to = [](double actual, double expected) { return std::abs(actual - expected) < 0.001; };

  canvas->set_view_zoom(0.4639);
  zoom_in->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 0.5));
  zoom_in->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 2.0 / 3.0));
  zoom_out->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 0.5));
  canvas->set_view_zoom(0.4639);
  zoom_out->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 1.0 / 3.0));

  // Rungs above 100% are the whole-hundred steps, and the ladder ends clamp.
  canvas->set_view_zoom(1.0);
  zoom_in->trigger();
  zoom_in->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 3.0));
  canvas->set_view_zoom(128.0);
  zoom_in->trigger();
  QApplication::processEvents();
  CHECK(close_to(canvas->view_zoom(), 128.0));

  // The Zoom tool click takes the same ladder; Alt inverts it.
  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  // Centered: the 12800% step above panned far off, and a plain zoom keeps that pan,
  // which can leave the clicked document point outside the canvas.
  canvas->set_view_zoom_centered(0.4639);
  const auto at = canvas->widget_position_for_document_point(QPoint(10, 10));
  CHECK(canvas->rect().contains(at));
  send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
  CHECK(close_to(canvas->view_zoom(), 0.5));
  send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  CHECK(close_to(canvas->view_zoom(), 1.0 / 3.0));
}

// The Zoom tool's 100% / Fit Screen / Fill Screen buttons show only for the
// Zoom tool and set the view like the View menu commands; Fill Screen (a new
// View command) uses the larger axis ratio where Fit uses the smaller.
void ui_zoom_options_bar_view_buttons_set_view() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* actual = window.findChild<QPushButton*>(QStringLiteral("zoomActualPixelsButton"));
  auto* fit = window.findChild<QPushButton*>(QStringLiteral("zoomFitScreenButton"));
  auto* fill = window.findChild<QPushButton*>(QStringLiteral("zoomFillScreenButton"));
  CHECK(actual != nullptr);
  CHECK(fit != nullptr);
  CHECK(fill != nullptr);
  if (actual == nullptr || fit == nullptr || fill == nullptr) {
    return;
  }
  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  CHECK(!actual->isVisible());
  CHECK(!fit->isVisible());
  CHECK(!fill->isVisible());
  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(actual->isVisible());
  CHECK(fit->isVisible());
  CHECK(fill->isVisible());
  CHECK(actual->isEnabled());

  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto doc_width = static_cast<double>(document.width());
  const auto doc_height = static_cast<double>(document.height());
  const auto fit_zoom = std::min((canvas->width() - 80.0) / doc_width, (canvas->height() - 80.0) / doc_height);
  const auto fill_zoom = std::max(canvas->width() / doc_width, canvas->height() / doc_height);
  CHECK(fill_zoom > fit_zoom);

  canvas->set_zoom(0.37);
  actual->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  fit->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - fit_zoom) < 0.001);
  fill->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - fill_zoom) < 0.001);

  canvas->set_zoom(0.37);
  require_action_by_text(window, QStringLiteral("Fill Screen"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - fill_zoom) < 0.001);
}

// GitHub issue 303: the Hand tool's options row shows the Zoom row's 100% /
// Fit Screen / Fill Screen buttons (the same widgets) and they set the view.
void ui_hand_options_bar_shows_zoom_view_buttons() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* actual = window.findChild<QPushButton*>(QStringLiteral("zoomActualPixelsButton"));
  auto* fit = window.findChild<QPushButton*>(QStringLiteral("zoomFitScreenButton"));
  auto* fill = window.findChild<QPushButton*>(QStringLiteral("zoomFillScreenButton"));
  CHECK(actual != nullptr);
  CHECK(fit != nullptr);
  CHECK(fill != nullptr);
  if (actual == nullptr || fit == nullptr || fill == nullptr) {
    return;
  }
  require_action(window, "toolHandAction")->trigger();
  QApplication::processEvents();
  CHECK(actual->isVisible());
  CHECK(fit->isVisible());
  CHECK(fill->isVisible());
  CHECK(actual->isEnabled());
  CHECK(fit->isEnabled());
  CHECK(fill->isEnabled());
  // The Zoom-only controls stay on the Zoom row.
  auto* scrubby = window.findChild<QCheckBox*>(QStringLiteral("zoomScrubbyCheck"));
  CHECK(scrubby != nullptr && !scrubby->isVisible());

  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto doc_width = static_cast<double>(document.width());
  const auto doc_height = static_cast<double>(document.height());
  const auto fit_zoom = std::min((canvas->width() - 80.0) / doc_width, (canvas->height() - 80.0) / doc_height);
  const auto fill_zoom = std::max(canvas->width() / doc_width, canvas->height() / doc_height);

  canvas->set_zoom(0.37);
  actual->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  fit->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - fit_zoom) < 0.001);
  fill->click();
  QApplication::processEvents();
  CHECK(std::abs(canvas->zoom() - fill_zoom) < 0.001);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Pan);

  if (auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options")); options_bar != nullptr) {
    save_widget_artifact("issue_303_hand_options_bar", *options_bar);
  }
}

// GitHub issue 304: with the Hand tool, and with Space held over another tool,
// Ctrl+click zooms in and Alt+click zooms out one Zoom-tool step about the
// clicked point, whatever the Zoom tool's own direction; a plain drag pans.
void ui_hand_tool_ctrl_alt_click_zooms_and_drag_pans() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Hand"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Pan);
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  const auto close_to = [](double actual, double expected) { return std::abs(actual - expected) < 0.001; };
  const QPoint document_point(10, 10);
  const auto stays_put = [canvas, document_point](QPoint before) {
    const auto after = canvas->widget_position_for_document_point(document_point);
    return std::abs(after.x() - before.x()) <= 1 && std::abs(after.y() - before.y()) <= 1;
  };
  const auto click = [canvas, document_point](Qt::KeyboardModifiers modifiers) {
    const auto at = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
    return at;
  };
  const auto drag_by = [canvas, document_point](QPoint delta, Qt::KeyboardModifiers modifiers) {
    const auto from = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, from + delta / 2, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, from + delta, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, from + delta, Qt::LeftButton, Qt::NoButton, modifiers);
  };

  canvas->set_view_zoom(1.0);
  auto at = click(Qt::ControlModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0));
  CHECK(stays_put(at));
  at = click(Qt::ControlModifier);
  CHECK(close_to(canvas->view_zoom(), 3.0));
  CHECK(stays_put(at));
  at = click(Qt::AltModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0));
  CHECK(stays_put(at));
  at = click(Qt::AltModifier);
  CHECK(close_to(canvas->view_zoom(), 1.0));
  CHECK(stays_put(at));
  // Alt wins over Ctrl, and the Zoom tool's Zoom Out mode does not flip the Hand.
  click(Qt::ControlModifier | Qt::AltModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0 / 3.0));
  canvas->set_zoom_tool_zooms_out(true);
  canvas->set_view_zoom(1.0);
  click(Qt::ControlModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0));
  canvas->set_zoom_tool_zooms_out(false);

  // A drag with the modifier does not pan; its release is the zoom click.
  canvas->set_view_zoom(1.0);
  const auto origin_before_modifier_drag = canvas->widget_position_for_document_point(QPoint(0, 0));
  drag_by(QPoint(0, 0), Qt::NoModifier);
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == origin_before_modifier_drag);
  drag_by(QPoint(40, 30), Qt::ControlModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0));

  // A plain drag pans and leaves the zoom alone.
  canvas->set_view_zoom(4.0);
  const auto origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  drag_by(QPoint(30, 20), Qt::NoModifier);
  CHECK(close_to(canvas->view_zoom(), 4.0));
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) != origin);

  // The cursor shows the Zoom tool's magnifier while Ctrl or Alt is held.
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_press(*canvas, Qt::Key_Control);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(10, 10));
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_press(*canvas, Qt::Key_Alt);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_key_release(*canvas, Qt::Key_Alt, Qt::AltModifier);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);

  // Space held over another tool is the same Hand: the click zooms and the
  // tool under it (Magic Wand) never sees the press.
  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  QApplication::processEvents();
  canvas->set_view_zoom(1.0);
  send_key_press(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  at = click(Qt::ControlModifier);
  CHECK(close_to(canvas->view_zoom(), 2.0));
  CHECK(stays_put(at));
  at = click(Qt::AltModifier);
  CHECK(close_to(canvas->view_zoom(), 1.0));
  CHECK(stays_put(at));
  canvas->set_view_zoom(4.0);
  const auto space_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  drag_by(QPoint(30, 20), Qt::NoModifier);
  CHECK(close_to(canvas->view_zoom(), 4.0));
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) != space_origin);
  send_key_release(*canvas, Qt::Key_Space);
  CHECK(!canvas->has_selection());
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagicWand);
}

void ui_tool_palette_icons_render_sheet() {
  patchy::ui::MainWindow window;
  show_window(window);

  struct ToolIconEntry {
    const char* action_name;
    const char* label;
  };
  std::vector<ToolIconEntry> tools = {
      {"toolMoveAction", "Move"},
      {"toolMarqueeAction", "Marquee"},
      {"toolEllipticalMarqueeAction", "Elliptical Marquee"},
      {"toolLassoAction", "Lasso"},
      {"toolPolygonalLassoAction", "Polygonal Lasso"},
      {"toolMagneticLassoAction", "Magnetic Lasso"},
      {"toolMagicWandAction", "Magic Wand"},
      {"toolQuickSelectAction", "Quick Select"},
      {"toolObjectSelectionAction", "Object Selection"},
      {"toolCropAction", "Crop"},
      {"toolBrushAction", "Brush"},
      {"toolCloneAction", "Clone"},
      {"toolPatternStampAction", "Pattern Stamp"},
      {"toolHealingBrushAction", "Healing Brush"},
      {"toolSpotHealingAction", "Spot Healing"},
      {"toolPatchAction", "Patch"},
      {"toolSmudgeAction", "Smudge"},
      {"toolMixerBrushAction", "Mixer Brush"},
      {"toolDodgeAction", "Dodge"},
      {"toolBurnAction", "Burn"},
      {"toolSpongeAction", "Sponge"},
      {"toolBlurAction", "Blur"},
      {"toolSharpenAction", "Sharpen"},
      {"toolEraserAction", "Eraser"},
      {"toolGradientAction", "Gradient"},
      {"toolFillAction", "Fill"},
      {"toolLineAction", "Line"},
      {"toolRectAction", "Rect"},
      {"toolEllipseAction", "Ellipse"},
      {"toolPenAction", "Pen"},
      {"toolAddAnchorAction", "Add Anchor"},
      {"toolDeleteAnchorAction", "Delete Anchor"},
      {"toolConvertPointAction", "Convert Point"},
      {"toolPathSelectAction", "Path Select"},
      {"toolDirectSelectAction", "Direct Select"},
      {"toolPolygonAction", "Polygon"},
      {"toolCustomShapeAction", "Custom Shape"},
      {"toolPickAction", "Pick"},
      {"toolTypeAction", "Type"},
      {"toolHandAction", "Hand"},
      {"toolZoomAction", "Zoom"},
  };
  // Object Selection exists only in builds with the automatic-selection models.
  patchy::ui::configure_automatic_selection_models();
  if (!patchy::ai::segmentation_available()) {
    std::erase_if(tools, [](const ToolIconEntry& entry) {
      return std::string_view(entry.action_name) == "toolObjectSelectionAction";
    });
  }

  // The real button backgrounds from the app stylesheet: palette, hover, checked.
  const std::array<QColor, 3> state_backgrounds = {QColor(0x53, 0x53, 0x53), QColor(0x4a, 0x4a, 0x4a),
                                                   QColor(0x2f, 0x75, 0xbd)};

  constexpr int kLabelWidth = 118;
  constexpr int kSmallCell = 34;
  constexpr int kLargeCell = 56;
  constexpr int kRowHeight = 56;
  const int sheet_width = kLabelWidth + kSmallCell * 4 + kLargeCell + 12;
  const int sheet_height = kRowHeight * static_cast<int>(tools.size()) + 8;
  QImage sheet(sheet_width, sheet_height, QImage::Format_RGB32);
  sheet.fill(QColor(0x2b, 0x2b, 0x2b));
  QPainter painter(&sheet);
  painter.setFont(visual_test_font());

  std::vector<QImage> normal_renders;
  QImage gradient_render;
  QStringList coverage_problems;
  int y = 4;
  for (const auto& tool : tools) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(tool.action_name));
    CHECK(action != nullptr);
    const auto icon = action->icon();
    CHECK(!icon.isNull());

    // Tool icons come from SVG resources; a missing file or typo'd qrc alias renders EMPTY
    // silently, so assert real pixel coverage of the 20px render the palette uses. The
    // sparsest legitimate icon is the single-stroke Line tool at ~30 covered pixels.
    const auto normal20 = icon.pixmap(QSize(20, 20)).toImage().convertToFormat(QImage::Format_ARGB32);
    CHECK(normal20.width() == 20 && normal20.height() == 20);
    int covered = 0;
    int bright = 0;
    for (int py = 0; py < normal20.height(); ++py) {
      for (int px = 0; px < normal20.width(); ++px) {
        const auto pixel = normal20.pixel(px, py);
        if (qAlpha(pixel) > 60) {
          ++covered;
          if (qGray(pixel) > 140) {
            ++bright;
          }
        }
      }
    }
    if (covered <= 25 || bright <= 15) {
      coverage_problems << QStringLiteral("%1: covered=%2 bright=%3")
                               .arg(QString::fromLatin1(tool.label))
                               .arg(covered)
                               .arg(bright);
    }
    normal_renders.push_back(normal20);
    if (QString::fromLatin1(tool.label) == QStringLiteral("Gradient")) {
      gradient_render = normal20;
    }

    painter.setPen(QColor(0xdc, 0xe2, 0xeb));
    painter.drawText(QRect(6, y, kLabelWidth - 10, kRowHeight - 8), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromLatin1(tool.label));
    int x = kLabelWidth;
    const auto draw_cell = [&painter](int cell_x, int cell_y, int cell_size, const QColor& background,
                                      const QPixmap& pixmap) {
      const QRect cell(cell_x, cell_y, cell_size, cell_size);
      painter.fillRect(cell, background);
      painter.drawPixmap(cell.x() + (cell.width() - pixmap.width()) / 2,
                         cell.y() + (cell.height() - pixmap.height()) / 2, pixmap);
    };
    const auto small20 = icon.pixmap(QSize(20, 20));
    for (const auto& background : state_backgrounds) {
      draw_cell(x + 3, y + (kRowHeight - kSmallCell) / 2 + 3, kSmallCell - 6, background, small20);
      x += kSmallCell;
    }
    draw_cell(x + 3, y + (kRowHeight - kSmallCell) / 2 + 3, kSmallCell - 6, state_backgrounds[0],
              icon.pixmap(QSize(20, 20), QIcon::Disabled));
    x += kSmallCell;
    draw_cell(x + 5, y + (kRowHeight - kLargeCell) / 2 + 5, kLargeCell - 10, state_backgrounds[0],
              icon.pixmap(QSize(40, 40)));
    y += kRowHeight;
  }
  painter.end();

  for (const auto& problem : coverage_problems) {
    std::fprintf(stderr, "tool icon coverage problem: %s\n", qPrintable(problem));
  }
  CHECK(coverage_problems.isEmpty());

  // Every tool must render distinctly (catches copy-paste mistakes in the qrc aliases).
  for (std::size_t i = 0; i < normal_renders.size(); ++i) {
    for (std::size_t j = i + 1; j < normal_renders.size(); ++j) {
      CHECK(normal_renders[i] != normal_renders[j]);
    }
  }

  // The Gradient icon is the one SVG relying on linearGradient support: its swatch must
  // interpolate from the light ink at the left edge to the dark icon surface at the
  // right (GitHub issue 129 asked for a light-to-dark ramp; the old ink-to-accent ramp
  // was two dark tones in the Light scheme).
  CHECK(!gradient_render.isNull());
  const auto gradient_left = gradient_render.pixel(5, 10);
  const auto gradient_right = gradient_render.pixel(15, 10);
  CHECK(qAlpha(gradient_left) > 200);
  CHECK(qAlpha(gradient_right) > 200);
  CHECK(qGray(gradient_left) - qGray(gradient_right) > 60);
  CHECK(std::abs(qBlue(gradient_right) - qRed(gradient_right)) < 40);

  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_tool_palette_icons_sheet.png")));

  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  save_widget_artifact("ui_tool_palette", *tool_palette);
}

void ui_filled_shape_preview_clears_after_commit() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  // This test exercises the legacy raster commit; Shape mode (the default)
  // would route the drag to a new shape layer instead.
  canvas->set_vector_tool_mode(patchy::ui::VectorToolMode::Pixels);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(96);
  canvas->set_fill_shapes(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 170)),
       canvas->widget_position_for_document_point(QPoint(370, 310)));
  QApplication::processEvents();

  const auto outside_final_shape = canvas_pixel(*canvas, QPoint(170, 125));
  CHECK(color_close(outside_final_shape, Qt::white, 10));
  save_widget_artifact("ui_filled_shape_preview_cleanup", *canvas);

  const auto immediate = canvas->grab().toImage();
  canvas->document_changed();
  QApplication::processEvents();
  const auto repainted = canvas->grab().toImage();
  CHECK(immediate.size() == repainted.size());
  CHECK(immediate.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))) ==
        repainted.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))));
}

// Fill tool options: Tol and Contiguous live beside Opacity/Soft, persist, and follow the
// current_* mirror onto new documents (GitHub issue 30).
// Issue 253 (Seth: "fix that too"): the Fill tool's Opacity runs 0..100 like the Brush bar's
// (Photoshop's Paint Bucket stops at 1%). The popup slider and the inline slider reach 0, and
// at 0% neither the Fill tool's flood nor the Fill command touches the layer or the history;
// 100% still fills.
void ui_fill_opacity_accepts_zero_and_fills_nothing() {
  {
    patchy::ui::MainWindow window;
    show_window(window);
    require_action_by_text(window, QStringLiteral("Fill"))->trigger();
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    CHECK(canvas->tool() == patchy::ui::CanvasTool::Fill);
    auto* spin = window.findChild<QSpinBox*>(QStringLiteral("fillOpacitySpin"));
    auto* inline_slider = window.findChild<QSlider*>(QStringLiteral("fillOpacitySlider"));
    auto* softness = window.findChild<QSpinBox*>(QStringLiteral("fillSoftnessSpin"));
    CHECK(spin != nullptr && inline_slider != nullptr && softness != nullptr);
    spin->setValue(50);
    CHECK(canvas->fill_opacity() == 50);

    auto* action = window.findChild<QAction*>(QStringLiteral("fillOpacityPopupAction"));
    CHECK(action != nullptr);
    action->trigger();
    QApplication::processEvents();
    auto* popup = window.findChild<QFrame*>(QStringLiteral("fillOpacityPopup"));
    auto* slider = window.findChild<QSlider*>(QStringLiteral("fillOpacityPopupSlider"));
    CHECK(popup != nullptr && popup->isVisible());
    CHECK(slider != nullptr);
    patchy::ui::set_slider_to_value(*slider, slider->minimum());
    QApplication::processEvents();
    save_window_popup_artifact("issue_253_fill_opacity_popup_minimum", window, *spin, *popup);
    CHECK(slider->minimum() == 0);
    CHECK(spin->value() == 0);
    CHECK(inline_slider->minimum() == 0);
    CHECK(inline_slider->value() == 0);
    CHECK(canvas->fill_opacity() == 0);
    popup->close();
    QApplication::processEvents();
    CHECK(spin->minimum() == softness->minimum());
    CHECK(spin->maximum() == softness->maximum());
    CHECK(spin->singleStep() == softness->singleStep());

    // The Fill command (Alt+Backspace) at 0% leaves the pixels and the history alone.
    require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
    QApplication::processEvents();
    canvas->set_primary_color(QColor(20, 140, 230));
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
         canvas->widget_position_for_document_point(QPoint(180, 150)));
    QApplication::processEvents();
    CHECK(canvas->has_selection());
    const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
    require_action(window, "layerFillForegroundAction")->trigger();
    QApplication::processEvents();
    CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 100)), Qt::white, 2));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
    spin->setValue(100);
    require_action(window, "layerFillForegroundAction")->trigger();
    QApplication::processEvents();
    CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 100)), QColor(20, 140, 230), 12));
  }

  // The Fill tool's flood at 0% leaves the layer byte for byte untouched; 100% fills.
  patchy::Document document(48, 48, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer(
      "Background", solid_pixels(48, 48, patchy::PixelFormat::rgba8(), Qt::white)).id();
  document.set_active_layer(layer_id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 192);
  canvas.set_document(&document);
  canvas.set_zoom(4.0);
  canvas.set_tool(patchy::ui::CanvasTool::Fill);
  canvas.set_primary_color(QColor(0, 180, 210));
  canvas.set_fill_tolerance(0);
  canvas.set_fill_softness(0);
  canvas.show();
  QApplication::processEvents();
  const auto click = [&](int x, int y) {
    const auto position = canvas.widget_position_for_document_point(QPoint(x, y));
    send_mouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  canvas.set_fill_opacity(0);
  CHECK(canvas.fill_opacity() == 0);
  const auto before_bytes = std::as_const(document).find_layer(layer_id)->pixels().data();
  const std::vector<std::uint8_t> before(before_bytes.begin(), before_bytes.end());
  click(10, 10);
  const auto after = std::as_const(document).find_layer(layer_id)->pixels().data();
  CHECK(after.size() == before.size() && std::equal(after.begin(), after.end(), before.begin()));
  canvas.set_fill_opacity(100);
  click(10, 10);
  const auto* filled = std::as_const(document).find_layer(layer_id)->pixels().pixel(10, 10);
  CHECK(filled[0] == 0 && filled[1] == 180 && filled[2] == 210);
}

void ui_fill_tool_tolerance_and_contiguous_persist_across_documents() {
  SettingsValueRestorer saved_tolerance(QStringLiteral("tools/fillTolerance"));
  SettingsValueRestorer saved_contiguous(QStringLiteral("tools/fillContiguous"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/fillTolerance"));
    settings.remove(QStringLiteral("tools/fillContiguous"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("fillToleranceSpin"));
  auto* contiguous = window.findChild<QCheckBox*>(QStringLiteral("fillContiguousCheck"));
  CHECK(tolerance != nullptr);
  CHECK(contiguous != nullptr);
  if (tolerance == nullptr || contiguous == nullptr) {
    return;
  }
  CHECK(tolerance->minimum() == 0);
  CHECK(tolerance->maximum() == 255);
  CHECK(canvas->fill_tolerance() == 32);
  CHECK(canvas->fill_contiguous());
  CHECK(tolerance->value() == 32);
  CHECK(contiguous->isChecked());

  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  CHECK(!tolerance->isVisible());
  CHECK(!contiguous->isVisible());
  require_action(window, "toolFillAction")->trigger();
  QApplication::processEvents();
  CHECK(tolerance->isVisible());
  CHECK(contiguous->isVisible());

  tolerance->setValue(48);
  contiguous->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->fill_tolerance() == 48);
  CHECK(!canvas->fill_contiguous());
  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/fillTolerance")).toInt() == 48);
    CHECK(!settings.value(QStringLiteral("tools/fillContiguous"), true).toBool());
  }

  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != canvas);
  CHECK(second->fill_tolerance() == 48);
  CHECK(!second->fill_contiguous());
  CHECK(tolerance->value() == 48);
  CHECK(!contiguous->isChecked());
}

// A Fill tool click honors the tool's own Tol, Contiguous, and Opacity (not the brush's).
void ui_fill_tool_click_honors_tolerance_contiguous_and_opacity() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(64, 64, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = pixels.pixel(x, y);
      const bool bar = y >= 30 && y <= 33;
      const std::uint8_t gray = ((x + y) % 2 == 0) ? 255 : 250;
      px[0] = px[1] = px[2] = bar ? 0 : gray;
      px[3] = 255;
    }
  }
  const auto layer_id = document.add_pixel_layer("Background", std::move(pixels)).id();
  document.set_active_layer(layer_id);
  const auto pixel_at = [&](int x, int y) {
    const auto* px = std::as_const(document).find_layer(layer_id)->pixels().pixel(x, y);
    return QColor(px[0], px[1], px[2], px[3]);
  };

  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 320);
  canvas.set_document(&document);
  canvas.set_zoom(4.0);
  canvas.set_tool(patchy::ui::CanvasTool::Fill);
  canvas.set_primary_color(QColor(0, 180, 210));
  canvas.set_brush_opacity(10);  // must not leak into the fill
  canvas.show();
  QApplication::processEvents();
  const auto click = [&](int x, int y) {
    const auto position = canvas.widget_position_for_document_point(QPoint(x, y));
    send_mouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas.set_fill_tolerance(0);
  click(2, 2);
  CHECK(pixel_at(2, 2) == QColor(0, 180, 210));
  CHECK(pixel_at(3, 2) == QColor(250, 250, 250));

  canvas.set_fill_tolerance(32);
  click(4, 4);
  CHECK(pixel_at(3, 2) == QColor(0, 180, 210));
  CHECK(pixel_at(63, 29) == QColor(0, 180, 210));
  CHECK(pixel_at(10, 31) == QColor(0, 0, 0));
  CHECK(pixel_at(10, 40) == QColor(255, 255, 255));

  // Contiguous off at tolerance 0: the lower island's 255-gray pixels are a checkerboard,
  // so none of them touch, yet one click fills them all; the 250 grays stay.
  canvas.set_fill_contiguous(false);
  canvas.set_fill_tolerance(0);
  click(10, 40);
  CHECK(pixel_at(10, 40) == QColor(0, 180, 210));
  CHECK(pixel_at(12, 40) == QColor(0, 180, 210));
  CHECK(pixel_at(60, 60) == QColor(0, 180, 210));
  CHECK(pixel_at(11, 40) == QColor(250, 250, 250));
  CHECK(pixel_at(10, 31) == QColor(0, 0, 0));

  canvas.set_fill_contiguous(true);
  canvas.set_fill_tolerance(0);
  canvas.set_fill_opacity(50);
  click(10, 31);
  CHECK(color_close(pixel_at(10, 31), QColor(0, 90, 105), 3));
  CHECK(color_close(pixel_at(50, 33), QColor(0, 90, 105), 3));
  CHECK(pixel_at(10, 40) == QColor(0, 180, 210));
}

// GitHub issue 34: wand-selecting the slightly uneven white background of a 1110 x 1388 image
// and filling it took about 45 s with both the Fill tool and Layer > Fill. The selection is a
// QRegion with thousands of row spans (the background weaves between the subject), and every
// per-pixel selection query went through QRegion::contains, which scans all of them. The canvas
// now rasterizes such a selection once; both fills must finish in a small fraction of a second.
void ui_fill_of_wand_selection_with_many_spans_is_fast() {
  constexpr int kWidth = 1110;
  constexpr int kHeight = 1388;
  constexpr int kDiscSpacing = 80;
  constexpr int kDiscRadius = 24;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(kWidth, kHeight, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      // A near-white background (253..255, within the wand's tolerance) with a grid of dark
      // discs on it: every row through a disc band crosses a dozen discs, so the wand's
      // background region holds thousands of spans.
      const auto dx = x % kDiscSpacing - kDiscSpacing / 2;
      const auto dy = y % kDiscSpacing - kDiscSpacing / 2;
      const bool disc = dx * dx + dy * dy <= kDiscRadius * kDiscRadius;
      const auto gray = static_cast<std::uint8_t>(disc ? 30 : 253 + (x * 7 + y * 13) % 3);
      auto* px = pixels.pixel(x, y);
      px[0] = px[1] = px[2] = gray;
      px[3] = 255;
    }
  }
  const auto layer_id = document.add_pixel_layer("Background", std::move(pixels)).id();
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Issue 34"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto& edited = patchy::ui::MainWindowTestAccess::document(window);
  const auto pixel_at = [&](int x, int y) {
    const auto* px = std::as_const(edited).find_layer(layer_id)->pixels().pixel(x, y);
    return QColor(px[0], px[1], px[2], px[3]);
  };
  const auto click = [&](int x, int y) {
    const auto position = canvas->widget_position_for_document_point(QPoint(x, y));
    send_mouse(*canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(32);
  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(false);
  canvas->set_selection_feather_radius(0);
  click(2, 2);
  const auto& selection = canvas->selected_document_region();
  CHECK(selection.contains(QPoint(2, 2)));
  CHECK(!selection.contains(QPoint(kDiscSpacing / 2, kDiscSpacing / 2)));
  CHECK(selection.rectCount() > 5000);
  CHECK(canvas->selection_alpha_at(QPoint(2, 2)) == 255U);
  CHECK(canvas->selection_alpha_at(QPoint(kDiscSpacing / 2, kDiscSpacing / 2)) == 0U);
  CHECK(canvas->selection_alpha_at(QPoint(kWidth - 3, kHeight - 3)) == 255U);
  CHECK(canvas->selection_alpha_at(QPoint(kWidth, kHeight - 3)) == 0U);
  CHECK(canvas->selection_alpha_at(QPoint(-1, 5)) == 0U);

  // Layer > Fill (the background variant needs no dialog).
  canvas->set_secondary_color(QColor(0, 180, 210));
  auto* fill_background = window.findChild<QAction*>(QStringLiteral("layerFillBackgroundAction"));
  CHECK(fill_background != nullptr);
  QElapsedTimer fill_command_timer;
  fill_command_timer.start();
  fill_background->trigger();
  QApplication::processEvents();
  const auto fill_command_ms = fill_command_timer.elapsed();
  CHECK(pixel_at(2, 2) == QColor(0, 180, 210));
  CHECK(pixel_at(kWidth - 3, kHeight - 3) == QColor(0, 180, 210));
  CHECK(pixel_at(kDiscSpacing / 2, kDiscSpacing / 2) == QColor(30, 30, 30));
  CHECK(fill_command_ms < 3000);

  // The Fill tool click inside the same selection floods the fresh fill color.
  canvas->set_tool(patchy::ui::CanvasTool::Fill);
  canvas->set_primary_color(QColor(200, 40, 60));
  canvas->set_fill_tolerance(32);
  canvas->set_fill_contiguous(true);
  canvas->set_fill_opacity(100);
  canvas->set_fill_softness(0);
  QElapsedTimer fill_tool_timer;
  fill_tool_timer.start();
  click(2, 2);
  const auto fill_tool_ms = fill_tool_timer.elapsed();
  CHECK(pixel_at(2, 2) == QColor(200, 40, 60));
  CHECK(pixel_at(kWidth - 3, kHeight - 3) == QColor(200, 40, 60));
  CHECK(pixel_at(kDiscSpacing / 2, kDiscSpacing / 2) == QColor(30, 30, 30));
  CHECK(fill_tool_ms < 3000);
  std::cout << "  fill command " << fill_command_ms << " ms, fill tool " << fill_tool_ms << " ms over "
            << selection.rectCount() << " selection spans\n";
}

// GitHub issue 66: clicking into an options-bar numeric field selects its whole
// value, so typing replaces it (Qt only selects on keyboard focus).
void ui_toolbar_spin_boxes_select_all_on_focus() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.activateWindow();
  QApplication::processEvents();

  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  auto* ratio_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  CHECK(ratio_width != nullptr);
  if (ratio_width != nullptr) {
    CHECK(ratio_width->isVisible());
    auto* editor = ratio_width->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (editor != nullptr) {
      editor->deselect();
      ratio_width->setFocus(Qt::MouseFocusReason);
      QApplication::processEvents();
      CHECK(editor->hasFocus());
      CHECK(editor->hasSelectedText());
      CHECK(editor->selectedText() == editor->text());
    }
  }

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(brush_size != nullptr);
  if (brush_size != nullptr) {
    auto* editor = brush_size->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (editor != nullptr) {
      editor->deselect();
      brush_size->setFocus(Qt::MouseFocusReason);
      QApplication::processEvents();
      CHECK(editor->hasFocus());
      CHECK(editor->selectedText() == editor->text());
    }
  }
}

namespace {

// The keys GitHub issue 254 added (docs/tool-options.md): cleared before each window so a
// test starts from the defaults, restored afterwards.
constexpr const char* kIssue254SettingsKeys[] = {
    "tools/activeTool",    "tools/brushSize",     "tools/brushOpacity",       "tools/brushFlow",
    "tools/brushSoftness", "tools/brushAirbrush", "tools/brushTip",           "tools/brushPreset",
    "tools/brushMode",     "tools/brushSmoothing", "tools/eraserSize",        "tools/eraserOpacity",
    "tools/eraserSoftness", "tools/marqueeCornerRadius", "tools/shapeFill"};

std::list<SettingsValueRestorer> clear_issue_254_settings() {
  std::list<SettingsValueRestorer> restorers;
  auto settings = patchy::ui::app_settings();
  for (const char* key : kIssue254SettingsKeys) {
    restorers.emplace_back(QString::fromLatin1(key));
    settings.remove(QLatin1String(key));
  }
  settings.sync();
  return restorers;
}

// The palette button that shows `action` as its flyout face, or nullptr when no flyout
// holds it or another member is shown.
QToolButton* flyout_button_showing(patchy::ui::MainWindow& window, QAction* action) {
  for (auto* button : window.findChildren<QToolButton*>()) {
    auto* menu = button->menu();
    if (button->property("toolFlyout").toBool() && menu != nullptr && menu->actions().contains(action)) {
      return button->defaultAction() == action ? button : nullptr;
    }
  }
  return nullptr;
}

}  // namespace

// GitHub issue 254: the next launch starts on the tool that was active at quit, flyout
// member included, and the Brush, Eraser and Marquee bars show the values they had.
void ui_active_tool_and_options_restore_on_next_launch() {
  const auto restorers = clear_issue_254_settings();
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(canvas->tool() == patchy::ui::CanvasTool::Brush);
    CHECK(canvas->brush_size() == 25);
    ensure_artifact_dir();
    window.grab().save(QStringLiteral("test-artifacts/issue_254_before.png"));
    auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
    auto* brush_flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
    auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
    auto* brush_airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
    auto* brush_smoothing = window.findChild<QSpinBox*>(QStringLiteral("brushSmoothingSpin"));
    auto* brush_mode = window.findChild<QComboBox*>(QStringLiteral("brushModeCombo"));
    auto* corner_radius = window.findChild<QSpinBox*>(QStringLiteral("selectionCornerRadiusSpin"));
    CHECK(brush_size != nullptr && brush_opacity != nullptr && brush_flow != nullptr && brush_softness != nullptr &&
          brush_airbrush != nullptr && brush_smoothing != nullptr && brush_mode != nullptr && corner_radius != nullptr);
    if (brush_size == nullptr || brush_opacity == nullptr || brush_flow == nullptr || brush_softness == nullptr ||
        brush_airbrush == nullptr || brush_smoothing == nullptr || brush_mode == nullptr || corner_radius == nullptr) {
      return;
    }
    brush_size->setValue(80);
    brush_opacity->setValue(60);
    brush_flow->setValue(35);
    brush_softness->setValue(45);
    brush_airbrush->setChecked(true);
    brush_smoothing->setValue(20);
    const auto multiply = brush_mode->findText(QStringLiteral("Multiply"));
    CHECK(multiply >= 0);
    brush_mode->setCurrentIndex(multiply);
    QApplication::processEvents();
    CHECK(canvas->brush_size() == 80);
    CHECK(patchy::ui::script_paint_mode_id(canvas->brush_paint_mode()) == QStringLiteral("multiply"));

    require_action(window, "toolEraserAction")->trigger();
    QApplication::processEvents();
    brush_size->setValue(33);
    brush_opacity->setValue(70);
    brush_softness->setValue(15);
    QApplication::processEvents();
    CHECK(canvas->brush_size() == 33);

    require_action(window, "toolMarqueeAction")->trigger();
    QApplication::processEvents();
    corner_radius->setValue(12);
    require_action(window, "toolEllipticalMarqueeAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);
    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/activeTool")).toString() == QStringLiteral("tools.elliptical_marquee"));
    CHECK(settings.value(QStringLiteral("tools/brushSize")).toInt() == 80);
    CHECK(settings.value(QStringLiteral("tools/brushOpacity")).toInt() == 60);
    CHECK(settings.value(QStringLiteral("tools/eraserSize")).toInt() == 33);
    CHECK(settings.value(QStringLiteral("tools/marqueeCornerRadius")).toInt() == 12);
  }
  {
    // "The next launch": a fresh window from the same settings store.
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);
    auto* elliptical = require_action(window, "toolEllipticalMarqueeAction");
    CHECK(elliptical->isChecked());
    CHECK(!require_action(window, "toolBrushAction")->isChecked());
    CHECK(flyout_button_showing(window, elliptical) != nullptr);
    CHECK(canvas->marquee_corner_radius() == 12);
    auto* corner_radius = window.findChild<QSpinBox*>(QStringLiteral("selectionCornerRadiusSpin"));
    CHECK(corner_radius != nullptr && corner_radius->value() == 12);

    require_action(window, "toolBrushAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->brush_size() == 80);
    CHECK(canvas->brush_opacity() == 60);
    CHECK(canvas->brush_flow() == 35);
    CHECK(canvas->brush_softness() == 45);
    CHECK(canvas->brush_build_up());
    CHECK(canvas->brush_smoothing() == 20);
    CHECK(patchy::ui::script_paint_mode_id(canvas->brush_paint_mode()) == QStringLiteral("multiply"));
    auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
    auto* brush_airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
    auto* brush_mode = window.findChild<QComboBox*>(QStringLiteral("brushModeCombo"));
    CHECK(brush_size != nullptr && brush_size->value() == 80);
    CHECK(brush_opacity != nullptr && brush_opacity->value() == 60);
    CHECK(brush_airbrush != nullptr && brush_airbrush->isChecked());
    CHECK(brush_mode != nullptr && brush_mode->currentText() == QStringLiteral("Multiply"));

    require_action(window, "toolEraserAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->brush_size() == 33);
    CHECK(canvas->brush_opacity() == 70);
    CHECK(canvas->brush_softness() == 15);
    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  }
  {
    // Quit on the Eraser: the next launch shows it selected with its own values in the bar.
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(canvas->tool() == patchy::ui::CanvasTool::Eraser);
    CHECK(require_action(window, "toolEraserAction")->isChecked());
    CHECK(canvas->brush_size() == 33);
    auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    CHECK(brush_size != nullptr && brush_size->isVisible() && brush_size->value() == 33);
    window.grab().save(QStringLiteral("test-artifacts/issue_254_after.png"));
  }
}

// A missing or hand-edited key never leaves the bar in a strange state: unknown tool
// tokens, non-numeric values and out-of-range numbers read as the defaults (or clamp).
void ui_tool_settings_fall_back_on_missing_or_malformed_keys() {
  const auto restorers = clear_issue_254_settings();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/activeTool"), QStringLiteral("tools.nonsense"));
    settings.setValue(QStringLiteral("tools/brushSize"), QStringLiteral("banana"));
    settings.setValue(QStringLiteral("tools/brushOpacity"), 999);
    settings.setValue(QStringLiteral("tools/brushFlow"), QStringLiteral("x"));
    settings.setValue(QStringLiteral("tools/brushAirbrush"), QStringLiteral("maybe"));
    settings.setValue(QStringLiteral("tools/brushTip"), QStringLiteral("no-such-tip"));
    settings.setValue(QStringLiteral("tools/brushPreset"), QStringLiteral("no-such-preset"));
    settings.setValue(QStringLiteral("tools/eraserSize"), QStringLiteral("huge"));
    settings.setValue(QStringLiteral("tools/marqueeCornerRadius"), QStringLiteral("big"));
    settings.setValue(QStringLiteral("tools/shapeFill"), QStringLiteral("sometimes"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Brush);
  CHECK(require_action(window, "toolBrushAction")->isChecked());
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 100);
  CHECK(!canvas->brush_build_up());
  CHECK(canvas->marquee_corner_radius() == 0);
  CHECK(!canvas->fill_shapes());
  auto* tip_picker = window.findChild<patchy::ui::BrushTipPicker*>();
  CHECK(tip_picker != nullptr && tip_picker->current_tip_id() == patchy::ui::builtin_round_brush_tip_id());
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(preset_combo != nullptr &&
        preset_combo->currentData().toString() == patchy::ui::default_startup_brush_preset_id());
  require_action(window, "toolEraserAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->brush_size() == 25);
}

void ui_options_bar_tracks_active_tool() {
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  SettingsValueRestorer saved_text_smoothing(QStringLiteral("tools/textSmoothing"));
  SettingsValueRestorer saved_show_transform_controls(QStringLiteral("tools/showTransformControls"));
  SettingsValueRestorer saved_move_auto_select(QStringLiteral("tools/moveAutoSelect"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/showTransformControls"));
  settings.sync();
  patchy::ui::MainWindow window;
  show_window(window);
  // Wide enough that no row wraps, so the inline sliders that a narrow bar drops
  // (ui_options_bar_drops_duplicate_sliders_before_wrapping) follow the tool alone.
  window.resize(2600, 900);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* move_auto_select = window.findChild<QCheckBox*>(QStringLiteral("moveAutoSelectCheck"));
  auto* move_show_transform_controls = window.findChild<QCheckBox*>(QStringLiteral("moveShowTransformControlsCheck"));
  auto* text_font = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_style = window.findChild<QComboBox*>(QStringLiteral("textStyleCombo"));
  auto* text_smoothing = window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_opacity_slider = window.findChild<QSlider*>(QStringLiteral("gradientOpacitySlider"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  auto* gradient_preview = window.findChild<QPushButton*>(QStringLiteral("gradientPreviewButton"));
  auto* gradient_presets = window.findChild<QPushButton*>(QStringLiteral("gradientPresetsButton"));
  auto* gradient_edit_stops = window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"));
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* healing_diffusion = window.findChild<QSpinBox*>(QStringLiteral("healingDiffusionSpin"));
  auto* local_strength = window.findChild<QSpinBox*>(QStringLiteral("localAdjustmentStrengthSpin"));
  auto* local_range = window.findChild<QComboBox*>(QStringLiteral("localToneRangeCombo"));
  auto* protect_tones = window.findChild<QCheckBox*>(QStringLiteral("localProtectTonesCheck"));
  auto* sponge_mode = window.findChild<QComboBox*>(QStringLiteral("spongeModeCombo"));
  auto* sponge_vibrance = window.findChild<QCheckBox*>(QStringLiteral("spongeVibranceCheck"));
  auto* wand_tolerance = window.findChild<QSpinBox*>(QStringLiteral("wandToleranceSpin"));
  auto* wand_contiguous = window.findChild<QCheckBox*>(QStringLiteral("wandContiguousCheck"));
  auto* wand_sample_all_layers = window.findChild<QCheckBox*>(QStringLiteral("wandSampleAllLayersCheck"));
  auto* zoom_scrubby = window.findChild<QCheckBox*>(QStringLiteral("zoomScrubbyCheck"));
  auto* feather_group = window.findChild<QWidget*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(move_auto_select != nullptr);
  CHECK(move_show_transform_controls != nullptr);
  CHECK(text_font != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(text_size->minimum() <= 0.01);
  CHECK(text_style != nullptr);
  CHECK(text_smoothing != nullptr);
  CHECK(text_color != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider")) == nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider")) == nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_opacity_slider != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(gradient_preview != nullptr);
  CHECK(gradient_presets != nullptr);
  CHECK(gradient_edit_stops != nullptr);
  CHECK(clone_aligned != nullptr);
  CHECK(healing_diffusion != nullptr);
  CHECK(local_strength != nullptr);
  CHECK(local_range != nullptr);
  CHECK(protect_tones != nullptr);
  CHECK(sponge_mode != nullptr);
  CHECK(sponge_vibrance != nullptr);
  CHECK(wand_tolerance != nullptr);
  CHECK(wand_contiguous != nullptr);
  CHECK(wand_sample_all_layers != nullptr);
  CHECK(zoom_scrubby != nullptr);
  CHECK(feather_group != nullptr);
  CHECK(anti_alias != nullptr);
  CHECK(anti_alias->isChecked());
  CHECK(wand_contiguous->isChecked());
  CHECK(!wand_sample_all_layers->isChecked());

  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!healing_diffusion->isVisible());
  CHECK(!local_strength->isVisible());
  CHECK(!local_range->isVisible());
  CHECK(!protect_tones->isVisible());
  CHECK(!sponge_mode->isVisible());
  CHECK(!sponge_vibrance->isVisible());
  CHECK(!gradient_method->isVisible());
  CHECK(!gradient_opacity->isVisible());
  CHECK(!gradient_reverse->isVisible());
  CHECK(!gradient_presets->isVisible());
  CHECK(!gradient_edit_stops->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!move_show_transform_controls->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());
  CHECK(!zoom_scrubby->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(move_auto_select->isVisible());
  CHECK(move_show_transform_controls->isVisible());
  CHECK(move_show_transform_controls->isChecked());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_size_slider->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!text_font->isVisible());
  move_auto_select->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->auto_select_layer());
  move_auto_select->setChecked(true);
  QApplication::processEvents();
  CHECK(move_auto_select->isChecked());
  CHECK(canvas->auto_select_layer());
  move_show_transform_controls->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->show_transform_controls());
  move_show_transform_controls->setChecked(true);
  QApplication::processEvents();
  CHECK(move_show_transform_controls->isChecked());
  CHECK(canvas->show_transform_controls());
  save_widget_artifact("ui_tool_options_move", window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(text_font->isVisible());
  CHECK(text_size->isVisible());
  CHECK(text_style->isVisible());
  CHECK(text_color->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  text_size->setValue(0.01);
  CHECK(std::abs(text_size->value() - 0.01) < 0.001);
  text_size->setValue(text_points_for_pixels(36));
  save_widget_artifact("ui_tool_options_text", window);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(wand_tolerance->isVisible());
  CHECK(wand_contiguous->isVisible());
  CHECK(wand_sample_all_layers->isVisible());
  CHECK(!zoom_scrubby->isVisible());
  CHECK(feather_group->isVisible());
  CHECK(anti_alias->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());
  wand_contiguous->setChecked(false);
  wand_sample_all_layers->setChecked(true);
  QApplication::processEvents();
  CHECK(!canvas->wand_contiguous());
  CHECK(canvas->wand_sample_all_layers());
  wand_contiguous->setChecked(true);
  wand_sample_all_layers->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->wand_contiguous());
  CHECK(!canvas->wand_sample_all_layers());

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(clone_aligned->isVisible());
  CHECK(clone_aligned->isChecked());
  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  clone_aligned->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->clone_aligned());
  CHECK(!wand_tolerance->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());

  require_action_by_text(window, QStringLiteral("Healing Brush"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);
  CHECK(brush_size->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(clone_aligned->isVisible());
  CHECK(healing_diffusion->isVisible());
  healing_diffusion->setValue(3);
  QApplication::processEvents();
  CHECK(canvas->healing_diffusion() == 3);

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!healing_diffusion->isVisible());

  require_action(window, "toolDodgeAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Dodge);
  CHECK(brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(local_strength->isVisible());
  CHECK(local_range->isVisible());
  CHECK(protect_tones->isVisible());
  CHECK(!sponge_mode->isVisible());
  CHECK(!sponge_vibrance->isVisible());
  local_strength->setValue(62);
  local_range->setCurrentIndex(local_range->findData(
      static_cast<int>(patchy::ui::CanvasWidget::LocalToneRange::Highlights)));
  protect_tones->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->local_adjustment_strength() == 62);
  CHECK(canvas->local_tone_range() == patchy::ui::CanvasWidget::LocalToneRange::Highlights);
  CHECK(!canvas->local_protect_tones());

  require_action(window, "toolSpongeAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Sponge);
  CHECK(local_strength->isVisible());
  CHECK(!local_range->isVisible());
  CHECK(!protect_tones->isVisible());
  CHECK(sponge_mode->isVisible());
  CHECK(sponge_vibrance->isVisible());
  sponge_mode->setCurrentIndex(
      sponge_mode->findData(static_cast<int>(patchy::ui::CanvasWidget::SpongeMode::Saturate)));
  sponge_vibrance->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->sponge_mode() == patchy::ui::CanvasWidget::SpongeMode::Saturate);
  CHECK(!canvas->sponge_vibrance());

  require_action(window, "toolBlurAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::BlurBrush);
  CHECK(local_strength->isVisible());
  CHECK(!local_range->isVisible());
  CHECK(!protect_tones->isVisible());
  CHECK(!sponge_mode->isVisible());
  CHECK(!sponge_vibrance->isVisible());

  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  QApplication::processEvents();
  CHECK(gradient_method->isVisible());
  CHECK(gradient_opacity->isVisible());
  CHECK(gradient_opacity_slider->isVisible());
  CHECK(gradient_reverse->isVisible());
  CHECK(gradient_preview->isVisible());
  CHECK(gradient_presets->isVisible());
  CHECK(gradient_edit_stops->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  const auto radial_index = gradient_method->findText(QStringLiteral("Radial"));
  CHECK(radial_index >= 0);
  gradient_method->setCurrentIndex(radial_index);
  gradient_opacity_slider->setValue(55);
  gradient_reverse->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_opacity() == 55);
  CHECK(canvas->gradient_reverse());

  QTimer::singleShot(0, [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gradientStopsDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("gradientStopsPreview"));
    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gradientStopsTable"));
    auto* add_stop = dialog->findChild<QPushButton*>(QStringLiteral("gradientAddStopButton"));
    auto* choose_color = dialog->findChild<QPushButton*>(QStringLiteral("gradientChooseStopColorButton"));
    CHECK(preview != nullptr);
    CHECK(table != nullptr);
    CHECK(add_stop != nullptr);
    CHECK(choose_color != nullptr);
    CHECK(table->rowCount() == 2);

    constexpr int gradient_gutter = 10;
    const auto handle_x = [preview](double location) {
      constexpr int gutter = 10;
      const int track_max = std::max(1, preview->width() - gutter * 2 - 2);
      return gutter + static_cast<int>(std::lround(std::clamp(location, 0.0, 1.0) * track_max));
    };
    const int bar_right_x = handle_x(1.0);
    const auto preview_image = preview->grab().toImage();
    CHECK(preview_image.rect().contains(QPoint(bar_right_x, 16)));
    CHECK(preview_image.pixelColor(bar_right_x, 16).value() > 180);
    int visible_left_handle_pixels = 0;
    int visible_right_handle_pixels = 0;
    for (int y = 44; y < preview_image.height(); ++y) {
      for (int x = 0; x <= gradient_gutter + 12 && x < preview_image.width(); ++x) {
        const auto color = preview_image.pixelColor(x, y);
        if (color.blue() > 120 || color.value() < 16) {
          ++visible_left_handle_pixels;
        }
      }
      for (int x = std::max(0, preview_image.width() - gradient_gutter - 13); x < preview_image.width(); ++x) {
        if (preview_image.pixelColor(x, y).value() > 180) {
          ++visible_right_handle_pixels;
        }
      }
    }
    CHECK(visible_left_handle_pixels > 12);
    CHECK(visible_right_handle_pixels > 12);

    const int handle_y = 48;
    const int x10 = handle_x(0.10);
    const int x50 = handle_x(0.50);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x10, handle_y), Qt::NoButton, Qt::NoButton);
    CHECK(preview->cursor().shape() == Qt::CrossCursor);
    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x10, handle_y), Qt::LeftButton, Qt::LeftButton);
    CHECK(table->rowCount() == 3);
    CHECK(table->currentRow() == 2);
    CHECK(table->item(2, 0)->text() == QStringLiteral("10"));
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, handle_y), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, handle_y), Qt::LeftButton, Qt::NoButton);
    CHECK(table->item(2, 0)->text() == QStringLiteral("50"));

    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(bar_right_x, 16), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(bar_right_x, 16), Qt::LeftButton, Qt::NoButton);
    const QColor sampled_color(table->item(2, 1)->text());
    CHECK(sampled_color.isValid());
    CHECK(sampled_color.value() > 180);

    add_stop->click();
    CHECK(table->rowCount() == 4);
    const int x60 = handle_x(0.60);
    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x60, handle_y), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x60, preview->height() + 4), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x60, preview->height() + 16), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, handle_y), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    CHECK(table->item(3, 0)->text() == QStringLiteral("50"));
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, handle_y), Qt::LeftButton, Qt::NoButton);
    CHECK(table->rowCount() == 4);

    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x50, handle_y), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, 8), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, 8), Qt::LeftButton, Qt::NoButton);
    CHECK(table->rowCount() == 3);

    table->item(2, 0)->setText(QStringLiteral("50"));
    table->item(2, 1)->setText(QStringLiteral("#00FF00"));
    table->item(2, 2)->setText(QStringLiteral("25"));
    table->setCurrentCell(2, 1);

    const QColor original_stop_color(table->item(2, 1)->text());
    bool saw_live_stop_picker = false;
    QTimer::singleShot(0, [&] {
      auto* color_dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyColorDialog")));
      CHECK(color_dialog != nullptr);
      auto* picker = color_dialog->findChild<patchy::ui::PatchyColorPicker*>(
          QStringLiteral("patchyAdvancedColorPicker"));
      CHECK(picker != nullptr);
      picker->setCurrentColor(QColor(12, 34, 56));
      QApplication::processEvents();
      CHECK(table->item(2, 1)->text() == QStringLiteral("#0C2238"));
      saw_live_stop_picker = true;
      color_dialog->reject();
    });
    choose_color->click();
    CHECK(saw_live_stop_picker);
    CHECK(table->item(2, 1)->text() == original_stop_color.name(QColor::HexRgb).toUpper());

    dialog->accept();
  });
  gradient_edit_stops->click();
  QApplication::processEvents();
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 3);
  CHECK(canvas->gradient_stops()->at(1).color.g == 255);
  CHECK(canvas->gradient_stops()->at(1).color.a >= 63);
  CHECK(canvas->gradient_stops()->at(1).color.a <= 64);
}

void ui_gradient_toolbar_preset_popup_applies_stops() {
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/gradientMethod"));
    settings.remove(QStringLiteral("tools/gradientReverse"));
    settings.remove(QStringLiteral("tools/gradientOpacity"));
    settings.remove(QStringLiteral("tools/gradientUseCustomStops"));
    settings.remove(QStringLiteral("tools/gradientStops"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->gradient_stops().has_value());

  auto& library = window.gradient_library();
  library.restore_default_gradients();  // suite-order independence: repair any deleted builtins
  CHECK(!library.entries().empty());
  QString sepia_id;
  QString cyanotype_id;
  for (const auto& entry : library.entries()) {
    if (entry.name == QStringLiteral("Sepia Wash")) {
      sepia_id = entry.storage_id;
    } else if (entry.name == QStringLiteral("Cyanotype")) {
      cyanotype_id = entry.storage_id;
    }
  }
  CHECK(!sepia_id.isEmpty());
  CHECK(!cyanotype_id.isEmpty());
  const auto find_item = [](QTreeWidget* tree, const QString& storage_id) -> QTreeWidgetItem* {
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
      if ((*it)->data(0, Qt::UserRole).toString() == storage_id) {
        return *it;
      }
    }
    return nullptr;
  };

  auto* presets_button = window.findChild<QPushButton*>(QStringLiteral("gradientPresetsButton"));
  CHECK(presets_button != nullptr);
  CHECK(presets_button->isVisible());
  presets_button->click();
  QApplication::processEvents();
  QPointer<QFrame> popup = presets_button->findChild<QFrame*>(QStringLiteral("gradientPresetsButtonPopup"));
  CHECK(popup != nullptr);
  CHECK(popup->isVisible());
  auto* tree = popup->findChild<QTreeWidget*>(QStringLiteral("gradientPresetsButtonTree"));
  CHECK(tree != nullptr);
  auto* sepia_item = find_item(tree, sepia_id);
  CHECK(sepia_item != nullptr);
  tree->scrollToItem(sepia_item);
  QApplication::processEvents();
  const auto sepia_rect = tree->visualItemRect(sepia_item);
  CHECK(sepia_rect.isValid());
  QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, sepia_rect.center());
  QApplication::processEvents();
  CHECK(popup == nullptr);  // selecting a leaf closes the WA_DeleteOnClose popup

  // Sepia Wash flattens to 33 sampled stops with exact endpoint colors.
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 33);
  const auto applied = *canvas->gradient_stops();
  CHECK(applied.front().color.r == 0x2b);
  CHECK(applied.front().color.g == 0x1b);
  CHECK(applied.front().color.b == 0x12);
  CHECK(applied.front().color.a == 255);
  CHECK(applied.back().color.r == 0xf1);
  CHECK(applied.back().color.g == 0xdf);
  CHECK(applied.back().color.b == 0xc0);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Sepia Wash")));
  auto* gradient_preview = window.findChild<QPushButton*>(QStringLiteral("gradientPreviewButton"));
  CHECK(gradient_preview != nullptr);
  CHECK(gradient_preview->styleSheet().contains(QStringLiteral("rgba(241, 223, 192, 255)")));

  // The Edit Stops dialog's Preset... button opens the same quick popup; a
  // rejected dialog discards the picked preset.
  bool checked_dialog_popup = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gradientStopsDialog"));
    CHECK(dialog != nullptr);
    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gradientStopsTable"));
    auto* preset_button = dialog->findChild<QPushButton*>(QStringLiteral("gradientPresetButton"));
    CHECK(table != nullptr);
    CHECK(preset_button != nullptr);
    CHECK(table->rowCount() == 33);
    preset_button->click();
    QApplication::processEvents();
    QPointer<QFrame> dialog_popup =
        preset_button->findChild<QFrame*>(QStringLiteral("gradientPresetButtonPopup"));
    CHECK(dialog_popup != nullptr);
    CHECK(dialog_popup->isVisible());
    auto* dialog_tree = dialog_popup->findChild<QTreeWidget*>(QStringLiteral("gradientPresetButtonTree"));
    CHECK(dialog_tree != nullptr);
    auto* cyanotype_item = find_item(dialog_tree, cyanotype_id);
    CHECK(cyanotype_item != nullptr);
    dialog_tree->scrollToItem(cyanotype_item);
    QApplication::processEvents();
    const auto cyanotype_rect = dialog_tree->visualItemRect(cyanotype_item);
    CHECK(cyanotype_rect.isValid());
    QTest::mouseClick(dialog_tree->viewport(), Qt::LeftButton, Qt::NoModifier, cyanotype_rect.center());
    QApplication::processEvents();
    CHECK(table->rowCount() == 33);
    CHECK(table->item(0, 1)->text() == QStringLiteral("#081C33"));
    checked_dialog_popup = true;
    dialog->reject();
  });
  auto* gradient_edit_stops = window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"));
  CHECK(gradient_edit_stops != nullptr);
  gradient_edit_stops->click();
  QApplication::processEvents();
  CHECK(checked_dialog_popup);
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 33);
  CHECK(canvas->gradient_stops()->front().color.r == 0x2b);
}

void ui_right_docks_collapse_layers_show_metadata_and_info_updates() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  auto* document_info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* active_layer_geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  auto* active_layer_mask = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  auto* active_layer_adjustment = window.findChild<QLabel*>(QStringLiteral("activeLayerAdjustmentLabel"));
  auto* active_layer_text = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
  auto* active_tool_info = window.findChild<QLabel*>(QStringLiteral("activeToolInfoLabel"));
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(layer_list != nullptr);
  CHECK(info != nullptr);
  CHECK(document_info != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(active_layer_geometry != nullptr);
  CHECK(active_layer_mask != nullptr);
  CHECK(active_layer_adjustment != nullptr);
  CHECK(active_layer_text != nullptr);
  CHECK(active_tool_info != nullptr);
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(document_info->text().contains(QStringLiteral("Document")));
  CHECK(document_info->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Paint Layer")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Pixel Layer")));
  CHECK(active_layer_geometry->text().contains(QStringLiteral("Bounds:")));
  CHECK(!active_layer_mask->isVisible());
  CHECK(!active_layer_adjustment->isVisible());
  CHECK(!active_layer_text->isVisible());
  CHECK(active_tool_info->text().contains(QStringLiteral("Brush")));
  auto* panel_column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* layers_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("layersDock"));
  auto* history_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("historyDock"));
  auto* properties_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("propertiesDock"));
  auto* history_list = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layers_dock != nullptr);
  CHECK(history_dock != nullptr);
  CHECK(properties_dock != nullptr);
  CHECK(history_list != nullptr);
  // Opens at the content-fit width; the user can drag it narrower.
  CHECK(panel_column->width() >= 280);
  // The 300 px Layers floor holds unless the screen is too short for it, in
  // which case it yields exactly enough for the window to fit (GitHub issue 108).
  CHECK(layers_dock->minimumHeight() >= 300 ||
        window.minimumSizeHint().height() <= patchy::ui::MainWindowTestAccess::available_window_height(window));
  CHECK(layer_list->minimumHeight() >= 120);
  CHECK(properties_dock->maximumHeight() <= 240);
  CHECK(properties_dock->height() <= 240);
  CHECK(window.minimumSizeHint().height() <= 780);
  CHECK(layer_list->contextMenuPolicy() == Qt::CustomContextMenu);
  const auto layer_action_buttons = window.findChildren<QAbstractButton*>();
  int visible_layer_action_buttons = 0;
  for (const auto* button : layer_action_buttons) {
    if (button->property("layerActionButton").toBool()) {
      ++visible_layer_action_buttons;
      // Full width in one footer row; a narrow column narrows them (GitHub issue 199).
      CHECK(button->maximumWidth() >= 40);
      CHECK(button->minimumWidth() >= 32);
      CHECK(button->minimumHeight() >= 34);
      CHECK(button->iconSize().width() >= 24);
      CHECK(button->iconSize().height() >= 24);
    }
  }
  // add, folder, adjustment, layer styles, add mask, duplicate, animation preview,
  // delete (rename lives on the row and its context menu, GitHub issue 94).
  CHECK(visible_layer_action_buttons == 8);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(window.findChild<QWidget*>(QStringLiteral("swatchesDock")) == nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));
  CHECK(history_toggle->icon().isNull());
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(history_list->count() > 0);
  history_toggle->setChecked(true);
  QApplication::processEvents();
  QApplication::processEvents();
  const auto history_row_height = history_list->sizeHintForRow(0);
  CHECK(history_row_height > 0);
  CHECK(history_toggle->text() == QStringLiteral("v"));
  // The expanded floor stays low so panel state cannot force the window
  // taller; the 190px working height comes from its neighbors instead.
  CHECK(history_dock->minimumHeight() >= 90);
  CHECK(history_dock->minimumHeight() <= 120);
  CHECK(history_dock->height() >= 190);
  CHECK(history_list->viewport()->height() >= history_row_height * 3);
  save_widget_artifact("ui_history_expanded_default", *history_dock);
  history_toggle->setChecked(false);
  QApplication::processEvents();
  QApplication::processEvents();
  // Collapsed sections are exactly their title rows: none may show a dead
  // strip under its header (History used to sit 8px taller).
  auto* info_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("infoDock"));
  auto* palette_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("paletteDock"));
  CHECK(info_dock != nullptr);
  CHECK(palette_dock != nullptr);
  CHECK(history_dock->height() == properties_dock->height());
  CHECK(history_dock->height() == info_dock->height());
  CHECK(history_dock->height() == palette_dock->height());
  CHECK(panel_column->width() >= 260);
  const auto dock_width_before_resize = panel_column->width();
  auto* dock_resize_handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(dock_resize_handle != nullptr);
  const auto dock_resize_point = dock_resize_handle->rect().center();
  send_mouse(*dock_resize_handle, QEvent::MouseButtonPress, dock_resize_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseMove, dock_resize_point + QPoint(-90, 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseButtonRelease, dock_resize_point + QPoint(-90, 0), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(panel_column->width() > dock_width_before_resize + 40);
  // Every section, palette included, spans the column.
  CHECK(palette_dock->width() == layers_dock->width());
  CHECK(layers_dock->width() == panel_column->width());

  auto* row_widget = layer_list->itemWidget(layer_list->item(0));
  CHECK(row_widget != nullptr);
  CHECK(row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")) != nullptr);

  const auto point = canvas->widget_position_for_document_point(QPoint(64, 48));
  send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("X: 64")));
  CHECK(info->text().contains(QStringLiteral("Y: 48")));
  CHECK(info->text().contains(QStringLiteral("RGB:")));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  const auto marquee_start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto marquee_end = canvas->widget_position_for_document_point(QPoint(140, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, marquee_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, marquee_end, Qt::NoButton, Qt::LeftButton);
  CHECK(info->text().contains(QStringLiteral("Selection:")));
  CHECK(info->text().contains(QStringLiteral(" at 40, 40")));
  send_mouse(*canvas, QEvent::MouseButtonRelease, marquee_end, Qt::LeftButton, Qt::NoButton);
  save_widget_artifact("ui_info_panel_layers_docks", window);
}

void ui_layer_opacity_control_defers_slow_rendering_and_undoes_once() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Opacity Target",
                      solid_pixels(90, 70, patchy::PixelFormat::rgba8(), QColor(30, 150, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{35, 25, 90, 70});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Opacity Deferred"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->force_refresh();
  QApplication::processEvents();

  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->value() == 100);

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_render_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("240"));

  // The popup slider drives the spin box with plain setValue calls, so this
  // sweep exercises the same path a slider drag does.
  QElapsedTimer slider_updates;
  slider_updates.start();
  for (int value = 99; value >= 25; --value) {
    opacity_spin->setValue(value);
    CHECK(opacity_spin->value() == value);
    QApplication::processEvents(QEventLoop::AllEvents, 1);
  }
  CHECK(slider_updates.elapsed() < 300);
  CHECK(opacity_spin->value() == 25);

  auto& edited_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* edited_layer = edited_document.find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  QElapsedTimer wait_for_apply;
  wait_for_apply.start();
  while (std::abs(edited_layer->opacity() - 0.25F) > 0.001F && wait_for_apply.elapsed() < 900) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(std::abs(edited_layer->opacity() - 0.25F) <= 0.001F);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 1.0F) <= 0.001F);
}

// Options-bar labels show no trailing colon (GitHub issue 127): the "Name:"
// source strings stay shared with the dialog forms, and the bound-text binding
// drops the colon on the bar, in every language (the fullwidth colon too).
void ui_options_bar_labels_drop_the_trailing_colon() {
  CHECK(patchy::ui::strip_trailing_colon(QStringLiteral("Opacity:")) == QStringLiteral("Opacity"));
  CHECK(patchy::ui::strip_trailing_colon(QStringLiteral("Opacity")) == QStringLiteral("Opacity"));
  CHECK(patchy::ui::strip_trailing_colon(QString::fromUtf8("\xe4\xb8\x8d\xe9\x80\x8f\xef\xbc\x9a")) ==
        QString::fromUtf8("\xe4\xb8\x8d\xe9\x80\x8f"));

  patchy::ui::MainWindow window;
  show_window(window);
  for (const auto* tool : {"Brush", "Marquee", "Crop", "Rect"}) {
    require_action_by_text(window, QString::fromLatin1(tool))->trigger();
    QApplication::processEvents();
    int visible_labels = 0;
    for (auto* label : window.findChildren<QLabel*>()) {
      // The Crop ratio's ":" between its two fields is a separator, not a name.
      if (!label->isVisible() || !label->property("optionLabel").toBool() ||
          label->text() == QStringLiteral(":")) {
        continue;
      }
      ++visible_labels;
      if (label->text().endsWith(QLatin1Char(':'))) {
        std::printf("  %s keeps a colon: %s\n", tool, label->text().toUtf8().constData());
      }
      CHECK(!label->text().endsWith(QLatin1Char(':')));
    }
    CHECK(visible_labels > 0);
  }
  // The marquee's Feather label is an ordinary option label, bound the same way.
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  auto* feather_label = window.findChild<QLabel*>(QStringLiteral("selectionFeatherLabel"));
  CHECK(feather_label != nullptr);
  CHECK(feather_label->text() == QStringLiteral("Feather"));
}

// Options-bar labels scrub their field (GitHub issue 46): a horizontal drag on
// "Size:" moves brushSizeSpin one step per pixel, ten with Shift, clamped to the
// range; a plain click changes nothing; the label wears the SizeHor cursor.
void ui_options_bar_label_scrub_changes_spin_value() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size_spin != nullptr);
  CHECK(size_spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  QLabel* size_label = nullptr;
  for (auto* label : window.findChildren<QLabel*>()) {
    if (label->isVisible() && label->text() == QStringLiteral("Size") &&
        label->property("optionLabel").toBool()) {
      size_label = label;
    }
  }
  CHECK(size_label != nullptr);
  CHECK(size_label->cursor().shape() == Qt::SizeHorCursor);

  size_spin->setValue(40);
  const auto origin = size_label->rect().center();
  const int drag_start = QApplication::startDragDistance();
  const auto scrub = [&](int dx, Qt::KeyboardModifiers modifiers) {
    send_mouse(*size_label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*size_label, QEvent::MouseMove, origin + QPoint(dx, 0), Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*size_label, QEvent::MouseButtonRelease, origin + QPoint(dx, 0), Qt::LeftButton, Qt::NoButton,
               modifiers);
    QApplication::processEvents();
  };
  scrub(drag_start + 15, Qt::NoModifier);
  CHECK(size_spin->value() == 40 + drag_start + 15);
  scrub(-(drag_start + 15), Qt::NoModifier);
  CHECK(size_spin->value() == 40);
  scrub(drag_start + 2, Qt::ShiftModifier);
  CHECK(size_spin->value() == 40 + 10 * (drag_start + 2));
  scrub(-5000, Qt::NoModifier);
  CHECK(size_spin->value() == size_spin->minimum());
  size_spin->setValue(40);
  // A press and release without a drag, and a move short of the drag distance.
  scrub(0, Qt::NoModifier);
  CHECK(size_spin->value() == 40);
  scrub(std::max(0, drag_start - 2), Qt::NoModifier);
  CHECK(size_spin->value() == 40);

  // Every labeled numeric field in the bar has a handle, including the nested
  // Feather group, the mixer percentages, the transform fields, and text size.
  for (const char* name : {"brushOpacitySpin", "brushFlowSpin", "selectionFeatherSpin", "mixerWetSpin",
                           "textSizeSpin"}) {
    auto* spin = window.findChild<QAbstractSpinBox*>(QString::fromLatin1(name));
    CHECK(spin != nullptr);
    CHECK(spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  }
  int handles = 0;
  for (auto* spin : window.findChildren<QAbstractSpinBox*>()) {
    handles += spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool() ? 1 : 0;
  }
  CHECK(handles >= 30);
}

// The field is part of the scrub zone (dialog_utils.hpp). Not being edited, a drag
// on it scrubs (SizeHor, one editingFinished, focus handed back) and a press released
// without a drag, or one short of the drag distance, enters editing with the number
// selected. While editing, drags select text and change nothing, clicks and
// double-clicks behave as in any text field, until Enter or Esc ends editing. A field
// focused with Tab is being edited from the start.
void ui_options_bar_field_drag_scrubs_and_click_edits() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size_spin != nullptr);
  auto* editor = size_spin->findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  int finished = 0;
  QObject::connect(size_spin, &QAbstractSpinBox::editingFinished, size_spin, [&finished] { ++finished; });
  const int drag_start = QApplication::startDragDistance();
  const QPoint origin(editor->width() / 2, editor->height() / 2);
  // Just past the right-aligned number, so a leftward drag from here crosses its digits.
  const QPoint click_point(editor->width() - editor->textMargins().right() - 2, editor->height() / 2);
  const auto focus_canvas = [&] {
    canvas->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    CHECK(canvas->hasFocus());
  };
  // A real click: the window system hands out focus before the press arrives.
  const auto press = [&](QPoint point) {
    if (!size_spin->hasFocus()) {
      size_spin->setFocus(Qt::MouseFocusReason);
    }
    send_mouse(*editor, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  };
  const auto move = [&](QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(*editor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton, modifiers);
  };
  const auto release = [&](QPoint point) {
    send_mouse(*editor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  // An unfocused field scrubs and hands focus back to the canvas.
  focus_canvas();
  CHECK(editor->cursor().shape() == Qt::SizeHorCursor);
  size_spin->setValue(40);
  press(origin);
  move(origin + QPoint(drag_start + 15, 0));
  CHECK(size_spin->value() == 40 + drag_start + 15);
  CHECK(editor->cursor().shape() == Qt::SizeHorCursor);
  release(origin + QPoint(drag_start + 15, 0));
  CHECK(finished == 1);
  CHECK(!size_spin->hasFocus());
  CHECK(canvas->hasFocus());

  // A click without a drag, and one that moved short of the drag distance, enter
  // editing with the number selected and change nothing.
  size_spin->setValue(40);
  for (const int short_move : {0, std::max(0, drag_start - 2)}) {
    focus_canvas();
    const int finished_before_click = finished;
    press(origin);
    move(origin + QPoint(short_move, 0));
    release(origin + QPoint(short_move, 0));
    CHECK(size_spin->value() == 40);
    CHECK(finished == finished_before_click);
    CHECK(size_spin->hasFocus());
    CHECK(editor->selectedText() == size_spin->cleanText());
    CHECK(editor->cursor().shape() == Qt::IBeamCursor);
  }

  // Editing: a drag selects text and changes no value; a click places the caret; a
  // double-click selects; typing edits.
  editor->deselect();
  const int finished_before_select = finished;
  press(click_point);
  move(click_point - QPoint(drag_start + 25, 0));
  release(click_point - QPoint(drag_start + 25, 0));
  CHECK(editor->cursorPositionAt(click_point) == static_cast<int>(editor->text().length()));
  CHECK(size_spin->value() == 40);
  CHECK(editor->hasSelectedText());
  CHECK(finished == finished_before_select);
  press(click_point);
  release(click_point);
  CHECK(!editor->hasSelectedText());
  CHECK(editor->cursorPosition() == editor->cursorPositionAt(click_point));
  press(click_point);
  release(click_point);
  send_mouse(*editor, QEvent::MouseButtonDblClick, click_point, Qt::LeftButton, Qt::LeftButton);
  release(click_point);
  CHECK(editor->hasSelectedText());
  // Let Qt's triple-click window close so the next press here is an ordinary press.
  process_events_for(QApplication::doubleClickInterval() + 50);
  send_key(*size_spin, Qt::Key_A, Qt::ControlModifier);
  CHECK(editor->selectedText() == size_spin->cleanText());
  for (int typed = 0; typed < 2; ++typed) {
    QKeyEvent digit(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(size_spin, &digit);
  }
  send_key(*size_spin, Qt::Key_End);
  send_key(*size_spin, Qt::Key_Home, Qt::ShiftModifier);
  CHECK(editor->hasSelectedText());

  // Enter ends editing: the focused field scrubs again and keeps focus, number selected.
  send_key(*size_spin, Qt::Key_Return);
  CHECK(size_spin->value() == 55);
  const int finished_before_scrub = finished;
  press(origin);
  move(origin + QPoint(drag_start + 5, 0));
  release(origin + QPoint(drag_start + 5, 0));
  CHECK(size_spin->value() == 55 + drag_start + 5);
  CHECK(finished == finished_before_scrub + 1);
  CHECK(size_spin->hasFocus());
  CHECK(editor->selectedText() == size_spin->cleanText());
  // Esc ends editing too.
  press(origin);
  release(origin);
  send_key(*size_spin, Qt::Key_Escape);
  size_spin->setValue(30);
  press(origin);
  move(origin - QPoint(drag_start + 4, 0));
  release(origin - QPoint(drag_start + 4, 0));
  CHECK(size_spin->value() == 30 - (drag_start + 4));

  // Tab into the field: editing from the start, so a drag selects.
  focus_canvas();
  size_spin->setValue(40);
  size_spin->setFocus(Qt::TabFocusReason);
  QApplication::processEvents();
  editor->deselect();
  press(click_point);
  move(click_point - QPoint(drag_start + 25, 0));
  release(click_point - QPoint(drag_start + 25, 0));
  CHECK(size_spin->value() == 40);
  CHECK(editor->hasSelectedText());

  // Review artifact: the Size field before and after a 25 px drag inside it.
  focus_canvas();
  size_spin->setValue(40);
  const auto grab_field = [size_spin] {
    return size_spin->grab().toImage().scaled(size_spin->size() * 3, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  };
  QList<QImage> strips{grab_field()};
  press(origin);
  move(origin + QPoint(25, 0));
  release(origin + QPoint(25, 0));
  strips.append(grab_field());
  CHECK(size_spin->value() == 65);
  save_stacked_artifact("ui_options_bar_field_drag", strips);

  // The Opacity field scrubs from inside the field as well.
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  CHECK(opacity_spin != nullptr);
  auto* opacity_editor = opacity_spin->findChild<QLineEdit*>();
  CHECK(opacity_editor != nullptr);
  opacity_spin->setValue(80);
  const QPoint opacity_origin(opacity_editor->width() / 2, opacity_editor->height() / 2);
  send_mouse(*opacity_editor, QEvent::MouseButtonPress, opacity_origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*opacity_editor, QEvent::MouseMove, opacity_origin + QPoint(-(drag_start + 10), 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*opacity_editor, QEvent::MouseButtonRelease, opacity_origin + QPoint(-(drag_start + 10), 0),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(opacity_spin->value() == 80 - (drag_start + 10));
}

// The gap between a label and its field scrubs like the label, with the SizeHor
// cursor on hover; a label whose field sits past a slider has no gap zone. Shift
// moves ten steps per pixel, Alt a tenth (integer fields every ten pixels), and a
// modifier change mid-drag continues from the current value.
void ui_scrub_gap_and_modifiers_follow_the_zone_rules() {
  QWidget panel;
  auto* rows = new QVBoxLayout(&panel);
  auto* row = new QHBoxLayout();
  row->setSpacing(10);
  auto* label = new QLabel(QStringLiteral("Amount"), &panel);
  auto* amount = new QSpinBox(&panel);
  amount->setRange(0, 1000);
  amount->setValue(50);
  auto* radius = new QDoubleSpinBox(&panel);
  radius->setRange(0.0, 100.0);
  radius->setDecimals(2);
  radius->setSingleStep(0.5);
  radius->setValue(2.0);
  row->addWidget(label);
  row->addWidget(amount);
  row->addWidget(new QLabel(QStringLiteral("Radius"), &panel));
  row->addWidget(radius);
  row->addStretch(1);
  rows->addLayout(row);
  auto* slider_row = new QHBoxLayout();
  auto* jitter_label = new QLabel(QStringLiteral("Jitter"), &panel);
  auto* jitter_slider = new QSlider(Qt::Horizontal, &panel);
  jitter_slider->setFixedWidth(120);
  auto* jitter = new QSpinBox(&panel);
  slider_row->addWidget(jitter_label);
  slider_row->addWidget(jitter_slider);
  slider_row->addWidget(jitter);
  slider_row->addStretch(1);
  rows->addLayout(slider_row);
  patchy::ui::install_scrub_labels_in(&panel);
  panel.resize(520, 120);
  panel.show();
  QApplication::processEvents();
  CHECK(jitter->property(patchy::ui::kScrubHandleInstalledProperty).toBool());

  const int drag_start = QApplication::startDragDistance();
  const int gap_left = label->geometry().right() + 1;
  const int gap_right = amount->geometry().left() - 1;
  CHECK(gap_right >= gap_left);
  const QPoint gap((gap_left + gap_right) / 2, label->geometry().center().y());
  CHECK(panel.childAt(gap) == nullptr);
  send_mouse(panel, QEvent::MouseMove, gap, Qt::NoButton, Qt::NoButton);
  CHECK(panel.cursor().shape() == Qt::SizeHorCursor);
  const QPoint empty(panel.width() - 4, label->geometry().center().y());
  send_mouse(panel, QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
  CHECK(panel.cursor().shape() != Qt::SizeHorCursor);
  const QPoint after_jitter(jitter_label->geometry().right() + 3, jitter_label->geometry().center().y());
  if (panel.childAt(after_jitter) == nullptr) {
    send_mouse(panel, QEvent::MouseMove, after_jitter, Qt::NoButton, Qt::NoButton);
    CHECK(panel.cursor().shape() != Qt::SizeHorCursor);
  }

  int finished = 0;
  QObject::connect(amount, &QAbstractSpinBox::editingFinished, amount, [&finished] { ++finished; });
  // A drag from the gap scrubs, one editingFinished; a click there changes nothing.
  send_mouse(panel, QEvent::MouseButtonPress, gap, Qt::LeftButton, Qt::LeftButton);
  send_mouse(panel, QEvent::MouseMove, gap + QPoint(drag_start + 6, 0), Qt::NoButton, Qt::LeftButton);
  CHECK(amount->value() == 50 + drag_start + 6);
  send_mouse(panel, QEvent::MouseButtonRelease, gap + QPoint(drag_start + 6, 0), Qt::LeftButton, Qt::NoButton);
  CHECK(finished == 1);
  amount->setValue(50);
  send_mouse(panel, QEvent::MouseButtonPress, gap, Qt::LeftButton, Qt::LeftButton);
  send_mouse(panel, QEvent::MouseButtonRelease, gap, Qt::LeftButton, Qt::NoButton);
  CHECK(amount->value() == 50);
  CHECK(finished == 1);

  // Alt on the label: a tenth of a step per pixel, whole steps on an integer field.
  const QPoint on_label = label->rect().center();
  send_mouse(*label, QEvent::MouseButtonPress, on_label, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*label, QEvent::MouseMove, on_label + QPoint(drag_start + 20, 0), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
  CHECK(amount->value() == 50 + static_cast<int>(std::trunc((drag_start + 20) * 0.1)));
  send_mouse(*label, QEvent::MouseButtonRelease, on_label + QPoint(drag_start + 20, 0), Qt::LeftButton,
             Qt::NoButton, Qt::AltModifier);
  // Shift pressed mid-drag continues from the current value at ten steps per pixel.
  amount->setValue(50);
  send_mouse(*label, QEvent::MouseButtonPress, on_label, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*label, QEvent::MouseMove, on_label + QPoint(drag_start + 5, 0), Qt::NoButton, Qt::LeftButton);
  const int before_shift = amount->value();
  CHECK(before_shift == 50 + drag_start + 5);
  send_mouse(*label, QEvent::MouseMove, on_label + QPoint(drag_start + 5, 0), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
  CHECK(amount->value() == before_shift);
  send_mouse(*label, QEvent::MouseMove, on_label + QPoint(drag_start + 8, 0), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
  CHECK(amount->value() == before_shift + 30);
  send_mouse(*label, QEvent::MouseButtonRelease, on_label + QPoint(drag_start + 8, 0), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
  // A decimal field moves by fractions of its step under Alt.
  auto* radius_editor = radius->findChild<QLineEdit*>();
  CHECK(radius_editor != nullptr);
  const QPoint radius_point(radius_editor->width() / 2, radius_editor->height() / 2);
  send_mouse(*radius_editor, QEvent::MouseButtonPress, radius_point, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*radius_editor, QEvent::MouseMove, radius_point + QPoint(drag_start + 10, 0), Qt::NoButton,
             Qt::LeftButton, Qt::AltModifier);
  send_mouse(*radius_editor, QEvent::MouseButtonRelease, radius_point + QPoint(drag_start + 10, 0), Qt::LeftButton,
             Qt::NoButton, Qt::AltModifier);
  CHECK(std::abs(radius->value() - (2.0 + (drag_start + 10) * 0.1 * 0.5)) < 0.011);
}

// The Brush bar's Mode combo (core/paint_mode.hpp): Normal, Dissolve, Behind, Clear,
// then the layer list's modes in its order without Pass Through; Brush only; the choice
// reaches the canvas, persists as tools/brushMode and comes back in a new window, and
// the bar still fits one row at the default window width.
void ui_brush_mode_combo_lists_paint_modes_and_persists() {
  SettingsValueRestorer saved_mode(QStringLiteral("tools/brushMode"));
  patchy::ui::app_settings().remove(QStringLiteral("tools/brushMode"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1280, 860);
    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    QApplication::processEvents();
    process_events_for(20);
    auto* combo = window.findChild<QComboBox*>(QStringLiteral("brushModeCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->isVisible());
    QComboBox layer_modes;
    patchy::ui::add_blend_mode_items(&layer_modes);
    CHECK(combo->count() == layer_modes.count() + 2);
    const QStringList head{QStringLiteral("Normal"), QStringLiteral("Dissolve"), QStringLiteral("Behind"),
                           QStringLiteral("Clear")};
    for (int i = 0; i < head.size(); ++i) {
      CHECK(combo->itemText(i) == head.at(i));
    }
    for (int i = 2; i < layer_modes.count(); ++i) {
      CHECK(combo->itemText(i + 2) == layer_modes.itemText(i));
    }
    CHECK(combo->findText(QStringLiteral("Pass Through")) < 0);
    CHECK(combo->currentText() == QStringLiteral("Normal"));
    // The Brush row keeps one line at the default window width.
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* dynamics = window.findChild<QWidget*>(QStringLiteral("brushDynamicsButton"));
    CHECK(bar != nullptr && size_spin != nullptr);
    if (dynamics != nullptr && dynamics->isVisible()) {
      CHECK(std::abs(dynamics->mapTo(bar, QPoint(0, 0)).y() - size_spin->mapTo(bar, QPoint(0, 0)).y()) <= 4);
    }
    CHECK(std::abs(combo->mapTo(bar, QPoint(0, 0)).y() + combo->height() / 2 -
                   size_spin->mapTo(bar, QPoint(0, 0)).y() - size_spin->height() / 2) <= 4);
    ensure_artifact_dir();
    save_widget_artifact("ui_brush_mode_options_bar", *bar);
    window.resize(1920, 860);
    QApplication::processEvents();
    process_events_for(20);
    save_widget_artifact("ui_brush_mode_options_bar_1920", *bar);
    window.resize(1280, 860);
    QApplication::processEvents();
    process_events_for(20);

    combo->setCurrentIndex(combo->findText(QStringLiteral("Multiply")));
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    CHECK(canvas->brush_paint_mode() == patchy::PaintMode::of(patchy::BlendMode::Multiply));
    CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/brushMode")).toString() == QStringLiteral("multiply"));
    for (const char* tool : {"Eraser", "Clone", "Mixer Brush"}) {
      if (auto* action = find_action_by_text(window, QString::fromLatin1(tool)); action != nullptr) {
        action->trigger();
        QApplication::processEvents();
        CHECK(!combo->isVisible());
      }
    }
    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    QApplication::processEvents();
    combo->setCurrentIndex(combo->findText(QStringLiteral("Behind")));
    QApplication::processEvents();
    CHECK(canvas->brush_paint_mode() == patchy::PaintMode::behind());
    CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/brushMode")).toString() == QStringLiteral("behind"));
  }
  patchy::ui::MainWindow reopened;
  show_window(reopened);
  require_action_by_text(reopened, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* combo = reopened.findChild<QComboBox*>(QStringLiteral("brushModeCombo"));
  CHECK(combo != nullptr);
  CHECK(combo->currentText() == QStringLiteral("Behind"));
  CHECK(require_canvas(reopened)->brush_paint_mode() == patchy::PaintMode::behind());
  // Script ids round-trip, including the two paint-only modes; Pass Through is no paint mode.
  for (const char* id : {"normal", "dissolve", "behind", "clear", "multiply", "linear-dodge", "luminosity"}) {
    patchy::PaintMode mode;
    CHECK(patchy::ui::script_paint_mode_from_id(QString::fromLatin1(id), &mode));
    CHECK(patchy::ui::script_paint_mode_id(mode) == QString::fromLatin1(id));
  }
  patchy::PaintMode rejected;
  CHECK(!patchy::ui::script_paint_mode_from_id(QStringLiteral("pass-through"), &rejected));
  CHECK(!patchy::ui::script_paint_mode_from_id(QStringLiteral("Multiply"), &rejected));
}

// Dialogs get scrub handles from exec_dialog / run_non_modal_dialog through
// install_scrub_labels_in, whose pairing must reach every row shape the dialogs
// use (GitHub issue 46): a direct form row, a "[slider] [spin]" row widget, a
// "[spin] - +" step-button row, a grid "label, slider, spin" row, a caption above
// an HBox, and rows inside a QScrollArea inside a QTabWidget. A label followed by
// a button and then a spin names the button, not the spin; a row label names the
// first field of its row, and the "to" between a range's two fields is exempt.
void ui_dialog_scrub_labels_pair_every_row_shape() {
  QDialog dialog;
  auto* root = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  root->addLayout(form);

  auto* direct = new QSpinBox(&dialog);
  direct->setRange(0, 100);
  direct->setValue(20);
  form->addRow(QStringLiteral("Direct:"), direct);
  auto* slider_row_spin = patchy::ui::add_dialog_slider_spin_row(form, &dialog, QStringLiteral("Slider row:"),
                                                                 QStringLiteral("scrubTestSlider"),
                                                                 QStringLiteral("scrubTestSliderSpin"), 0, 100, 40);
  auto* stepped = new QSpinBox(&dialog);
  stepped->setRange(0, 100);
  form->addRow(QStringLiteral("Stepped:"),
               patchy::ui::wrap_spin_with_step_buttons(stepped, &dialog, QStringLiteral("Stepped")));
  auto* button_then_spin = new QSpinBox(&dialog);
  auto* button_row = new QWidget(&dialog);
  auto* button_row_layout = new QHBoxLayout(button_row);
  button_row_layout->addWidget(new QPushButton(QStringLiteral("Pick"), button_row));
  button_row_layout->addWidget(button_then_spin);
  auto* color_label = new QLabel(QStringLiteral("Grid color:"), &dialog);
  form->addRow(color_label, button_row);
  // A form row whose field starts with a spin box pairs that first field with the
  // row label; the exempt "to" leaves the second field alone.
  auto* range_row = new QWidget(&dialog);
  auto* range_layout = new QHBoxLayout(range_row);
  auto* range_minimum = new QSpinBox(range_row);
  auto* range_to = new QLabel(QStringLiteral("to"), range_row);
  range_to->setProperty(patchy::ui::kScrubLabelExemptProperty, true);
  auto* range_maximum = new QSpinBox(range_row);
  range_layout->addWidget(range_minimum);
  range_layout->addWidget(range_to);
  range_layout->addWidget(range_maximum);
  form->addRow(QStringLiteral("Range:"), range_row);

  auto* grid = new QGridLayout();
  root->addLayout(grid);
  auto* grid_label = new QLabel(QStringLiteral("Jitter"), &dialog);
  auto* grid_spin = new QSpinBox(&dialog);
  grid->addWidget(grid_label, 0, 0);
  grid->addWidget(new QSlider(Qt::Horizontal, &dialog), 0, 1);
  grid->addWidget(grid_spin, 0, 2);

  auto* caption = new QLabel(QStringLiteral("Bend"), &dialog);
  root->addWidget(caption);
  auto* caption_row = new QHBoxLayout();
  auto* caption_spin = new QDoubleSpinBox(&dialog);
  caption_row->addWidget(new QSlider(Qt::Horizontal, &dialog));
  caption_row->addWidget(caption_spin);
  root->addLayout(caption_row);

  // A section caption above a grid of labeled rows (Canvas Size's "New Size:")
  // leaves the spins to the labels inside the grid.
  auto* section_caption = new QLabel(QStringLiteral("New Size: 1.2M"), &dialog);
  root->addWidget(section_caption);
  auto* section_grid = new QGridLayout();
  auto* section_label = new QLabel(QStringLiteral("Width"), &dialog);
  auto* section_spin = new QDoubleSpinBox(&dialog);
  section_grid->addWidget(section_label, 0, 0);
  section_grid->addWidget(section_spin, 0, 1);
  root->addLayout(section_grid);

  auto* tabs = new QTabWidget(&dialog);
  auto* scroll = new QScrollArea(tabs);
  auto* page = new QWidget(scroll);
  auto* page_form = new QFormLayout(page);
  auto* page_spin = new QSpinBox(page);
  page_form->addRow(QStringLiteral("Spacing:"), page_spin);
  scroll->setWidget(page);
  tabs->addTab(scroll, QStringLiteral("Page"));
  root->addWidget(tabs);

  patchy::ui::install_scrub_labels_in(&dialog);
  for (auto* spin : {static_cast<QAbstractSpinBox*>(direct), static_cast<QAbstractSpinBox*>(slider_row_spin),
                     static_cast<QAbstractSpinBox*>(stepped), static_cast<QAbstractSpinBox*>(grid_spin),
                     static_cast<QAbstractSpinBox*>(caption_spin), static_cast<QAbstractSpinBox*>(page_spin),
                     static_cast<QAbstractSpinBox*>(range_minimum), static_cast<QAbstractSpinBox*>(section_spin)}) {
    CHECK(spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  }
  CHECK(section_label->cursor().shape() == Qt::SizeHorCursor);
  CHECK(section_caption->cursor().shape() != Qt::SizeHorCursor);
  for (auto* spin : {button_then_spin, range_maximum}) {
    CHECK(!spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  }
  CHECK(grid_label->cursor().shape() == Qt::SizeHorCursor);
  CHECK(caption->cursor().shape() == Qt::SizeHorCursor);
  CHECK(color_label->cursor().shape() != Qt::SizeHorCursor);
  CHECK(range_to->cursor().shape() != Qt::SizeHorCursor);
  auto* slider_row_label = qobject_cast<QLabel*>(form->labelForField(slider_row_spin->parentWidget()));
  CHECK(slider_row_label != nullptr);
  CHECK(slider_row_label->cursor().shape() == Qt::SizeHorCursor);

  // Installing again is a no-op, and a drag on a form-row label moves its field.
  patchy::ui::install_scrub_labels_in(&dialog);
  dialog.show();
  QApplication::processEvents();
  const auto origin = slider_row_label->rect().center();
  const int drag_start = QApplication::startDragDistance();
  send_mouse(*slider_row_label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*slider_row_label, QEvent::MouseMove, origin + QPoint(drag_start + 12, 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*slider_row_label, QEvent::MouseButtonRelease, origin + QPoint(drag_start + 12, 0), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(slider_row_spin->value() == 40 + drag_start + 12);

  // A drag inside the field itself scrubs the same way.
  auto* direct_editor = direct->findChild<QLineEdit*>();
  CHECK(direct_editor != nullptr);
  const QPoint field_origin(direct_editor->width() / 2, direct_editor->height() / 2);
  send_mouse(*direct_editor, QEvent::MouseButtonPress, field_origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*direct_editor, QEvent::MouseMove, field_origin + QPoint(drag_start + 7, 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*direct_editor, QEvent::MouseButtonRelease, field_origin + QPoint(drag_start + 7, 0), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(direct->value() == 20 + drag_start + 7);
}

// The Layers panel's "Opacity:" prefix scrubs the field (GitHub issue 46): the
// drag is one undo entry, a plain click on the prefix focuses the field with the
// number selected, and a drag that starts on the number scrubs too, also as one
// undo entry.
void ui_layer_opacity_prefix_scrub_is_one_undo_entry() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Scrub Target",
                      solid_pixels(90, 70, patchy::PixelFormat::rgba8(), QColor(30, 150, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{35, 25, 90, 70});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Opacity Scrub"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->force_refresh();
  QApplication::processEvents();

  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->value() == 100);
  CHECK(opacity_spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  auto* fill_spin = window.findChild<QSpinBox*>(QStringLiteral("layerFillOpacitySpin"));
  CHECK(fill_spin != nullptr);
  CHECK(fill_spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  auto* editor = opacity_spin->findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  const QPoint prefix_point(editor->textMargins().left() + 4, editor->height() / 2);
  CHECK(editor->cursorPositionAt(prefix_point) < static_cast<int>(opacity_spin->prefix().length()));
  const int drag_start = QApplication::startDragDistance();

  send_mouse(*editor, QEvent::MouseButtonPress, prefix_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseMove, prefix_point + QPoint(-(drag_start + 30), 0), Qt::NoButton,
             Qt::LeftButton);
  CHECK(opacity_spin->value() == 100 - (drag_start + 30));
  send_mouse(*editor, QEvent::MouseMove, prefix_point + QPoint(-30, 0), Qt::NoButton, Qt::LeftButton);
  CHECK(opacity_spin->value() == 70);
  send_mouse(*editor, QEvent::MouseButtonRelease, prefix_point + QPoint(-30, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(opacity_spin->value() == 70);
  CHECK(!editor->hasSelectedText());

  auto* edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  QElapsedTimer wait_for_apply;
  wait_for_apply.start();
  while (std::abs(edited_layer->opacity() - 0.7F) > 0.001F && wait_for_apply.elapsed() < 900) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(std::abs(edited_layer->opacity() - 0.7F) <= 0.001F);
  auto* undo = require_action_by_text(window, QStringLiteral("Undo"));
  CHECK(undo->isEnabled());
  undo->trigger();
  QApplication::processEvents();
  edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 1.0F) <= 0.001F);
  CHECK(!undo->isEnabled());
  CHECK(opacity_spin->value() == 100);

  // A plain click on the prefix: focus with the number selected, no value change.
  send_mouse(*editor, QEvent::MouseButtonPress, prefix_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseButtonRelease, prefix_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(opacity_spin->value() == 100);
  CHECK(editor->hasSelectedText());

  // The prefix click entered editing, so a drag on the number now selects text and
  // changes nothing; once focus leaves, a drag on the number scrubs. Toolbar spin boxes
  // right-align their text, so the number's x comes from the editor's own cursor
  // geometry rather than from the left margin.
  editor->deselect();
  QPoint number_point(-1, editor->height() / 2);
  for (int x = 0; x < editor->width() && number_point.x() < 0; ++x) {
    if (editor->cursorPositionAt(QPoint(x, number_point.y())) == static_cast<int>(opacity_spin->prefix().length()) + 1) {
      number_point.setX(x + 2);
    }
  }
  CHECK(number_point.x() >= 0);
  CHECK(editor->cursorPositionAt(number_point) >= static_cast<int>(opacity_spin->prefix().length()));
  send_mouse(*editor, QEvent::MouseButtonPress, number_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseMove, number_point + QPoint(drag_start + 20, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseButtonRelease, number_point + QPoint(drag_start + 20, 0), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(opacity_spin->value() == 100);
  CHECK(editor->hasSelectedText());
  opacity_spin->clearFocus();
  QApplication::processEvents();
  editor->deselect();
  send_mouse(*editor, QEvent::MouseButtonPress, number_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseMove, number_point + QPoint(-(drag_start + 20), 0), Qt::NoButton, Qt::LeftButton);
  CHECK(opacity_spin->value() == 100 - (drag_start + 20));
  send_mouse(*editor, QEvent::MouseButtonRelease, number_point + QPoint(-(drag_start + 20), 0), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  wait_for_apply.restart();
  const float dragged_opacity = static_cast<float>(100 - (drag_start + 20)) / 100.0F;
  edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  while (edited_layer != nullptr && std::abs(edited_layer->opacity() - dragged_opacity) > 0.001F &&
         wait_for_apply.elapsed() < 900) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  }
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - dragged_opacity) <= 0.001F);
  CHECK(undo->isEnabled());
  undo->trigger();
  QApplication::processEvents();
  edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 1.0F) <= 0.001F);
  CHECK(!undo->isEnabled());
  CHECK(opacity_spin->value() == 100);
}

// Every blend-mode combo steps with Left/Right like Up/Down, closed and with
// the list open, as the Opacity and Fill fields beside it do.
void ui_blend_mode_combos_step_with_left_and_right_arrows() {
  QComboBox combo;
  patchy::ui::add_blend_mode_items(&combo);
  patchy::ui::add_blend_mode_items(&combo);  // a refill must not install the filter twice
  combo.clear();
  patchy::ui::add_blend_mode_items(&combo);
  combo.show();
  QApplication::processEvents();
  CHECK(combo.currentText() == QStringLiteral("Normal"));
  send_key(combo, Qt::Key_Right);
  CHECK(combo.currentText() == QStringLiteral("Dissolve"));
  send_key(combo, Qt::Key_Right);
  CHECK(combo.currentText() == QStringLiteral("Darken"));
  send_key(combo, Qt::Key_Left);
  CHECK(combo.currentText() == QStringLiteral("Dissolve"));
  send_key(combo, Qt::Key_Left);
  send_key(combo, Qt::Key_Left);  // clamps at the first mode like Up does
  CHECK(combo.currentIndex() == 0);
  send_key(combo, Qt::Key_Right, Qt::ControlModifier);  // modified arrows keep their own meaning
  CHECK(combo.currentIndex() == 0);

  combo.showPopup();
  QApplication::processEvents();
  auto* view = combo.view();
  CHECK(view != nullptr);
  CHECK(view->currentIndex().row() == 0);
  send_key(*view, Qt::Key_Right);
  send_key(*view, Qt::Key_Right);
  CHECK(view->currentIndex().row() == 2);
  send_key(*view, Qt::Key_Left);
  CHECK(view->currentIndex().row() == 1);
  CHECK(combo.currentIndex() == 0);  // the open list only moves its highlight
  combo.hidePopup();
  QApplication::processEvents();
}

// Stepping the Layers-panel blend mode is one undo entry per run, like an
// Opacity drag, and the run ends on a pause or at the next separate edit.
void ui_layer_blend_mode_steps_coalesce_into_one_undo_entry() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Blend Target",
                      solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(30, 150, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{20, 20, 60, 40});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Blend Steps"));
  show_window(window);
  QApplication::processEvents();

  using Access = patchy::ui::MainWindowTestAccess;
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  CHECK(blend_combo->currentText() == QStringLiteral("Normal"));
  const auto blend_of_target = [&window, layer_id] {
    const auto* target = std::as_const(Access::document(window)).find_layer(layer_id);
    CHECK(target != nullptr);
    return target->blend_mode();
  };

  const auto depth_before = Access::active_session_undo_depth(window);
  send_key(*blend_combo, Qt::Key_Right);
  send_key(*blend_combo, Qt::Key_Right);
  send_key(*blend_combo, Qt::Key_Down);
  CHECK(blend_combo->currentText() == QStringLiteral("Multiply"));
  CHECK(blend_of_target() == patchy::BlendMode::Multiply);
  CHECK(Access::active_session_undo_depth(window) == depth_before + 1);
  CHECK(Access::layer_blend_edit_pending(window));

  // A pause ends the run: the next step is a new entry.
  QElapsedTimer pause;
  pause.start();
  while (Access::layer_blend_edit_pending(window) && pause.elapsed() < 3000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(!Access::layer_blend_edit_pending(window));
  send_key(*blend_combo, Qt::Key_Right);
  CHECK(blend_of_target() == patchy::BlendMode::ColorBurn);
  CHECK(Access::active_session_undo_depth(window) == depth_before + 2);

  // A separate edit also ends the run instead of folding into it.
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(opacity_spin != nullptr);
  opacity_spin->setValue(50);
  QApplication::processEvents();
  CHECK(!Access::layer_blend_edit_pending(window));
  CHECK(Access::active_session_undo_depth(window) == depth_before + 3);

  Access::undo(window);  // opacity
  QApplication::processEvents();
  CHECK(blend_of_target() == patchy::BlendMode::ColorBurn);
  Access::undo(window);  // the single step after the pause
  QApplication::processEvents();
  CHECK(blend_of_target() == patchy::BlendMode::Multiply);
  Access::undo(window);  // the whole first run at once
  QApplication::processEvents();
  CHECK(blend_of_target() == patchy::BlendMode::Normal);
  CHECK(blend_combo->currentText() == QStringLiteral("Normal"));
}

namespace blend_rows {

struct FolderDocument {
  patchy::LayerId pixel_id{};
  patchy::LayerId folder_id{};
};

// A white Background, a pixel layer, and a Pass Through folder holding one child.
FolderDocument add_folder_document(patchy::ui::MainWindow& window, const QString& title) {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  FolderDocument ids;
  patchy::Layer pixel(document.allocate_layer_id(), "Paint",
                      solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(30, 150, 220, 255)));
  ids.pixel_id = pixel.id();
  document.add_layer(std::move(pixel));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.set_blend_mode(patchy::BlendMode::PassThrough);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Inside",
                                 solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(220, 60, 40, 255))));
  ids.folder_id = folder.id();
  document.add_layer(std::move(folder));
  document.set_active_layer(ids.folder_id);
  window.add_document_session(std::move(document), title);
  return ids;
}

patchy::BlendMode blend_of(patchy::ui::MainWindow& window, patchy::LayerId id) {
  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  return layer == nullptr ? patchy::BlendMode::Normal : layer->blend_mode();
}

void activate(patchy::ui::MainWindow& window, patchy::LayerId id) {
  patchy::ui::MainWindowTestAccess::document(window).set_active_layer(id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
}

void wait_for_blend_run_end(patchy::ui::MainWindow& window) {
  QElapsedTimer pause;
  pause.start();
  while (patchy::ui::MainWindowTestAccess::layer_blend_edit_pending(window) && pause.elapsed() < 3000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(!patchy::ui::MainWindowTestAccess::layer_blend_edit_pending(window));
}

// The second line of the layer's row ("Multiply  100%").
QString row_details(patchy::ui::MainWindow& window, patchy::LayerId id) {
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  for (int index = 0; layer_list != nullptr && index < layer_list->count(); ++index) {
    auto* item = layer_list->item(index);
    if (item->data(Qt::UserRole).toULongLong() != static_cast<qulonglong>(id)) {
      continue;
    }
    auto* row = layer_list->itemWidget(item);
    auto* details = row == nullptr ? nullptr : row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
    return details == nullptr ? QString() : details->text();
  }
  CHECK(false);
  return {};
}

// Opens Blending Options on the active layer and hands its blend combo to `inspect`.
void with_layer_style_blend_combo(patchy::ui::MainWindow& window, const std::function<void(QComboBox&)>& inspect,
                                  bool accept) {
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* blend =
        dialog == nullptr ? nullptr : dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
    CHECK(blend != nullptr);
    if (blend != nullptr) {
      inspect(*blend);
      saw_dialog = true;
    }
    if (dialog != nullptr) {
      if (accept) {
        dialog->accept();
      } else {
        dialog->reject();
      }
    }
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

}  // namespace blend_rows

// Pass Through is offered first, and only, for a folder (GitHub issue 235): a folder
// shows it, can be set back to it from another mode, and undo restores each step.
void ui_layer_folder_blend_mode_offers_pass_through() {
  patchy::ui::MainWindow window;
  const auto ids = blend_rows::add_folder_document(window, QStringLiteral("Pass Through"));
  show_window(window);
  QApplication::processEvents();
  using Access = patchy::ui::MainWindowTestAccess;
  const auto pass_through = static_cast<int>(patchy::BlendMode::PassThrough);
  const auto normal = static_cast<int>(patchy::BlendMode::Normal);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  if (blend_combo == nullptr) {
    return;
  }
  // Review capture: the folder's open blend list with the panel above it.
  blend_combo->showPopup();
  QApplication::processEvents();
  if (auto* popup = blend_combo->view() != nullptr ? blend_combo->view()->window() : nullptr; popup != nullptr) {
    auto* panel = window.findChild<QWidget*>(QStringLiteral("layersDock"));
    save_stacked_artifact("issue_235_folder_blend_list",
                          {panel != nullptr ? panel->grab(QRect(0, 0, panel->width(), 120)).toImage() : QImage(),
                           popup->grab().toImage()});
  }
  blend_combo->hidePopup();
  QApplication::processEvents();
  CHECK(blend_combo->itemData(0).toInt() == pass_through);
  CHECK(blend_combo->currentText() == QStringLiteral("Pass Through"));

  const auto depth_before = Access::active_session_undo_depth(window);
  blend_combo->setCurrentIndex(blend_combo->findData(normal));
  QApplication::processEvents();
  CHECK(blend_rows::blend_of(window, ids.folder_id) == patchy::BlendMode::Normal);
  blend_rows::wait_for_blend_run_end(window);
  blend_combo->setCurrentIndex(blend_combo->findData(pass_through));
  QApplication::processEvents();
  CHECK(blend_rows::blend_of(window, ids.folder_id) == patchy::BlendMode::PassThrough);
  CHECK(Access::active_session_undo_depth(window) == depth_before + 2);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(blend_rows::blend_of(window, ids.folder_id) == patchy::BlendMode::Normal);
  CHECK(blend_combo->currentText() == QStringLiteral("Normal"));
  CHECK(blend_combo->itemData(0).toInt() == pass_through);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(blend_rows::blend_of(window, ids.folder_id) == patchy::BlendMode::PassThrough);
  CHECK(blend_combo->currentText() == QStringLiteral("Pass Through"));

  // The folder's Blending Options list it first and show it.
  blend_rows::with_layer_style_blend_combo(
      window,
      [](QComboBox& blend) {
        const auto folder_mode = static_cast<int>(patchy::BlendMode::PassThrough);
        CHECK(blend.itemData(0).toInt() == folder_mode);
        CHECK(blend.currentData().toInt() == folder_mode);
      },
      false);

  // A pixel layer is offered it neither in the panel nor in its Blending Options.
  blend_rows::activate(window, ids.pixel_id);
  CHECK(blend_combo->findData(pass_through) < 0);
  CHECK(blend_combo->currentText() == QStringLiteral("Normal"));
  CHECK(blend_combo->itemData(0).toInt() == normal);
  blend_rows::with_layer_style_blend_combo(
      window,
      [](QComboBox& blend) { CHECK(blend.findData(static_cast<int>(patchy::BlendMode::PassThrough)) < 0); }, false);
  // Back on the folder, the panel offers it again.
  blend_rows::activate(window, ids.folder_id);
  CHECK(blend_combo->itemData(0).toInt() == pass_through);
  CHECK(blend_combo->currentText() == QStringLiteral("Pass Through"));
}

// A blend mode change shows in the layer's row at once (GitHub issue 236), from the
// panel combo, the Layer Style dialog and a script alike, for a folder and a layer.
void ui_layer_row_shows_blend_mode_change_at_once() {
  patchy::ui::MainWindow window;
  const auto ids = blend_rows::add_folder_document(window, QStringLiteral("Row Blend"));
  show_window(window);
  QApplication::processEvents();
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  if (blend_combo == nullptr) {
    return;
  }
  CHECK(blend_rows::row_details(window, ids.folder_id).startsWith(QStringLiteral("Pass Through  100%")));

  blend_combo->setCurrentIndex(blend_combo->findData(static_cast<int>(patchy::BlendMode::Multiply)));
  QApplication::processEvents();
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersDock")); panel != nullptr) {
    save_widget_artifact("issue_236_row_after_mode_change", *panel);
  }
  CHECK(blend_rows::row_details(window, ids.folder_id).startsWith(QStringLiteral("Multiply  100%")));
  blend_rows::wait_for_blend_run_end(window);

  blend_rows::activate(window, ids.pixel_id);
  blend_combo->setCurrentIndex(blend_combo->findData(static_cast<int>(patchy::BlendMode::Screen)));
  QApplication::processEvents();
  CHECK(blend_rows::row_details(window, ids.pixel_id).startsWith(QStringLiteral("Screen  100%")));
  blend_rows::wait_for_blend_run_end(window);

  blend_rows::with_layer_style_blend_combo(
      window,
      [](QComboBox& blend) { blend.setCurrentIndex(blend.findData(static_cast<int>(patchy::BlendMode::Overlay))); },
      true);
  CHECK(blend_rows::blend_of(window, ids.pixel_id) == patchy::BlendMode::Overlay);
  CHECK(blend_rows::row_details(window, ids.pixel_id).startsWith(QStringLiteral("Overlay  100%")));

  CHECK(patchy::test::ui::scripting_support::run_script(
      window, QStringLiteral("app.activeDocument.activeLayer.blendMode = 'difference';")));
  QApplication::processEvents();
  CHECK(blend_rows::blend_of(window, ids.pixel_id) == patchy::BlendMode::Difference);
  CHECK(blend_rows::row_details(window, ids.pixel_id).startsWith(QStringLiteral("Difference  100%")));
}

void ui_collapsed_right_docks_keep_deep_layer_rows_readable() {
  patchy::Document document(128, 128, patchy::PixelFormat::rgba8());
  patchy::Layer root(document.allocate_layer_id(), "Root Folder", patchy::LayerKind::Group);
  auto* current = &root;
  // Deep enough that the indented rows overflow the measured minimum dock
  // width (which grew past the old 280 px floor) and force horizontal scroll.
  for (int depth = 1; depth <= 14; ++depth) {
    current->add_child(
        patchy::Layer(document.allocate_layer_id(), "Nested Folder " + std::to_string(depth), patchy::LayerKind::Group));
    current = &current->children().back();
  }
  auto deep_pixels = solid_pixels(128, 128, patchy::PixelFormat::rgba8(), QColor(20, 120, 220, 255));
  patchy::Layer deep_layer(document.allocate_layer_id(), "Deep Paint Layer With Long Name", std::move(deep_pixels));
  const auto deep_layer_id = deep_layer.id();
  current->add_child(std::move(deep_layer));
  for (int index = 1; index <= 24; ++index) {
    current->add_child(patchy::Layer(document.allocate_layer_id(), "Deep Scroll Filler " + std::to_string(index),
                                     solid_pixels(128, 128, patchy::PixelFormat::rgba8(),
                                                  QColor(35, 70 + (index * 7) % 120, 160, 255))));
  }
  document.add_layer(std::move(root));
  document.set_active_layer(deep_layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Deep Layers"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layer_list != nullptr);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));

  auto* deep_item = require_layer_item(*layer_list, QStringLiteral("Deep Paint Layer With Long Name"));
  layer_list->scrollToItem(deep_item, QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  auto* row_widget = layer_list->itemWidget(deep_item);
  CHECK(row_widget != nullptr);
  auto* visibility = row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* thumbnail = row_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* horizontal_scroll = layer_list->horizontalScrollBar();
  CHECK(visibility != nullptr);
  CHECK(thumbnail != nullptr);
  CHECK(horizontal_scroll != nullptr);
  CHECK(horizontal_scroll->maximum() > horizontal_scroll->minimum());
  horizontal_scroll->setValue(horizontal_scroll->minimum());
  QApplication::processEvents();
  const auto initial_visibility_left = visibility->mapTo(layer_list->viewport(), QPoint()).x();
  const auto initial_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(initial_visibility_left >= 0);
  CHECK(initial_visibility_left < layer_list->viewport()->width() / 2);
  horizontal_scroll->setValue(std::clamp(initial_thumbnail_right - (layer_list->viewport()->width() - 16),
                                        horizontal_scroll->minimum(), horizontal_scroll->maximum()));
  QApplication::processEvents();
  const auto scrolled_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(scrolled_thumbnail_right <= layer_list->viewport()->width() - 16);

  auto scrollbar_ancestor = [](QWidget* widget) -> QScrollBar* {
    for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
      if (auto* scroll = qobject_cast<QScrollBar*>(current); scroll != nullptr) {
        return scroll;
      }
    }
    return nullptr;
  };
  auto scrollbar_slider_rect = [](QScrollBar* scroll) {
    QStyleOptionSlider option;
    option.initFrom(scroll);
    option.orientation = scroll->orientation();
    option.minimum = scroll->minimum();
    option.maximum = scroll->maximum();
    option.singleStep = scroll->singleStep();
    option.pageStep = scroll->pageStep();
    option.sliderPosition = scroll->sliderPosition();
    option.sliderValue = scroll->value();
    option.upsideDown = scroll->invertedAppearance();
    return scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, scroll);
  };
  auto check_scrollbar_hit_target = [&](QScrollBar* scroll) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = scroll->orientation() == Qt::Vertical
                           ? QPoint(std::clamp(scroll->width() - 2, slider.left(), slider.right()),
                                    slider.center().y())
                           : QPoint(slider.center().x(),
                                    std::clamp(scroll->height() - 2, slider.top(), slider.bottom()));
    auto* hit = layer_list->childAt(scroll->mapTo(layer_list, start));
    CHECK(scrollbar_ancestor(hit) == scroll);
  };
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());

  auto clear_layer_row_masks = [&] {
    for (int row_index = 0; row_index < layer_list->count(); ++row_index) {
      if (auto* row = layer_list->itemWidget(layer_list->item(row_index)); row != nullptr) {
        row->clearMask();
      }
    }
  };
  auto send_mouse_at_global = [](QWidget& widget, QEvent::Type type, QPoint global_position,
                                 Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, widget.mapFromGlobal(global_position), global_position, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
  };
  auto drag_scrollbar_through_current_hit = [&](QScrollBar* scroll, int pixels) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = slider.center();
    const auto start_global = scroll->mapToGlobal(start);
    auto* hit = layer_list->childAt(layer_list->mapFromGlobal(start_global));
    CHECK(hit != nullptr);
    const auto before = scroll->value();
    const auto end_global =
        start_global + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0));
    if (hit == scroll) {
      send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseMove, start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels)
                                                                                             : QPoint(pixels, 0)),
                 Qt::NoButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseButtonRelease,
                 start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0)),
                 Qt::LeftButton, Qt::NoButton);
    } else {
      send_mouse_at_global(*hit, QEvent::MouseButtonPress, start_global, Qt::LeftButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseMove, end_global, Qt::NoButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseButtonRelease, end_global, Qt::LeftButton, Qt::NoButton);
    }
    return scroll->value() > before;
  };
  QMessageBox warning(QMessageBox::Warning, QStringLiteral("Warning"), QStringLiteral("Warning"), QMessageBox::Ok,
                      &window);
  QTimer::singleShot(0, &warning, [&] { warning.accept(); });
  warning.exec();
  clear_layer_row_masks();
  CHECK(drag_scrollbar_through_current_hit(layer_list->verticalScrollBar(), 48));
  CHECK(drag_scrollbar_through_current_hit(layer_list->horizontalScrollBar(), 48));
  QEvent activate_event(QEvent::WindowActivate);
  QApplication::sendEvent(layer_list, &activate_event);
  QApplication::processEvents();
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());
  save_widget_artifact("ui_collapsed_right_docks_deep_layer_rows", window);
}

void ui_right_dock_panels_expand_within_window_height() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Worst-case palette: 256 colors is 22 swatch rows, which must scroll
  // inside the panel instead of forcing the dock (and the window) taller.
  patchy::DocumentPaletteEditing editing;
  for (int index = 0; index < 256; ++index) {
    editing.palette.colors.push_back(patchy::RgbColor{static_cast<std::uint8_t>(index),
                                                      static_cast<std::uint8_t>(255 - index),
                                                      static_cast<std::uint8_t>(index / 2)});
  }
  editing.palette_revision = 902;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();

  for (const auto* name : {"historyDockCollapseButton", "propertiesDockCollapseButton",
                           "infoDockCollapseButton", "paletteDockCollapseButton"}) {
    auto* toggle = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(toggle != nullptr);
    toggle->setChecked(true);
    QApplication::processEvents();
  }
  QApplication::processEvents();
  QApplication::processEvents();

  // Expanding every panel must leave the window minimum bounded (the palette
  // grid alone used to add ~440px of hard minimum, pushing past 1300): the
  // all-expanded window still fits a 1080p work area, and each expand
  // releases its height demand so nothing pins the minimum above the floors.
  CHECK(window.height() <= 950);
  CHECK(window.minimumSizeHint().height() <= 950);

  auto* palette_scroll = window.findChild<QScrollArea*>(QStringLiteral("paletteScrollArea"));
  CHECK(palette_scroll != nullptr);
  CHECK(palette_scroll->verticalScrollBar()->maximum() > 0);

  int docks_height = 0;
  for (const auto* name : {"layersDock", "historyDock", "propertiesDock", "infoDock", "paletteDock"}) {
    const auto* dock = patchy::ui::MainWindowTestAccess::panel_section(window, QLatin1String(name));
    CHECK(dock != nullptr);
    CHECK(dock->height() <= window.height());
    docks_height += dock->height();
  }
  CHECK(docks_height <= window.height());
}

// The right column's floors (the Layers panel's 300 px, the expand demands)
// never hold the window's minimum height above the screen: a 1366x768 laptop
// at a 125% scale on Wayland had the column's bottom panels off-screen, and
// expanding one grew the window further out of view (GitHub issue 108). The
// offscreen platform's 600 px screen stands in for that laptop.
void ui_right_dock_floors_fit_the_screen_height() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layers_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("layersDock"));
  CHECK(layers_dock != nullptr);
  if (layers_dock == nullptr) {
    return;
  }
  // The offscreen screen is tall enough for the untouched floors; stand in a
  // screen a little shorter than the window's minimum, as the laptop was.
  const int real_budget = patchy::ui::MainWindowTestAccess::available_window_height(window);
  CHECK(real_budget > 0);
  CHECK(window.minimumSizeHint().height() <= real_budget);
  // The tab group's 300 px floor.
  const int layers_floor_before = layers_dock->minimumHeight();
  CHECK(layers_floor_before > 200);
  const int budget = window.minimumSizeHint().height() - 30;
  patchy::ui::MainWindowTestAccess::set_available_window_height(window, budget);
  patchy::ui::MainWindowTestAccess::fit_right_dock_floors_to_screen(window);
  QApplication::processEvents();
  CHECK(window.minimumSizeHint().height() <= budget);
  // The floor only drops as far as the budget needs; the panel's own layout
  // minimum still holds.
  CHECK(layers_dock->minimumHeight() < layers_floor_before);
  CHECK(layers_dock->minimumSizeHint().height() > 0);
  auto* layers_panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
  CHECK(layers_panel != nullptr && layers_panel->layout() != nullptr &&
        layers_panel->minimumHeight() >= layers_panel->layout()->minimumSize().height());

  // At the screen's height, expanding Properties takes its room from the
  // Layers panel rather than growing the window: the expand demand yields to
  // the budget before the layout pass sees it.
  window.resize(window.width(), budget);
  QApplication::processEvents();
  CHECK(window.height() == budget);
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  CHECK(properties_toggle != nullptr);
  if (properties_toggle != nullptr) {
    properties_toggle->setChecked(true);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(window.height() == budget);
    CHECK(window.minimumSizeHint().height() <= budget);
    const auto* properties_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("propertiesDock"));
    CHECK(properties_dock != nullptr && properties_dock->height() > 40);
    CHECK(layers_dock->height() < layers_floor_before);
  }
  // Once every panel is open the column's content minimums alone (the Info
  // panel's four lines, the Properties scroll area) can exceed such a short
  // screen; then the window grows only to that irreducible minimum, never
  // because of a floor the fit could have lowered.
  for (const auto* name : {"infoDockCollapseButton", "historyDockCollapseButton", "paletteDockCollapseButton"}) {
    auto* toggle = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(toggle != nullptr);
    if (toggle == nullptr) {
      continue;
    }
    toggle->setChecked(true);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(window.height() <= std::max(budget, window.minimumSizeHint().height()));
    CHECK(layers_dock->minimumHeight() <= 300);
  }
  for (const auto* name : {"layersDock", "historyDock", "propertiesDock", "infoDock", "paletteDock"}) {
    const auto* dock = patchy::ui::MainWindowTestAccess::panel_section(window, QLatin1String(name));
    CHECK(dock != nullptr);
    if (dock != nullptr) {
      CHECK(QRect(dock->mapTo(&window, QPoint(0, 0)), dock->size()).bottom() < window.height());
    }
  }
  // Collapsing and re-expanding Layers re-asserts its 300 px floor, which must
  // yield again instead of pushing the window off the screen.
  auto* layers_toggle = window.findChild<QToolButton*>(QStringLiteral("layersDockCollapseButton"));
  CHECK(layers_toggle != nullptr);
  if (layers_toggle != nullptr) {
    const int height_before = window.height();
    layers_toggle->setChecked(false);
    QApplication::processEvents();
    layers_toggle->setChecked(true);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(window.height() <= std::max(height_before, window.minimumSizeHint().height()));
    CHECK(layers_dock->minimumHeight() <= 300);
  }
  save_widget_artifact("ui_right_dock_floors_fit_screen", window);
  patchy::ui::MainWindowTestAccess::set_available_window_height(window, 0);
}

void ui_collapsed_right_docks_have_uniform_title_height() {
  patchy::ui::MainWindow window;
  show_window(window);
  // History/Properties/Info/Palette all start collapsed: each section is exactly its
  // title row (min == max), with no dead band under the header, at the column width.
  int first_height = -1;
  for (const auto* prefix : {"history", "properties", "info", "palette"}) {
    auto* dock = patchy::ui::MainWindowTestAccess::panel_section(window, QLatin1String(prefix) + QStringLiteral("Dock"));
    auto* title = window.findChild<QWidget*>(QLatin1String(prefix) + QStringLiteral("DockTitle"));
    CHECK(dock != nullptr && title != nullptr);
    if (dock == nullptr || title == nullptr) {
      continue;
    }
    CHECK(dock->minimumHeight() == dock->maximumHeight());
    CHECK(dock->height() <= title->sizeHint().height() + 4);
    if (first_height < 0) {
      first_height = dock->height();
    }
    CHECK(dock->height() == first_height);
    CHECK(dock->width() == patchy::ui::MainWindowTestAccess::panel_column(window)->width());
  }
  save_widget_artifact("ui_collapsed_right_docks_uniform_titles", window);
}

// GitHub issue 101: Layers, Channels and Paths share one tab group, which keeps the height
// of its tallest member. Collapsing the shown tab collapses the whole group, so no dead
// band stays under the strip; expanding any of them expands the group again.
void ui_tabbed_right_dock_group_collapses_as_one() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* group = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("layersDock"));
  auto* layers_toggle = window.findChild<QToolButton*>(QStringLiteral("layersDockCollapseButton"));
  auto* channels_toggle = window.findChild<QToolButton*>(QStringLiteral("channelsDockCollapseButton"));
  auto* paths_toggle = window.findChild<QToolButton*>(QStringLiteral("pathsDockCollapseButton"));
  auto* tab_bar = column->tab_bar(QStringLiteral("layersDock"));
  CHECK(group != nullptr && tab_bar != nullptr);
  CHECK(layers_toggle != nullptr && channels_toggle != nullptr && paths_toggle != nullptr);
  if (group == nullptr || tab_bar == nullptr || layers_toggle == nullptr || channels_toggle == nullptr ||
      paths_toggle == nullptr) {
    return;
  }
  CHECK(group == patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("channelsDock")));
  CHECK(group == patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("pathsDock")));
  CHECK(layers_toggle->isChecked() && channels_toggle->isChecked() && paths_toggle->isChecked());
  const int expanded_height = group->height();
  auto* layers_title = window.findChild<QWidget*>(QStringLiteral("layersDockTitle"));

  layers_toggle->setChecked(false);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(!channels_toggle->isChecked() && !paths_toggle->isChecked());
  CHECK(group->minimumHeight() == group->maximumHeight());
  CHECK(group->height() <= tab_bar->sizeHint().height() + layers_title->sizeHint().height() + 4);
  CHECK(group->height() < expanded_height);

  // Expanding a hidden tab's toggle (or the shown one's) brings the group back.
  channels_toggle->setChecked(true);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layers_toggle->isChecked() && paths_toggle->isChecked());
  CHECK(group->maximumHeight() > tab_bar->sizeHint().height() + layers_title->sizeHint().height());
  save_widget_artifact("ui_tabbed_right_dock_group_collapses_as_one", window);
}

void ui_short_panel_scroll_bar_drags_by_handle() {
  // A squeezed panel's scrollbar is short. Without box properties on the
  // scroll bar's QSS widget rule, the groove rect came from the native
  // style, whose arrow-button reservation made the groove smaller than the
  // styled handle; the drag span went negative and every handle drag
  // snapped the value to the minimum.
  patchy::ui::MainWindow window;
  show_window(window);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  for (const auto* name : {"historyDockCollapseButton", "propertiesDockCollapseButton",
                           "infoDockCollapseButton", "paletteDockCollapseButton"}) {
    auto* toggle = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(toggle != nullptr);
    toggle->setChecked(true);
    QApplication::processEvents();
  }
  QApplication::processEvents();
  QApplication::processEvents();

  auto* properties_scroll = window.findChild<QScrollArea*>(QStringLiteral("propertiesScrollArea"));
  CHECK(properties_scroll != nullptr);
  auto* vbar = properties_scroll->verticalScrollBar();
  CHECK(vbar->isVisible());
  CHECK(vbar->maximum() > vbar->minimum());
  CHECK(vbar->height() < 120);

  vbar->setValue((vbar->minimum() + vbar->maximum()) / 2);
  QApplication::processEvents();
  QStyleOptionSlider option;
  option.initFrom(vbar);
  option.orientation = vbar->orientation();
  option.minimum = vbar->minimum();
  option.maximum = vbar->maximum();
  option.singleStep = vbar->singleStep();
  option.pageStep = vbar->pageStep();
  option.sliderPosition = vbar->sliderPosition();
  option.sliderValue = vbar->value();
  option.upsideDown = vbar->invertedAppearance();
  const auto slider =
      vbar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, vbar);
  const auto groove =
      vbar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarGroove, vbar);
  CHECK(slider.isValid());
  // The groove must fit the styled handle or the drag span is negative; the
  // widget rule's margin: 0 is what makes QSS own the groove rect.
  CHECK(groove.height() >= slider.height());

  const auto before = vbar->value();
  const auto start = slider.center();
  send_mouse(*vbar, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*vbar, QEvent::MouseMove, start + QPoint(0, 10), Qt::NoButton, Qt::LeftButton);
  send_mouse(*vbar, QEvent::MouseButtonRelease, start + QPoint(0, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(vbar->value() > before);
}

// The column drags narrower than its panels' content (GitHub issue 96): the
// width the user picks is kept and, below the Layers panel's compact width
// (every row wrapped, GitHub issue 199), each panel scrolls sideways, with the
// scroll bar at the bottom, instead of the drag stopping at the content width.
void ui_right_dock_drags_below_content_minimum_with_scroll() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();
  QWidget* layers_dock = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* history_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("historyDock"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(layers_dock != nullptr && history_dock != nullptr && delete_button != nullptr);
  // The Layers panel's own scroll area: findChild from the column finds a shallower one first.
  auto* overflow = window.findChild<QWidget*>(QStringLiteral("layersDock"))
                       ->findChild<QScrollArea*>(QStringLiteral("rightDockOverflowScroll"));
  auto* handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(overflow != nullptr && handle != nullptr);
  const auto content_minimum = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  CHECK(layers_dock->width() == content_minimum);
  CHECK(!overflow->horizontalScrollBar()->isVisible());

  // Drag right, past the content width and the compact width.
  const auto start = handle->rect().center();
  const auto narrow_width = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window) - 40;
  const auto narrow_delta = content_minimum - narrow_width;
  CHECK(narrow_width >= 120 && narrow_delta > 0);
  // One move: the splitter handle travels with the drag, so a second move in its own
  // coordinates would overshoot.
  QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(handle, start + QPoint(narrow_delta, 0));
  QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + QPoint(narrow_delta, 0));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layers_dock->width() == narrow_width);
  CHECK(history_dock->width() == narrow_width);
  auto* bar = overflow->horizontalScrollBar();
  CHECK(bar->isVisible());
  CHECK(bar->maximum() > 0);
  // Scrolled to the end, the footer's last button sits whole inside the
  // viewport, above the scroll bar.
  bar->setValue(bar->maximum());
  QApplication::processEvents();
  const QRect delete_rect(delete_button->mapTo(overflow->viewport(), QPoint()), delete_button->size());
  CHECK(overflow->viewport()->rect().contains(delete_rect));
  save_widget_artifact("ui_right_dock_narrowed_with_scroll", *layers_dock);

  // Widening back past the content width drops the scroll bar again.
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, content_minimum + 30);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layers_dock->width() == content_minimum + 30);
  CHECK(!overflow->horizontalScrollBar()->isVisible());
}

// A handle drag that ends near the content-fit width snaps onto it, the width
// that just fits the Layers footer, from either side.
void ui_right_dock_drag_snaps_to_content_width() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();
  QWidget* layers_dock = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(layers_dock != nullptr);
  auto* handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(handle != nullptr);
  const auto content_minimum = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  const auto drag_by = [&](int dx) {
    const auto start = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + QPoint(dx, 0));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + QPoint(dx, 0));
    QApplication::processEvents();
    QApplication::processEvents();
  };

  // Well clear of the snap: the width follows the pointer.
  drag_by(-60);
  CHECK(layers_dock->width() == content_minimum + 60);
  // Back to 7 px wider than the content width: snaps onto it.
  drag_by(53);
  CHECK(layers_dock->width() == content_minimum);
  // Narrower than the content, then back to 6 px short of it: snaps again.
  drag_by(50);
  CHECK(layers_dock->width() == content_minimum - 50);
  drag_by(-44);
  CHECK(layers_dock->width() == content_minimum);
  // Just past the snap distance stays where it was dropped.
  drag_by(11);
  CHECK(layers_dock->width() == content_minimum - 11);
}

// The column width handle stays usable while a non-modal preview dialog
// (Invert here; filters, adjustments and G'MIC share the path) is open.
void ui_right_dock_drag_works_during_preview_dialog() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();
  QWidget* layers_dock = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(layers_dock != nullptr);
  auto* handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(handle != nullptr);
  const auto width_before = layers_dock->width();
  bool saw_dialog = false;
  int width_during = 0;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog") || !widget->isVisible()) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      saw_dialog = true;
      CHECK(dialog->windowModality() == Qt::NonModal);
      // Real window-system input, so a modal block would swallow it.
      const auto start = handle->rect().center();
      QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
      QTest::mouseMove(handle, start + QPoint(60, 0));
      QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + QPoint(60, 0));
      QApplication::processEvents();
      width_during = layers_dock->width();
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  CHECK(saw_dialog);
  CHECK(width_during == width_before - 60);
}

void ui_right_dock_separator_drags_between_docks() {
  // The dividers between the column's sections stay draggable: expanded sections
  // keep low floors, so a divider keeps travel either way.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  CHECK(history_toggle != nullptr);
  history_toggle->setChecked(true);
  QApplication::processEvents();
  QApplication::processEvents();

  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* layers_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("layersDock"));
  auto* history_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("historyDock"));
  CHECK(column != nullptr && layers_dock != nullptr && history_dock != nullptr);
  auto* divider = column->splitter()->handle(1);
  CHECK(divider != nullptr && divider->height() >= 5);
  CHECK(divider->geometry().top() > layers_dock->geometry().bottom());
  CHECK(divider->geometry().bottom() < history_dock->geometry().top());
  const auto layers_before = layers_dock->height();
  const auto start = divider->rect().center();
  QTest::mousePress(divider, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(divider, start + QPoint(0, -24));
  QTest::mouseMove(divider, start + QPoint(0, -48));
  QTest::mouseRelease(divider, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, -48));
  QApplication::processEvents();
  CHECK(layers_dock->height() < layers_before);
}

// Stands in for KDE's Breeze: polishing a widget installs an event filter on it,
// which Qt puts ahead of the filters already there. The reinstall is capped so
// a regression fails the test instead of hanging the suite.
class FilterInstallingStyle : public QProxyStyle {
public:
  using QProxyStyle::polish;
  void polish(QWidget* widget) override {
    QProxyStyle::polish(widget);
    ++polish_count;
    if (polish_count < 200) {
      widget->removeEventFilter(&probe_);
      widget->installEventFilter(&probe_);
    }
  }
  int polish_count = 0;

private:
  QObject probe_;
};

void ui_layer_action_button_foreign_drag_never_repolishes_in_filter() {
  // GitHub issue 62: the layer action buttons see drag events that carry no
  // layers (on Wayland the old dock drags were real QDrags; any foreign drag
  // still is). Repolishing the button from inside its event filter froze
  // Patchy under Breeze, whose polish() reinstalls filters and sends Qt's
  // filter loop back to Patchy's.
  FilterInstallingStyle style;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  CHECK(button != nullptr);
  CHECK(button->property("layerDropAction").isValid());
  button->setEnabled(true);
  button->setStyle(&style);
  QApplication::processEvents();
  style.polish_count = 0;

  QMimeData dock_drag;
  dock_drag.setData(QStringLiteral("application/x-patchy-foreign-drag"), QByteArray("1"));
  QDragEnterEvent enter(button->rect().center(), Qt::MoveAction, &dock_drag, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(button, &enter);
  QDragMoveEvent move(button->rect().center(), Qt::MoveAction, &dock_drag, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(button, &move);
  QDragLeaveEvent leave;
  QApplication::sendEvent(button, &leave);
  CHECK(style.polish_count == 0);
  CHECK(!button->property("layerDropActive").toBool());
  QApplication::processEvents();
  CHECK(style.polish_count == 0);
  button->setStyle(nullptr);
}

void ui_menu_disabled_items_render_grayed() {
  // The app stylesheet styles QMenu::item text, so without an explicit :disabled rule
  // disabled entries rendered in the same bright color as enabled ones and were only
  // discoverable by their refusal to highlight.
  patchy::ui::MainWindow window;
  show_window(window);

  QMenu menu(&window);
  auto* enabled_action = menu.addAction(QStringLiteral("Enabled entry"));
  auto* disabled_action = menu.addAction(QStringLiteral("Disabled entry"));
  disabled_action->setEnabled(false);
  menu.popup(window.mapToGlobal(QPoint(60, 60)));
  QApplication::processEvents();

  const auto image = menu.grab().toImage();
  const auto enabled_rect = menu.actionGeometry(enabled_action);
  const auto disabled_rect = menu.actionGeometry(disabled_action);
  menu.close();
  QApplication::processEvents();

  const QColor enabled_text(0xe6, 0xe6, 0xe6);
  const QColor disabled_text(0x73, 0x73, 0x73);
  CHECK(count_pixels_close(image, enabled_rect, enabled_text, 24) > 10);   // bright enabled label
  CHECK(count_pixels_close(image, disabled_rect, enabled_text, 24) == 0);  // no bright pixels on the disabled row
  CHECK(count_pixels_close(image, disabled_rect, disabled_text, 24) > 10);  // grayed label
}

}  // namespace

std::vector<patchy::test::TestCase> canvas_view_tools_tests() {
  return {
      {"ui_startup_restores_stored_brush_and_gradient_settings",
       ui_startup_restores_stored_brush_and_gradient_settings},
      {"ui_canvas_wheel_matches_photoshop_navigation", ui_canvas_wheel_matches_photoshop_navigation},
      {"ui_canvas_wheel_zoom_mode_zooms_at_cursor", ui_canvas_wheel_zoom_mode_zooms_at_cursor},
      {"ui_canvas_trackpad_scroll_pans_both_axes", ui_canvas_trackpad_scroll_pans_both_axes},
#if PATCHY_HAVE_QT_DBUS
      {"ui_portal_color_pick_request_path_follows_the_portal_convention",
       ui_portal_color_pick_request_path_follows_the_portal_convention},
      {"ui_portal_color_pick_reads_a_delivered_response", ui_portal_color_pick_reads_a_delivered_response},
#endif
      {"ui_own_window_color_sample_reads_widget_without_screen_grab",
       ui_own_window_color_sample_reads_widget_without_screen_grab},
      {"ui_canvas_trackpad_scroll_ignored_during_pointer_gesture",
       ui_canvas_trackpad_scroll_ignored_during_pointer_gesture},
      {"ui_status_bar_zoom_percent_box_edits_zoom", ui_status_bar_zoom_percent_box_edits_zoom},
      {"ui_status_bar_zoom_box_wheel_scrub_and_buttons", ui_status_bar_zoom_box_wheel_scrub_and_buttons},
      {"ui_status_bar_zoom_box_fits_large_values", ui_status_bar_zoom_box_fits_large_values},
      {"ui_status_bar_document_info_fits_at_1366", ui_status_bar_document_info_fits_at_1366},
      {"ui_status_bar_document_info_shows_document_facts", ui_status_bar_document_info_shows_document_facts},
      {"ui_status_bar_document_info_drops_parts_when_narrow",
       ui_status_bar_document_info_drops_parts_when_narrow},
      {"ui_status_bar_document_info_follows_active_document",
       ui_status_bar_document_info_follows_active_document},
      {"ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels",
       ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels},
      {"ui_image_resize_recenters_view_and_zoom_double_click_shows_document",
       ui_image_resize_recenters_view_and_zoom_double_click_shows_document},
      {"ui_size_dialogs_open_with_width_focused_and_selected",
       ui_size_dialogs_open_with_width_focused_and_selected},
      {"ui_zoom_preset_recovers_parked_view", ui_zoom_preset_recovers_parked_view},
      {"ui_canvas_resize_keeps_document_centered_and_view_center_stable",
       ui_canvas_resize_keeps_document_centered_and_view_center_stable},
      {"ui_open_shows_small_images_at_actual_size_and_fits_large_ones",
       ui_open_shows_small_images_at_actual_size_and_fits_large_ones},
      {"ui_window_resize_panels_tabs_and_floats_keep_small_document_centered",
       ui_window_resize_panels_tabs_and_floats_keep_small_document_centered},
      {"ui_canvas_focus_in_restores_tool_cursor", ui_canvas_focus_in_restores_tool_cursor},
      {"ui_max_brush_uses_overlay_cursor", ui_max_brush_uses_overlay_cursor},
      {"ui_canvas_pan_keeps_document_partly_visible", ui_canvas_pan_keeps_document_partly_visible},
      {"ui_canvas_scroll_bars_reflect_pan_range", ui_canvas_scroll_bars_reflect_pan_range},
      {"ui_canvas_hand_pan_updates_scroll_bars", ui_canvas_hand_pan_updates_scroll_bars},
      {"ui_canvas_scroll_bar_scrolls_view", ui_canvas_scroll_bar_scrolls_view},
      {"ui_canvas_scroll_bars_follow_zoom_and_resize", ui_canvas_scroll_bars_follow_zoom_and_resize},
      {"ui_canvas_backdrop_context_menu_sets_color", ui_canvas_backdrop_context_menu_sets_color},
      {"ui_zoom_tool_context_menu_offers_view_zoom_commands", ui_zoom_tool_context_menu_offers_view_zoom_commands},
      {"ui_canvas_fractional_zoom_paints_to_document_edge", ui_canvas_fractional_zoom_paints_to_document_edge},
      {"ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp",
       ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp},
      {"ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp",
       ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp},
      {"ui_canvas_display_smoothing_rule_pins_zoom_bands", ui_canvas_display_smoothing_rule_pins_zoom_bands},
      {"ui_canvas_fractional_zoom_smooths_between_whole_zooms",
       ui_canvas_fractional_zoom_smooths_between_whole_zooms},
      {"ui_canvas_styled_text_at_150_percent_has_no_repeated_columns",
       ui_canvas_styled_text_at_150_percent_has_no_repeated_columns},
      {"ui_canvas_pixel_grid_shows_from_600_percent", ui_canvas_pixel_grid_shows_from_600_percent},
      {"ui_canvas_pixel_grid_lands_on_pixel_boundaries", ui_canvas_pixel_grid_lands_on_pixel_boundaries},
      {"ui_canvas_pixel_grid_capture_light_and_dark_areas", ui_canvas_pixel_grid_capture_light_and_dark_areas},
      {"ui_pixel_grid_toggle_persists_and_scripts", ui_pixel_grid_toggle_persists_and_scripts},
      {"ui_zoomed_out_canvas_uses_downsampled_display_mip",
       ui_zoomed_out_canvas_uses_downsampled_display_mip},
      {"ui_shape_flyout_and_zoom_tool_work", ui_shape_flyout_and_zoom_tool_work},
      {"ui_zoom_tool_scrubby_option_persists_and_reaches_canvas",
       ui_zoom_tool_scrubby_option_persists_and_reaches_canvas},
      {"ui_zoom_tool_scrubby_drag_zooms_live_around_press_point",
       ui_zoom_tool_scrubby_drag_zooms_live_around_press_point},
      {"ui_zoom_tool_direction_buttons_set_click_direction", ui_zoom_tool_direction_buttons_set_click_direction},
      {"ui_zoom_steps_follow_photoshop_ladder", ui_zoom_steps_follow_photoshop_ladder},
      {"ui_zoom_options_bar_view_buttons_set_view", ui_zoom_options_bar_view_buttons_set_view},
      {"ui_hand_options_bar_shows_zoom_view_buttons", ui_hand_options_bar_shows_zoom_view_buttons},
      {"ui_hand_tool_ctrl_alt_click_zooms_and_drag_pans", ui_hand_tool_ctrl_alt_click_zooms_and_drag_pans},
      {"ui_stamp_and_gradient_flyouts_swap_tools", ui_stamp_and_gradient_flyouts_swap_tools},
      {"ui_tool_cycle_hotkeys_walk_each_flyout", ui_tool_cycle_hotkeys_walk_each_flyout},
      {"ui_shape_flyout_leads_with_rect_and_cycles_in_menu_order",
       ui_shape_flyout_leads_with_rect_and_cycles_in_menu_order},
      {"ui_tool_flyout_double_click_opens_menu", ui_tool_flyout_double_click_opens_menu},
      {"ui_tool_flyout_right_click_opens_menu", ui_tool_flyout_right_click_opens_menu},
      {"ui_tool_flyout_opens_beside_its_button_with_palette_size_icons",
       ui_tool_flyout_opens_beside_its_button_with_palette_size_icons},
      {"ui_tool_flyout_marks_the_tool_its_button_shows", ui_tool_flyout_marks_the_tool_its_button_shows},
      {"ui_tool_palette_icons_render_sheet", ui_tool_palette_icons_render_sheet},
      {"ui_filled_shape_preview_clears_after_commit", ui_filled_shape_preview_clears_after_commit},
      {"ui_toolbar_spin_boxes_select_all_on_focus", ui_toolbar_spin_boxes_select_all_on_focus},
      {"ui_options_bar_tracks_active_tool", ui_options_bar_tracks_active_tool},
      {"ui_active_tool_and_options_restore_on_next_launch", ui_active_tool_and_options_restore_on_next_launch},
      {"ui_tool_settings_fall_back_on_missing_or_malformed_keys",
       ui_tool_settings_fall_back_on_missing_or_malformed_keys},
      {"ui_fill_tool_tolerance_and_contiguous_persist_across_documents",
       ui_fill_tool_tolerance_and_contiguous_persist_across_documents},
      {"ui_fill_opacity_accepts_zero_and_fills_nothing", ui_fill_opacity_accepts_zero_and_fills_nothing},
      {"ui_fill_tool_click_honors_tolerance_contiguous_and_opacity",
       ui_fill_tool_click_honors_tolerance_contiguous_and_opacity},
      {"ui_fill_of_wand_selection_with_many_spans_is_fast", ui_fill_of_wand_selection_with_many_spans_is_fast},
      {"ui_gradient_toolbar_preset_popup_applies_stops", ui_gradient_toolbar_preset_popup_applies_stops},
      {"ui_options_bar_spinboxes_fit_widest_value", ui_options_bar_spinboxes_fit_widest_value},
      {"ui_options_bar_keeps_one_height_across_tools", ui_options_bar_keeps_one_height_across_tools},
      {"ui_selection_options_bars_group_their_controls", ui_selection_options_bars_group_their_controls},
      {"ui_shape_options_bars_group_their_controls", ui_shape_options_bars_group_their_controls},
      {"ui_crop_options_bar_groups_its_controls", ui_crop_options_bar_groups_its_controls},
      {"ui_options_bars_follow_the_separator_rules", ui_options_bars_follow_the_separator_rules},
      {"ui_swap_dimension_buttons_show_a_themed_full_size_glyph",
       ui_swap_dimension_buttons_show_a_themed_full_size_glyph},
      {"ui_swap_dimension_buttons_sit_evenly_between_width_and_height",
       ui_swap_dimension_buttons_sit_evenly_between_width_and_height},
      {"ui_options_bar_drops_duplicate_sliders_before_wrapping",
       ui_options_bar_drops_duplicate_sliders_before_wrapping},
      {"ui_options_bar_checkboxes_paint_without_a_frame", ui_options_bar_checkboxes_paint_without_a_frame},
      {"ui_line_options_bar_fits_one_row_with_room_to_spare", ui_line_options_bar_fits_one_row_with_room_to_spare},
      {"ui_text_options_bar_size_shows_photoshop_precision", ui_text_options_bar_size_shows_photoshop_precision},
      {"ui_options_bar_spinboxes_show_their_extremes_unclipped",
       ui_options_bar_spinboxes_show_their_extremes_unclipped},
      {"ui_right_docks_collapse_layers_show_metadata_and_info_updates",
       ui_right_docks_collapse_layers_show_metadata_and_info_updates},
      {"ui_options_bar_labels_drop_the_trailing_colon", ui_options_bar_labels_drop_the_trailing_colon},
      {"ui_options_bar_label_scrub_changes_spin_value", ui_options_bar_label_scrub_changes_spin_value},
      {"ui_options_bar_field_drag_scrubs_and_click_edits", ui_options_bar_field_drag_scrubs_and_click_edits},
      {"ui_scrub_gap_and_modifiers_follow_the_zone_rules", ui_scrub_gap_and_modifiers_follow_the_zone_rules},
      {"ui_brush_mode_combo_lists_paint_modes_and_persists", ui_brush_mode_combo_lists_paint_modes_and_persists},
      {"ui_dialog_scrub_labels_pair_every_row_shape", ui_dialog_scrub_labels_pair_every_row_shape},
      {"ui_layer_opacity_prefix_scrub_is_one_undo_entry", ui_layer_opacity_prefix_scrub_is_one_undo_entry},
      {"ui_layer_opacity_control_defers_slow_rendering_and_undoes_once",
       ui_layer_opacity_control_defers_slow_rendering_and_undoes_once},
      {"ui_blend_mode_combos_step_with_left_and_right_arrows", ui_blend_mode_combos_step_with_left_and_right_arrows},
      {"ui_layer_blend_mode_steps_coalesce_into_one_undo_entry",
       ui_layer_blend_mode_steps_coalesce_into_one_undo_entry},
      {"ui_layer_folder_blend_mode_offers_pass_through", ui_layer_folder_blend_mode_offers_pass_through},
      {"ui_layer_row_shows_blend_mode_change_at_once", ui_layer_row_shows_blend_mode_change_at_once},
      {"ui_collapsed_right_docks_keep_deep_layer_rows_readable",
       ui_collapsed_right_docks_keep_deep_layer_rows_readable},
      {"ui_right_dock_panels_expand_within_window_height", ui_right_dock_panels_expand_within_window_height},
      {"ui_right_dock_floors_fit_the_screen_height", ui_right_dock_floors_fit_the_screen_height},
      {"ui_collapsed_right_docks_have_uniform_title_height",
       ui_collapsed_right_docks_have_uniform_title_height},
      {"ui_tabbed_right_dock_group_collapses_as_one", ui_tabbed_right_dock_group_collapses_as_one},
      {"ui_right_dock_separator_drags_between_docks", ui_right_dock_separator_drags_between_docks},
      {"ui_right_dock_drags_below_content_minimum_with_scroll",
       ui_right_dock_drags_below_content_minimum_with_scroll},
      {"ui_right_dock_drag_snaps_to_content_width", ui_right_dock_drag_snaps_to_content_width},
      {"ui_right_dock_drag_works_during_preview_dialog", ui_right_dock_drag_works_during_preview_dialog},
      {"ui_short_panel_scroll_bar_drags_by_handle", ui_short_panel_scroll_bar_drags_by_handle},
      {"ui_layer_action_button_foreign_drag_never_repolishes_in_filter",
       ui_layer_action_button_foreign_drag_never_repolishes_in_filter},
      {"ui_menu_disabled_items_render_grayed", ui_menu_disabled_items_render_grayed},
  };
}
