// Adjustment dialog presets and layout (#588 Hue/Saturation, #589 Levels, #591 Color
// Balance, #592 Curves, #528 Black & White): the shared user-preset store and buttons,
// the shipped presets, the painted slider tracks, the Levels eyedroppers and the dialog
// layouts.

#include "core/adjustment_layer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/photoshop_adjustment_presets.hpp"
#include "test_harness.hpp"
#include "ui/adjustment_presets.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_manager.hpp"
#include "unicode_path_names.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::AdjustmentPresetKind;

void clear_user_presets(AdjustmentPresetKind kind) {
  for (const auto& preset : patchy::ui::user_adjustment_presets(kind)) {
    QFile::remove(preset.path);
  }
}

QString artifact_path(const QString& name) {
  ensure_artifact_dir();
  return QFileInfo(QStringLiteral("test-artifacts/") + name).absoluteFilePath();
}

template <typename T>
T* child(QWidget& parent, const char* name) {
  auto* found = parent.findChild<T*>(QString::fromLatin1(name));
  CHECK(found != nullptr);
  return found;
}

// Picks a combo entry by its data, as a user's choice does.
void choose(QComboBox& combo, const QString& data) {
  const auto index = combo.findData(data);
  CHECK(index >= 0);
  if (index >= 0) {
    combo.setCurrentIndex(index);
    Q_EMIT combo.activated(index);
  }
}

void answer_preset_name(const QString& name) {
  when_dialog_visible(QStringLiteral("adjustmentPresetNameDialog"), [name](QDialog& dialog) {
    auto* prompt = qobject_cast<QInputDialog*>(&dialog);
    CHECK(prompt != nullptr);
    if (prompt != nullptr) {
      prompt->setTextValue(name);
    }
    dialog.accept();
  });
}

void answer_yes(const QString& object_name) {
  when_dialog_visible(object_name, [](QDialog& dialog) {
    auto* box = qobject_cast<QMessageBox*>(&dialog);
    CHECK(box != nullptr);
    if (box == nullptr) {
      dialog.reject();
      return;
    }
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::YesRole || box->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
}

// Answers the next file dialog as the Curves .acv test does: armed right before the click.
// A dialog that stays open after the answer is rejected, so a test can never hang on it.
void answer_file(const QString& object_name, const QString& path) {
  QTimer::singleShot(0, guarded([object_name, path] {
    auto* files = qobject_cast<QFileDialog*>(find_top_level_dialog(object_name));
    CHECK(files != nullptr);
    if (files == nullptr) {
      return;
    }
    files->selectFile(path);
    static_cast<QDialog*>(files)->accept();
    if (files->isVisible()) {
      std::cerr << "file dialog " << object_name.toStdString() << " stayed open after selecting "
                << path.toStdString() << '\n';
      CHECK(false);
      files->reject();
    }
  }));
}

QColor property_color(const QWidget& widget, const char* name) {
  return widget.property(name).value<QColor>();
}

// Both schemes, through the themed capture helper.
void capture_both_schemes(QDialog& dialog, const std::string& name) {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    process_events_for(60);
    save_widget_artifact(name + (scheme == patchy::ui::ColorSchemePreference::Dark ? "_dark" : "_light"), dialog);
  }
}

// The tab-focus chain from `first`, `count` widgets long.
std::vector<QWidget*> focus_chain(QWidget* first, int count) {
  std::vector<QWidget*> chain{first};
  for (auto* widget = first->nextInFocusChain(); widget != nullptr && widget != first &&
                                                 static_cast<int>(chain.size()) < count;
       widget = widget->nextInFocusChain()) {
    // A spin box's own line edit follows it in the chain; the spin box stands for both.
    if ((widget->focusPolicy() & Qt::TabFocus) != 0 && widget->isVisibleTo(first->window()) &&
        qobject_cast<QAbstractSpinBox*>(widget->parentWidget()) == nullptr) {
      chain.push_back(widget);
    }
  }
  return chain;
}

QRect geometry_in(const QWidget& widget, const QWidget& ancestor) {
  return QRect(widget.mapTo(&ancestor, QPoint(0, 0)), widget.size());
}

// Save, Delete, Import and Export in one row under `above`, as wide as each other (within the
// 1 px a split leaves) and together spanning `above` edge to edge (#588 follow-up 2).
void check_preset_button_row_spans(QDialog& dialog, const char* prefix, const QWidget& above) {
  std::vector<QRect> rects;
  for (const auto* role : {"Save", "Delete", "Import", "Export"}) {
    const auto name = std::string(prefix) + role + "PresetButton";
    auto* button = child<QPushButton>(dialog, name.c_str());
    if (button == nullptr) {
      return;
    }
    rects.push_back(geometry_in(*button, dialog));
  }
  const auto target = geometry_in(above, dialog);
  std::cerr << prefix << " preset row: target " << target.left() << ".." << target.right() << ", buttons";
  for (const auto& rect : rects) {
    std::cerr << ' ' << rect.left() << '+' << rect.width();
  }
  std::cerr << '\n';
  CHECK(rects.front().left() == target.left());
  CHECK(rects.back().right() == target.right());
  for (std::size_t index = 0; index < rects.size(); ++index) {
    CHECK(std::abs(rects[index].width() - rects.front().width()) <= 1);
    CHECK(rects[index].top() > target.bottom());
    CHECK(rects[index].top() == rects.front().top());
    if (index > 0) {
      const auto gap = rects[index].left() - rects[index - 1].right() - 1;
      CHECK(gap >= 0 && gap <= 8);
    }
  }
}

constexpr std::array<patchy::ui::ColorSchemePreference, 2> kBothSchemes{patchy::ui::ColorSchemePreference::Dark,
                                                                        patchy::ui::ColorSchemePreference::Light};

// The Hue/Saturation preset combo's left edge in its dialog, Dark then Light: the indent the
// Levels preset combo shares (#589 follow-up 2).
std::array<int, 2> hue_saturation_preset_left_edges(QWidget& parent) {
  std::array<int, 2> lefts{-1, -1};
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyHueSaturationDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* presets = child<QComboBox>(*dialog, "hueSaturationPresetCombo");
    CHECK(presets != nullptr);
    for (std::size_t index = 0; presets != nullptr && index < kBothSchemes.size(); ++index) {
      ColorSchemeRestorer restore(kBothSchemes[index]);
      process_events_for(60);
      lefts[index] = geometry_in(*presets, *dialog).left();
    }
    dialog->reject();
  }));
  CHECK(!patchy::ui::request_hue_saturation_settings(&parent, {}).has_value());
  return lefts;
}

