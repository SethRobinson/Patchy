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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
// Animation-preview tests, a part file of the layer_panel_organization group (the
// aggregator in layer_panel_organization_tests.cpp appends this part's vector).
#include "ui/animation_preview_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

// Bottom to top: Base, Hidden (invisible), "blink 0.25s", Top. Playback frames are the
// visible layers top to bottom: Top, blink, Base.
patchy::Document animation_test_document() {
  patchy::Document document(16, 12, patchy::PixelFormat::rgba8());
  for (const auto* name : {"Base", "Hidden", "blink 0.25s", "Top"}) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), name,
                                     solid_pixels(16, 12, patchy::PixelFormat::rgba8(),
                                                  QColor(40, 90, 220, 255))));
  }
  document.layers()[1].set_visible(false);
  return document;
}

// Resets the preview's persisted checkboxes to their defaults (off) for one test and
// restores the user's values afterwards.
class AnimationPreviewSettingsReset {
public:
  AnimationPreviewSettingsReset() {
    for (const auto* key : kKeys) {
      patchy::ui::app_settings().remove(QString::fromLatin1(key));
    }
  }

private:
  static constexpr const char* kKeys[] = {"animationPreview/ignoreNameDelays", "animationPreview/playHiddenLayers",
                                          "animationPreview/applyToAllLayers", "animationPreview/playReverse",
                                          "animationPreview/noteClosed"};
  SettingsValueRestorer ignore_{QString::fromLatin1(kKeys[0])};
  SettingsValueRestorer hidden_{QString::fromLatin1(kKeys[1])};
  SettingsValueRestorer all_{QString::fromLatin1(kKeys[2])};
  SettingsValueRestorer reverse_{QString::fromLatin1(kKeys[3])};
  SettingsValueRestorer note_{QString::fromLatin1(kKeys[4])};
};

patchy::ui::AnimationPreviewWindow* open_animation_preview(patchy::ui::MainWindow& window) {
  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  return window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
}

// Types into a delay field the way a user does: replace the text, then Return (which
// commits it, or puts the last value back).
void type_frame_delay(QDoubleSpinBox& field, const QString& text) {
  auto* edit = field.findChild<QLineEdit*>();
  CHECK(edit != nullptr);
  if (edit == nullptr) return;
  edit->setFocus(Qt::OtherFocusReason);
  edit->selectAll();
  QTest::keyClick(edit, Qt::Key_Delete);
  if (!text.isEmpty()) QTest::keyClicks(edit, text);
  QTest::keyClick(edit, Qt::Key_Return);
  QApplication::processEvents();
}

// The field's text for a delay, in the panel's locale.
QString frame_delay_text(const QWidget& panel, double seconds) {
  return panel.locale().toString(seconds, 'g', 6) + QStringLiteral(" s");
}

// Visibility of the four layers bottom to top.
std::vector<bool> top_level_visibility(patchy::ui::MainWindow& window) {
  std::vector<bool> visibility;
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    visibility.push_back(layer.visible());
  }
  return visibility;
}

// Frames and layers named "all" (docs/animation.md#frames-and-layers-named-all). Bottom to
// top: a shared green background "all"; "white" (left half) with a black layer clipped to it;
// an "all" group at 50% opacity holding red "c1 0.2s" and blue "c2". Frames, top first: c2,
// c1 (each drawn inside the half-opaque group over the background), then white with its
// clipped black layer. `ids` receives the layer ids in that bottom-to-top order, children after
// their group.
patchy::Document all_layers_test_document(std::vector<patchy::LayerId>* ids = nullptr) {
  constexpr std::int32_t kWidth = 8;
  constexpr std::int32_t kHeight = 4;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  const auto make = [&document](const char* name, QColor color, std::int32_t width) {
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), color);
    for (std::int32_t y = 0; y < kHeight; ++y) {
      for (std::int32_t x = width; x < kWidth; ++x) {
        pixels.pixel(x, y)[3] = 0;
      }
    }
    return patchy::Layer(document.allocate_layer_id(), name, std::move(pixels));
  };
  document.add_layer(make("all", QColor(0, 200, 0), kWidth));
  document.add_layer(make("white", QColor(255, 255, 255), kWidth / 2));
  auto tint = make("tint", QColor(0, 0, 0), kWidth);
  tint.set_clipped(true);
  document.add_layer(std::move(tint));
  patchy::Layer group(document.allocate_layer_id(), "All", patchy::LayerKind::Group);
  group.children().push_back(make("c1 0.2s", QColor(255, 0, 0), kWidth));
  group.children().push_back(make("c2", QColor(0, 0, 255), kWidth));
  group.set_opacity(0.5F);
  document.add_layer(std::move(group));
  if (ids != nullptr) {
    ids->clear();
    for (const auto& layer : std::as_const(document).layers()) {
      ids->push_back(layer.id());
      for (const auto& child : layer.children()) {
        ids->push_back(child.id());
      }
    }
  }
  return document;
}

bool rgba_near(const patchy::PixelBuffer& pixels, std::int32_t x, std::int32_t y, std::array<int, 4> expected) {
  const auto* px = pixels.pixel(x, y);
  for (std::size_t channel = 0; channel < 4; ++channel) {
    if (std::abs(static_cast<int>(px[channel]) - expected[channel]) > 2) {
      std::fprintf(stderr, "pixel (%d,%d) = %d,%d,%d,%d, expected %d,%d,%d,%d\n", x, y, px[0], px[1], px[2], px[3],
                   expected[0], expected[1], expected[2], expected[3]);
      return false;
    }
  }
  return true;
}

void ui_animated_export_all_layers_render_shared_and_grouped_frames() {
  const auto document = all_layers_test_document();
  patchy::ui::ImageSaveOptions options;
  std::vector<patchy::PixelBuffer> frames;
  std::vector<std::string> names;
  patchy::ui::for_each_animation_export_frame(document, options, nullptr,
                                              [&](patchy::PixelBuffer rgba, const patchy::Layer& layer) {
                                                frames.push_back(std::move(rgba));
                                                names.push_back(layer.name());
                                              });
  CHECK(names == (std::vector<std::string>{"c2", "c1 0.2s", "white"}));
  CHECK(frames.size() == 3U);
  if (frames.size() != 3U) return;
  // c2 and c1: the group's 50% opacity applies to each child over the shared background.
  CHECK(rgba_near(frames[0], 1, 1, {0, 100, 128, 255}));
  CHECK(rgba_near(frames[0], 6, 2, {0, 100, 128, 255}));
  CHECK(rgba_near(frames[1], 1, 1, {128, 100, 0, 255}));
  // white: its clipped black layer draws on its left half only; the background shows right.
  CHECK(rgba_near(frames[2], 1, 1, {0, 0, 0, 255}));
  CHECK(rgba_near(frames[2], 6, 2, {0, 200, 0, 255}));

  // The GIF has the three frames, with the child's own name delay kept.
  std::filesystem::create_directories("test-artifacts");
  options.gif_animate = true;
  options.gif_frame_delay_cs = 7;
  const auto path = QStringLiteral("test-artifacts/ui_animated_export_all_layers.gif");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("gif"), options);
  QImageReader reader(path);
  CHECK(reader.imageCount() == 3);
  const std::array<int, 3> expected_delays_ms = {70, 200, 70};
  for (std::size_t frame = 0; frame < 3; ++frame) {
    CHECK(!reader.read().isNull());
    CHECK(reader.nextImageDelay() == expected_delays_ms[frame]);
  }
}