void ui_adjustment_presets_store_names_and_listing() {
  const auto folder = patchy::ui::user_adjustment_presets_directory(AdjustmentPresetKind::HueSaturation);
  CHECK(folder.endsWith(QStringLiteral("/hue-saturation")));
  CHECK(folder.startsWith(QDir(qEnvironmentVariable("PATCHY_ADJUSTMENT_PRESETS_DIR")).absolutePath()));
  CHECK(patchy::ui::user_adjustment_presets_directory(AdjustmentPresetKind::Levels).endsWith(QStringLiteral("/levels")));
  CHECK(patchy::ui::user_adjustment_presets_directory(AdjustmentPresetKind::Curves).endsWith(QStringLiteral("/curves")));
  CHECK(patchy::ui::user_adjustment_presets_directory(AdjustmentPresetKind::BlackWhite)
            .endsWith(QStringLiteral("/black-white")));
  const auto file_of = [](const QString& name) {
    return QFileInfo(patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::HueSaturation, name)).fileName();
  };
  CHECK(file_of(QStringLiteral("a/b:c?d")) == QStringLiteral("a_b_c_d.ahu"));
  CHECK(file_of(QStringLiteral("  .Warm. ")) == QStringLiteral("Warm.ahu"));
  CHECK(file_of(QStringLiteral("con")) == QStringLiteral("_con.ahu"));
  CHECK(patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::HueSaturation, QStringLiteral(" . ")).isEmpty());
  CHECK(QFileInfo(patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::BlackWhite, QStringLiteral("x")))
            .suffix() == QStringLiteral("blw"));

  // A Unicode name stores, lists and reads back.
  clear_user_presets(AdjustmentPresetKind::HueSaturation);
  QDir().mkpath(folder);
  const auto unicode_name = patchy::ui::to_qstring(std::filesystem::path(patchy::test::kUnicodeCombinedStem));
  const auto path = patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::HueSaturation, unicode_name);
  patchy::HueSaturationAdjustment settings;
  settings.hue_shift = 33;
  patchy::ahu::write_file(patchy::ui::to_filesystem_path(path), settings);
  CHECK(QFile(QDir(folder).filePath(QStringLiteral("not a preset.txt"))).open(QIODevice::WriteOnly));
  const auto listed = patchy::ui::user_adjustment_presets(AdjustmentPresetKind::HueSaturation);
  CHECK(listed.size() == 1);
  if (listed.size() == 1) {
    CHECK(listed.front().name == QFileInfo(path).completeBaseName());
    CHECK(patchy::ahu::read_file(patchy::ui::to_filesystem_path(listed.front().path)).hue_shift == 33);
  }
  QFile::remove(QDir(folder).filePath(QStringLiteral("not a preset.txt")));
  clear_user_presets(AdjustmentPresetKind::HueSaturation);

  // Shipped presets: permanent ids, distinct, our own values.
  QStringList ids;
  for (const auto& preset : patchy::ui::builtin_hue_saturation_presets()) {
    ids << QString::fromLatin1(preset.id);
  }
  CHECK(ids.size() == 15 && ids.front() == QStringLiteral("hue_saturation.default"));
  CHECK(ids.removeDuplicates() == 0);
  QStringList level_ids;
  for (const auto& preset : patchy::ui::builtin_levels_presets()) {
    level_ids << QString::fromLatin1(preset.id);
    // None repeats a value set Photoshop ships (input 30/1.00/225, gamma 1.25 alone, ...).
    CHECK(!(preset.settings.black_input == 30 && preset.settings.white_input == 225));
    CHECK(preset.settings.gamma_percent != 125 && preset.settings.gamma_percent != 75 &&
          preset.settings.gamma_percent != 160);
  }
  CHECK(level_ids.size() == 20 && level_ids.front() == QStringLiteral("levels.default"));
  CHECK(level_ids.removeDuplicates() == 0);
}

void ui_adjustment_presets_hue_saturation_presets_tracks_and_buttons() {
  clear_user_presets(AdjustmentPresetKind::HueSaturation);
  const auto export_path = artifact_path(QStringLiteral("ui-hue-export.ahu"));
  const auto import_path = artifact_path(QStringLiteral("Imported Hue.ahu"));
  QFile::remove(export_path);
  patchy::HueSaturationAdjustment imported;
  imported.bands[0].hue_shift = 50;
  patchy::ahu::write_file(patchy::ui::to_filesystem_path(import_path), imported);

  // Owned by a parent carrying the app style sheet, as in the app, so it opens at its real size.
  const auto themed_parent = make_themed_dialog_parent();
  auto& parent = *themed_parent;
  parent.show();
  QApplication::processEvents();
  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyHueSaturationDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* presets = child<QComboBox>(*dialog, "hueSaturationPresetCombo");
    auto* range = child<QComboBox>(*dialog, "hueSaturationRangeCombo");
    auto* colorize = child<QCheckBox>(*dialog, "hueSaturationColorizeCheck");
    auto* swatch = child<QWidget>(*dialog, "hueSaturationColorizeSwatch");
    auto* hue = child<QSpinBox>(*dialog, "hueSaturationHueSpin");
    auto* saturation = child<QSpinBox>(*dialog, "hueSaturationSaturationSpin");
    auto* hue_slider = child<QSlider>(*dialog, "hueSaturationHueSlider");
    auto* saturation_slider = child<QSlider>(*dialog, "hueSaturationSaturationSlider");
    auto* lightness_slider = child<QSlider>(*dialog, "hueSaturationLightnessSlider");
    auto* save = child<QPushButton>(*dialog, "hueSaturationSavePresetButton");
    auto* remove = child<QPushButton>(*dialog, "hueSaturationDeletePresetButton");
    auto* import = child<QPushButton>(*dialog, "hueSaturationImportPresetButton");
    auto* export_button = child<QPushButton>(*dialog, "hueSaturationExportPresetButton");
    if (presets == nullptr || range == nullptr || colorize == nullptr || swatch == nullptr || hue == nullptr ||
        saturation == nullptr || hue_slider == nullptr || saturation_slider == nullptr || lightness_slider == nullptr ||
        save == nullptr || remove == nullptr || import == nullptr || export_button == nullptr) {
      dialog->reject();
      return;
    }
    // No Edit tabs: the range stays a combo (Seth's call on #588).
    CHECK(dialog->findChild<QTabBar*>() == nullptr);
    CHECK(presets->count() == 16);
    CHECK(presets->currentData().toString() == QStringLiteral("hue_saturation.default"));
    CHECK(!remove->isEnabled());
    process_events_for(60);

    // Follow-up layout (#588): the Preset and Edit combos and Colorize line up with the
    // tracks (5 px into each slider), a separator under the preset buttons, colons on the
    // slider labels, Reset at the right under the Lightness field, Preview on the OK line.
    auto* reset = child<QPushButton>(*dialog, "hueSaturationResetButton");
    auto* separator = child<QWidget>(*dialog, "hueSaturationPresetSeparator");
    auto* preview = child<QCheckBox>(*dialog, "hueSaturationPreviewCheck");
    auto* lightness = child<QSpinBox>(*dialog, "hueSaturationLightnessSpin");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    if (reset == nullptr || separator == nullptr || preview == nullptr || lightness == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }
    const auto track_left = geometry_in(*hue_slider, *dialog).left() + 5;
    CHECK(geometry_in(*range, *dialog).left() == track_left);
    CHECK(geometry_in(*presets, *dialog).left() == track_left);
    CHECK(geometry_in(*colorize, *dialog).left() == track_left);
    CHECK(geometry_in(*saturation_slider, *dialog).left() + 5 == track_left);
    CHECK(geometry_in(*separator, *dialog).top() > geometry_in(*export_button, *dialog).bottom());
    CHECK(geometry_in(*separator, *dialog).bottom() < geometry_in(*range, *dialog).top());
    CHECK(geometry_in(*presets, *dialog).bottom() < geometry_in(*save, *dialog).top());
    check_preset_button_row_spans(*dialog, "hueSaturation", *presets);
    CHECK(geometry_in(*reset, *dialog).top() > geometry_in(*lightness, *dialog).bottom());
    CHECK(std::abs(geometry_in(*reset, *dialog).right() - geometry_in(*lightness, *dialog).right()) <= 1);
    CHECK(std::abs(geometry_in(*reset, *dialog).center().y() - geometry_in(*colorize, *dialog).center().y()) <= 4);
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    CHECK(ok->width() == buttons->button(QDialogButtonBox::Cancel)->width());
    CHECK(std::abs(geometry_in(*preview, *dialog).center().y() - geometry_in(*ok, *dialog).center().y()) <= 4);
    CHECK(geometry_in(*preview, *dialog).right() < geometry_in(*ok, *dialog).left());
    for (const auto* name : {"Hue:", "Saturation:", "Lightness:"}) {
      bool found = false;
      for (auto* label : dialog->findChildren<QLabel*>()) {
        found = found || label->text() == QString::fromLatin1(name);
      }
      CHECK(found);
    }
    // Tab order follows the layout.
    const std::vector<QWidget*> order{presets,
                                      save,
                                      remove,
                                      import,
                                      export_button,
                                      range,
                                      hue_slider,
                                      hue,
                                      saturation_slider,
                                      saturation,
                                      lightness_slider,
                                      lightness,
                                      colorize,
                                      reset,
                                      preview,
                                      ok};
    const auto order_chain = focus_chain(presets, static_cast<int>(order.size()));
    if (order_chain != order) {
      std::cerr << "hue/saturation tab order:";
      for (auto* widget : order_chain) {
        std::cerr << ' ' << widget->metaObject()->className() << ':' << widget->objectName().toStdString();
      }
      std::cerr << '\n';
    }
    CHECK(order_chain == order);
    capture_both_schemes(*dialog, "ui_hue_saturation_presets_default");

    // An Edit entry with a value off zero shows an asterisk; Reset or zeroing it by hand
    // takes it away. The entries keep their indices.
    range->setCurrentIndex(1);
    hue->setValue(20);
    CHECK(range->itemText(1) == QStringLiteral("Reds*") && range->itemText(0) == QStringLiteral("Master"));
    range->setCurrentIndex(0);
    saturation->setValue(-10);
    CHECK(range->itemText(0) == QStringLiteral("Master*") && range->itemText(1) == QStringLiteral("Reds*"));
    capture_both_schemes(*dialog, "ui_hue_saturation_edit_asterisks");
    reset->click();
    CHECK(hue->value() == 0 && saturation->value() == 0 && range->itemText(0) == QStringLiteral("Master"));
    CHECK(range->itemText(1) == QStringLiteral("Reds*"));
    range->setCurrentIndex(1);
    CHECK(hue->value() == 20);
    hue->setValue(0);
    CHECK(range->itemText(1) == QStringLiteral("Reds"));
    CHECK(presets->currentData().toString() == QStringLiteral("hue_saturation.default"));
    // While Colorize is on, Reset restores the Colorize defaults and leaves Colorize on.
    colorize->setChecked(true);
    hue->setValue(200);
    saturation->setValue(70);
    reset->click();
    CHECK(colorize->isChecked() && hue->value() == 0 && saturation->value() == 25);
    colorize->setChecked(false);
    range->setCurrentIndex(0);

    // Master: the hue track is the wheel around red, saturation runs from gray.
    CHECK(std::abs(property_color(*hue_slider, "patchy.trackMiddle").hsvHue() - 0) <= 2);
    CHECK(property_color(*hue_slider, "patchy.trackStart").hsvHue() == 180);
    CHECK(property_color(*saturation_slider, "patchy.trackStart") == QColor(128, 128, 128));
    CHECK(property_color(*lightness_slider, "patchy.trackStart") == QColor(0, 0, 0));
    CHECK(property_color(*lightness_slider, "patchy.trackEnd") == QColor(255, 255, 255));

    // A range preset shows its range in Edit; the hue track centres on that range.
    choose(*presets, QStringLiteral("hue_saturation.autumn_leaves"));
    CHECK(range->currentIndex() == 3);  // Greens
    CHECK(hue->value() == -55 && saturation->value() == 35);
    CHECK(std::abs(property_color(*hue_slider, "patchy.trackMiddle").hsvHue() - 120) <= 2);
    CHECK(std::abs(property_color(*saturation_slider, "patchy.trackEnd").hsvHue() - 65) <= 2);
    capture_both_schemes(*dialog, "ui_hue_saturation_presets_greens");

    // A colorize preset: the swatch shows what a mid gray becomes.
    choose(*presets, QStringLiteral("hue_saturation.sepia_wash"));
    CHECK(colorize->isChecked() && swatch->isEnabled() && !range->isEnabled());
    CHECK(hue->value() == 32 && saturation->value() == 28);
    patchy::AdjustmentSettings sepia;
    sepia.kind = patchy::AdjustmentKind::HueSaturation;
    sepia.hue_saturation.colorize = true;
    sepia.hue_saturation.colorize_hue = 32;
    sepia.hue_saturation.colorize_saturation = 28;
    const auto mid = patchy::apply_adjustment_to_color(patchy::RgbColor{128, 128, 128}, sepia);
    CHECK(property_color(*swatch, "patchy.swatchColor") == QColor(mid.red, mid.green, mid.blue));
    capture_both_schemes(*dialog, "ui_hue_saturation_presets_colorize");

    // An edit is Custom; Save stores it as a user preset and selects it.
    saturation->setValue(29);
    CHECK(presets->currentData().toString() == QStringLiteral("custom"));
    answer_preset_name(QStringLiteral("My Sepia"));
    save->click();
    const auto saved_path =
        patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::HueSaturation, QStringLiteral("My Sepia"));
    CHECK(QFileInfo::exists(saved_path));
    CHECK(presets->currentData().toString() == QStringLiteral("user:My Sepia"));
    CHECK(remove->isEnabled());
    if (QFileInfo::exists(saved_path)) {
      const auto saved = patchy::ahu::read_file(patchy::ui::to_filesystem_path(saved_path));
      CHECK(saved.colorize && saved.colorize_hue == 32 && saved.colorize_saturation == 29);
    }
    choose(*presets, QStringLiteral("hue_saturation.default"));
    CHECK(!colorize->isChecked() && !remove->isEnabled());
    choose(*presets, QStringLiteral("user:My Sepia"));
    CHECK(colorize->isChecked() && saturation->value() == 29);

    // Export writes the current settings as .ahu.
    answer_file(QStringLiteral("hueSaturationPresetExportFileDialog"),
                export_path.left(export_path.size() - 4));
    export_button->click();
    CHECK(QFileInfo::exists(export_path));
    if (QFileInfo::exists(export_path)) {
      CHECK(patchy::ahu::read_file(patchy::ui::to_filesystem_path(export_path)).colorize_saturation == 29);
    }

    // Delete asks first, then removes the file and the entry.
    answer_yes(QStringLiteral("adjustmentPresetDeleteMessageBox"));
    remove->click();
    CHECK(!QFileInfo::exists(saved_path));
    CHECK(presets->findData(QStringLiteral("user:My Sepia")) < 0);
    CHECK(!remove->isEnabled());

    // Import adds the file under its name and applies it: a Reds edit.
    answer_file(QStringLiteral("hueSaturationPresetImportFileDialog"), import_path);
    import->click();
    CHECK(presets->currentData().toString() == QStringLiteral("user:Imported Hue"));
    CHECK(!colorize->isChecked() && range->currentIndex() == 1 && hue->value() == 50);
    checked = true;
    dialog->accept();
  }));
  const auto result = patchy::ui::request_hue_saturation_settings(&parent, {});
  CHECK(checked);
  CHECK(result.has_value() && result->bands[0].hue_shift == 50 && !result->colorize);
  clear_user_presets(AdjustmentPresetKind::HueSaturation);
}