void ui_animated_export_all_layers_trim_includes_shared_layers() {
  // A shared one-pixel mark in the corner keeps the trimmed frames large enough to hold it,
  // though each frame layer covers one pixel in the middle.
  patchy::Document document(8, 4, patchy::PixelFormat::rgba8());
  const auto dot = [&document](const char* name, std::int32_t x, std::int32_t y) {
    patchy::PixelBuffer pixels(8, 4, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    auto* px = pixels.pixel(x, y);
    px[0] = 255;
    px[3] = 255;
    document.add_pixel_layer(name, std::move(pixels));
  };
  dot("all", 0, 0);
  dot("one", 4, 2);
  dot("two", 5, 2);
  patchy::ui::ImageSaveOptions options;
  options.export_trim_transparent = true;
  std::vector<std::pair<std::int32_t, std::int32_t>> sizes;
  patchy::ui::for_each_animation_export_frame(document, options, nullptr,
                                              [&](patchy::PixelBuffer rgba, const patchy::Layer&) {
                                                CHECK(rgba.pixel(0, 0)[3] == 255);
                                                sizes.emplace_back(rgba.width(), rgba.height());
                                              });
  CHECK(sizes == (std::vector<std::pair<std::int32_t, std::int32_t>>{{6, 3}, {6, 3}}));
}

void ui_animation_preview_plays_all_layers() {
  AnimationPreviewSettingsReset preview_settings;
  std::vector<patchy::LayerId> ids;  // all, white, tint, All, c1, c2
  auto source = all_layers_test_document(&ids);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(source), QStringLiteral("All layers"));
  show_window(window);
  auto* panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  if (panel == nullptr) return;
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  auto* status_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameStatusLabel"));
  CHECK(play_button != nullptr);
  CHECK(status_label != nullptr);
  if (play_button == nullptr || status_label == nullptr) return;
  const auto visibility = [&window, &ids] {
    std::vector<bool> visible;
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    for (const auto id : ids) {
      const auto* layer = document.find_layer(id);
      visible.push_back(layer != nullptr && layer->visible());
    }
    return visible;
  };
  const std::vector<bool> start{true, true, true, true, true, true};
  CHECK(visibility() == start);

  play_button->click();
  CHECK(panel->playing());
  CHECK(status_label->text().contains(QStringLiteral("3")));
  // Frame 1 is c2 inside its group, over the shared background; white is off, and its
  // clipped layer keeps its own visibility (it shows only with its base).
  CHECK(visibility() == (std::vector<bool>{true, false, true, true, false, true}));
  panel->advance_frame();
  CHECK(visibility() == (std::vector<bool>{true, false, true, true, true, false}));
  CHECK(panel->current_frame_delay_ms() == 200);
  panel->advance_frame();
  CHECK(visibility() == (std::vector<bool>{true, true, true, true, false, false}));

  play_button->click();
  CHECK(!panel->playing());
  CHECK(visibility() == start);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

// Whether `text` carries the "all" note in any shipped language (the keyword stays "all").
bool mentions_all_layers(const QString& text) {
  return text.contains(QStringLiteral("\"all\"")) || text.contains(QStringLiteral("「all」"));
}

// Opens one animation options dialog (parented to `window`, so it carries the app theme),
// checks its explanation names the "all" rules and no label clips, captures it when
// `capture` is set, and cancels.
void check_animation_options_dialog(patchy::ui::MainWindow& window, const QString& object_name,
                                    const QString& label_name, const std::string& capture,
                                    const std::function<bool(QWidget*)>& open) {
  bool saw_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(object_name);
    CHECK(dialog != nullptr);
    if (dialog == nullptr) return;
    auto* label = dialog->findChild<QLabel*>(label_name);
    CHECK(label != nullptr);
    if (label != nullptr) {
      CHECK(mentions_all_layers(label->text()));
      CHECK(label->wordWrap());
    }
    const auto clipped = clipped_labels(*dialog);
    for (const auto& entry : clipped) {
      std::fprintf(stderr, "clipped label in %s: %s\n", object_name.toStdString().c_str(), entry.toStdString().c_str());
    }
    CHECK(clipped.isEmpty());
    if (!capture.empty()) {
      save_widget_artifact(capture, *dialog);
    }
    saw_dialog = true;
    dialog->reject();
  }));
  CHECK(!open(&window));
  CHECK(saw_dialog);
}

void ui_animation_texts_explain_all_layers_in_every_language() {
  AnimationPreviewSettingsReset preview_settings;
  auto& localization = patchy::ui::LocalizationManager::instance();
  const auto original_language = localization.current_language();
  const QSize document_size(16, 12);
  for (const auto* code : {"en", "de", "es", "fr", "it", "ja", "ko", "pl", "pt_BR", "ru", "zh_CN", "zh_TW"}) {
    const auto language = QString::fromLatin1(code);
    CHECK(localization.set_language(language, false));
    const bool capture = language == QStringLiteral("en") || language == QStringLiteral("de");
    const auto shot = [&](const char* name) { return capture ? std::string(name) + "_" + code : std::string(); };

    patchy::ui::MainWindow window;
    window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
    show_window(window);

    // The Animation Preview's note under the groups.
    auto* panel = open_animation_preview(window);
    CHECK(panel != nullptr);
    if (panel != nullptr) {
      auto* hint = panel->findChild<QLabel*>(QStringLiteral("animationPreviewHintLabel"));
      CHECK(hint != nullptr);
      if (hint != nullptr) CHECK(mentions_all_layers(hint->text()));
      CHECK(clipped_labels(*panel).isEmpty());
      if (capture) save_widget_artifact("animation_preview_all_note_" + std::string(code), *panel);
      panel->close();
      QApplication::processEvents();
    }

    patchy::ui::ImageSaveOptions defaults;
    // Save As .gif on a layered document, then File > Export > Layers as Animated GIF.
    check_animation_options_dialog(window, QStringLiteral("gifSaveOptionsDialog"),
                                   QStringLiteral("gifAnimationExplanationLabel"), shot("gif_save_options_all_note"),
                                   [&](QWidget* parent) {
                                     return patchy::ui::prompt_gif_save_options(parent, defaults, true, false, true,
                                                                                document_size)
                                         .has_value();
                                   });
    check_animation_options_dialog(window, QStringLiteral("gifSaveOptionsDialog"),
                                   QStringLiteral("gifAnimationExplanationLabel"), shot("gif_export_options_all_note"),
                                   [&](QWidget* parent) {
                                     return patchy::ui::prompt_gif_save_options(parent, defaults, false, true, true,
                                                                                document_size)
                                         .has_value();
                                   });
    // Save As .webp / .jxl offering the animation.
    auto offered = defaults;
    offered.offer_animation = true;
    offered.has_visible_frames = true;
    check_animation_options_dialog(window, QStringLiteral("webpSaveOptionsDialog"),
                                   QStringLiteral("webpAnimationExplanationLabel"), std::string(),
                                   [&](QWidget* parent) {
                                     return patchy::ui::prompt_image_save_options(parent, QStringLiteral("webp"),
                                                                                  offered, false, document_size)
                                         .has_value();
                                   });
    check_animation_options_dialog(window, QStringLiteral("jxlSaveOptionsDialog"),
                                   QStringLiteral("jxlAnimationExplanationLabel"), std::string(),
                                   [&](QWidget* parent) {
                                     return patchy::ui::prompt_image_save_options(parent, QStringLiteral("jxl"),
                                                                                  offered, false, document_size)
                                         .has_value();
                                   });
  }
  CHECK(localization.set_language(original_language.isEmpty() ? QStringLiteral("en") : original_language, false));
}

void ui_animation_preview_plays_visible_layers_and_restores() {
  AnimationPreviewSettingsReset preview_settings;
  SettingsValueRestorer delay_setting(QStringLiteral("saveOptions/gifFrameDelayCs"));
  SettingsValueRestorer ms_setting(QStringLiteral("saveOptions/animationFrameDelayMs"));
  patchy::ui::app_settings().remove(QStringLiteral("saveOptions/gifFrameDelayCs"));
  patchy::ui::app_settings().remove(QStringLiteral("saveOptions/animationFrameDelayMs"));

  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  CHECK(panel->isVisible());

  // The delay field edits the shared export default.
  auto* delay_field = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationFrameDelaySpin"));
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  auto* status_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameStatusLabel"));
  CHECK(delay_field != nullptr);
  CHECK(play_button != nullptr);
  CHECK(status_label != nullptr);
  CHECK(panel->default_frame_delay_ms() == 100);
  type_frame_delay(*delay_field, QStringLiteral("0.033"));
  CHECK(panel->default_frame_delay_ms() == 33);
  // Return commits the field and nothing else: it never presses Play.
  CHECK(!panel->playing());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/animationFrameDelayMs")).toInt() == 33);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/gifFrameDelayCs")).toInt() == 3);
  type_frame_delay(*delay_field, QStringLiteral("0.30"));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/gifFrameDelayCs")).toInt() == 30);

  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  play_button->click();
  CHECK(play_button->text().contains(QStringLiteral("Stop")));
  CHECK(panel->playing());
  // Frame 1 is the TOP layer (the animated GIF export's order); the hidden layer and
  // every other layer are off.
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  CHECK(status_label->text().contains(QStringLiteral("1")));
  CHECK(status_label->text().contains(QStringLiteral("3")));
  // "Top" has no name token, so the spin's default applies.
  CHECK(panel->current_frame_delay_ms() == 300);

  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  // "blink 0.25s" overrides the default.
  CHECK(panel->current_frame_delay_ms() == 250);
  // The panel rows follow without a rebuild: row 0 is the top layer (off), row 1 the
  // current frame (on).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->checkState() == Qt::Unchecked);
  CHECK(layer_list->item(1)->checkState() == Qt::Checked);

  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  panel->advance_frame();  // wraps back to the top layer
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));

  play_button->click();
  CHECK(!panel->playing());
  CHECK(play_button->text().contains(QStringLiteral("Play")));
  CHECK(status_label->text().isEmpty());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));
  // Playback is preview-only: no undo entries, and the session never went modified.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The playback timer really advances frames on its own.
  type_frame_delay(*delay_field, QStringLiteral("0.01"));
  CHECK(panel->default_frame_delay_ms() == 10);
  play_button->click();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  CHECK(process_events_until(
      [&window] { return top_level_visibility(window) == (std::vector<bool>{false, false, true, false}); }));
  play_button->click();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  // Clicking the layers-panel film button again closes the panel (WA_DeleteOnClose
  // destroys it, so only the guard may be inspected afterwards).
  QPointer<patchy::ui::AnimationPreviewWindow> panel_guard(panel);
  animation_button->click();
  QApplication::processEvents();
  CHECK(panel_guard == nullptr);
}

// Layer names bottom to top.
std::vector<std::string> top_level_names(patchy::ui::MainWindow& window) {
  std::vector<std::string> names;
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    names.push_back(layer.name());
  }
  return names;
}

// Layer > Arrange > Reverse and the animation preview's Reverse Selected Layers: the selected
// frames trade places as one undo step (a copied run reversed makes a bounce); one layer alone
// changes nothing and says why; the button stops playback first.
void ui_animation_preview_reverses_selected_layers() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));
  // Rows are top-first: rows 0 to 2 are Top, blink, Hidden.
  const auto select_rows = [layer_list](std::initializer_list<int> rows) {
    layer_list->clearSelection();
    for (const int row : rows) {
      layer_list->item(row)->setSelected(true);
    }
  };

  auto* reverse_action = window.findChild<QAction*>(QStringLiteral("layerReverseAction"));
  CHECK(reverse_action != nullptr);
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  if (reverse_action != nullptr) {
    select_rows({0, 1, 2});
    reverse_action->trigger();
    CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Top", "blink 0.25s", "Hidden"}));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
    patchy::ui::MainWindowTestAccess::undo(window);
    CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));
    // One layer: nothing to reverse, no undo step.
    select_rows({0});
    reverse_action->trigger();
    CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before);
  }

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  if (panel == nullptr) {
    return;
  }
  auto* reverse_button = panel->findChild<QPushButton*>(QStringLiteral("animationReverseLayersButton"));
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  CHECK(reverse_button != nullptr && play_button != nullptr);
  if (reverse_button != nullptr && play_button != nullptr) {
    // Mid-play: playback stops (visibility restored) before the reorder and its undo snapshot.
    play_button->click();
    CHECK(panel->playing());
    select_rows({0, 1});
    reverse_button->click();
    CHECK(!panel->playing());
    CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "Top", "blink 0.25s"}));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
    patchy::ui::MainWindowTestAccess::undo(window);
    CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));
  }
}