void ui_adjustment_presets_levels_presets_fields_and_eyedroppers() {
  clear_user_presets(AdjustmentPresetKind::Levels);
  std::optional<patchy::ui::CurvesCanvasMode> mode;
  std::function<void(const patchy::ui::CurvesCanvasSample&)> sampler;
  patchy::ui::CurvesDialogHooks hooks;
  hooks.set_canvas_mode = [&](patchy::ui::CurvesCanvasMode next,
                              std::function<void(const patchy::ui::CurvesCanvasSample&)> callback) {
    mode = next;
    sampler = std::move(callback);
  };
  hooks.clear_canvas_mode = [&] {
    mode.reset();
    sampler = {};
  };
  const auto pick = [&](QColor color) {
    CHECK(static_cast<bool>(sampler));
    if (sampler) {
      patchy::ui::CurvesCanvasSample sample;
      sample.input_color = color;
      sample.gesture.phase = patchy::ui::CanvasReadPhase::Press;
      sampler(sample);
    }
  };

  // Owned by a parent carrying the app style sheet, as in the app, so it opens at its real size.
  const auto themed_parent = make_themed_dialog_parent();
  auto& parent = *themed_parent;
  parent.show();
  QApplication::processEvents();
  const auto hue_saturation_lefts = hue_saturation_preset_left_edges(parent);
  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* presets = child<QComboBox>(*dialog, "levelsPresetCombo");
    auto* tabs = child<QTabBar>(*dialog, "levelsChannelTabs");
    auto* black = child<QSpinBox>(*dialog, "levelsBlackInputSpin");
    auto* gamma = child<QSpinBox>(*dialog, "levelsGammaSpin");
    auto* white = child<QSpinBox>(*dialog, "levelsWhiteInputSpin");
    auto* black_output = child<QSpinBox>(*dialog, "levelsBlackOutputSpin");
    auto* white_output = child<QSpinBox>(*dialog, "levelsWhiteOutputSpin");
    auto* black_pick = child<QToolButton>(*dialog, "levelsBlackPointButton");
    auto* gray_pick = child<QToolButton>(*dialog, "levelsGrayPointButton");
    auto* white_pick = child<QToolButton>(*dialog, "levelsWhitePointButton");
    auto* auto_button = child<QPushButton>(*dialog, "levelsAutoButton");
    auto* save = child<QPushButton>(*dialog, "levelsSavePresetButton");
    auto* remove = child<QPushButton>(*dialog, "levelsDeletePresetButton");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    if (presets == nullptr || tabs == nullptr || black == nullptr || gamma == nullptr || white == nullptr ||
        black_output == nullptr || white_output == nullptr || black_pick == nullptr || gray_pick == nullptr ||
        white_pick == nullptr || auto_button == nullptr || save == nullptr || remove == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(presets->count() == 21);
    CHECK(presets->currentData().toString() == QStringLiteral("levels.default"));
    // The R, G and B tabs carry a channel swatch; RGB does not.
    CHECK(tabs->tabIcon(0).isNull() && !tabs->tabIcon(1).isNull() && !tabs->tabIcon(2).isNull() &&
          !tabs->tabIcon(3).isNull());
    // B:, G:, W: fields at about half the old width, each named in its tooltip.
    for (const auto* name : {"levelsBlackInputLabel", "levelsGammaLabel", "levelsWhiteInputLabel"}) {
      auto* label = child<QLabel>(*dialog, name);
      CHECK(label != nullptr && !label->toolTip().isEmpty());
    }
    CHECK(child<QLabel>(*dialog, "levelsBlackInputLabel")->text() == QStringLiteral("B:"));
    CHECK(!black->toolTip().isEmpty() && !gamma->toolTip().isEmpty() && !white->toolTip().isEmpty());
    CHECK(black->width() < black_output->width());
    // OK, Cancel and Auto are one width.
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    const auto ok_width = ok->width();
    CHECK(cancel->width() == ok_width && auto_button->width() == ok_width);
    // Layout as the other preset dialogs (#589 follow-up): the combo indented as far as the
    // Hue/Saturation one (follow-up 2), the four preset buttons under it sharing its width, a
    // separator under them, then the tabs; Auto on the "Input Levels:" line at the right;
    // Preview at the left of the OK/Cancel line, which is the bottom right.
    auto* separator = child<QWidget>(*dialog, "levelsPresetSeparator");
    auto* export_button = child<QPushButton>(*dialog, "levelsExportPresetButton");
    auto* preset_label = child<QLabel>(*dialog, "levelsPresetLabel");
    auto* input_label = child<QLabel>(*dialog, "levelsInputLabel");
    auto* preview = child<QCheckBox>(*dialog, "levelsPreviewCheck");
    if (separator == nullptr || export_button == nullptr || preset_label == nullptr || input_label == nullptr ||
        preview == nullptr) {
      dialog->reject();
      return;
    }
    const auto at = [&](const QWidget* widget) { return geometry_in(*widget, *dialog); };
    CHECK(at(presets).left() > at(preset_label).left() + preset_label->sizeHint().width() + 8);
    for (std::size_t index = 0; index < kBothSchemes.size(); ++index) {
      ColorSchemeRestorer restore(kBothSchemes[index]);
      process_events_for(60);
      std::cerr << "preset combo left: levels " << at(presets).left() << ", hue/saturation "
                << hue_saturation_lefts[index] << '\n';
      CHECK(hue_saturation_lefts[index] > 0);
      CHECK(std::abs(at(presets).left() - hue_saturation_lefts[index]) <= 1);
    }
    check_preset_button_row_spans(*dialog, "levels", *presets);
    CHECK(at(separator).top() > at(export_button).bottom());
    CHECK(at(separator).bottom() < at(tabs).top());
    CHECK(at(tabs).bottom() < at(input_label).top());
    CHECK(std::abs(at(auto_button).center().y() - at(input_label).center().y()) <= 4);
    CHECK(at(auto_button).left() > at(input_label).right());
    CHECK(std::abs(at(auto_button).right() - at(cancel).right()) <= 1);
    CHECK(at(ok).top() > at(white_output).bottom() && at(cancel).left() > at(ok).right());
    CHECK(std::abs(at(preview).center().y() - at(ok).center().y()) <= 4);
    CHECK(at(preview).right() < at(ok).left() && at(preview).left() < at(black_output).left() + 8);
    CHECK(dialog->width() - at(cancel).right() < 24 && dialog->height() - at(cancel).bottom() < 24);
    // Tab order follows the layout.
    const std::vector<QWidget*> order{presets,     save,       remove, child<QPushButton>(*dialog, "levelsImportPresetButton"),
                                      export_button, tabs,     auto_button, black,
                                      black_pick,  gamma,      gray_pick,   white,
                                      white_pick,  black_output, white_output, preview,
                                      ok};
    const auto order_chain = focus_chain(presets, static_cast<int>(order.size()));
    if (order_chain != order) {
      std::cerr << "levels tab order:";
      for (auto* widget : order_chain) {
        std::cerr << ' ' << widget->metaObject()->className() << ':' << widget->objectName().toStdString();
      }
      std::cerr << '\n';
    }
    CHECK(order_chain == order);
    // No channel has changed values: no asterisks.
    CHECK(tabs->tabText(0) == QStringLiteral("RGB") && tabs->tabText(1) == QStringLiteral("Red") &&
          tabs->tabText(2) == QStringLiteral("Green") && tabs->tabText(3) == QStringLiteral("Blue"));
    capture_both_schemes(*dialog, "ui_levels_presets_dialog");

    // A channel whose values change shows an asterisk on its tab; back at the defaults it
    // goes away. The swatches stay.
    tabs->setCurrentIndex(1);
    white->setValue(230);
    CHECK(tabs->tabText(1) == QStringLiteral("Red*") && tabs->tabText(0) == QStringLiteral("RGB"));
    tabs->setCurrentIndex(0);
    gamma->setValue(120);
    CHECK(tabs->tabText(0) == QStringLiteral("RGB*") && tabs->tabText(1) == QStringLiteral("Red*") &&
          tabs->tabText(2) == QStringLiteral("Green"));
    CHECK(!tabs->tabIcon(1).isNull() && tabs->tabIcon(0).isNull());
    capture_both_schemes(*dialog, "ui_levels_tab_asterisks");
    gamma->setValue(100);
    CHECK(tabs->tabText(0) == QStringLiteral("RGB"));
    tabs->setCurrentIndex(1);
    white->setValue(255);
    CHECK(tabs->tabText(1) == QStringLiteral("Red"));
    // A preset sets the RGB record only.
    choose(*presets, QStringLiteral("levels.deep_blacks"));
    CHECK(tabs->tabText(0) == QStringLiteral("RGB*") && tabs->tabText(1) == QStringLiteral("Red"));
    tabs->setCurrentIndex(0);
    choose(*presets, QStringLiteral("levels.default"));
    CHECK(tabs->tabText(0) == QStringLiteral("RGB"));

    // An output-level preset.
    choose(*presets, QStringLiteral("levels.faded_medium"));
    CHECK(black_output->value() == 26 && white_output->value() == 234 && gamma->value() == 100);
    gamma->setValue(101);
    CHECK(presets->currentData().toString() == QStringLiteral("custom"));
    choose(*presets, QStringLiteral("levels.default"));

    // The eyedroppers: one at a time; picks set each color channel's record.
    CHECK(black_pick->isEnabled() && gray_pick->isEnabled() && white_pick->isEnabled());
    black_pick->click();
    CHECK(mode == patchy::ui::CurvesCanvasMode::BlackPoint);
    pick(QColor(10, 20, 30));
    white_pick->click();
    CHECK(!black_pick->isChecked() && white_pick->isChecked());
    CHECK(mode == patchy::ui::CurvesCanvasMode::WhitePoint);
    pick(QColor(240, 230, 220));
    gray_pick->click();
    pick(QColor(128, 100, 160));
    gray_pick->click();
    CHECK(!mode.has_value());
    tabs->setCurrentIndex(1);
    CHECK(black->value() == 10 && white->value() == 240);
    const auto expected_gamma = static_cast<int>(std::lround(100.0 * std::log(118.0 / 230.0) / std::log(0.5)));
    CHECK(gamma->value() == expected_gamma);
    tabs->setCurrentIndex(3);
    CHECK(black->value() == 30 && white->value() == 220);
    tabs->setCurrentIndex(0);
    CHECK(black->value() == 0 && white->value() == 255);

    // Save and Delete.
    answer_preset_name(QStringLiteral("Picked"));
    save->click();
    const auto saved_path =
        patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::Levels, QStringLiteral("Picked"));
    CHECK(QFileInfo::exists(saved_path));
    CHECK(presets->currentData().toString() == QStringLiteral("user:Picked"));
    if (QFileInfo::exists(saved_path)) {
      const auto saved = patchy::alv::read_file(patchy::ui::to_filesystem_path(saved_path));
      CHECK((saved.red == patchy::LevelsRecord{10, 240, expected_gamma, 0, 255}));
    }
    answer_yes(QStringLiteral("adjustmentPresetDeleteMessageBox"));
    remove->click();
    CHECK(!QFileInfo::exists(saved_path));
    checked = true;
    dialog->accept();
  }));
  const auto result = patchy::ui::request_levels_settings(&parent, {}, {}, nullptr, hooks);
  CHECK(checked);
  CHECK(result.has_value() && result->red.black_input == 10 && result->blue.white_input == 220);
  CHECK(!mode.has_value());
  clear_user_presets(AdjustmentPresetKind::Levels);
}