// "Play in reverse" (GitHub issue 665) plays the frames bottom to top, the counter numbering
// them in the export's order; toggling it mid-play carries on from the frame on screen, and
// the choice persists. The layers and their order never change.
void ui_animation_preview_plays_in_reverse() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  auto* panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  if (panel == nullptr) return;
  auto* reverse_check = panel->findChild<QCheckBox*>(QStringLiteral("animationPlayReverseCheck"));
  auto* hidden_check = panel->findChild<QCheckBox*>(QStringLiteral("animationPlayHiddenLayersCheck"));
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  auto* status_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameStatusLabel"));
  CHECK(reverse_check != nullptr && hidden_check != nullptr && play_button != nullptr && status_label != nullptr);
  if (reverse_check == nullptr || hidden_check == nullptr || play_button == nullptr || status_label == nullptr) return;
  CHECK(!reverse_check->isChecked());
  CHECK(!reverse_check->toolTip().isEmpty());
  // It sits right under "Play hidden layers", in the same column.
  CHECK(reverse_check->geometry().top() > hidden_check->geometry().bottom());
  CHECK(reverse_check->geometry().left() == hidden_check->geometry().left());

  // Visible frames top first: Top, blink, Base. Reversed: Base, blink, Top.
  reverse_check->setChecked(true);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("animationPreview/playReverse")).toBool());
  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  CHECK(status_label->text().contains(QStringLiteral("3/3")));
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  CHECK(status_label->text().contains(QStringLiteral("2/3")));
  CHECK(panel->current_frame_delay_ms() == 250);  // blink's own time follows it
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  CHECK(status_label->text().contains(QStringLiteral("1/3")));
  panel->advance_frame();  // wraps back to the bottom layer
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  panel->advance_frame();  // blink again

  // Off mid-play: blink stays on screen, then the frames run top to bottom again (Base next).
  reverse_check->setChecked(false);
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  CHECK(status_label->text().contains(QStringLiteral("2/3")));
  CHECK(panel->current_frame_delay_ms() == 250);
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  CHECK(status_label->text().contains(QStringLiteral("3/3")));
  // On again at the last frame: the next one is blink, the frame above it.
  reverse_check->setChecked(true);
  CHECK(status_label->text().contains(QStringLiteral("3/3")));
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  CHECK(status_label->text().contains(QStringLiteral("2/3")));

  // Stop restores the layers; nothing was reordered and the document is not modified.
  play_button->click();
  CHECK(!panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // A reopened panel reads the choice back and starts from the bottom layer.
  panel->close();
  QApplication::processEvents();
  panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  if (panel == nullptr) return;
  reverse_check = panel->findChild<QCheckBox*>(QStringLiteral("animationPlayReverseCheck"));
  CHECK(reverse_check != nullptr && reverse_check->isChecked());
  save_widget_artifact("ui_animation_preview_play_in_reverse", *panel);
  panel->start_playback();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  panel->stop_playback(true);
  panel->close();
  QApplication::processEvents();
}

// The note at the bottom (GitHub issue 666): its x hides it and the window gets shorter; the
// choice persists; the info mark beside the Playback title brings it back.
void ui_animation_preview_note_closes_and_comes_back() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  auto* panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  if (panel == nullptr) return;
  const auto find_parts = [&panel] {
    return std::tuple{panel->findChild<QWidget*>(QStringLiteral("animationPreviewNote")),
                      panel->findChild<QToolButton*>(QStringLiteral("animationPreviewNoteCloseButton")),
                      panel->findChild<QAbstractButton*>(QStringLiteral("animationPreviewNoteInfoButton")),
                      panel->findChild<QWidget*>(QStringLiteral("animationPlaybackGroup"))};
  };
  auto [note, close_button, info_button, playback_group] = find_parts();
  CHECK(note != nullptr && close_button != nullptr && info_button != nullptr && playback_group != nullptr);
  if (note == nullptr || close_button == nullptr || info_button == nullptr || playback_group == nullptr) return;
  CHECK(note->isVisible());
  CHECK(!info_button->isVisible());
  CHECK(note->findChild<QLabel*>(QStringLiteral("animationPreviewHintLabel")) != nullptr);
  CHECK(note->findChild<QToolButton*>(QStringLiteral("animationPreviewNoteIcon")) != nullptr);
  CHECK(!close_button->toolTip().isEmpty());
  // The x is at the far right of the note.
  CHECK(close_button->geometry().right() > note->width() - 12);
  CHECK(patchy::test::ui::clipped_labels(*panel).isEmpty());
  save_widget_artifact("ui_animation_preview_note_shown", *panel);
  const int open_height = panel->height();

  close_button->click();
  QApplication::processEvents();
  CHECK(!note->isVisible());
  CHECK(info_button->isVisible());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("animationPreview/noteClosed")).toBool());
  std::printf("  window height %d with the note, %d without\n", open_height, panel->height());
  CHECK(panel->height() < open_height - 30);
  CHECK(panel->height() > open_height / 2);  // shorter, never collapsed
  CHECK(panel->minimumHeight() <= panel->height());
  // The mark sits on the Playback title row, above the group's first field, and says what
  // the note says.
  const auto info_top = info_button->mapTo(panel, QPoint(0, 0)).y();
  auto* delay_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameDelayLabel"));
  CHECK(delay_label != nullptr && info_top < delay_label->mapTo(panel, QPoint(0, 0)).y());
  CHECK(info_top < playback_group->mapTo(panel, QPoint(0, 12)).y());
  CHECK(info_button->mapTo(playback_group, QPoint(0, 0)).x() > 20);
  CHECK(mentions_all_layers(info_button->toolTip()));
  CHECK(patchy::test::ui::clipped_labels(*panel).isEmpty());
  save_widget_artifact("ui_animation_preview_note_closed", *panel);

  // Reopened: still closed, at the short height.
  panel->close();
  QApplication::processEvents();
  panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  if (panel == nullptr) return;
  std::tie(note, close_button, info_button, playback_group) = find_parts();
  CHECK(note != nullptr && info_button != nullptr);
  if (note == nullptr || info_button == nullptr) return;
  CHECK(!note->isVisible());
  CHECK(info_button->isVisible());
  CHECK(panel->height() < open_height - 30);
  CHECK(panel->height() > open_height / 2);
  CHECK(patchy::test::ui::clipped_labels(*panel).isEmpty());

  // The info mark brings it back, and the window grows to fit it again.
  info_button->click();
  QApplication::processEvents();
  CHECK(note->isVisible());
  CHECK(!info_button->isVisible());
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("animationPreview/noteClosed")).toBool());
  CHECK(panel->height() >= open_height - 2);
  CHECK(patchy::test::ui::clipped_labels(*panel).isEmpty());
  panel->close();
  QApplication::processEvents();
}