void ui_adjustment_presets_curves_layout_and_user_presets() {
  clear_user_presets(AdjustmentPresetKind::Curves);
  const auto export_path = artifact_path(QStringLiteral("ui-curves-export.acv"));
  const auto import_path = artifact_path(QStringLiteral("Curves Import.acv"));
  QFile::remove(export_path);
  patchy::CurvesAdjustment imported;
  imported.rgb = {{0, 0}, {70, 40}, {190, 220}, {255, 255}};
  imported.blue = {{0, 20}, {255, 235}};
  patchy::acv::write_file(patchy::ui::to_filesystem_path(import_path), imported);
  const auto shipped = patchy::ui::builtin_curves_presets();
  // Owned by a parent carrying the app style sheet, as in the app, so it opens at its real size.
  const auto themed_parent = make_themed_dialog_parent();
  auto& parent = *themed_parent;
  parent.show();
  QApplication::processEvents();
  const auto hue_saturation_lefts = hue_saturation_preset_left_edges(parent);
  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    process_events_for(60);
    auto* presets = child<QComboBox>(*dialog, "curvesPresetCombo");
    auto* editor = child<patchy::ui::CurvesEditorWidget>(*dialog, "curvesEditor");
    auto* tabs = child<QTabBar>(*dialog, "curvesChannelTabs");
    auto* input = child<QSpinBox>(*dialog, "curvesInputSpin");
    auto* auto_button = child<QPushButton>(*dialog, "curvesAutoButton");
    auto* reset = child<QPushButton>(*dialog, "curvesResetButton");
    auto* save = child<QPushButton>(*dialog, "curvesSavePresetButton");
    auto* remove = child<QPushButton>(*dialog, "curvesDeletePresetButton");
    auto* import = child<QPushButton>(*dialog, "curvesImportPresetButton");
    auto* export_button = child<QPushButton>(*dialog, "curvesExportPresetButton");
    if (presets == nullptr || editor == nullptr || tabs == nullptr || input == nullptr || auto_button == nullptr ||
        reset == nullptr || save == nullptr || remove == nullptr || import == nullptr || export_button == nullptr) {
      dialog->reject();
      return;
    }
    // The icon strip is gone (#592 follow-up 3): a Preset combo as in Levels and
    // Hue/Saturation, each entry with its curve's thumbnail.
    CHECK(dialog->findChild<QListWidget*>() == nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("curvesLoadPresetButton")) == nullptr);
    CHECK(import->text() == QStringLiteral("Import...") && export_button->text() == QStringLiteral("Export..."));
    CHECK(save->text() == QStringLiteral("Save...") && remove->text() == QStringLiteral("Delete"));
    CHECK(presets->count() == static_cast<int>(shipped.size()) + 1);
    CHECK(presets->currentData().toString() == QStringLiteral("curves.default"));
    CHECK(presets->itemText(0) == QStringLiteral("Custom"));
    for (int index = 0; index < presets->count(); ++index) {
      CHECK(!presets->itemIcon(index).isNull());
    }
    // Top to bottom (#592): Preset with its buttons under the combo, a separator, the canvas
    // tools, the editor. Input, Output, Auto, Reset and Before form a column right of the
    // graph, each label above its field and a gap before the buttons. OK is as wide as Cancel.
    auto* output = child<QSpinBox>(*dialog, "curvesOutputSpin");
    auto* graph = child<QWidget>(*dialog, "curvesGraph");
    auto* separator = child<QWidget>(*dialog, "curvesPresetSeparator");
    auto* target = child<QPushButton>(*dialog, "curvesTargetedAdjustmentButton");
    auto* both = child<QPushButton>(*dialog, "curvesClippingPreviewButton");
    auto* before = child<QPushButton>(*dialog, "curvesBeforeButton");
    auto* preview = child<QCheckBox>(*dialog, "curvesPreviewCheck");
    auto* label = child<QLabel>(*dialog, "curvesPresetLabel");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    if (output == nullptr || graph == nullptr || separator == nullptr || target == nullptr || both == nullptr ||
        before == nullptr || preview == nullptr || label == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(label->text() == QStringLiteral("Preset:") && label->buddy() == presets);
    const auto check_preset_rows = [&] {
      check_preset_button_row_spans(*dialog, "curves", *presets);
      CHECK(geometry_in(*separator, *dialog).top() > geometry_in(*save, *dialog).bottom());
      CHECK(geometry_in(*target, *dialog).top() > geometry_in(*separator, *dialog).bottom());
    };
    check_preset_rows();
    // The combo starts where the Hue/Saturation and Levels ones do.
    {
      std::array<int, 2> lefts{};
      for (std::size_t index = 0; index < kBothSchemes.size(); ++index) {
        ColorSchemeRestorer restore(kBothSchemes[index]);
        process_events_for(60);
        lefts[index] = geometry_in(*presets, *dialog).left();
      }
      std::cerr << "curves preset left " << lefts[0] << ' ' << lefts[1] << ", hue/saturation "
                << hue_saturation_lefts[0] << ' ' << hue_saturation_lefts[1] << '\n';
      CHECK(lefts == hue_saturation_lefts);
    }
    CHECK(geometry_in(*both, *dialog).center().y() == geometry_in(*target, *dialog).center().y());
    CHECK(geometry_in(*tabs, *dialog).top() > geometry_in(*target, *dialog).bottom());
    const auto graph_box = geometry_in(*graph, *dialog);
    QLabel* input_caption = nullptr;
    QLabel* output_caption = nullptr;
    for (auto* caption : editor->findChildren<QLabel*>()) {
      if (caption->buddy() == input) {
        input_caption = caption;
      } else if (caption->buddy() == output) {
        output_caption = caption;
      }
    }
    CHECK(input_caption != nullptr && output_caption != nullptr);
    if (input_caption != nullptr && output_caption != nullptr) {
      CHECK(geometry_in(*input_caption, *dialog).bottom() < geometry_in(*input, *dialog).top());
      CHECK(geometry_in(*input, *dialog).bottom() < geometry_in(*output_caption, *dialog).top());
      CHECK(geometry_in(*output_caption, *dialog).bottom() < geometry_in(*output, *dialog).top());
      CHECK(geometry_in(*input_caption, *dialog).left() > graph_box.right());
    }
    const auto column_left = geometry_in(*input, *dialog).left();
    CHECK(column_left > graph_box.right());
    for (QWidget* widget : {static_cast<QWidget*>(output), static_cast<QWidget*>(auto_button),
                            static_cast<QWidget*>(reset), static_cast<QWidget*>(before)}) {
      CHECK(geometry_in(*widget, *dialog).left() == column_left);
      CHECK(geometry_in(*widget, *dialog).width() == input->width());
    }
    CHECK(geometry_in(*auto_button, *dialog).top() - geometry_in(*output, *dialog).bottom() >= 12);
    CHECK(geometry_in(*reset, *dialog).top() > geometry_in(*auto_button, *dialog).bottom());
    CHECK(geometry_in(*before, *dialog).top() > geometry_in(*reset, *dialog).bottom());
    CHECK(geometry_in(*before, *dialog).bottom() <= graph_box.bottom());
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    CHECK(ok->width() == buttons->button(QDialogButtonBox::Cancel)->width());
    CHECK(std::abs(geometry_in(*preview, *dialog).center().y() - geometry_in(*ok, *dialog).center().y()) <= 4);
    CHECK(geometry_in(*preview, *dialog).right() < geometry_in(*ok, *dialog).left());
    // Tab order follows the layout.
    const std::vector<QWidget*> order{presets, save, remove, import, export_button, target};
    CHECK(focus_chain(presets, static_cast<int>(order.size())) == order);
    const std::vector<QWidget*> column_order{input, output, auto_button, reset, before, preview, ok};
    CHECK(focus_chain(input, static_cast<int>(column_order.size())) == column_order);
    CHECK(tabs->tabIcon(0).isNull() && !tabs->tabIcon(1).isNull());
    // Qt caps a new window's height at 2/3 of the screen, and the offscreen screen is short,
    // so the dialog is given its size hint here, as on a desktop screen.
    dialog->resize(dialog->sizeHint());
    process_events_for(60);
    std::cerr << "curves dialog size " << dialog->width() << 'x' << dialog->height() << ", minimum "
              << dialog->minimumSizeHint().width() << 'x' << dialog->minimumSizeHint().height() << ", graph "
              << graph->width() << 'x' << graph->height() << '\n';
    CHECK(dialog->width() < 560);
    CHECK(graph->height() >= 300);
    capture_both_schemes(*dialog, "ui_curves_presets_dialog");
    // At its minimum size (#592 follow-up 3) the column beside the graph sets the height: the
    // graph ends near Before, and the dialog is far below the old 600 px.
    dialog->resize(dialog->minimumSizeHint());
    process_events_for(60);
    {
      const auto before_bottom = geometry_in(*before, *dialog).bottom();
      const auto graph_bottom = geometry_in(*graph, *dialog).bottom();
      std::cerr << "curves minimum " << dialog->width() << 'x' << dialog->height() << ", graph " << graph->width()
                << 'x' << graph->height() << ", before bottom " << before_bottom << ", graph bottom " << graph_bottom
                << '\n';
      CHECK(dialog->height() < 480);
      CHECK(graph_bottom >= before_bottom && graph_bottom - before_bottom <= 12);
      CHECK(graph->height() >= graph->minimumHeight());
      check_preset_rows();
      capture_both_schemes(*dialog, "ui_curves_presets_dialog_minimum");
    }
    // Larger, the graph grows with the dialog.
    const auto minimum_graph = graph->size();
    dialog->resize(dialog->minimumSizeHint() + QSize(200, 200));
    process_events_for(60);
    CHECK(graph->height() >= minimum_graph.height() + 150 && graph->width() >= minimum_graph.width() + 150);
    check_preset_rows();
    dialog->resize(dialog->minimumSizeHint());
    process_events_for(60);
    // The combo open: every entry with its icon.
    for (const auto scheme : kBothSchemes) {
      ColorSchemeRestorer restore(scheme);
      process_events_for(60);
      presets->showPopup();
      process_events_for(120);
      auto* popup = presets->view() != nullptr ? presets->view()->window() : nullptr;
      CHECK(popup != nullptr && popup->isVisible());
      if (popup != nullptr && popup->isVisible()) {
        // Plain grabs, the list drawn where it sits: the theme check flags Qt's private popup
        // container (QComboBoxPrivateContainer), whose own background the themed list covers.
        const auto dialog_shot = dialog->grab().toImage();
        const auto popup_shot = popup->grab().toImage();
        const auto offset = popup->mapToGlobal(QPoint(0, 0)) - dialog->mapToGlobal(QPoint(0, 0));
        const auto bounds = QRect(QPoint(0, 0), dialog->size()).united(QRect(offset, popup->size()));
        QImage shot(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        shot.fill(Qt::transparent);
        {
          QPainter painter(&shot);
          painter.drawImage(-bounds.topLeft(), dialog_shot);
          painter.drawImage(offset - bounds.topLeft(), popup_shot);
        }
        save_image_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "ui_curves_presets_combo_open_dark"
                                                                              : "ui_curves_presets_combo_open_light",
                            shot);
      }
      presets->hidePopup();
      process_events_for(60);
    }

    // Choosing a preset sets the curve; editing it selects Custom, whose icon follows.
    const auto icon_image = [&](int index) { return presets->itemIcon(index).pixmap(QSize(30, 20)).toImage(); };
    choose(*presets, shipped[2].id);
    CHECK(editor->adjustment() == shipped[2].adjustment);
    const auto custom_icon = icon_image(0);
    output->setValue(output->value() > 200 ? output->value() - 40 : output->value() + 40);
    CHECK(presets->currentData().toString() == QStringLiteral("custom"));
    CHECK(icon_image(0) != custom_icon);

    // Save a shipped curve as a user preset: it joins the combo after a separator and stays
    // selected while it equals the shipped one.
    choose(*presets, QStringLiteral("curves.matte"));
    CHECK(!remove->isEnabled());
    const auto count = presets->count();
    answer_preset_name(QStringLiteral("My Matte"));
    save->click();
    CHECK(presets->count() == count + 2);  // The separator and the preset.
    CHECK(presets->currentData().toString() == QStringLiteral("user:My Matte"));
    CHECK(!presets->itemIcon(presets->currentIndex()).isNull());
    CHECK(remove->isEnabled());
    const auto saved_path =
        patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::Curves, QStringLiteral("My Matte"));
    CHECK(QFileInfo::exists(saved_path));
    // Another preset and back.
    choose(*presets, QStringLiteral("curves.default"));
    CHECK(!remove->isEnabled());
    choose(*presets, QStringLiteral("user:My Matte"));
    CHECK(remove->isEnabled());
    // Export writes the current curve.
    answer_file(QStringLiteral("curvesPresetExportFileDialog"), export_path);
    export_button->click();
    CHECK(QFileInfo::exists(export_path));
    if (QFileInfo::exists(export_path)) {
      CHECK(patchy::acv::read_file(patchy::ui::to_filesystem_path(export_path)) == editor->adjustment());
    }
    answer_yes(QStringLiteral("adjustmentPresetDeleteMessageBox"));
    remove->click();
    CHECK(presets->count() == count && !QFileInfo::exists(saved_path));
    // Import adds the file under its name and applies it.
    answer_file(QStringLiteral("curvesPresetImportFileDialog"), import_path);
    import->click();
    CHECK(presets->currentData().toString() == QStringLiteral("user:Curves Import"));
    CHECK(editor->adjustment() == imported);
    CHECK(remove->isEnabled());
    checked = true;
    dialog->accept();
  }));
  const auto result = patchy::ui::request_curves_settings(&parent, {});
  CHECK(checked);
  CHECK(result.has_value() && *result == imported);
  clear_user_presets(AdjustmentPresetKind::Curves);
}