// The info mark beside the Playback title (GitHub issue 666 follow-up). The title sits above
// the group's top frame line, which runs on under it; the mark sits there too, lined up with
// the title text, with a clear row above the line and the line unbroken beneath it, in Dark
// and Light. On Windows the mark reached down onto the line and its background hid part of it.
void ui_animation_preview_info_mark_clears_the_frame_line() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::app_settings().setValue(QStringLiteral("animationPreview/noteClosed"), true);
  ColorSchemeRestorer restore_scheme;
  const auto differs = [](QRgb a, QRgb b) {
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b)) >= 12;
  };
  // Opened from a main window, as in the app: it carries the app's sheet, so the capture does
  // not restyle (and relayout) the panel between the pixels and the geometry read here.
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const std::string scheme = preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    auto* opened = open_animation_preview(window);
    CHECK(opened != nullptr);
    if (opened == nullptr) return;
    auto& panel = *opened;
    auto* group = panel.findChild<QWidget*>(QStringLiteral("animationPlaybackGroup"));
    auto* info_button = panel.findChild<QAbstractButton*>(QStringLiteral("animationPreviewNoteInfoButton"));
    CHECK(group != nullptr && info_button != nullptr);
    if (group == nullptr || info_button == nullptr) return;
    CHECK(info_button->isVisible());
    const auto image = capture_widget_artifact("ui_animation_preview_info_mark_" + scheme, panel);
    const qreal ratio = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    const auto pixel = [&](int x, int y) { return image.pixel(qRound(x * ratio), qRound(y * ratio)); };
    const QRect mark(info_button->mapTo(&panel, QPoint(0, 0)), info_button->size());
    const QPoint group_origin = group->mapTo(&panel, QPoint(0, 0));
    CHECK(mark.top() >= group_origin.y());
    CHECK(mark.left() > group_origin.x() + 20);
    // The frame's top row, found right of the mark.
    const int line_x = mark.right() + 30;
    CHECK(line_x < group_origin.x() + group->width() - 10);
    const auto background = pixel(line_x, group_origin.y());
    int line_y = -1;
    for (int y = group_origin.y(); y < group_origin.y() + 32 && line_y < 0; ++y) {
      if (differs(pixel(line_x, y), background)) line_y = y;
    }
    // The title text's rows, between the frame's left edge and the mark.
    int text_top = -1;
    int text_bottom = -1;
    for (int y = group_origin.y(); y < line_y; ++y) {
      for (int x = group_origin.x() + 6; x < mark.left() - 1; ++x) {
        if (differs(pixel(x, y), background)) {
          if (text_top < 0) text_top = y;
          text_bottom = y;
          break;
        }
      }
    }
    std::printf("  [%s] mark rows %d-%d, title text rows %d-%d, frame line row %d\n", scheme.c_str(), mark.top(),
                mark.bottom(), text_top, text_bottom, line_y);
    CHECK(line_y >= 0 && text_top >= 0);
    if (line_y < 0 || text_top < 0) continue;
    // Above the line with a clear row between them, ink and all.
    CHECK(mark.bottom() < line_y - 1);
    for (int x = mark.left(); x <= mark.right(); ++x) {
      CHECK(!differs(pixel(x, line_y - 1), background));
      // The line runs on unbroken under the mark.
      CHECK(differs(pixel(x, line_y), background));
    }
    // Lined up with the title text.
    const int mark_middle = mark.top() + mark.bottom();
    const int text_middle = text_top + text_bottom;
    CHECK(std::abs(mark_middle - text_middle) <= 6);  // within 3 px, in half pixels
    panel.close();
    QApplication::processEvents();
  }
}