void ui_adjustment_presets_black_white_user_presets() {
  clear_user_presets(AdjustmentPresetKind::BlackWhite);
  const auto export_path = artifact_path(QStringLiteral("ui-bw-export.blw"));
  const auto import_path = artifact_path(QStringLiteral("Tinted Import.blw"));
  QFile::remove(export_path);
  patchy::BlackWhiteAdjustment imported;
  imported.reds = 120;
  imported.tint = true;
  imported.tint_color = patchy::RgbColor{150, 120, 200};
  patchy::blw::write_file(patchy::ui::to_filesystem_path(import_path), imported);

  QWidget parent;
  parent.show();
  QApplication::processEvents();
  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyBlackWhiteDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    process_events_for(60);
    auto* strip = child<QScrollArea>(*dialog, "blackWhitePresetStrip");
    auto* header = child<QLabel>(*dialog, "blackWhitePresetUserHeader");
    auto* reds = child<QSpinBox>(*dialog, "blackWhiteRedsSpin");
    auto* tint = child<QCheckBox>(*dialog, "blackWhiteTintCheck");
    auto* save = child<QPushButton>(*dialog, "blackWhiteSavePresetButton");
    auto* remove = child<QPushButton>(*dialog, "blackWhiteDeletePresetButton");
    auto* import = child<QPushButton>(*dialog, "blackWhiteImportPresetButton");
    auto* export_button = child<QPushButton>(*dialog, "blackWhiteExportPresetButton");
    if (strip == nullptr || header == nullptr || reds == nullptr || tint == nullptr || save == nullptr ||
        remove == nullptr || import == nullptr || export_button == nullptr) {
      dialog->reject();
      return;
    }
    // A column of buttons to the right of the strip; no user section yet.
    CHECK(geometry_in(*save, *dialog).left() > geometry_in(*strip, *dialog).right());
    // The strip is one row of tiles high and ends where Export... does (#528 follow-up 2).
    {
      const auto strip_box = geometry_in(*strip, *dialog);
      const auto save_box = geometry_in(*save, *dialog);
      const auto export_box = geometry_in(*export_button, *dialog);
      auto* first_tile = dialog->findChild<QAbstractButton*>(QStringLiteral("blackWhitePreset_custom"));
      CHECK(first_tile != nullptr);
      std::cerr << "black & white strip " << strip_box.top() << ".." << strip_box.bottom() << ", viewport "
                << strip->viewport()->height() << ", tile " << (first_tile != nullptr ? first_tile->height() : 0)
                << ", buttons " << save_box.top() << ".." << export_box.bottom() << '\n';
      CHECK(std::abs(strip_box.top() - save_box.top()) <= 1);
      CHECK(strip_box.bottom() >= export_box.bottom() && strip_box.bottom() - export_box.bottom() <= 3);
      if (first_tile != nullptr) {
        CHECK(strip->viewport()->height() >= first_tile->height());
        CHECK(strip->viewport()->height() < 2 * first_tile->height());
      }
    }
    CHECK(header->isHidden() && !remove->isEnabled());

    reds->setValue(77);
    answer_preset_name(QStringLiteral("Strong Reds"));
    save->click();
    auto* tile = dialog->findChild<QAbstractButton*>(QStringLiteral("blackWhiteUserPreset_Strong Reds"));
    CHECK(tile != nullptr && tile->isChecked());
    CHECK(!header->isHidden() && remove->isEnabled());
    const auto saved_path =
        patchy::ui::user_adjustment_preset_path(AdjustmentPresetKind::BlackWhite, QStringLiteral("Strong Reds"));
    CHECK(QFileInfo::exists(saved_path));
    if (QFileInfo::exists(saved_path)) {
      CHECK(patchy::blw::read_file(patchy::ui::to_filesystem_path(saved_path)).reds == 77);
    }
    capture_both_schemes(*dialog, "ui_black_white_user_presets");
    if (auto* neutral = dialog->findChild<QAbstractButton*>(QStringLiteral("blackWhitePreset_black_white.default"))) {
      neutral->click();
    }
    CHECK(reds->value() == 40 && !remove->isEnabled());
    if (tile != nullptr) {
      tile->click();
    }
    CHECK(reds->value() == 77 && remove->isEnabled());

    answer_file(QStringLiteral("blackWhitePresetExportFileDialog"), export_path);
    export_button->click();
    CHECK(QFileInfo::exists(export_path));
    if (QFileInfo::exists(export_path)) {
      CHECK(patchy::blw::read_file(patchy::ui::to_filesystem_path(export_path)).reds == 77);
    }
    answer_yes(QStringLiteral("adjustmentPresetDeleteMessageBox"));
    remove->click();
    CHECK(!QFileInfo::exists(saved_path));
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("blackWhiteUserPreset_Strong Reds")) == nullptr);
    CHECK(header->isHidden());

    answer_file(QStringLiteral("blackWhitePresetImportFileDialog"), import_path);
    import->click();
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("blackWhiteUserPreset_Tinted Import")) != nullptr);
    CHECK(reds->value() == 120 && tint->isChecked());
    checked = true;
    dialog->accept();
  }));
  const auto result = patchy::ui::request_black_white_settings(&parent, {}, {}, nullptr);
  CHECK(checked);
  CHECK(result.has_value() && result->reds == 120 && result->tint &&
        (result->tint_color == patchy::RgbColor{150, 120, 200}));
  clear_user_presets(AdjustmentPresetKind::BlackWhite);
}

void ui_adjustment_presets_color_balance_tabs_tracks_reset_and_order() {
  QWidget parent;
  parent.show();
  QApplication::processEvents();
  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorBalanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    process_events_for(60);
    auto* tabs = child<QTabBar>(*dialog, "colorBalanceToneTabs");
    auto* cyan_red = child<QSpinBox>(*dialog, "colorBalanceCyanRedSpin");
    auto* magenta_green = child<QSpinBox>(*dialog, "colorBalanceMagentaGreenSpin");
    auto* yellow_blue = child<QSpinBox>(*dialog, "colorBalanceYellowBlueSpin");
    auto* preserve = child<QCheckBox>(*dialog, "colorBalancePreserveLuminosityCheck");
    auto* preview = child<QCheckBox>(*dialog, "colorBalancePreviewCheck");
    auto* reset = child<QPushButton>(*dialog, "colorBalanceResetButton");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    if (tabs == nullptr || cyan_red == nullptr || magenta_green == nullptr || yellow_blue == nullptr ||
        preserve == nullptr || preview == nullptr || reset == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("colorBalanceToneCombo")) == nullptr);
    CHECK(tabs->count() == 3 && tabs->currentIndex() == 1);
    // Tracks run between the colors each slider trades.
    const std::array<std::pair<const char*, std::pair<QColor, QColor>>, 3> tracks{{
        {"colorBalanceCyanRedSlider", {QColor(0, 255, 255), QColor(255, 0, 0)}},
        {"colorBalanceMagentaGreenSlider", {QColor(255, 0, 255), QColor(0, 255, 0)}},
        {"colorBalanceYellowBlueSlider", {QColor(255, 255, 0), QColor(0, 0, 255)}},
    }};
    for (const auto& [name, ends] : tracks) {
      auto* slider = child<QSlider>(*dialog, name);
      CHECK(slider != nullptr && property_color(*slider, "patchy.trackStart") == ends.first &&
            property_color(*slider, "patchy.trackEnd") == ends.second);
    }
    // Tab order: tones, each slider and its field, Preserve Luminosity, Reset, Preview, then the
    // buttons.
    const std::vector<QWidget*> expected{tabs,
                                         child<QSlider>(*dialog, "colorBalanceCyanRedSlider"),
                                         cyan_red,
                                         child<QSlider>(*dialog, "colorBalanceMagentaGreenSlider"),
                                         magenta_green,
                                         child<QSlider>(*dialog, "colorBalanceYellowBlueSlider"),
                                         yellow_blue,
                                         preserve,
                                         reset,
                                         preview};
    const auto chain = focus_chain(tabs, static_cast<int>(expected.size()));
    if (chain != expected) {
      std::cerr << "color balance tab order:";
      for (auto* widget : chain) {
        std::cerr << ' ' << widget->metaObject()->className() << ':' << widget->objectName().toStdString();
      }
      std::cerr << '\n';
    }
    CHECK(chain == expected);
    // Follow-up (#591): sliders at least 180 px; Preserve Luminosity lines up with the tracks;
    // Reset on its line, right under the Yellow/Blue field; Preview on the OK line; OK as
    // wide as Cancel.
    auto* yellow_blue_slider = child<QSlider>(*dialog, "colorBalanceYellowBlueSlider");
    CHECK(yellow_blue_slider->minimumWidth() == 180 && yellow_blue_slider->width() >= 180);
    CHECK(geometry_in(*preserve, *dialog).left() == geometry_in(*yellow_blue_slider, *dialog).left() + 5);
    CHECK(std::abs(geometry_in(*reset, *dialog).center().y() - geometry_in(*preserve, *dialog).center().y()) <= 4);
    CHECK(std::abs(geometry_in(*reset, *dialog).right() - geometry_in(*yellow_blue, *dialog).right()) <= 1);
    CHECK(geometry_in(*reset, *dialog).top() > geometry_in(*yellow_blue, *dialog).bottom());
    CHECK(!buttons->isAncestorOf(reset));
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    CHECK(ok->width() == cancel->width());
    CHECK(std::abs(geometry_in(*preview, *dialog).center().y() - geometry_in(*ok, *dialog).center().y()) <= 4);
    CHECK(geometry_in(*preview, *dialog).right() < geometry_in(*ok, *dialog).left());
    capture_both_schemes(*dialog, "ui_color_balance_tabs_dialog");

    cyan_red->setValue(20);
    tabs->setCurrentIndex(0);
    CHECK(cyan_red->value() == 0);
    magenta_green->setValue(-15);
    preserve->setChecked(false);
    reset->click();
    CHECK(cyan_red->value() == 0 && magenta_green->value() == 0 && preserve->isChecked());
    tabs->setCurrentIndex(1);
    CHECK(cyan_red->value() == 0);
    tabs->setCurrentIndex(2);
    yellow_blue->setValue(44);
    checked = true;
    dialog->accept();
  }));
  const auto result =
      patchy::ui::request_color_balance_settings(&parent, {}, patchy::ui::new_color_balance_settings());
  CHECK(checked);
  CHECK(result.has_value() && (result->highlights == patchy::ColorBalanceTone{0, 0, 44}) &&
        !result->tone(patchy::ColorBalanceToneRange::Midtones).has_effect() && result->preserve_luminosity);
}

}  // namespace

std::vector<patchy::test::TestCase> adjustment_presets_tests() {
  return {
      {"ui_adjustment_presets_store_names_and_listing", ui_adjustment_presets_store_names_and_listing},
      {"ui_adjustment_presets_hue_saturation_presets_tracks_and_buttons",
       ui_adjustment_presets_hue_saturation_presets_tracks_and_buttons},
      {"ui_adjustment_presets_levels_presets_fields_and_eyedroppers",
       ui_adjustment_presets_levels_presets_fields_and_eyedroppers},
      {"ui_adjustment_presets_curves_layout_and_user_presets", ui_adjustment_presets_curves_layout_and_user_presets},
      {"ui_adjustment_presets_black_white_user_presets", ui_adjustment_presets_black_white_user_presets},
      {"ui_adjustment_presets_color_balance_tabs_tracks_reset_and_order",
       ui_adjustment_presets_color_balance_tabs_tracks_reset_and_order},
  };
}