void ui_animation_preview_sets_and_removes_frame_time_names() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  auto* time_spin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationSelectionTimeSpin"));
  auto* set_button = panel->findChild<QPushButton*>(QStringLiteral("animationSetFrameTimeButton"));
  auto* remove_button = panel->findChild<QPushButton*>(QStringLiteral("animationRemoveFrameTimeButton"));
  CHECK(time_spin != nullptr);
  CHECK(set_button != nullptr);
  CHECK(remove_button != nullptr);
  save_widget_artifact("ui_animation_preview_frame_time_controls", *panel);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 4);
  // Rows are top-first: row 0 is "Top", row 1 is "blink 0.25s".
  const auto select_top_two = [layer_list] {
    layer_list->clearSelection();
    layer_list->item(0)->setSelected(true);
    layer_list->item(1)->setSelected(true);
  };

  // Set replaces an existing token and appends to token-free names, as one undo step.
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  select_top_two();
  time_spin->setValue(0.5);
  set_button->click();
  CHECK(top_level_names(window) ==
        (std::vector<std::string>{"Base", "Hidden", "blink 0.5s", "Top 0.5s"}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Setting the same time again changes nothing and pushes no undo entry.
  select_top_two();
  set_button->click();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

  // Remove strips the tokens; a second remove has nothing left to strip.
  select_top_two();
  remove_button->click();
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink", "Top"}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 2);
  select_top_two();
  remove_button->click();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 2);

  // Both steps undo cleanly back to the original names.
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(top_level_names(window) ==
        (std::vector<std::string>{"Base", "Hidden", "blink 0.5s", "Top 0.5s"}));
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));

  // Stamping mid-play stops playback first, so the undo snapshot and the rename see the
  // restored visibility, not a preview frame's.
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  CHECK(play_button != nullptr);
  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  select_top_two();
  time_spin->setValue(0.25);
  set_button->click();
  CHECK(!panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));
  CHECK(top_level_names(window) ==
        (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top 0.25s"}));

  panel->close();
  QApplication::processEvents();
}

// "Ignore delays in layer names" plays every frame at the frame delay; "Play hidden layers"
// adds hidden top-level layers to the cycle (and restarts a running playback to pick them
// up). Both persist. The one "Default frame delay" field takes typed values, 0.1 s steps
// and presets from its drop-down, and puts the last value back for text it cannot use.
// The Layer names group's field has the same steps and presets.
void ui_animation_preview_ignores_name_delays_and_plays_hidden_layers() {
  AnimationPreviewSettingsReset preview_settings;
  SettingsValueRestorer delay_setting(QStringLiteral("saveOptions/gifFrameDelayCs"));
  SettingsValueRestorer ms_setting(QStringLiteral("saveOptions/animationFrameDelayMs"));
  patchy::ui::app_settings().remove(QStringLiteral("saveOptions/gifFrameDelayCs"));
  patchy::ui::app_settings().remove(QStringLiteral("saveOptions/animationFrameDelayMs"));

  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  auto* panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  auto* delay_field = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationFrameDelaySpin"));
  auto* delay_presets = panel->findChild<QToolButton*>(QStringLiteral("animationFrameDelayPresetsButton"));
  auto* ignore_check = panel->findChild<QCheckBox*>(QStringLiteral("animationIgnoreNameDelaysCheck"));
  auto* hidden_check = panel->findChild<QCheckBox*>(QStringLiteral("animationPlayHiddenLayersCheck"));
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  auto* status_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameStatusLabel"));
  CHECK(delay_field != nullptr);
  CHECK(delay_presets != nullptr && delay_presets->menu() != nullptr);
  if (delay_field == nullptr || delay_presets == nullptr || delay_presets->menu() == nullptr) return;
  CHECK(ignore_check != nullptr);
  CHECK(hidden_check != nullptr);
  CHECK(play_button != nullptr);
  CHECK(status_label != nullptr);
  CHECK(!ignore_check->isChecked());
  CHECK(!hidden_check->isChecked());
  CHECK(!ignore_check->toolTip().isEmpty());
  CHECK(!hidden_check->toolTip().isEmpty());
  CHECK(!delay_field->toolTip().isEmpty());
  // The dialog's spin style reaches the field even under the main window's sheet: its text
  // stops short of the - and + buttons (a field that kept the main window's rules drew
  // the platform arrows instead and ran its text under them).
  auto* delay_edit = delay_field->findChild<QLineEdit*>();
  CHECK(delay_edit != nullptr && delay_edit->width() < delay_field->width() - 50);
  // Both fields start at the same x and stretch so each drop-down ends where the buttons
  // below it end, at the opening size and with the window widened.
  {
    auto* time_field = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationSelectionTimeSpin"));
    auto* time_drop = panel->findChild<QToolButton*>(QStringLiteral("animationSelectionTimePresetsButton"));
    auto* remove_time = panel->findChild<QPushButton*>(QStringLiteral("animationRemoveFrameTimeButton"));
    CHECK(time_field != nullptr && time_drop != nullptr && remove_time != nullptr);
    if (time_field != nullptr && time_drop != nullptr && remove_time != nullptr) {
      const auto left = [panel](const QWidget* widget) { return widget->mapTo(panel, QPoint(0, 0)).x(); };
      const auto right = [panel](const QWidget* widget) {
        return widget->mapTo(panel, QPoint(widget->width(), 0)).x();
      };
      const auto check_edges = [&](const char* label) {
        std::printf("  [%s] fields x=%d/%d, drop-downs end %d/%d, buttons end %d/%d\n", label, left(delay_field),
                    left(time_field), right(delay_presets), right(time_drop), right(play_button), right(remove_time));
        CHECK(left(delay_field) == left(time_field));
        CHECK(right(delay_presets) == right(play_button));
        CHECK(right(time_drop) == right(remove_time));
      };
      check_edges("opening size");
      panel->resize(panel->width() + 120, panel->height());
      QApplication::processEvents();
      check_edges("widened");
      panel->resize(panel->minimumWidth(), panel->height());
      QApplication::processEvents();
    }
  }
  // Each group's buttons sit further below the checkbox above them than the checkboxes sit
  // from each other (GitHub issue 198 follow-up: Set Time crowded Apply to all layers).
  {
    auto* all_layers = panel->findChild<QCheckBox*>(QStringLiteral("animationFrameTimeAllLayersCheck"));
    auto* set_time = panel->findChild<QPushButton*>(QStringLiteral("animationSetFrameTimeButton"));
    CHECK(all_layers != nullptr && set_time != nullptr);
    if (all_layers != nullptr && set_time != nullptr) {
      const auto top = [panel](const QWidget* widget) { return widget->mapTo(panel, QPoint(0, 0)).y(); };
      const auto bottom = [panel](const QWidget* widget) {
        return widget->mapTo(panel, QPoint(0, widget->height())).y();
      };
      const int option_gap = top(hidden_check) - bottom(ignore_check);
      std::printf("  [button gaps] options %d, Play %d, Set Time %d\n", option_gap,
                  top(play_button) - bottom(hidden_check), top(set_time) - bottom(all_layers));
      CHECK(top(play_button) - bottom(hidden_check) >= option_gap + 6);
      CHECK(top(set_time) - bottom(all_layers) >= option_gap + 6);
    }
  }

  // No combo boxes are left: each delay is one field with its presets drop-down attached.
  CHECK(panel->findChildren<QComboBox*>().isEmpty());

  // The drop-down: five presets with their unit; the one the field holds is ticked.
  auto* preset_menu = delay_presets->menu();
  const auto presets = preset_menu->actions();
  CHECK(presets.size() == 5);
  CHECK(presets.front()->text() == frame_delay_text(*panel, 0.05));
  CHECK(presets.back()->text() == frame_delay_text(*panel, 1.0));
  CHECK(delay_field->text() == frame_delay_text(*panel, 0.1));
  const auto ticked_preset_ms = [preset_menu] {
    emit preset_menu->aboutToShow();
    int ticked = -1;
    for (const auto* action : preset_menu->actions()) {
      if (action->isChecked()) ticked = action->data().toInt();
    }
    return ticked;
  };
  CHECK(ticked_preset_ms() == 100);

  // A typed value commits, shows with its unit and ticks no preset.
  type_frame_delay(*delay_field, QStringLiteral("0.3"));
  CHECK(panel->default_frame_delay_ms() == 300);
  CHECK(delay_field->text() == frame_delay_text(*panel, 0.3));
  CHECK(ticked_preset_ms() == -1);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/animationFrameDelayMs")).toInt() == 300);
  // The unit may be typed too.
  type_frame_delay(*delay_field, QStringLiteral("0.125 s"));
  CHECK(panel->default_frame_delay_ms() == 125);

  // The - and + buttons step a tenth of a second and stop at 0.
  delay_field->stepBy(1);
  CHECK(panel->default_frame_delay_ms() == 225);
  CHECK(delay_field->text() == frame_delay_text(*panel, 0.225));
  delay_field->stepBy(-3);
  CHECK(panel->default_frame_delay_ms() == 0);

  // Picking a preset sets the field and the shared export default.
  for (auto* action : presets) {
    if (action->data().toInt() == 500) action->trigger();
  }
  CHECK(panel->default_frame_delay_ms() == 500);
  CHECK(delay_field->text() == frame_delay_text(*panel, 0.5));
  CHECK(ticked_preset_ms() == 500);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/animationFrameDelayMs")).toInt() == 500);

  // Text it cannot use (letters, nothing, past the maximum) puts the last value back.
  for (const auto& bad : {QStringLiteral("abc"), QString(), QStringLiteral("99999")}) {
    type_frame_delay(*delay_field, bad);
    CHECK(panel->default_frame_delay_ms() == 500);
    CHECK(delay_field->text() == frame_delay_text(*panel, 0.5));
  }
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/animationFrameDelayMs")).toInt() == 500);

  // A typed value that equals a preset ticks it.
  type_frame_delay(*delay_field, QStringLiteral("0.2"));
  CHECK(ticked_preset_ms() == 200);
  CHECK(panel->default_frame_delay_ms() == 200);

  // The Layer names field steps and takes presets the same way, without touching the default.
  auto* time_spin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationSelectionTimeSpin"));
  auto* time_presets = panel->findChild<QToolButton*>(QStringLiteral("animationSelectionTimePresetsButton"));
  CHECK(time_spin != nullptr);
  CHECK(time_presets != nullptr && time_presets->menu() != nullptr);
  if (time_spin != nullptr && time_presets != nullptr && time_presets->menu() != nullptr) {
    CHECK(time_presets->menu()->actions().size() == 5);
    time_spin->setValue(0.1);
    time_spin->stepBy(1);
    CHECK(std::llround(time_spin->value() * 1000.0) == 200);
    time_presets->menu()->actions().back()->trigger();
    CHECK(std::llround(time_spin->value() * 1000.0) == 1000);
    CHECK(panel->default_frame_delay_ms() == 200);
  }

  // Ignoring name delays: "blink 0.25s" plays for the frame delay instead.
  play_button->click();
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  CHECK(panel->current_frame_delay_ms() == 250);
  ignore_check->setChecked(true);
  CHECK(panel->current_frame_delay_ms() == 200);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("animationPreview/ignoreNameDelays")).toBool());
  ignore_check->setChecked(false);
  CHECK(panel->current_frame_delay_ms() == 250);
  CHECK(status_label->text().contains(QStringLiteral("3")));

  // Playing hidden layers restarts playback with four frames: Top, blink, Hidden, Base.
  hidden_check->setChecked(true);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("animationPreview/playHiddenLayers")).toBool());
  CHECK(panel->playing());
  CHECK(status_label->text().contains(QStringLiteral("4")));
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, true, false, false}));
  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  panel->advance_frame();  // wraps back to the top layer
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  // Stop restores the hidden layer as hidden.
  play_button->click();
  CHECK(!panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // A reopened panel reads both choices back.
  ignore_check->setChecked(true);
  panel->close();
  QApplication::processEvents();
  panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  ignore_check = panel->findChild<QCheckBox*>(QStringLiteral("animationIgnoreNameDelaysCheck"));
  hidden_check = panel->findChild<QCheckBox*>(QStringLiteral("animationPlayHiddenLayersCheck"));
  CHECK(ignore_check != nullptr && ignore_check->isChecked());
  CHECK(hidden_check != nullptr && hidden_check->isChecked());
  save_widget_artifact("ui_animation_preview_options", *panel);
  panel->close();
  QApplication::processEvents();
}

// "Apply to all layers" points Set Time and Remove Time at every top-level layer, the
// hidden one included, whatever the selection; each is one undo step.
void ui_animation_preview_sets_and_removes_frame_time_on_all_layers() {
  AnimationPreviewSettingsReset preview_settings;
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);
  auto* panel = open_animation_preview(window);
  CHECK(panel != nullptr);
  auto* time_spin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationSelectionTimeSpin"));
  auto* set_button = panel->findChild<QPushButton*>(QStringLiteral("animationSetFrameTimeButton"));
  auto* remove_button = panel->findChild<QPushButton*>(QStringLiteral("animationRemoveFrameTimeButton"));
  auto* all_check = panel->findChild<QCheckBox*>(QStringLiteral("animationFrameTimeAllLayersCheck"));
  CHECK(time_spin != nullptr);
  CHECK(set_button != nullptr);
  CHECK(remove_button != nullptr);
  CHECK(all_check != nullptr);
  CHECK(remove_button->text() == QStringLiteral("Remove Time"));
  CHECK(!all_check->isChecked());
  CHECK(!all_check->toolTip().isEmpty());

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  // Only "Top" selected: all layers still change.
  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  all_check->setChecked(true);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("animationPreview/applyToAllLayers")).toBool());

  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  time_spin->setValue(0.4);
  set_button->click();
  CHECK(top_level_names(window) ==
        (std::vector<std::string>{"Base 0.4s", "Hidden 0.4s", "blink 0.4s", "Top 0.4s"}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  // Again: nothing to change, no undo entry.
  set_button->click();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

  remove_button->click();
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink", "Top"}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 2);
  remove_button->click();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 2);

  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(top_level_names(window) ==
        (std::vector<std::string>{"Base 0.4s", "Hidden 0.4s", "blink 0.4s", "Top 0.4s"}));
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top"}));

  // Unticked, the selection rules again: only "Top" changes.
  all_check->setChecked(false);
  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  set_button->click();
  CHECK(top_level_names(window) == (std::vector<std::string>{"Base", "Hidden", "blink 0.25s", "Top 0.4s"}));

  panel->close();
  QApplication::processEvents();
}

void ui_animation_preview_stops_on_tab_switch_and_close() {
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation A"));
  show_window(window);
  auto* canvas_a = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas_a != nullptr);

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  CHECK(play_button != nullptr);

  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));

  // Opening a second document switches tabs; playback stops and document A's visibility
  // is restored while it is still the outgoing active document.
  window.add_document_session(patchy::Document(8, 8, patchy::PixelFormat::rgba8()),
                              QStringLiteral("Animation B"));
  QApplication::processEvents();
  CHECK(!panel->playing());
  {
    const auto* document_a = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas_a);
    CHECK(document_a != nullptr);
    const auto& layers = std::as_const(*document_a).layers();
    CHECK(layers[0].visible());
    CHECK(!layers[1].visible());
    CHECK(layers[2].visible());
    CHECK(layers[3].visible());
  }

  // Back on document A: closing the panel mid-play restores too (done() stops playback;
  // WA_DeleteOnClose then destroys the panel, so only the guard may be inspected).
  patchy::ui::MainWindowTestAccess::activate_canvas(window, canvas_a);
  QApplication::processEvents();
  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  auto* chrome_close = panel->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton"));
  CHECK(chrome_close != nullptr);
  QPointer<patchy::ui::AnimationPreviewWindow> closed_panel_guard(panel);
  chrome_close->click();
  QApplication::processEvents();
  CHECK(closed_panel_guard == nullptr);
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  // Closing the playing document's tab stops playback before the document is destroyed.
  animation_button->click();
  QApplication::processEvents();
  panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  play_button->click();
  CHECK(panel->playing());
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, 0));
  QApplication::processEvents();
  CHECK(!panel->playing());
}

// The panel has to fit its wrapped hint text under whatever font the platform supplies.
// The browser build sets the UI in Noto Sans (see docs/fonts.md), which is wider than
// Windows' Segoe UI, so the hint wrapped onto an extra line and the panel's hardcoded
// opening height cut it off. This opens the panel under the browser build's own font,
// and again at a deliberately larger size, and checks the text fits both times.
void ui_animation_preview_fits_its_text_at_larger_fonts() {
  AnimationPreviewSettingsReset preview_settings;  // the note shown
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);

  const auto open_panel = [&window]() -> patchy::ui::AnimationPreviewWindow* {
    auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
    CHECK(animation_button != nullptr);
    animation_button->click();
    QApplication::processEvents();
    return window.findChild<patchy::ui::AnimationPreviewWindow*>(
        QStringLiteral("animationPreviewWindow"));
  };
  const auto check_fits = [](patchy::ui::AnimationPreviewWindow& panel, const char* label) {
    auto* hint = panel.findChild<QLabel*>(QStringLiteral("animationPreviewHintLabel"));
    std::puts(QStringLiteral("  [%1] panel=%2x%3 hint=%4x%5 needs=%6")
                  .arg(QString::fromUtf8(label))
                  .arg(panel.width())
                  .arg(panel.height())
                  .arg(hint != nullptr ? hint->width() : -1)
                  .arg(hint != nullptr ? hint->height() : -1)
                  .arg(hint != nullptr ? hint->heightForWidth(hint->width()) : -1)
                  .toUtf8()
                  .constData());
    const auto clipped = patchy::test::ui::clipped_labels(panel);
    if (!clipped.isEmpty()) {
      std::printf("  clipped at %s: %s\n", label, clipped.join(QStringLiteral("; ")).toUtf8().constData());
    }
    CHECK(clipped.isEmpty());
  };

  auto* panel = open_panel();
  CHECK(panel != nullptr);
  check_fits(*panel, "the suite default font");
  // Closed so each font gets a freshly built panel, the way the browser build builds one
  // under its own font.
  panel->close();
  QApplication::processEvents();

  // The theme drives fonts through QSS rather than setFont, so a QSS rule is what a
  // different platform font looks like to the layout. Stylesheets propagate from the
  // window into the panel, which is its child.
  const auto original_style = window.styleSheet();
  const auto open_with_hint_font = [&](const QString& rule, const char* label) {
    window.setStyleSheet(original_style +
                         QStringLiteral("\nQLabel#animationPreviewHintLabel { %1 }").arg(rule));
    auto* styled = open_panel();
    CHECK(styled != nullptr);
    if (styled != nullptr) {
      check_fits(*styled, label);
      styled->close();
      QApplication::processEvents();
    }
  };

  // The browser build's actual UI font, if the bundled web tree is staged.
  const auto noto_path =
      QStringLiteral(PATCHY_SOURCE_DIR "/third_party/fonts-web/noto_sans/NotoSans-Regular.ttf");
  if (QFileInfo::exists(noto_path) && QFontDatabase::addApplicationFont(noto_path) >= 0) {
    open_with_hint_font(QStringLiteral("font-family: \"Noto Sans\";"), "the browser build's Noto Sans");
  }
  // A font well past any real platform's, so the panel has to grow rather than clip.
  open_with_hint_font(QStringLiteral("font-size: 20px;"), "a 20px font");

  window.setStyleSheet(original_style);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests_animation_part() {
  return {
      {"ui_animation_preview_plays_visible_layers_and_restores",
       ui_animation_preview_plays_visible_layers_and_restores},
      {"ui_animation_preview_sets_and_removes_frame_time_names",
       ui_animation_preview_sets_and_removes_frame_time_names},
      {"ui_animation_preview_reverses_selected_layers", ui_animation_preview_reverses_selected_layers},
      {"ui_animation_preview_plays_in_reverse", ui_animation_preview_plays_in_reverse},
      {"ui_animation_preview_note_closes_and_comes_back", ui_animation_preview_note_closes_and_comes_back},
      {"ui_animation_preview_info_mark_clears_the_frame_line", ui_animation_preview_info_mark_clears_the_frame_line},
      {"ui_animation_preview_ignores_name_delays_and_plays_hidden_layers",
       ui_animation_preview_ignores_name_delays_and_plays_hidden_layers},
      {"ui_animation_preview_sets_and_removes_frame_time_on_all_layers",
       ui_animation_preview_sets_and_removes_frame_time_on_all_layers},
      {"ui_animation_preview_stops_on_tab_switch_and_close",
       ui_animation_preview_stops_on_tab_switch_and_close},
      {"ui_animation_preview_fits_its_text_at_larger_fonts",
       ui_animation_preview_fits_its_text_at_larger_fonts},
      {"ui_animated_export_all_layers_render_shared_and_grouped_frames",
       ui_animated_export_all_layers_render_shared_and_grouped_frames},
      {"ui_animated_export_all_layers_trim_includes_shared_layers",
       ui_animated_export_all_layers_trim_includes_shared_layers},
      {"ui_animation_preview_plays_all_layers", ui_animation_preview_plays_all_layers},
      {"ui_animation_texts_explain_all_layers_in_every_language",
       ui_animation_texts_explain_all_layers_in_every_language},
  };
}
