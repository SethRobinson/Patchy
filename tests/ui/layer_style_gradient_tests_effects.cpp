#include "ui/curved_slider.hpp"
#include "ui/angle_dial.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/app_settings.hpp"

#include "ui/canvas_widget.hpp"
#include "core/document_depth.hpp"
#include "core/pattern_presets.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/curves_editor.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "formats/xcf_document_io.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizeGrip>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "local_psd_fixtures.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_blend_if_range_editor_renders_and_edits_split_handles() {
  patchy::ui::BlendIfRangeEditorWidget editor;
  editor.resize(420, 56);
  editor.set_accessibility_text(QStringLiteral("Test Blend If range"),
                                QStringLiteral("Alt-drag splits a joined handle"));
  editor.set_ramp_channel(patchy::BlendIfChannel::Blue);
  editor.set_thresholds(patchy::BlendIfThresholds{40, 40, 210, 210});
  editor.show();
  QApplication::processEvents();
  editor.setFocus(Qt::OtherFocusReason);

  struct Change {
    patchy::BlendIfThresholds value;
    bool immediate{false};
  };
  std::vector<Change> changes;
  editor.changed = [&](patchy::BlendIfThresholds value, bool immediate) {
    changes.push_back(Change{value, immediate});
    editor.set_thresholds(value);
  };

  constexpr int ramp_left = 12;
  const auto x_for_value = [&](int value) {
    return ramp_left + static_cast<int>(std::lround(static_cast<double>(value) / 255.0 *
                                                    static_cast<double>(editor.width() - 26)));
  };
  const QPoint joined_black_handle(x_for_value(40) + 3, 36);
  const QPoint split_black_handle = joined_black_handle + QPoint(60, 0);
  send_mouse(editor, QEvent::MouseButtonPress, joined_black_handle, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(editor, QEvent::MouseMove, split_black_handle, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(editor, QEvent::MouseButtonRelease, split_black_handle, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);

  CHECK(editor.value().black_low == 40);
  CHECK(editor.value().black_high > editor.value().black_low);
  CHECK(changes.size() >= 2);
  CHECK(!changes.front().immediate);
  CHECK(changes.back().immediate);

  editor.set_thresholds(patchy::BlendIfThresholds{20, 50, 180, 230});
  changes.clear();
  send_key(editor, Qt::Key_Right, Qt::ShiftModifier);
  CHECK(editor.value().black_high == 60);
  CHECK(!changes.empty() && changes.back().immediate);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_Home);
  CHECK(editor.value().white_low == 60);
  const auto changes_at_white_minimum = changes.size();
  send_key(editor, Qt::Key_Left);
  CHECK(editor.value().white_low == 60);
  CHECK(changes.size() == changes_at_white_minimum);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_End);
  CHECK(editor.value().white_high == 255);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_Home);
  send_key(editor, Qt::Key_Right, Qt::ShiftModifier);
  CHECK(editor.value().black_low == 10);
  const auto before_page_up = editor.value();
  send_key(editor, Qt::Key_PageUp);
  CHECK(editor.value() == before_page_up);
  CHECK(editor.accessibleName() == QStringLiteral("Test Blend If range"));
  CHECK(editor.accessibleDescription().contains(QStringLiteral("Alt-drag")));
  editor.set_thresholds(patchy::BlendIfThresholds{20, 70, 175, 235});
  QApplication::processEvents();
  save_widget_artifact("ui_blend_if_range_editor", editor);
}

void ui_layer_style_blend_if_controls_load_channels_and_map_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Blend If Controls",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto original = blend_if_ui_test_settings();
  layer.set_blend_if_payload(patchy::encode_layer_blend_if(original), true);
  CHECK(layer.blend_if_payload_status() == patchy::BlendIfPayloadStatus::Supported);

  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* group = dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendIfChannelCombo"));
    auto* reset_channel =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfResetChannelButton"));
    auto* this_editor = dynamic_cast<patchy::ui::BlendIfRangeEditorWidget*>(
        dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfThisEditor")));
    auto* underlying_editor = dynamic_cast<patchy::ui::BlendIfRangeEditorWidget*>(
        dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfUnderlyingEditor")));
    auto* this_black_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    auto* this_black_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* this_white_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisWhiteLowSpin"));
    auto* this_white_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisWhiteHighSpin"));
    auto* underlying_black_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingBlackLowSpin"));
    auto* underlying_black_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingBlackHighSpin"));
    auto* underlying_white_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteLowSpin"));
    auto* underlying_white_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteHighSpin"));
    CHECK(group != nullptr && group->isEnabled());
    CHECK(channel != nullptr);
    CHECK(reset_channel != nullptr);
    CHECK(this_editor != nullptr);
    CHECK(underlying_editor != nullptr);
    CHECK(this_black_low != nullptr);
    CHECK(this_black_high != nullptr);
    CHECK(this_white_low != nullptr);
    CHECK(this_white_high != nullptr);
    CHECK(underlying_black_low != nullptr);
    CHECK(underlying_black_high != nullptr);
    CHECK(underlying_white_low != nullptr);
    CHECK(underlying_white_high != nullptr);
    CHECK(this_editor->accessibleName() == QStringLiteral("This Layer Blend If range"));
    CHECK(underlying_editor->accessibleName() == QStringLiteral("Underlying Layer Blend If range"));
    CHECK(this_editor->accessibleDescription().contains(QStringLiteral("Page Up or Page Down")));
    CHECK(this_black_low->accessibleName() == QStringLiteral("This Layer black transition start"));
    CHECK(this_black_high->accessibleName() == QStringLiteral("This Layer black transition end"));
    CHECK(this_white_low->accessibleName() == QStringLiteral("This Layer white transition start"));
    CHECK(this_white_high->accessibleName() == QStringLiteral("This Layer white transition end"));
    CHECK(underlying_black_low->accessibleName() ==
          QStringLiteral("Underlying Layer black transition start"));
    CHECK(underlying_black_high->accessibleName() ==
          QStringLiteral("Underlying Layer black transition end"));
    CHECK(underlying_white_low->accessibleName() ==
          QStringLiteral("Underlying Layer white transition start"));
    CHECK(underlying_white_high->accessibleName() ==
          QStringLiteral("Underlying Layer white transition end"));

    CHECK(channel->currentData().toInt() == static_cast<int>(patchy::BlendIfChannel::Gray));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Gray);
    CHECK(this_black_low->value() == 10);
    CHECK(this_black_high->value() == 20);
    CHECK(underlying_white_low->value() == 190);

    // GitHub issue 606: the fields have no - / + buttons; the arrow keys step
    // them. A joined handle still splits: the first field steps the left half
    // down and the second the right half up, never swapping.
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpinDecreaseButton")) ==
          nullptr);
    this_black_high->setValue(74);
    this_black_low->setValue(74);
    this_black_low->stepBy(-1);
    CHECK(this_black_low->value() == 73);
    CHECK(this_black_high->value() == 74);
    this_black_low->stepBy(1);
    CHECK(this_black_low->value() == 74);
    this_black_high->stepBy(-1);
    CHECK(this_black_high->value() == 74);
    this_black_high->stepBy(1);
    CHECK(this_black_high->value() == 75);
    this_black_high->stepBy(-1);
    CHECK(this_black_high->value() == 74);
    // Both values share the This Layer line.
    CHECK(std::abs(this_black_low->mapTo(dialog, QPoint(0, 0)).y() -
                   this_white_high->mapTo(dialog, QPoint(0, 0)).y()) <= 2);
    this_black_low->setValue(10);
    this_black_high->setValue(20);
    auto* white_value_edit = this_white_low->findChild<QLineEdit*>();
    CHECK(white_value_edit != nullptr);
    CHECK(white_value_edit->width() >= 36);
    CHECK(white_value_edit->text() == QStringLiteral("200"));
    save_widget_artifact("ui_layer_style_blend_if_controls", *dialog);

    this_black_high->setValue(25);
    underlying_white_low->setValue(185);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Red)));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Red);
    CHECK(this_black_low->value() == 30);
    CHECK(this_black_high->value() == 40);
    CHECK(underlying_black_low->value() == 32);
    this_black_high->setValue(45);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Blue)));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Blue);
    CHECK(this_black_low->value() == 70);
    CHECK(underlying_black_high->value() == 82);
    CHECK(underlying_white_high->value() == 170);
    underlying_white_high->setValue(175);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Gray)));
    CHECK(this_black_high->value() == 25);
    CHECK(underlying_white_low->value() == 185);
    inspected = true;
    dialog->accept();
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  CHECK(settings.has_value());
  CHECK((settings->blend_if.channels[0].this_layer == patchy::BlendIfThresholds{10, 25, 200, 240}));
  CHECK((settings->blend_if.channels[0].underlying_layer == patchy::BlendIfThresholds{12, 24, 185, 230}));
  CHECK((settings->blend_if.channels[1].this_layer == patchy::BlendIfThresholds{30, 45, 180, 220}));
  CHECK(settings->blend_if.channels[2] == original.channels[2]);
  CHECK((settings->blend_if.channels[3].underlying_layer == patchy::BlendIfThresholds{72, 82, 130, 175}));
}

void ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores() {
  patchy::Document direct_document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer direct_layer(direct_document.allocate_layer_id(), "Blend If Preview",
                             solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto original = blend_if_ui_test_settings();
  const auto original_payload = patchy::encode_layer_blend_if(original);
  direct_layer.set_blend_if_payload(original_payload, true);

  std::vector<patchy::ui::LayerStyleSettings> previews;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
    auto* black_high = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    CHECK(preview != nullptr && preview->isChecked());
    CHECK(black_high != nullptr);
    black_high->setValue(28);
    // The preview arrives on the coalescing timer; a fixed wait was too short on a busy ARM64 host.
    CHECK(QTest::qWaitFor(
        [&] { return !previews.empty() && previews.back().blend_if.channels[0].this_layer.black_high == 28; },
        5000));

    preview->setChecked(false);
    QApplication::processEvents();
    CHECK(previews.back().blend_if == original);
    black_high->setValue(32);
    QTest::qWait(80);
    CHECK(previews.back().blend_if == original);
    dialog->accept();
  }));

  const auto accepted = patchy::ui::request_layer_style_settings(
      nullptr, direct_layer,
      [&](const patchy::ui::LayerStyleSettings& settings) { previews.push_back(settings); });
  CHECK(accepted.has_value());
  CHECK(accepted->blend_if.channels[0].this_layer.black_high == 32);
  CHECK(!previews.empty());
  CHECK(previews.back().blend_if == original);

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Blend If Cancel",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_blend_if_payload(original_payload, true);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Blend If Cancel"));
  show_window(window);
  bool saw_live_preview = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* black_high = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    CHECK(black_high != nullptr);
    black_high->setValue(35);
    QTest::qWait(80);
    const auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
    CHECK(preview_layer != nullptr);
    saw_live_preview = preview_layer->blend_if().channels[0].this_layer.black_high == 35;
    dialog->reject();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_live_preview);
  const auto* restored = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->raw_psd_blending_ranges() == original_payload);
  CHECK(restored->blend_if() == original);
}

void ui_layer_style_blend_if_unsupported_requires_explicit_replace() {
  constexpr std::array<std::uint8_t, 8> unsupported_payload{0, 0, 255, 255, 0, 0, 255, 255};
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Unsupported Blend If",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_blend_if_payload(std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()), true);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Unsupported Blend If"));
  show_window(window);
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  const auto run_style_dialog = [&](const std::function<void(QDialog&)>& interaction) {
    bool handled = false;
    QTimer::singleShot(0, &window, guarded([&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
      CHECK(dialog != nullptr);
      interaction(*dialog);
      handled = true;
    }));
    blending_options->trigger();
    QApplication::processEvents();
    CHECK(handled);
  };

  run_style_dialog([](QDialog& dialog) {
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog.findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    CHECK(warning != nullptr && warning->isVisible());
    CHECK(warning->text().contains(QStringLiteral("unsupported color mode or payload shape")));
    CHECK(group != nullptr && !group->isEnabled());
    CHECK(replace != nullptr && replace->isEnabled());
    dialog.accept();
  });
  const auto* preserved_without_replace =
      patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(preserved_without_replace != nullptr);
  CHECK(preserved_without_replace->raw_psd_blending_ranges() ==
        std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));

  run_style_dialog([](QDialog& dialog) {
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog.findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    auto* black_high = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* black_low = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    CHECK(warning != nullptr);
    CHECK(group != nullptr);
    CHECK(replace != nullptr);
    CHECK(black_high != nullptr);
    CHECK(black_low != nullptr);
    replace->click();
    QApplication::processEvents();
    CHECK(group->isEnabled());
    CHECK(!replace->isEnabled());
    CHECK(warning->text().contains(QStringLiteral("will be replaced with editable RGB defaults")));
    black_high->setValue(35);
    black_low->setValue(15);
    dialog.reject();
  });
  const auto* preserved_after_cancel = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(preserved_after_cancel != nullptr);
  CHECK(preserved_after_cancel->raw_psd_blending_ranges() ==
        std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));

  run_style_dialog([](QDialog& dialog) {
    auto* group = dialog.findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    auto* black_high = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* black_low = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    CHECK(group != nullptr);
    CHECK(replace != nullptr);
    CHECK(black_high != nullptr);
    CHECK(black_low != nullptr);
    replace->click();
    QApplication::processEvents();
    CHECK(group->isEnabled());
    black_high->setValue(35);
    black_low->setValue(15);
    dialog.accept();
  });
  const auto* replaced = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(replaced != nullptr);
  CHECK(replaced->blend_if_payload_status() == patchy::BlendIfPayloadStatus::Supported);
  CHECK(replaced->raw_psd_blending_ranges().size() == 40);
  CHECK((replaced->blend_if().channels[0].this_layer == patchy::BlendIfThresholds{15, 35, 255, 255}));

  patchy::Document non_rgb_document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer non_rgb_layer(non_rgb_document.allocate_layer_id(), "Non-RGB Blend If",
                              solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto non_rgb_payload = patchy::encode_layer_blend_if(blend_if_ui_test_settings());
  non_rgb_layer.set_blend_if_payload(non_rgb_payload, false);
  CHECK(non_rgb_layer.blend_if_payload_status() == patchy::BlendIfPayloadStatus::Unsupported);
  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    CHECK(warning != nullptr && warning->isVisible());
    CHECK(group != nullptr && !group->isEnabled());
    dialog->reject();
  }));
  const auto non_rgb_result = patchy::ui::request_layer_style_settings(nullptr, non_rgb_layer, {});
  CHECK(!non_rgb_result.has_value());
  CHECK(non_rgb_layer.raw_psd_blending_ranges() == non_rgb_payload);
  CHECK(!non_rgb_layer.blend_if_rgb_compatible());
}

void ui_layer_style_shadow_distances_accept_photoshop_30000() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Far Shadows",
                      solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 6.0F;
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerInnerShadow inner;
  inner.enabled = true;
  inner.distance = 4.0F;
  layer.layer_style().inner_shadows.push_back(inner);

  bool drove_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* drop = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
    auto* drop_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
    auto* inner_spin = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerShadowDistanceSpin"));
    auto* inner_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleInnerShadowDistanceSlider"));
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    CHECK(drop != nullptr && drop_slider != nullptr && inner_spin != nullptr && inner_slider != nullptr);
    // Edits commit to the selected effect category.
    const auto select_category = [categories](const QString& name) {
      const auto items = categories->findItems(name, Qt::MatchExactly);
      CHECK(!items.empty());
      categories->setCurrentItem(items.front());
    };
    select_category(QStringLiteral("Drop Shadow"));
    // Photoshop's typed 0..30000 px; the curved sliders keep their historical
    // reach (2000 and 1000 at the track's end).
    CHECK(drop->maximum() == 30000 && drop_slider->maximum() == patchy::ui::kCurvedSliderPositions);
    CHECK(inner_spin->maximum() == 30000 && inner_slider->maximum() == patchy::ui::kCurvedSliderPositions);
    // A typed value past the slider parks the slider at its end without
    // echoing back into the spin box.
    drop->setValue(5000);
    CHECK(drop->value() == 5000 && drop_slider->value() == patchy::ui::kCurvedSliderPositions);
    CHECK(patchy::ui::slider_value(*drop_slider) == 2000);
    patchy::ui::set_slider_to_value(*drop_slider, 120);
    CHECK(drop->value() == 120);
    drop->setValue(25000);
    select_category(QStringLiteral("Inner Shadow"));
    inner_spin->setValue(20000);
    CHECK(inner_spin->value() == 20000 && patchy::ui::slider_value(*inner_slider) == 1000);
    drove_dialog = true;
    dialog->accept();
  }));
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(drove_dialog);
  CHECK(settings.has_value());
  CHECK(settings->style.drop_shadows.size() == 1U && settings->style.inner_shadows.size() == 1U);
  CHECK(std::lround(settings->style.drop_shadows.front().distance) == 25000);
  CHECK(std::lround(settings->style.inner_shadows.front().distance) == 20000);
}

void ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Coalesced Style",
                      solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.6F;
  shadow.distance = 6.0F;
  shadow.size = 4.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  int preview_calls = 0;
  int latest_preview_opacity = -1;
  int latest_preview_shadow_distance = -1;
  bool queued_slow_changes = false;

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
    auto* shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
    auto* shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
    CHECK(categories != nullptr);
    CHECK(opacity != nullptr);
    CHECK(opacity_slider != nullptr);
    CHECK(shadow_distance != nullptr);
    CHECK(shadow_distance_slider != nullptr);
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    categories->setCurrentItem(shadow_items.front());
    for (int value = 1; value <= 24; ++value) {
      opacity_slider->setValue(value);
      patchy::ui::set_slider_to_value(*shadow_distance_slider, value);
      CHECK(opacity->value() == value);
      CHECK(shadow_distance->value() == value);
    }
    QTimer::singleShot(360, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings& preview) {
        ++preview_calls;
        latest_preview_opacity = preview.opacity;
        latest_preview_shadow_distance =
            preview.style.drop_shadows.empty()
                ? -1
                : static_cast<int>(std::lround(preview.style.drop_shadows.front().distance));
        if (!queued_slow_changes && latest_preview_opacity == 24 && latest_preview_shadow_distance == 24) {
          queued_slow_changes = true;
          QTimer::singleShot(0, guarded([] {
            auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
            CHECK(dialog != nullptr);
            auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
            auto* shadow_distance_slider =
                dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
            CHECK(opacity_slider != nullptr);
            CHECK(shadow_distance_slider != nullptr);
            for (int value = 25; value <= 48; ++value) {
              opacity_slider->setValue(value);
              patchy::ui::set_slider_to_value(*shadow_distance_slider, value + 6);
            }
          }));
          QElapsedTimer slow_preview;
          slow_preview.start();
          while (slow_preview.elapsed() < 45) {
          }
        }
      });

  CHECK(settings.has_value());
  CHECK(settings->opacity == 48);
  CHECK(!settings->style.drop_shadows.empty());
  CHECK(static_cast<int>(std::lround(settings->style.drop_shadows.front().distance)) == 54);
  CHECK(queued_slow_changes);
  CHECK(preview_calls >= 2);
  CHECK(preview_calls < 10);
  CHECK(latest_preview_opacity == 48);
  CHECK(latest_preview_shadow_distance == 54);
}

void ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Slow Style Preview",
                      solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(60, 120, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{48, 38, 120, 90});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Slow Style Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_render_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("520"));

  bool exercised_slider = false;
  qint64 elapsed_ms = 0;
  int awaited_renders = -1;
  // The property under test is structural: no slider change may make the GUI
  // thread wait on the (synthetically 520 ms) render, which awaited_renders
  // counts exactly. The wall clock is only a hang guard; a tight budget here
  // measured host load, not blocking (a full suite on a busy host overran
  // 1200 ms while the loop itself takes about 160 ms and awaits none).
  constexpr qint64 kResponsiveSliderLoopBudgetMs = patchy::test::kWallClockBackstopMs;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
    CHECK(opacity != nullptr);
    CHECK(opacity_slider != nullptr);
    const auto before = canvas->render_cache_diagnostics();
    QElapsedTimer timer;
    timer.start();
    for (int value = 99; value >= 52; --value) {
      opacity_slider->setValue(value);
      CHECK(opacity->value() == value);
      QApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    elapsed_ms = timer.elapsed();
    const auto after = canvas->render_cache_diagnostics();
    awaited_renders = after.awaited_renders - before.awaited_renders;
    std::cout << "[INFO] opacity slider loop: " << elapsed_ms << " ms, awaited renders " << awaited_renders
              << '\n';
    exercised_slider = true;
    CHECK(awaited_renders == 0);
    CHECK(elapsed_ms < kResponsiveSliderLoopBudgetMs);
    QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
  }));

  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(exercised_slider);
  CHECK(awaited_renders == 0);
  CHECK(elapsed_ms < kResponsiveSliderLoopBudgetMs);
  const auto* edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 0.52F) <= 0.001F);
}

// GitHub #601: slider drags in dialogs on a large image. A drag is one value
// change per 60 Hz frame followed by the event processing that frame gets; the
// longest single event-loop pass is the stall a user sees as a stuck handle.
struct SliderDragTiming {
  int steps{0};
  qint64 total_ms{0};
  qint64 worst_pass_ms{0};
  // Passes of kSlowSliderPassMs or more of wall time, reported for diagnosis: a loaded host adds
  // them without any work on the GUI thread (see worst_pass_cpu_ms).
  int slow_passes{0};
  // The most GUI-thread CPU time one event pass used, or negative where the platform cannot
  // tell. Work on the GUI thread (the #601 mip rebuild) shows here; a pass the host did not
  // schedule shows only in worst_pass_ms.
  double worst_pass_cpu_ms{-1.0};
  qint64 settle_ms{0};
  // The synchronous cost of one value change (the handler chain a motion event runs).
  double change_mean_ms{0.0};
  double change_worst_ms{0.0};
};

constexpr qint64 kSlowSliderPassMs = 60;
// The coarsest thread CPU accounting the suites run on: Windows counts in 15.625 ms clock ticks.
constexpr double kThreadCpuTickMs = 15.625;

QSize slider_drag_document_size() {
  QSize size(4000, 3000);
  if (const auto text = qEnvironmentVariable("PATCHY_TEST_601_SIZE"); !text.isEmpty()) {
    const auto parts = text.split(QLatin1Char('x'));
    if (parts.size() == 2) {
      size = QSize(parts[0].toInt(), parts[1].toInt());
    }
  }
  return size;
}

// A white background and an anti-aliased ellipse layer over the middle 36% of
// the canvas (under half, so a canvas patch for it is a partial refresh).
patchy::Document slider_drag_document(bool stroked, patchy::LayerId& layer_id) {
  const auto size = slider_drag_document_size();
  const int width = size.width();
  const int height = size.height();
  const auto format = patchy::PixelFormat::rgba8();
  patchy::Document document(width, height, format);
  document.add_pixel_layer("Background", solid_pixels(width, height, format, QColor(Qt::white)));
  QImage shape(width * 6 / 10, height * 6 / 10, QImage::Format_RGBA8888);
  shape.fill(Qt::transparent);
  {
    QPainter painter(&shape);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(60, 120, 220));
    painter.drawEllipse(QRectF(0, 0, shape.width(), shape.height()).adjusted(4, 4, -4, -4));
  }
  patchy::PixelBuffer pixels(shape.width(), shape.height(), format);
  for (int y = 0; y < shape.height(); ++y) {
    std::memcpy(pixels.pixel(0, y), shape.constScanLine(y), static_cast<std::size_t>(shape.width()) * 4);
  }
  patchy::Layer layer(document.allocate_layer_id(), "Shape", std::move(pixels));
  layer.set_bounds(patchy::Rect{width * 2 / 10, height * 2 / 10, shape.width(), shape.height()});
  if (stroked) {
    patchy::LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = 29.0F;
    stroke.color = {200, 40, 40};
    layer.layer_style().strokes = {stroke};
  }
  layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  if (qEnvironmentVariableIntValue("PATCHY_TEST_601_DEEP") != 0) {
    patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
  }
  return document;
}

SliderDragTiming drive_slider_drag(patchy::ui::CanvasWidget& canvas, const std::vector<int>& values,
                                   const std::function<void(int)>& set_value,
                                   const std::function<void(int)>& after_step = {}) {
  SliderDragTiming timing;
  QElapsedTimer total;
  total.start();
  double change_total_ms = 0.0;
  for (const auto value : values) {
    QElapsedTimer change;
    change.start();
    set_value(value);
    const auto change_ms = static_cast<double>(change.nsecsElapsed()) / 1.0e6;
    change_total_ms += change_ms;
    timing.change_worst_ms = std::max(timing.change_worst_ms, change_ms);
    ++timing.steps;
    QElapsedTimer frame;
    frame.start();
    while (frame.elapsed() < 16) {
      QElapsedTimer pass;
      pass.start();
      const auto cpu_before = patchy::test::current_thread_cpu_ms();
      QApplication::processEvents(QEventLoop::AllEvents, 1);
      const auto pass_ms = pass.elapsed();
      const auto cpu_after = patchy::test::current_thread_cpu_ms();
      if (cpu_before >= 0.0 && cpu_after >= 0.0) {
        timing.worst_pass_cpu_ms = std::max(timing.worst_pass_cpu_ms, cpu_after - cpu_before);
      }
      timing.worst_pass_ms = std::max(timing.worst_pass_ms, pass_ms);
      if (pass_ms >= kSlowSliderPassMs) {
        ++timing.slow_passes;
      }
    }
    if (after_step) {
      after_step(timing.steps);
    }
  }
  timing.total_ms = total.elapsed();
  timing.change_mean_ms = timing.steps > 0 ? change_total_ms / timing.steps : 0.0;
  // Time until the last value is on the canvas.
  QElapsedTimer settle;
  settle.start();
  while (settle.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::AllEvents, 5);
    if (settle.elapsed() > 100 && canvas.render_settled()) {
      break;
    }
  }
  timing.settle_ms = settle.elapsed();
  return timing;
}

void print_slider_drag_timing(const char* what, const SliderDragTiming& timing) {
  const auto size = slider_drag_document_size();
  std::printf("[601] %s %dx%d %s: %d steps in %lld ms, value change mean %.2f ms worst %.2f ms, worst event pass "
              "%lld ms, settle %lld ms\n",
              what, size.width(), size.height(),
              qEnvironmentVariableIntValue("PATCHY_TEST_601_DEEP") != 0 ? "16-bit" : "8-bit", timing.steps,
              static_cast<long long>(timing.total_ms), timing.change_mean_ms, timing.change_worst_ms,
              static_cast<long long>(timing.worst_pass_ms), static_cast<long long>(timing.settle_ms));
  std::fflush(stdout);
}

std::vector<int> slider_drag_values(int from, int to, int step) {
  std::vector<int> values;
  for (int value = from; step > 0 ? value <= to : value >= to; value += step) {
    values.push_back(value);
  }
  return values;
}

// Pumps events until the canvas holds a settled frame equal to a fresh render of
// the document (the preview that landed last is the current one, rendered
// exactly as the non-coalesced path would).
bool wait_for_canvas_to_match_document(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas) {
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    // Past the preview throttle, with no dialog preview render still running.
    if (wait.elapsed() < 150 || canvas.preview_renders_in_flight() > 0 || !canvas.render_settled()) {
      continue;
    }
    const auto shown = canvas.current_composite();
    if (shown.isNull()) {
      continue;
    }
    const auto expected =
        patchy::ui::qimage_from_document(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), true)
            .convertToFormat(QImage::Format_RGBA8888);
    if (shown.convertToFormat(QImage::Format_RGBA8888) == expected) {
      return true;
    }
  }
  return false;
}

// The Layer Style dialog's Stroke Size, the reporter's case: the per-move cost
// stays small, the canvas follows during the drag (the preview is throttled,
// not held until the drag pauses), nothing waits on a render, and the frame
// that settles is the exact render of the final value.
void ui_layer_style_stroke_size_drag_stays_responsive_on_large_image() {
  patchy::LayerId layer_id = 0;
  patchy::ui::MainWindow window;
  window.add_document_session(slider_drag_document(true, layer_id), QStringLiteral("Stroke Drag"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();
  const auto stroke_size_in_document = [&window, layer_id] {
    const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
    return layer != nullptr && !layer->layer_style().strokes.empty()
               ? static_cast<int>(std::lround(layer->layer_style().strokes.front().size))
               : -1;
  };

  bool exercised = false;
  bool settled_exactly = false;
  int size_mid_drag = -1;
  int awaited_renders = -1;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    for (int row = 0; row < categories->count(); ++row) {
      if (categories->item(row)->text() == QStringLiteral("Stroke")) {
        categories->setCurrentRow(row);
        break;
      }
    }
    QApplication::processEvents();
    auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
    CHECK(size != nullptr);
    const auto before = canvas->render_cache_diagnostics();
    const auto timing = drive_slider_drag(
        *canvas, slider_drag_values(31, 184, 5), [size](int value) { size->setValue(value); },
        [&](int step) {
          if (step == 20) {
            size_mid_drag = stroke_size_in_document();
          }
        });
    print_slider_drag_timing("layer style stroke size", timing);
    awaited_renders = canvas->render_cache_diagnostics().awaited_renders - before.awaited_renders;
    settled_exactly = wait_for_canvas_to_match_document(window, *canvas);
    exercised = true;
    QTimer::singleShot(0, dialog, [dialog] { dialog->accept(); });
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(exercised);
  // Twenty moves in (about 320 ms of dragging), the canvas already previews a
  // size past the starting 29; a debounced preview stayed at 29 until a pause.
  CHECK(size_mid_drag > 29);
  CHECK(awaited_renders == 0);
  CHECK(settled_exactly);
  CHECK(stroke_size_in_document() == 181);
}

// GitHub #601 follow-up, the reporter's second video: a 3580 x 5370 photo
// zoomed to fit, a small layer, Stroke Size dragged from 4 to over 200, then
// Opacity. Each preview frame that landed made the GUI thread rebuild the
// zoomed-out display mips of the whole frame (70 to 146 ms per frame on an
// i9-11900KF), so the handle froze; the drag took five times its length. The
// worker now builds the mips with the frame and, under the dialog, re-renders
// only what changed over the frame on screen; a full render settles it.
void ui_layer_style_drag_on_zoomed_out_large_image_never_stalls() {
  constexpr int kWidth = 3580;
  constexpr int kHeight = 5370;
  const auto format = patchy::PixelFormat::rgba8();
  patchy::Document document(kWidth, kHeight, format);
  // Photo-like noise, so no solid-color shortcut makes the composite cheap.
  patchy::PixelBuffer background(kWidth, kHeight, format);
  for (int y = 0; y < kHeight; ++y) {
    auto* row = background.pixel(0, y);
    for (int x = 0; x < kWidth; ++x) {
      auto hash = static_cast<std::uint32_t>(x) * 2654435761U ^ static_cast<std::uint32_t>(y) * 2246822519U;
      hash ^= hash >> 13U;
      row[x * 4 + 0] = static_cast<std::uint8_t>((x * 255 / kWidth + (hash & 31U)) & 255U);
      row[x * 4 + 1] = static_cast<std::uint8_t>((y * 255 / kHeight + ((hash >> 5U) & 31U)) & 255U);
      row[x * 4 + 2] = static_cast<std::uint8_t>((hash >> 10U) & 255U);
      row[x * 4 + 3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(background));
  patchy::Layer layer(document.allocate_layer_id(), "Layer 2", solid_pixels(760, 900, format, QColor(150, 130, 130)));
  layer.set_bounds(patchy::Rect{1400, 2000, 760, 900});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 4.0F;
  stroke.color = {255, 0, 0};
  layer.layer_style().strokes = {stroke};
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.size = 5.0F;
  layer.layer_style().drop_shadows = {shadow};
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Zoomed Out Drag"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  canvas->fit_to_view();
  QApplication::processEvents();
  CHECK(canvas->view_zoom() < 0.25);

  struct Drag {
    const char* category;
    const char* spin;
    int from;
    int to;
    int step;
  };
  const std::vector<Drag> drags{{"Stroke", "layerStyleStrokeSizeSpin", 4, 241, 3},
                                {"Stroke", "layerStyleStrokeOpacitySpin", 100, 10, -2},
                                {"Drop Shadow", "layerStyleDropShadowSizeSpin", 5, 250, 3}};
  bool exercised = false;
  bool settled_exactly = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    for (const auto& drag : drags) {
      for (int row = 0; row < categories->count(); ++row) {
        if (categories->item(row)->text() == QLatin1String(drag.category)) {
          categories->setCurrentRow(row);
          break;
        }
      }
      QApplication::processEvents();
      auto* spin = dialog->findChild<QSpinBox*>(QLatin1String(drag.spin));
      CHECK(spin != nullptr);
      const auto before = canvas->render_cache_diagnostics();
      const auto timing = drive_slider_drag(*canvas, slider_drag_values(drag.from, drag.to, drag.step),
                                            [spin](int value) { spin->setValue(value); });
      const auto after = canvas->render_cache_diagnostics();
      std::printf("[601] zoomed out %s: %d steps in %lld ms, worst event pass %lld ms (%d slow, most GUI CPU in "
                  "one pass %.1f ms), settle %lld ms, %d preview frames (%d region), %d with worker mips, %d GUI mip "
                  "rebuilds\n",
                  drag.spin, timing.steps, static_cast<long long>(timing.total_ms),
                  static_cast<long long>(timing.worst_pass_ms), timing.slow_passes, timing.worst_pass_cpu_ms,
                  static_cast<long long>(timing.settle_ms),
                  after.intermediate_refreshes - before.intermediate_refreshes,
                  after.region_preview_refreshes - before.region_preview_refreshes,
                  after.background_display_mips - before.background_display_mips,
                  after.display_mip_rebuilds - before.display_mip_rebuilds);
      std::fflush(stdout);
      // Before: one GUI mip rebuild per frame, 98 to 146 ms of GUI work each on the i9-11900KF. Now
      // no event pass does kSlowSliderPassMs of GUI work (plus one 15.625 ms tick of Windows CPU
      // accounting). Bound CPU time, not wall time: a host that does not schedule the GUI thread
      // adds wall time with no work in it (November 6, 2026: the 1.27 binary, clean the day
      // before, showed isolated 90 to 210 ms passes on a busy dev box, all at one tick of CPU or
      // less, and an injected 100 ms of GUI work failed this check every run). A GUI thread
      // blocked on a worker uses no CPU either: awaited_renders below is that check.
      if (timing.worst_pass_cpu_ms >= 0.0) {
        CHECK(timing.worst_pass_cpu_ms < static_cast<double>(kSlowSliderPassMs) + kThreadCpuTickMs);
      }
      CHECK(after.display_mip_rebuilds == before.display_mip_rebuilds);
      CHECK(after.background_display_mips > before.background_display_mips);
      CHECK(after.region_preview_refreshes > before.region_preview_refreshes);
      CHECK(after.awaited_renders == before.awaited_renders);
    }
    settled_exactly = wait_for_canvas_to_match_document(window, *canvas);
    exercised = true;
    QTimer::singleShot(0, dialog, [dialog] { dialog->accept(); });
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(exercised);
  // The region preview frames never stand as the settled canvas: it equals a
  // fresh full render of the final values.
  CHECK(settled_exactly);
  const auto* edited = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
  CHECK(edited != nullptr && edited->layer_style().strokes.size() == 1U &&
        edited->layer_style().drop_shadows.size() == 1U);
  CHECK(std::lround(edited->layer_style().strokes.front().size) == 241);
  CHECK(std::abs(edited->layer_style().strokes.front().opacity - 0.10F) <= 0.001F);
  CHECK(std::lround(edited->layer_style().drop_shadows.front().size) == 248);
}

// Levels on a layer over a third of a large canvas: the previews land as
// background canvas refreshes, never a GUI-thread patch or an awaited render.
void ui_levels_drag_stays_responsive_on_large_image() {
  patchy::LayerId layer_id = 0;
  patchy::ui::MainWindow window;
  window.add_document_session(slider_drag_document(false, layer_id), QStringLiteral("Levels Drag"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();
  bool exercised = false;
  bool settled_exactly = false;
  int awaited_renders = -1;
  int async_refreshes = -1;
  int region_batches = -1;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLevelsDialog")));
    CHECK(dialog != nullptr);
    auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    CHECK(black != nullptr);
    const auto before = canvas->render_cache_diagnostics();
    const auto timing = drive_slider_drag(*canvas, slider_drag_values(2, 120, 4),
                                          [black](int value) { black->setValue(value); });
    print_slider_drag_timing("levels", timing);
    settled_exactly = wait_for_canvas_to_match_document(window, *canvas);
    const auto after = canvas->render_cache_diagnostics();
    awaited_renders = after.awaited_renders - before.awaited_renders;
    async_refreshes = after.live_preview_async_refreshes - before.live_preview_async_refreshes;
    region_batches = after.dirty_region_batches - before.dirty_region_batches;
    exercised = true;
    QTimer::singleShot(0, dialog, [dialog] { dialog->reject(); });
  }));
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(exercised);
  CHECK(awaited_renders == 0);
  CHECK(async_refreshes > 0);
  CHECK(region_batches == 0);
  CHECK(settled_exactly);
}

// Gaussian Blur through its filter dialog: the same background canvas refresh.
void ui_gaussian_blur_drag_stays_responsive_on_large_image() {
  patchy::LayerId layer_id = 0;
  patchy::ui::MainWindow window;
  window.add_document_session(slider_drag_document(false, layer_id), QStringLiteral("Blur Drag"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();
  bool exercised = false;
  bool settled_exactly = false;
  int awaited_renders = -1;
  int async_refreshes = -1;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    const auto spins = dialog->findChildren<QDoubleSpinBox*>();
    CHECK(!spins.isEmpty());
    auto* radius = spins.front();
    const auto before = canvas->render_cache_diagnostics();
    const auto timing = drive_slider_drag(*canvas, slider_drag_values(2, 40, 1),
                                          [radius](int value) { radius->setValue(value); });
    print_slider_drag_timing("gaussian blur", timing);
    settled_exactly = wait_for_canvas_to_match_document(window, *canvas);
    const auto after = canvas->render_cache_diagnostics();
    awaited_renders = after.awaited_renders - before.awaited_renders;
    async_refreshes = after.live_preview_async_refreshes - before.live_preview_async_refreshes;
    exercised = true;
    QTimer::singleShot(0, dialog, [dialog] { dialog->reject(); });
  }));
  require_action(window, "filterAction_patchy_filters_gaussian_blur")->trigger();
  QApplication::processEvents();
  CHECK(exercised);
  CHECK(awaited_renders == 0);
  CHECK(async_refreshes > 0);
  CHECK(settled_exactly);
}

class ProbeSlider final : public QSlider {
 public:
  using QSlider::QSlider;
  QPoint handle_center() {
    QStyleOptionSlider option;
    initStyleOption(&option);
    return style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this).center();
  }
};

// The app-wide slider move coalescing (GitHub #601): moves that queue up during
// one event-loop pass reach the slider as one move to the latest position.
void ui_slider_drag_moves_coalesce_to_the_latest_position() {
  patchy::ui::ensure_slider_drag_coalescing();
  QDialog dialog;
  ProbeSlider slider(Qt::Horizontal, &dialog);
  slider.setRange(0, 1000);
  slider.setGeometry(10, 10, 600, 24);
  dialog.resize(640, 60);
  dialog.show();
  QApplication::processEvents();
  int changes = 0;
  QObject::connect(&slider, &QSlider::valueChanged, &dialog, [&changes](int) { ++changes; });
  const auto drag = [&](bool coalesce) {
    slider.setValue(0);
    QApplication::processEvents();
    changes = 0;
    patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(coalesce);
    const auto start = slider.handle_center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), slider.mapToGlobal(QPointF(start)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&slider, &press);
    CHECK(slider.isSliderDown());
    QPointF last(start);
    for (int step = 1; step <= 20; ++step) {
      last = QPointF(start.x() + step * 20, start.y());
      QCoreApplication::postEvent(&slider, new QMouseEvent(QEvent::MouseMove, last, slider.mapToGlobal(last),
                                                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier));
    }
    QApplication::processEvents();
    QApplication::processEvents();
    const auto during = changes;
    const auto value = slider.value();
    QMouseEvent release(QEvent::MouseButtonRelease, last, slider.mapToGlobal(last), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&slider, &release);
    patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(false);
    return std::pair{during, value};
  };
  const auto coalesced_before = patchy::ui::slider_drag_moves_coalesced_for_testing();
  const auto [plain_changes, plain_value] = drag(false);
  CHECK(patchy::ui::slider_drag_moves_coalesced_for_testing() == coalesced_before);
  const auto [coalesced_changes, coalesced_value] = drag(true);
  CHECK(plain_changes == 20);
  // The first move applies at once; the other 19 arrive as one replay.
  CHECK(coalesced_changes == 2);
  CHECK(patchy::ui::slider_drag_moves_coalesced_for_testing() - coalesced_before == 19);
  CHECK(coalesced_value == plain_value);
  CHECK(plain_value > 0);
}

void ui_layer_style_dialog_does_not_open_a_second_dialog() {
  // Opening Blending Options while one is already open (e.g. by double-clicking
  // another layer) must NOT stack a second dialog -- the nested event loops
  // crash. edit_active_layer_style() guards on preview_dialog_edit_locked().
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer styled(document.allocate_layer_id(), "Styled Layer",
                       solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(60, 120, 220, 255)));
  const auto styled_id = styled.id();
  document.add_layer(std::move(styled));
  document.set_active_layer(styled_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("No Stack"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();

  const auto count_style_dialogs = [] {
    int count = 0;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* dialog = qobject_cast<QDialog*>(widget);
          dialog != nullptr && dialog->objectName() == QStringLiteral("patchyLayerStyleDialog") && dialog->isVisible()) {
        ++count;
      }
    }
    return count;
  };

  // Watchdog: if a second (buggy) nested dialog opens, the inner trigger() blocks
  // forever. Force every style dialog closed so the test fails instead of hanging.
  bool watchdog_fired = false;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
    watchdog_fired = true;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* dialog = qobject_cast<QDialog*>(widget);
          dialog != nullptr && dialog->objectName() == QStringLiteral("patchyLayerStyleDialog")) {
        dialog->reject();
      }
    }
  });

  bool reentered = false;
  int dialogs_after_second_trigger = 0;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    if (dialog == nullptr) {
      return;
    }
    watchdog.start(2000);
    // Try to open another one. With the guard this returns immediately; without
    // it, this blocks in a second nested loop until the watchdog rescues us.
    require_action(window, "layerBlendingOptionsAction")->trigger();
    dialogs_after_second_trigger = count_style_dialogs();
    reentered = true;
    watchdog.stop();
    dialog->accept();
  });

  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();

  CHECK(reentered);
  CHECK(!watchdog_fired);
  CHECK(dialogs_after_second_trigger == 1);
}

patchy::Layer make_gradient_fill_test_layer(patchy::Document& document) {
  patchy::Layer layer(document.allocate_layer_id(), "Gradient Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{255, 255, 255}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{0, 0, 0}});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{0.0F, 1.0F});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{1.0F, 1.0F});
  layer.layer_style().gradient_fills.push_back(fill);
  return layer;
}

void select_layer_style_gradient_page(QDialog& dialog) {
  auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
  CHECK(categories != nullptr);
  const auto gradient_items = categories->findItems(QStringLiteral("Gradient Overlay"), Qt::MatchExactly);
  CHECK(!gradient_items.empty());
  categories->setCurrentItem(gradient_items.front());
  QApplication::processEvents();
}

void ui_layer_style_gradient_editor_drag_updates_stops_and_previews() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  int preview_calls = 0;
  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(editor->isVisible());
    CHECK(location->value() == 0);
    // The color tags live below the bar (two-track geometry: bar 30..60, color
    // tags around y 66..89); the 0% tag sits at the left gutter x=10.
    const int tag_y = editor->height() - 18;
    const int bar_span = editor->width() - 22;
    const QPoint from(10, tag_y);
    const QPoint to(10 + (bar_span * 2) / 5, tag_y);
    drag(*editor, from, to);
    QApplication::processEvents();
    CHECK(location->value() == 40);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++preview_calls; });

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& stops = settings->style.gradient_fills.front().gradient.color_stops;
  CHECK(stops.size() == 2);
  CHECK(std::abs(stops.front().location - 0.40F) < 0.005F);
  CHECK(stops.back().location > 0.99F);
  CHECK(preview_calls >= 1);
}

QDialog* visible_color_picker_dialog() {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
      return qobject_cast<QDialog*>(widget);
    }
  }
  return nullptr;
}

// Sets the open picker's color the way a user edit does (live callback fires).
void set_visible_picker_color(QColor color) {
  auto* picker_dialog = visible_color_picker_dialog();
  CHECK(picker_dialog != nullptr);
  if (picker_dialog == nullptr) {
    return;
  }
  auto* picker = picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  if (picker != nullptr) {
    picker->setCurrentColor(color);
  }
}

void click_gradient_color_tag(patchy::ui::GradientStopsEditorWidget& editor, bool right_end) {
  // Two-track geometry: color tags sit below the bar; the 0% tag at x=10, the
  // 100% tag at width-11.
  const QPoint tag(right_end ? editor.width() - 11 : 10, editor.height() - 18);
  send_mouse(editor, QEvent::MouseButtonPress, tag, Qt::LeftButton, Qt::LeftButton);
  send_mouse(editor, QEvent::MouseButtonRelease, tag, Qt::LeftButton, Qt::NoButton);
}

bool stop_color_is(const patchy::GradientColorStop& stop, int red, int green, int blue) {
  return stop.color.red == red && stop.color.green == green && stop.color.blue == blue;
}

// GitHub issue 216: the stop-color picker is non-modal. With it open on the 0%
// stop, clicking the 100% stop selected that stop in the dialog while the
// picker went on editing the 0% stop, so the next color meant for the right end
// replaced the left end's and the right end never changed.
void ui_layer_style_gradient_stop_picker_follows_the_selected_stop() {
  ensure_artifact_dir();
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);
  bool picker_closed_on_switch = false;
  bool saw_second_stop_selected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientStopSwatchButton"));
    auto* hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
    CHECK(editor != nullptr);
    CHECK(swatch != nullptr);
    CHECK(hex != nullptr);
    if (editor == nullptr || swatch == nullptr || hex == nullptr) {
      return;
    }
    CHECK(hex->text() == QStringLiteral("#FFFFFF"));
    QTimer::singleShot(0, dialog, [&, dialog, editor, swatch, hex] {
      // The 0% stop's picker is open: pick red and leave the picker open, then
      // select the 100% stop in the dialog.
      set_visible_picker_color(QColor(220, 30, 40));
      click_gradient_color_tag(*editor, true);
      QTimer::singleShot(0, dialog, [&, dialog, swatch, hex] {
        picker_closed_on_switch = visible_color_picker_dialog() == nullptr;
        saw_second_stop_selected = hex->text() == QStringLiteral("#000000");
        QTimer::singleShot(0, dialog, [dialog] {
          set_visible_picker_color(QColor(30, 60, 200));
          if (auto* picker_dialog = visible_color_picker_dialog(); picker_dialog != nullptr) {
            picker_dialog->accept();
          }
          QTimer::singleShot(0, dialog, [dialog] {
            save_widget_artifact("ui_layer_style_gradient_stop_picker_follows_the_selected_stop", *dialog);
            dialog->accept();
          });
        });
        swatch->click();
      });
    });
    swatch->click();
  }));

  // Themed like the app: the dialog's artifact is the issue's screenshot.
  const auto themed_parent = make_themed_dialog_parent();
  const auto settings = patchy::ui::request_layer_style_settings(themed_parent.get(), layer, {});
  CHECK(settings.has_value());
  CHECK(picker_closed_on_switch);
  CHECK(saw_second_stop_selected);
  if (!settings.has_value() || settings->style.gradient_fills.size() != 1) {
    CHECK(false);
    return;
  }
  const auto& stops = settings->style.gradient_fills.front().gradient.color_stops;
  CHECK(stops.size() == 2);
  if (stops.size() == 2) {
    CHECK(stop_color_is(stops.front(), 220, 30, 40));
    CHECK(stop_color_is(stops.back(), 30, 60, 200));
  }
}

// GitHub issue 216: choosing OK in Layer Style while a stop's picker was still
// open (its live preview already showed the color) rejected the picker and put
// the stop back to its old color. Also covers a second dialog session on the
// committed gradient.
void ui_layer_style_ok_keeps_the_open_stop_picker_color() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);
  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_layer_style_gradient_page(*dialog);
    auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientStopSwatchButton"));
    CHECK(swatch != nullptr);
    if (swatch == nullptr) {
      return;
    }
    QTimer::singleShot(0, dialog, [dialog] {
      set_visible_picker_color(QColor(200, 120, 20));
      dialog->accept();
    });
    swatch->click();
  }));
  const auto first = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(first.has_value());
  if (!first.has_value() || first->style.gradient_fills.size() != 1) {
    CHECK(false);
    return;
  }
  CHECK(stop_color_is(first->style.gradient_fills.front().gradient.color_stops.front(), 200, 120, 20));
  CHECK(visible_color_picker_dialog() == nullptr);

  // Second session on the committed style: edit the right end through its
  // picker's OK; the left end keeps the color from the first session.
  layer.layer_style() = first->style;
  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientStopSwatchButton"));
    CHECK(editor != nullptr);
    CHECK(swatch != nullptr);
    if (editor == nullptr || swatch == nullptr) {
      return;
    }
    click_gradient_color_tag(*editor, true);
    QApplication::processEvents();
    QTimer::singleShot(0, dialog, [dialog] {
      set_visible_picker_color(QColor(10, 140, 90));
      if (auto* picker_dialog = visible_color_picker_dialog(); picker_dialog != nullptr) {
        picker_dialog->accept();
      }
      QTimer::singleShot(0, dialog, [dialog] { dialog->accept(); });
    });
    swatch->click();
  }));
  const auto second = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(second.has_value());
  if (!second.has_value() || second->style.gradient_fills.size() != 1) {
    CHECK(false);
    return;
  }
  const auto& stops = second->style.gradient_fills.front().gradient.color_stops;
  CHECK(stops.size() == 2);
  if (stops.size() == 2) {
    CHECK(stop_color_is(stops.front(), 200, 120, 20));
    CHECK(stop_color_is(stops.back(), 10, 140, 90));
  }
}

void ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    auto* stop_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopOpacitySpin"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(stop_opacity != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(stop_hex->isVisible());
    CHECK(!stop_opacity->isVisible());
    // Click the empty opacity track above the bar at 50% to add an opacity stop.
    const int bar_span = editor->width() - 22;
    const QPoint add_point(10 + bar_span / 2, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, add_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, add_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(stop_opacity->isVisible());
    CHECK(!stop_hex->isVisible());
    CHECK(location->value() == 50);
    CHECK(stop_opacity->value() == 100);
    stop_opacity->setValue(0);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& alpha_stops = settings->style.gradient_fills.front().gradient.alpha_stops;
  CHECK(alpha_stops.size() == 3);
  CHECK(std::abs(alpha_stops[1].location - 0.5F) < 0.01F);
  CHECK(alpha_stops[1].opacity == 0.0F);
  CHECK(alpha_stops.front().opacity == 1.0F);
  CHECK(alpha_stops.back().opacity == 1.0F);
}

void ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);
  auto& gradient = layer.layer_style().gradient_fills.front().gradient;
  gradient.color_stops.back().midpoint = 0.25F;
  gradient.alpha_stops.back().midpoint = 0.75F;

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    auto* midpoint = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopMidpointSpin"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(midpoint != nullptr);
    CHECK(!midpoint->isEnabled());  // first sorted stop has no incoming segment

    const QPoint color_tag(editor->width() - 10, editor->height() - 18);
    send_mouse(*editor, QEvent::MouseButtonPress, color_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, color_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 25);
    midpoint->setValue(72);
    location->setValue(0);
    QApplication::processEvents();
    CHECK(!midpoint->isEnabled());  // an equal-position hard stop has no segment midpoint
    location->setValue(100);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 72);

    const QPoint opacity_tag(editor->width() - 10, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, opacity_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, opacity_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 75);
    midpoint->setValue(33);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  const auto& result = settings->style.gradient_fills.front().gradient;
  CHECK(std::abs(result.color_stops.back().midpoint - 0.72F) < 0.001F);
  CHECK(std::abs(result.alpha_stops.back().midpoint - 0.33F) < 0.001F);
}

void ui_layer_style_gradient_page_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientBlendModeCombo"));
    auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientReverseCheck"));
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientStyleCombo"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
    CHECK(blend != nullptr);
    CHECK(reverse != nullptr);
    CHECK(style_combo != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(!reverse->isChecked());
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    reverse->setChecked(true);
    style_combo->setCurrentIndex(
        std::max(0, style_combo->findData(static_cast<int>(patchy::LayerStyleGradientType::Radial))));
    stop_hex->setText(QStringLiteral("00FF00"));
    send_key(*stop_hex, Qt::Key_Return);
    CHECK(stop_hex->text() == QStringLiteral("#00FF00"));
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& fill = settings->style.gradient_fills.front();
  CHECK(fill.blend_mode == patchy::BlendMode::Multiply);
  CHECK(fill.gradient.reverse);
  CHECK(fill.gradient.type == patchy::LayerStyleGradientType::Radial);
  CHECK(fill.gradient.color_stops.front().color.red == 0);
  CHECK(fill.gradient.color_stops.front().color.green == 255);
  CHECK(fill.gradient.color_stops.front().color.blue == 0);
}

// A Noise gradient overlay (Photoshop's ClNs form: a seed and channel ranges, no stops)
// opened as the default white-to-black ramp because the dialog replaced any gradient
// without color stops (GitHub issue 262, ag-psd's effects/src.psd 'other' layer). The
// dialog must show the Noise page with the file's settings and hand them back untouched.
// Offscreen dialog shots must carry the active theme even when the dialog has no
// themed ancestor (a nullptr parent, as request_layer_style_settings takes here):
// the #262 shots were grabbed with Qt's default light list and buttons showing
// through. capture_widget_artifact themes such a window before the grab; this
// pins it in Dark and Light on the Layer Style dialog's category list and OK
// button, which the app QSS paints with field_bg_large and button_bg.
void ui_offscreen_dialog_capture_uses_active_theme() {
  auto& manager = patchy::ui::ThemeManager::instance();
  const auto saved_preference = manager.preference();
  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const bool light = scheme == patchy::ui::ColorScheme::Light;
    manager.set_preference(light ? patchy::ui::ColorSchemePreference::Light
                                 : patchy::ui::ColorSchemePreference::Dark,
                           false);
    QApplication::processEvents();
    const auto& palette = patchy::ui::theme(scheme);
    patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
    auto layer = make_gradient_fill_test_layer(document);
    bool inspected = false;
    // Nothing throws across the dialog's event loop (an exception there aborts
    // on macOS): the checks run through report_failure and the palette the
    // stylesheet engine leaves on each widget, not through platform-sensitive
    // pixels.
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
      if (dialog == nullptr) {
        patchy::test::report_failure("no Layer Style dialog");
        return;
      }
      select_layer_style_gradient_page(*dialog);
      // The capture restores the dialog's own sheet when it returns; read the
      // palettes while the theme is applied, as the guard sees them.
      ThemedCaptureScope themed(*dialog);
      const auto shot = capture_widget_artifact(
          std::string("ui_offscreen_dialog_capture_theme_") + (light ? "light" : "dark"), *dialog);
      auto* list = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      auto* ok = buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
      if (shot.isNull() || list == nullptr || ok == nullptr) {
        patchy::test::report_failure("capture, category list or OK button missing");
      } else {
        const auto list_ground = list->viewport()->palette().color(QPalette::Base);
        const auto button_ground = ok->palette().color(QPalette::Button);
        if (list_ground.rgb() != palette.field_bg_large.rgb()) {
          patchy::test::report_failure("category list ground " + list_ground.name().toStdString() + " vs " +
                                       palette.field_bg_large.name().toStdString());
        }
        if (button_ground.rgb() != palette.button_bg.rgb()) {
          patchy::test::report_failure("OK button ground " + button_ground.name().toStdString() + " vs " +
                                       palette.button_bg.name().toStdString());
        }
      }
      inspected = true;
      QTimer::singleShot(0, dialog, [dialog] { dialog->reject(); });
    });
    (void)patchy::ui::request_layer_style_settings(nullptr, layer, {});
    CHECK(inspected);
  }
  manager.set_preference(saved_preference, false);
  QApplication::processEvents();
}

void ui_layer_style_noise_gradient_overlay_loads_into_dialog() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Noise Overlay",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.gradient.name = "Custom";
  fill.gradient.form = patchy::GradientDefinitionForm::Noise;
  fill.gradient.noise.seed = 43487260U;
  fill.gradient.noise.roughness = 2048;
  fill.gradient.noise.color_model = patchy::GradientNoiseColorModel::Lab;
  fill.gradient.noise.restrict_colors = true;
  fill.gradient.noise.add_transparency = false;
  fill.gradient.noise.minimum = {0, 0, 0, 0};
  fill.gradient.noise.maximum = {100, 100, 100, 100};
  fill.gradient.angle_degrees = 90.0F;
  layer.layer_style().gradient_fills.push_back(fill);

  auto& manager = patchy::ui::ThemeManager::instance();
  const auto saved_preference = manager.preference();
  bool inspected = false;
  std::optional<patchy::ui::LayerStyleSettings> settings;
  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const bool light = scheme == patchy::ui::ColorScheme::Light;
    manager.set_preference(light ? patchy::ui::ColorSchemePreference::Light
                                 : patchy::ui::ColorSchemePreference::Dark,
                           false);
    QApplication::processEvents();
    inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* form = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientFormCombo"));
    auto* seed = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientNoiseSeedSpin"));
    auto* roughness = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientNoiseRoughnessSpin"));
    auto* model = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientNoiseColorModelCombo"));
    auto* restrict_colors =
        dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientNoiseRestrictColorsCheck"));
    CHECK(form != nullptr && seed != nullptr && roughness != nullptr && model != nullptr &&
          restrict_colors != nullptr);
    save_widget_artifact(std::string("ui_layer_style_noise_gradient_overlay_loads_into_dialog_") +
                             (light ? "light" : "dark"),
                         *dialog);
    CHECK(form->currentData().toInt() == static_cast<int>(patchy::GradientDefinitionForm::Noise));
    CHECK(seed->value() == 43487260);
    CHECK(roughness->value() == 50);
    CHECK(model->currentData().toInt() == static_cast<int>(patchy::GradientNoiseColorModel::Lab));
    CHECK(restrict_colors->isChecked());
    CHECK(seed->isVisibleTo(dialog));
    inspected = true;
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  }
  manager.set_preference(saved_preference, false);
  QApplication::processEvents();
  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& result = settings->style.gradient_fills.front().gradient;
  CHECK(result.form == patchy::GradientDefinitionForm::Noise);
  CHECK(result.noise.seed == 43487260U);
  CHECK(result.noise.roughness == 2048);
  CHECK(result.noise.color_model == patchy::GradientNoiseColorModel::Lab);
  CHECK(result.noise.restrict_colors);
  CHECK(result.noise.maximum[0] == 100);
}

void ui_layer_style_preview_is_transient_and_show_effects_persists() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Preview State",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 5.0F;
  layer.layer_style().strokes.push_back(stroke);
  layer.layer_style().effects_visible = false;

  std::vector<patchy::ui::LayerStyleSettings> previews;
  QTimer::singleShot(0, guarded([&previews] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* show_effects = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleShowEffectsCheck"));
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
    auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
    CHECK(categories != nullptr);
    CHECK(show_effects != nullptr);
    CHECK(preview != nullptr);
    CHECK(stroke_size != nullptr);
    CHECK(!show_effects->isChecked());
    CHECK(preview->isChecked());

    show_effects->setChecked(true);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    categories->setCurrentItem(stroke_items.front());
    stroke_size->setValue(17);
    preview->setChecked(false);
    QApplication::processEvents();
    CHECK(preview->isVisible());
    CHECK(!previews.empty());
    CHECK(!previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 5.0F);
    // Edits continue accumulating while the canvas is showing the untouched
    // original state.
    stroke_size->setValue(19);
    QTest::qWait(60);
    CHECK(!previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 5.0F);
    preview->setChecked(true);
    QApplication::processEvents();
    CHECK(previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 19.0F);
    preview->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings& value) { previews.push_back(value); });

  CHECK(settings.has_value());
  CHECK(settings->style.effects_visible);
  CHECK(settings->style.strokes.size() == 1);
  CHECK(settings->style.strokes.front().size == 19.0F);
  CHECK(!previews.empty());
  CHECK(!previews.back().style.effects_visible);
  CHECK(previews.back().style.strokes.size() == 1);
  CHECK(previews.back().style.strokes.front().size == 5.0F);
}

void ui_layer_style_blending_options_round_trip_the_interior_group_flag() {
  // "Blend Interior Effects as Group" ('infx') changes rendering but no effect
  // page owns it, so the Blending Options checkbox is the only thing keeping a
  // dialog edit from silently clearing an imported layer's flag.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Grouped Interior",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  layer.layer_style().color_overlays.push_back(overlay);
  layer.layer_style().blend_interior_elements = true;

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* blend_interior = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBlendInteriorCheck"));
    CHECK(blend_interior != nullptr);
    CHECK(blend_interior->isChecked());
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  const auto kept = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(kept.has_value());
  CHECK(kept->style.blend_interior_elements);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* blend_interior = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBlendInteriorCheck"));
    CHECK(blend_interior != nullptr);
    blend_interior->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  const auto cleared = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(cleared.has_value());
  CHECK(!cleared->style.blend_interior_elements);
}

void ui_layer_style_blending_options_round_trip_the_clipped_group_flag() {
  // "Blend Clipped Layers as Group" ('clbl', on by default) also has no effect
  // page of its own; the Blending Options checkbox keeps an imported off state
  // through a dialog edit and can turn it back on.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Ungrouped Clip Base",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  layer.layer_style().color_overlays.push_back(overlay);
  layer.layer_style().blend_clipped_elements = false;

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* blend_clipped = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBlendClippedCheck"));
    CHECK(blend_clipped != nullptr);
    CHECK(!blend_clipped->isChecked());
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  const auto kept = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(kept.has_value());
  CHECK(!kept->style.blend_clipped_elements);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* blend_clipped = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBlendClippedCheck"));
    CHECK(blend_clipped != nullptr);
    blend_clipped->setChecked(true);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  const auto restored = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(restored.has_value());
  CHECK(restored->style.blend_clipped_elements);
}

void ui_layer_style_blending_options_round_trip_channel_restrictions() {
  // Advanced Blending "Channels": checked = the channel composites, so an
  // imported Green exclusion loads with G unchecked and commits back as the
  // kRestrict* mask. Preview-off must restore the layer's original mask AND
  // its real Fill Opacity (original_settings used to default fill to 100).
  patchy::Document direct_document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer direct_layer(direct_document.allocate_layer_id(), "Restricted",
                             solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  direct_layer.set_restricted_channels(patchy::kRestrictGreen);
  direct_layer.set_fill_opacity(0.4F);

  std::vector<patchy::ui::LayerStyleSettings> previews;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* red = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelRedCheck"));
    auto* green = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelGreenCheck"));
    auto* blue = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelBlueCheck"));
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
    CHECK(red != nullptr && green != nullptr && blue != nullptr && preview != nullptr);
    CHECK(red->isEnabled() && green->isEnabled() && blue->isEnabled());
    CHECK(red->isChecked());
    CHECK(!green->isChecked());
    CHECK(blue->isChecked());

    blue->setChecked(false);
    QTest::qWait(80);
    CHECK(!previews.empty());
    CHECK(previews.back().restricted_channels ==
          (patchy::kRestrictGreen | patchy::kRestrictBlue));

    preview->setChecked(false);
    QApplication::processEvents();
    CHECK(previews.back().restricted_channels == patchy::kRestrictGreen);
    CHECK(previews.back().fill_opacity == 40);
    preview->setChecked(true);
    QApplication::processEvents();

    green->setChecked(true);
    QTest::qWait(80);
    dialog->accept();
  }));
  const auto accepted = patchy::ui::request_layer_style_settings(
      nullptr, direct_layer,
      [&](const patchy::ui::LayerStyleSettings& settings) { previews.push_back(settings); });
  CHECK(accepted.has_value());
  CHECK(accepted->restricted_channels == patchy::kRestrictBlue);
  CHECK(accepted->fill_opacity == 40);

  // Cancel restores the live-previewed mask through MainWindow.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Restrict Cancel",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_restricted_channels(patchy::kRestrictGreen);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Restrict Cancel"));
  show_window(window);
  bool saw_live_preview = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* blue = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelBlueCheck"));
    CHECK(blue != nullptr);
    blue->setChecked(false);
    QTest::qWait(80);
    const auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
    CHECK(preview_layer != nullptr);
    saw_live_preview = preview_layer->restricted_channels() ==
                       (patchy::kRestrictGreen | patchy::kRestrictBlue);
    dialog->reject();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_live_preview);
  const auto* restored = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->restricted_channels() == patchy::kRestrictGreen);
}

void ui_layer_style_channel_checkboxes_disabled_for_unsupported_payload() {
  // An unmodelable native 'brst' (non-RGB source) presents as disabled,
  // checked boxes; committing must not clobber the preserved payload (the
  // returned mask stays zero and MainWindow skips the setter).
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Unsupported Restriction",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  layer.set_channel_restriction_unsupported();

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* red = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelRedCheck"));
    auto* green = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelGreenCheck"));
    auto* blue = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleChannelBlueCheck"));
    CHECK(red != nullptr && green != nullptr && blue != nullptr);
    CHECK(!red->isEnabled() && !green->isEnabled() && !blue->isEnabled());
    CHECK(red->isChecked() && green->isChecked() && blue->isChecked());
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->restricted_channels == 0U);
}

void ui_layer_style_gradient_stroke_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Gradient Stroke",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.uses_gradient = true;
  stroke.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 255, 255}},
                                 {1.0F, patchy::RgbColor{0, 0, 0}}};
  stroke.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  layer.layer_style().strokes.push_back(stroke);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* fill = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeFillCombo"));
    auto* gradient_group = dialog->findChild<QGroupBox*>(QStringLiteral("layerStyleStrokeGradientGroup"));
    auto* editor = dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(
        QStringLiteral("layerStyleStrokeGradientStopsEditor"));
    auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeGradientReverseCheck"));
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeGradientStyleCombo"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientAngleSpin"));
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientScaleSpin"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleStrokeGradientStopHexEdit"));
    auto* midpoint = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientStopMidpointSpin"));
    CHECK(categories != nullptr);
    CHECK(fill != nullptr);
    CHECK(gradient_group != nullptr);
    CHECK(editor != nullptr);
    CHECK(reverse != nullptr);
    CHECK(style_combo != nullptr);
    CHECK(angle != nullptr);
    CHECK(scale != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(midpoint != nullptr);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    categories->setCurrentItem(stroke_items.front());
    QApplication::processEvents();
    CHECK(fill->currentData().toBool());
    CHECK(gradient_group->isVisible());
    CHECK(editor->isVisible());
    CHECK(!editor->visibleRegion().isEmpty());
    // The stroke page scrolls (its gradient block must not size the dialog),
    // so bring the lower rows into the viewport before probing visibility.
    auto* stroke_scroll = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleStrokeScroll"));
    CHECK(stroke_scroll != nullptr);
    stroke_scroll->ensureWidgetVisible(scale);
    // The settings pane scrolls too when the dialog opens shorter than the
    // tallest page (GitHub issue 606).
    if (auto* pane = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll")); pane != nullptr) {
      pane->ensureWidgetVisible(scale);
    }
    QApplication::processEvents();
    CHECK(!scale->visibleRegion().isEmpty());
    CHECK(!midpoint->isEnabled());
    fill->setCurrentIndex(0);
    CHECK(!gradient_group->isVisible());
    fill->setCurrentIndex(1);
    CHECK(gradient_group->isVisible());
    reverse->setChecked(true);
    style_combo->setCurrentIndex(
        std::max(0, style_combo->findData(static_cast<int>(patchy::LayerStyleGradientType::Diamond))));
    angle->setValue(-35);
    scale->setValue(175);
    stop_hex->setText(QStringLiteral("#123456"));
    send_key(*stop_hex, Qt::Key_Return);
    const QPoint color_tag(editor->width() - 10, editor->height() - 18);
    send_mouse(*editor, QEvent::MouseButtonPress, color_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, color_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    midpoint->setValue(64);
    const QPoint opacity_tag(editor->width() - 10, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, opacity_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, opacity_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    midpoint->setValue(36);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.strokes.size() == 1);
  const auto& result = settings->style.strokes.front();
  CHECK(result.uses_gradient);
  CHECK(result.gradient.reverse);
  CHECK(result.gradient.type == patchy::LayerStyleGradientType::Diamond);
  CHECK(result.gradient.angle_degrees == -35.0F);
  CHECK(std::abs(result.gradient.scale - 1.75F) < 0.001F);
  CHECK(result.gradient.color_stops.front().color.red == 0x12);
  CHECK(result.gradient.color_stops.front().color.green == 0x34);
  CHECK(result.gradient.color_stops.front().color.blue == 0x56);
  CHECK(std::abs(result.gradient.color_stops.back().midpoint - 0.64F) < 0.001F);
  CHECK(std::abs(result.gradient.alpha_stops.back().midpoint - 0.36F) < 0.001F);
}

void ui_layer_style_stroke_gradient_fill_keeps_dialog_height() {
  // A gradient-fill stroke un-hides the stroke page's nested gradient block,
  // which used to raise the stacked layout's minimum and open the whole dialog
  // ~180px taller than any visible page needed. The stroke page scrolls
  // instead now, so the dialog height must not depend on the stroke fill.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());

  int color_height = 0;
  int color_minimum = 0;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    QApplication::processEvents();
    color_height = dialog->height();
    color_minimum = dialog->minimumSizeHint().height();
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  patchy::Layer color_layer(document.allocate_layer_id(), "Color Stroke",
                            solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke color_stroke;
  color_stroke.enabled = true;
  color_layer.layer_style().strokes.push_back(color_stroke);
  CHECK(patchy::ui::request_layer_style_settings(nullptr, color_layer, {}).has_value());

  int gradient_height = 0;
  int gradient_minimum = 0;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleStrokeScroll")) != nullptr);
    QApplication::processEvents();
    gradient_height = dialog->height();
    gradient_minimum = dialog->minimumSizeHint().height();
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));
  patchy::Layer gradient_layer(document.allocate_layer_id(), "Gradient Stroke",
                               solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke gradient_stroke;
  gradient_stroke.enabled = true;
  gradient_stroke.uses_gradient = true;
  gradient_stroke.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 255, 255}},
                                          {1.0F, patchy::RgbColor{0, 0, 0}}};
  gradient_stroke.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  gradient_layer.layer_style().strokes.push_back(gradient_stroke);
  CHECK(patchy::ui::request_layer_style_settings(nullptr, gradient_layer, {}).has_value());

  CHECK(color_height > 0);
  CHECK(gradient_height == color_height);
  CHECK(gradient_minimum == color_minimum);
}

void ui_layer_style_bevel_lighting_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Bevel Lighting",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{240, 230, 220};
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{20, 30, 40};
  bevel.style = patchy::BevelEmbossStyleKind::OuterBevel;
  bevel.technique = patchy::BevelTechnique::ChiselHard;
  bevel.soften = 3.0F;
  layer.layer_style().bevels.push_back(bevel);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* highlight_blend =
        dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelHighlightBlendModeCombo"));
    auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelShadowBlendModeCombo"));
    auto* highlight_opacity =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelHighlightOpacitySpin"));
    auto* shadow_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelShadowOpacitySpin"));
    auto* style = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelStyleCombo"));
    auto* technique = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelTechniqueCombo"));
    auto* soften = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSoftenSpin"));
    auto* highlight_color =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelHighlightColorButton"));
    auto* shadow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelShadowColorButton"));
    CHECK(categories != nullptr);
    CHECK(highlight_blend != nullptr);
    CHECK(shadow_blend != nullptr);
    CHECK(highlight_opacity != nullptr);
    CHECK(shadow_opacity != nullptr);
    CHECK(style != nullptr);
    CHECK(technique != nullptr);
    CHECK(soften != nullptr);
    CHECK(highlight_color != nullptr);
    CHECK(shadow_color != nullptr);
    const auto bevel_items = categories->findItems(QStringLiteral("Bevel & Emboss"), Qt::MatchExactly);
    CHECK(!bevel_items.empty());
    categories->setCurrentItem(bevel_items.front());
    QApplication::processEvents();
    CHECK(style->count() == 5);
    CHECK(technique->count() == 3);
    CHECK(style->currentData().toInt() == static_cast<int>(patchy::BevelEmbossStyleKind::OuterBevel));
    CHECK(technique->currentData().toInt() == static_cast<int>(patchy::BevelTechnique::ChiselHard));
    CHECK(soften->value() == 3);
    // The dialog opens shorter than the Bevel page on the offscreen screen
    // (GitHub issue 606): scroll each swatch into view first.
    auto* pane = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
    CHECK(pane != nullptr);
    if (pane != nullptr) {
      pane->ensureWidgetVisible(highlight_color);
      QApplication::processEvents();
    }
    CHECK(!highlight_color->visibleRegion().isEmpty());
    if (pane != nullptr) {
      pane->ensureWidgetVisible(shadow_color);
      QApplication::processEvents();
    }
    CHECK(!shadow_color->visibleRegion().isEmpty());
    highlight_blend->setCurrentIndex(
        std::max(0, highlight_blend->findData(static_cast<int>(patchy::BlendMode::ColorDodge))));
    shadow_blend->setCurrentIndex(
        std::max(0, shadow_blend->findData(static_cast<int>(patchy::BlendMode::LinearBurn))));
    highlight_opacity->setValue(62);
    shadow_opacity->setValue(37);
    style->setCurrentIndex(
        std::max(0, style->findData(static_cast<int>(patchy::BevelEmbossStyleKind::PillowEmboss))));
    technique->setCurrentIndex(
        std::max(0, technique->findData(static_cast<int>(patchy::BevelTechnique::ChiselSoft))));
    soften->setValue(5);

    const auto choose_color = [](QPushButton* button, QColor color) {
      QTimer::singleShot(0, guarded([color] {
        auto* picker_dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyColorDialog")));
        CHECK(picker_dialog != nullptr);
        auto* picker = picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(
            QStringLiteral("patchyAdvancedColorPicker"));
        CHECK(picker != nullptr);
        picker->setCurrentColor(color);
        picker_dialog->accept();
      }));
      button->click();
    };
    choose_color(highlight_color, QColor(12, 34, 56));
    choose_color(shadow_color, QColor(78, 90, 123));
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.bevels.size() == 1);
  const auto& result = settings->style.bevels.front();
  CHECK(result.highlight_blend_mode == patchy::BlendMode::ColorDodge);
  CHECK(result.highlight_color.red == 12 && result.highlight_color.green == 34 && result.highlight_color.blue == 56);
  CHECK(std::abs(result.highlight_opacity - 0.62F) < 0.001F);
  CHECK(result.shadow_blend_mode == patchy::BlendMode::LinearBurn);
  CHECK(result.shadow_color.red == 78 && result.shadow_color.green == 90 && result.shadow_color.blue == 123);
  CHECK(std::abs(result.shadow_opacity - 0.37F) < 0.001F);
  CHECK(result.style == patchy::BevelEmbossStyleKind::PillowEmboss);
  CHECK(result.technique == patchy::BevelTechnique::ChiselSoft);
  CHECK(result.soften == 5.0F);
}

void ui_layer_style_satin_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Satin Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.unsupported_contour_options = true;
  layer.layer_style().satins.push_back(satin);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleSatinBlendModeCombo"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinOpacitySpin"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinAngleSpin"));
    auto* distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinDistanceSpin"));
    auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinSizeSpin"));
    auto* invert = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleSatinInvertCheck"));
    auto* red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinRedSpin"));
    auto* green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinGreenSpin"));
    auto* blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinBlueSpin"));
    auto* contour_warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleSatinContourWarning"));
    CHECK(categories != nullptr);
    CHECK(blend != nullptr);
    CHECK(opacity != nullptr);
    CHECK(angle != nullptr);
    CHECK(distance != nullptr);
    CHECK(size != nullptr);
    CHECK(invert != nullptr);
    CHECK(red != nullptr);
    CHECK(green != nullptr);
    CHECK(blue != nullptr);
    CHECK(contour_warning != nullptr);
    CHECK(contour_warning->isVisible());
    CHECK(contour_warning->text().contains(QStringLiteral("non-anti-aliased Linear contour")));
    const auto satin_items = categories->findItems(QStringLiteral("Satin"), Qt::MatchExactly);
    CHECK(!satin_items.empty());
    categories->setCurrentItem(satin_items.front());
    QApplication::processEvents();
    CHECK(!opacity->visibleRegion().isEmpty());
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Screen))));
    opacity->setValue(63);
    angle->setValue(-42);
    distance->setValue(27);
    size->setValue(19);
    invert->setChecked(false);
    red->setValue(12);
    green->setValue(34);
    blue->setValue(56);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  int previews = 0;
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++previews; });
  CHECK(settings.has_value());
  CHECK(settings->style.satins.size() == 1);
  const auto& result = settings->style.satins.front();
  CHECK(result.enabled);
  CHECK(result.blend_mode == patchy::BlendMode::Screen);
  CHECK(std::abs(result.opacity - 0.63F) < 0.001F);
  CHECK(result.angle_degrees == -42.0F);
  CHECK(result.distance == 27.0F);
  CHECK(result.size == 19.0F);
  CHECK(!result.invert);
  CHECK(result.color.red == 12 && result.color.green == 34 && result.color.blue == 56);
  CHECK(!result.unsupported_contour_options);
  CHECK(previews >= 1);
}

void ui_layer_style_outer_glow_technique_and_range_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Glow Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.technique = patchy::LayerGlowTechnique::Precise;
  glow.range = 80.0F;
  layer.layer_style().outer_glows.push_back(glow);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* technique = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleOuterGlowTechniqueCombo"));
    auto* range = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowRangeSpin"));
    auto* spread = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowSpreadSpin"));
    CHECK(categories != nullptr);
    CHECK(technique != nullptr);
    CHECK(range != nullptr);
    CHECK(spread != nullptr);
    // The imported values load into the controls.
    CHECK(technique->currentData().toInt() == static_cast<int>(patchy::LayerGlowTechnique::Precise));
    CHECK(range->value() == 80);
    const auto glow_items = categories->findItems(QStringLiteral("Outer Glow"), Qt::MatchExactly);
    CHECK(!glow_items.empty());
    categories->setCurrentItem(glow_items.front());
    QApplication::processEvents();
    CHECK(!range->visibleRegion().isEmpty());
    technique->setCurrentIndex(
        std::max(0, technique->findData(static_cast<int>(patchy::LayerGlowTechnique::Softer))));
    range->setValue(25);
    spread->setValue(8);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.outer_glows.size() == 1);
  const auto& result = settings->style.outer_glows.front();
  CHECK(result.enabled);
  CHECK(result.technique == patchy::LayerGlowTechnique::Softer);
  CHECK(result.range == 25.0F);
  CHECK(result.spread == 8.0F);
}

// The Patchy-only Continuous option: its badge sits beside the checkbox, the
// info banner and the Fade row follow the checkbox, and both values round
// trip through the dialog.
void ui_layer_style_continuous_shadow_controls_map_to_settings_and_mark_patchy_only() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Long Shadow",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 40.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* continuous = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowContinuousCheck"));
    auto* fade = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowFadeSpin"));
    auto* banner = dialog->findChild<QLabel*>(QStringLiteral("layerStylePatchyOnlyBanner"));
    auto* badge = continuous == nullptr ? nullptr
                                        : continuous->parentWidget()->findChild<QLabel*>(QStringLiteral("patchyOnlyBadge"));
    CHECK(categories != nullptr);
    CHECK(continuous != nullptr);
    CHECK(fade != nullptr);
    CHECK(banner != nullptr);
    CHECK(badge != nullptr);
    CHECK(!badge->toolTip().isEmpty());
    CHECK(continuous->toolTip().contains(QStringLiteral("Photoshop")));
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    categories->setCurrentItem(shadow_items.front());
    QApplication::processEvents();
    CHECK(!continuous->isChecked());
    CHECK(!banner->isVisible());
    CHECK(!fade->isEnabled());
    continuous->setChecked(true);
    QApplication::processEvents();
    CHECK(banner->isVisible());
    CHECK(fade->isEnabled());
    fade->setValue(40);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.drop_shadows.size() == 1);
  const auto& result = settings->style.drop_shadows.front();
  CHECK(result.enabled);
  CHECK(result.continuous);
  CHECK(result.fade == 40.0F);
  CHECK(result.distance == 40.0F);
}

// The Drop Shadow page's Contour row (#507): an imported custom curve shows as Custom,
// survives OK untouched, and Anti-aliased maps to the setting. With the reporter's file
// present, the dialog for its magenta GIMP shadow is saved for the desk card.
void ui_layer_style_drop_shadow_contour_keeps_a_custom_curve() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Shadowed",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto fixture = patchy::test::local_fixtures_root() / "xcf-issue-507" / "replace-magenta.xcf";
  const bool imported = std::filesystem::exists(fixture);
  if (imported) {
    const auto gimp = patchy::xcf::DocumentIo::read_file(fixture);
    for (const auto& candidate : gimp.layers()) {
      if (candidate.visible() && !candidate.layer_style().drop_shadows.empty()) {  // the magenta one
        layer.layer_style() = candidate.layer_style();
      }
    }
  } else {
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0xda, 0x11, 0xc9};
    shadow.opacity = 0.75F;
    shadow.contour = patchy::StyleContour{"GIMP linear light", {{0.0F, 0.0F, false}, {127.5F, 96.4F, false},
                                                                {255.0F, 182.9F, false}}};
    layer.layer_style().drop_shadows.push_back(shadow);
  }
  CHECK(layer.layer_style().drop_shadows.size() == 1);
  const auto original = layer.layer_style().drop_shadows.front();
  CHECK(original.opacity == 0.75F);
  CHECK(original.contour.points.size() >= 3);

  QTimer::singleShot(0, guarded([imported] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* contour = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowContourCombo"));
    auto* anti_aliased = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowAntiAliasedCheck"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowOpacitySpin"));
    CHECK(categories != nullptr);
    CHECK(contour != nullptr);
    CHECK(anti_aliased != nullptr);
    CHECK(opacity != nullptr);
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    categories->setCurrentItem(shadow_items.front());
    QApplication::processEvents();
    CHECK(!contour->visibleRegion().isEmpty());
    CHECK(opacity->value() == 75);
    CHECK(!anti_aliased->isChecked());
    if (imported) {
      std::filesystem::create_directories("test-artifacts");
      dialog->grab().save(QStringLiteral("test-artifacts/issue507_layer_style_drop_shadow.png"));
    }
    anti_aliased->setChecked(true);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.drop_shadows.size() == 1);
  const auto& result = settings->style.drop_shadows.front();
  CHECK(result.contour == original.contour);
  CHECK(result.anti_aliased);
  CHECK(result.opacity == 0.75F);
  CHECK(result.color == original.color);
}

void ui_layer_style_inner_glow_technique_and_range_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Glow Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.technique = patchy::LayerGlowTechnique::Precise;
  glow.range = 80.0F;
  layer.layer_style().inner_glows.push_back(glow);

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* technique = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleInnerGlowTechniqueCombo"));
    auto* range = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerGlowRangeSpin"));
    auto* choke = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerGlowChokeSpin"));
    CHECK(categories != nullptr);
    CHECK(technique != nullptr);
    CHECK(range != nullptr);
    CHECK(choke != nullptr);
    // The imported values load into the controls.
    CHECK(technique->currentData().toInt() == static_cast<int>(patchy::LayerGlowTechnique::Precise));
    CHECK(range->value() == 80);
    const auto glow_items = categories->findItems(QStringLiteral("Inner Glow"), Qt::MatchExactly);
    CHECK(!glow_items.empty());
    categories->setCurrentItem(glow_items.front());
    QApplication::processEvents();
    CHECK(!range->visibleRegion().isEmpty());
    technique->setCurrentIndex(
        std::max(0, technique->findData(static_cast<int>(patchy::LayerGlowTechnique::Softer))));
    range->setValue(25);
    choke->setValue(8);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.inner_glows.size() == 1);
  const auto& result = settings->style.inner_glows.front();
  CHECK(result.enabled);
  CHECK(result.technique == patchy::LayerGlowTechnique::Softer);
  CHECK(result.range == 25.0F);
  CHECK(result.choke == 8.0F);
}

void ui_layer_style_pattern_warning_follows_resolvability() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Photoshop Effects",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.pattern_id = patchy::builtin_pattern_presets().front().id;
  pattern.pattern_name = patchy::builtin_pattern_presets().front().english_name;
  layer.layer_style().pattern_overlays.push_back(pattern);

  // A resolvable reference (built-in preset) shows neither the legacy
  // preserved-only banner nor the missing-pattern banner.
  bool checked_resolvable = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("layerStyleUnsupportedEffectsWarning")) == nullptr);
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning")) == nullptr);
    checked_resolvable = true;
    dialog->reject();
  }));
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(!settings.has_value());
  CHECK(checked_resolvable);

  // An unresolvable reference (id absent from the document store and not a
  // built-in) shows the missing-pattern banner naming the pattern.
  layer.layer_style().pattern_overlays.front().pattern_id = "00000000-dead-beef-0000-000000000000";
  layer.layer_style().pattern_overlays.front().pattern_name = "Lost Pattern";
  bool checked_missing = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning"));
    CHECK(warning != nullptr);
    CHECK(warning->isVisible());
    CHECK(warning->text().contains(QStringLiteral("Lost Pattern")));
    CHECK(warning->text().contains(QStringLiteral("not embedded")));
    checked_missing = true;
    dialog->reject();
  }));
  const auto missing_settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(!missing_settings.has_value());
  CHECK(checked_missing);
}

// Photoshop presents a bevel Texture sub-option whose pattern cannot be
// resolved as UNCHECKED and renders without it (pinned on tips.psd, July 2026:
// useTexture=true in lfx2 with a pattern in neither the document nor the
// presets shows as texture-off, with no warning banner). Patchy mirrors that;
// re-checking the row surfaces the live missing-pattern warning.
void ui_layer_style_bevel_texture_missing_pattern_presents_unchecked() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Textured Bevel",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = "00000000-dead-beef-0000-000000000001";
  bevel.texture.pattern_name = "EVGREEN2.JPG";
  layer.layer_style().bevels.push_back(bevel);

  bool checked = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* texture_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureCategoryCheck"));
    CHECK(texture_check != nullptr);
    CHECK(!texture_check->isChecked());
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning")) == nullptr);
    checked = true;
    dialog->reject();
  }));
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(!settings.has_value());
  CHECK(checked);
}

void ui_layer_style_pattern_overlay_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Patterned",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));

  const auto bricks_id = QString::fromLatin1(patchy::builtin_pattern_presets()[9].id);
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayCategoryCheck"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayBlendModeCombo"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayOpacitySpin"));
    auto* pattern_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayPatternCombo"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayAngleSpin"));
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayScaleSpin"));
    auto* link = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayLinkCheck"));
    auto* snap = dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlaySnapOriginButton"));
    CHECK(categories != nullptr);
    CHECK(check != nullptr);
    CHECK(blend != nullptr);
    CHECK(opacity != nullptr);
    CHECK(pattern_combo != nullptr);
    CHECK(angle != nullptr);
    CHECK(scale != nullptr);
    CHECK(link != nullptr);
    CHECK(snap != nullptr);
    // Every built-in preset is offered with its persisted id.
    CHECK(pattern_combo->count() >= static_cast<int>(patchy::builtin_pattern_presets().size()));
    const auto pattern_items = categories->findItems(QStringLiteral("Pattern Overlay"), Qt::MatchExactly);
    CHECK(!pattern_items.empty());
    categories->setCurrentItem(pattern_items.front());
    QApplication::processEvents();
    check->setChecked(true);
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    opacity->setValue(63);
    pattern_combo->setCurrentIndex(std::max(0, pattern_combo->findData(bricks_id)));
    angle->setValue(15);
    scale->setValue(250);
    link->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.pattern_overlays.size() == 1);
  const auto& result = settings->style.pattern_overlays.front();
  CHECK(result.enabled);
  CHECK(result.blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(result.opacity - 0.63F) < 0.001F);
  CHECK(result.pattern_id == patchy::builtin_pattern_presets()[9].id);
  CHECK(result.pattern_name == patchy::builtin_pattern_presets()[9].english_name);
  CHECK(result.angle_degrees == 15.0F);
  CHECK(std::abs(result.scale - 2.5F) < 0.001F);
  CHECK(!result.link_with_layer);
}

void ui_layer_style_slider_rows_have_step_buttons() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Step Buttons",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  layer.layer_style().bevels.push_back(bevel);

  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    const auto click_step_button = [](QPushButton* button) {
      send_mouse(*button, QEvent::MouseButtonPress, button->rect().center(), Qt::LeftButton,
                 Qt::LeftButton);
      send_mouse(*button, QEvent::MouseButtonRelease, button->rect().center(), Qt::LeftButton,
                 Qt::NoButton);
    };

    // Every effect slider row carries the pair; only the R/G/B colour rows and the
    // standalone fields (gradient stops, noise) stay bare.
    int slider_rows = 0;
    for (auto* spin : dialog->findChildren<QSpinBox*>()) {
      const auto name = spin->objectName();
      if (!name.startsWith(QStringLiteral("layerStyle")) || !name.endsWith(QStringLiteral("Spin"))) {
        continue;
      }
      auto slider_name = name;
      slider_name.chop(4);
      slider_name += QStringLiteral("Slider");
      if (dialog->findChild<QSlider*>(slider_name) == nullptr) {
        continue;
      }
      const bool colour_channel = name.endsWith(QStringLiteral("RedSpin")) ||
                                  name.endsWith(QStringLiteral("GreenSpin")) ||
                                  name.endsWith(QStringLiteral("BlueSpin"));
      auto* decrease = dialog->findChild<QPushButton*>(name + QStringLiteral("DecreaseButton"));
      auto* increase = dialog->findChild<QPushButton*>(name + QStringLiteral("IncreaseButton"));
      if (colour_channel) {
        CHECK(decrease == nullptr && increase == nullptr);
        continue;
      }
      ++slider_rows;
      CHECK(decrease != nullptr && increase != nullptr);
      if (decrease == nullptr || increase == nullptr) {
        continue;
      }
      CHECK(!decrease->icon().isNull() && !increase->icon().isNull());
      CHECK(decrease->autoRepeat() && increase->autoRepeat());
    }
    CHECK(slider_rows >= 40);

    const auto bevel_items = categories->findItems(QStringLiteral("Bevel & Emboss"), Qt::MatchExactly);
    CHECK(!bevel_items.isEmpty());
    categories->setCurrentItem(bevel_items.front());
    QApplication::processEvents();

    auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSizeSpin"));
    auto* size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelSizeSlider"));
    auto* size_minus = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelSizeSpinDecreaseButton"));
    auto* size_plus = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelSizeSpinIncreaseButton"));
    CHECK(size != nullptr && size_slider != nullptr);
    CHECK(size_minus != nullptr && size_plus != nullptr);
    CHECK(size_minus->accessibleName() == QStringLiteral("Decrease Size"));
    CHECK(size_plus->accessibleName() == QStringLiteral("Increase Size"));
    CHECK(size_minus->toolTip() == QStringLiteral("Decrease Size"));
    for (auto* button : {size_minus, size_plus}) {
      CHECK(button->isVisible());
      CHECK(button->width() >= 20 && button->height() >= 20);
    }
    // The buttons sit right of the field, and the field keeps a readable width.
    CHECK(size_minus->x() > size->x() + size->width() - 1);
    CHECK(size_plus->x() > size_minus->x());
    CHECK(size->width() >= 60);

    size->setValue(5);
    click_step_button(size_plus);
    CHECK(size->value() == 6);
    CHECK(size_slider->value() == 6);
    click_step_button(size_minus);
    CHECK(size->value() == 5);
    CHECK(size_slider->value() == 5);

    size->setValue(size->minimum());
    CHECK(!size_minus->isEnabled());
    CHECK(size_plus->isEnabled());
    size->setValue(size->maximum());
    CHECK(size_minus->isEnabled());
    CHECK(!size_plus->isEnabled());
    size->setValue(7);
    CHECK(size_minus->isEnabled() && size_plus->isEnabled());

    // The slider side stays in sync when the stepper drives an opacity row too.
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelHighlightOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelHighlightOpacitySlider"));
    auto* opacity_minus =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelHighlightOpacitySpinDecreaseButton"));
    CHECK(opacity != nullptr && opacity_slider != nullptr && opacity_minus != nullptr);
    opacity->setValue(75);
    click_step_button(opacity_minus);
    CHECK(opacity->value() == 74);
    CHECK(opacity_slider->value() == 74);

    save_widget_artifact("ui_layer_style_step_buttons", *dialog);
    inspected = true;
    dialog->accept();
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  CHECK(settings.has_value());
  CHECK(settings->style.bevels.size() == 1);
  const auto& result = settings->style.bevels.front();
  CHECK(std::abs(result.size - 7.0F) < 0.01F);
  CHECK(std::abs(result.highlight_opacity - 0.74F) < 0.005F);
}

// Draws the dialog where it sits on its screen, dimming whatever lies past the
// screen's available area and outlining that area in red, so the artifact shows
// at a glance whether OK/Cancel are reachable.
QImage dialog_on_screen_image(QWidget& dialog, QRect available) {
  const auto frame = dialog.frameGeometry();
  const auto canvas = available.united(frame).adjusted(-8, -8, 8, 8);
  QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor(40, 40, 40));
  QPainter painter(&image);
  painter.translate(-canvas.topLeft());
  painter.fillRect(available, QColor(96, 104, 112));
  painter.drawPixmap(dialog.geometry().topLeft(), dialog.grab());
  QPainterPath outside;
  outside.addRect(canvas);
  QPainterPath inside;
  inside.addRect(available);
  painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 150));
  painter.setPen(QPen(QColor(230, 40, 40), 3));
  painter.drawRect(available.adjusted(1, 1, -2, -2));
  return image;
}

void ui_layer_style_dialog_fits_a_short_screen() {
  // GitHub issue 190: the Blending Options page (Blend If included) made the
  // dialog taller than a 768 px screen (Linux desktop fonts are larger than
  // the suite's), so OK and Cancel sat below its edge. The offscreen screen is
  // 800 x 800; an imported Photoshop layer whose three notices (missing
  // pattern, Satin contour, unsupported Blend If) sit above the pages makes
  // the dialog taller than that here. It must cap itself to the available
  // height, keep its buttons on screen, and scroll the settings pane instead.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Short Screen",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerPatternOverlay missing_pattern;
  missing_pattern.enabled = true;
  missing_pattern.pattern_id = "issue-190-missing-pattern";
  missing_pattern.pattern_name = "Missing Pattern";
  layer.layer_style().pattern_overlays.push_back(missing_pattern);
  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.unsupported_contour_options = true;
  layer.layer_style().satins.push_back(satin);
  layer.set_blend_if_payload({1});
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  // Opened from the main window, as in the app: the dialog inherits the
  // window's theme sheet (its scroll bar included) and sizes to its screen.
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Short Screen"));
  show_window(window);
  bool inspected = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    const auto* screen = dialog->screen();
    CHECK(screen != nullptr);
    const auto available = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr);
    const auto stack = dialog->findChild<QWidget*>(QStringLiteral("layerStyleOptionsStack"));
    CHECK(stack != nullptr);
    auto* pane = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
    const auto frame = dialog->frameGeometry();
    std::printf("layer style dialog %dx%d (minimum %dx%d) frame %d,%d %dx%d on %d,%d %dx%d available "
                "(stack minimum %d, pane %d)\n",
                dialog->width(), dialog->height(), dialog->minimumWidth(), dialog->minimumHeight(), frame.x(),
                frame.y(), frame.width(), frame.height(), available.x(), available.y(), available.width(),
                available.height(), stack != nullptr ? stack->minimumSizeHint().height() : -1,
                pane != nullptr ? pane->height() : -1);
    save_image_artifact("ui_layer_style_dialog_short_screen", dialog_on_screen_image(*dialog, available));
    if (stack != nullptr) {
      save_widget_artifact("ui_layer_style_blending_options_page", *stack);
    }
    CHECK(available.contains(dialog->frameGeometry()));
    if (buttons != nullptr) {
      const QRect button_rect(buttons->mapToGlobal(QPoint(0, 0)), buttons->size());
      CHECK(available.contains(button_rect));
      CHECK(!buttons->visibleRegion().isEmpty());
    }
    // The settings pane scrolls: its last Blend If field is out of view at
    // first and comes into view by scrolling, never by growing the dialog.
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
    CHECK(scroll != nullptr);
    auto* last_field = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteHighSpin"));
    CHECK(last_field != nullptr);
    if (scroll != nullptr && last_field != nullptr) {
      CHECK(scroll->verticalScrollBar()->maximum() > 0);
      const auto height_before = dialog->height();
      scroll->ensureWidgetVisible(last_field);
      QApplication::processEvents();
      CHECK(!last_field->visibleRegion().isEmpty());
      CHECK(dialog->height() == height_before);
      // The pane is wide enough for its widest page next to the scroll bar:
      // no field is clipped on the right.
      CHECK(last_field->mapTo(scroll->viewport(), QPoint(last_field->width(), 0)).x() <=
            scroll->viewport()->width());
    }
    inspected = true;
    dialog->reject();
  }));

  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

void ui_layer_style_dialog_resizes_below_its_tallest_page() {
  // GitHub issue 536: below the issue 190 screen cap the settings pane's
  // minimum was the whole Blending Options page, so the dialog could not be
  // dragged shorter. Once shown, the dialog can be made much smaller than its
  // page and the pane scrolls; it still opens at its natural size, a page
  // switch keeps the size, and the next open restores the size it was left at.
  patchy::ui::set_stored_layer_style_dialog_size({});
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Resize",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(200, 120, 60, 255)));
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Resize"));
  show_window(window);

  const auto open_dialog = [&window](const std::function<void(QDialog&, QScrollArea&)>& inspect) {
    bool inspected = false;
    QTimer::singleShot(0, &window, guarded([&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      QApplication::processEvents();
      auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
      CHECK(scroll != nullptr);
      if (scroll != nullptr) {
        inspect(*dialog, *scroll);
      }
      inspected = true;
      dialog->reject();
    }));
    require_action(window, "layerBlendingOptionsAction")->trigger();
    QApplication::processEvents();
    CHECK(inspected);
  };

  QSize natural;
  QSize small;
  open_dialog([&](QDialog& dialog, QScrollArea& scroll) {
    natural = dialog.size();
    const auto pages_height = scroll.widget()->minimumSizeHint().height();
    std::printf("layer style dialog opens %dx%d (minimum %dx%d), pages %d tall, viewport %d\n", natural.width(),
                natural.height(), dialog.minimumWidth(), dialog.minimumHeight(), pages_height,
                scroll.viewport()->height());
    save_image_artifact("ui_layer_style_dialog_natural_size", dialog.grab().toImage());
    // GitHub issue 606: it opens no taller than three quarters of the screen;
    // a taller page scrolls.
    const auto available = dialog.screen()->availableGeometry();
    CHECK(natural.height() <= std::max(520, available.height() * 3 / 4));
    CHECK(scroll.viewport()->height() >= pages_height || scroll.verticalScrollBar()->maximum() > 0);
    CHECK(dialog.minimumHeight() < natural.height() - 200);

    dialog.resize(600, 400);
    QApplication::processEvents();
    std::printf("resized to 600x400: %dx%d, viewport %d, scroll maximum %d\n", dialog.width(), dialog.height(),
                scroll.viewport()->height(), scroll.verticalScrollBar()->maximum());
    save_image_artifact("ui_layer_style_dialog_resized_small", dialog.grab().toImage());
    CHECK(dialog.height() <= 410);
    CHECK(dialog.width() == std::max(600, dialog.minimumWidth()));
    CHECK(scroll.verticalScrollBar()->isVisible());
    CHECK(scroll.verticalScrollBar()->maximum() > 0);
    // The last Blend If field is reached by scrolling, and the pane keeps the
    // widest page beside its scroll bar: nothing is clipped on the right.
    auto* last_field = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteHighSpin"));
    CHECK(last_field != nullptr);
    if (last_field != nullptr) {
      scroll.ensureWidgetVisible(last_field);
      QApplication::processEvents();
      CHECK(!last_field->visibleRegion().isEmpty());
      CHECK(last_field->mapTo(scroll.viewport(), QPoint(last_field->width(), 0)).x() <= scroll.viewport()->width());
    }
    small = dialog.size();
    // Another page keeps the size the user chose.
    auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    if (categories != nullptr) {
      for (int row = 0; row < categories->count(); ++row) {
        if (categories->item(row)->text().contains(QStringLiteral("Stroke"))) {
          categories->setCurrentRow(row);
          break;
        }
      }
      QApplication::processEvents();
      CHECK(dialog.size() == small);
    }
  });
  CHECK(patchy::ui::stored_layer_style_dialog_size() == small);

  // Reopened, it comes back at the size it was left at.
  open_dialog([&](QDialog& dialog, QScrollArea& scroll) {
    CHECK(dialog.size() == small);
    CHECK(scroll.verticalScrollBar()->maximum() > 0);
  });

  // With no remembered size it opens at its natural size again.
  patchy::ui::set_stored_layer_style_dialog_size({});
  open_dialog([&](QDialog& dialog, QScrollArea&) { CHECK(dialog.size() == natural); });
  CHECK(!patchy::ui::stored_layer_style_dialog_size().isValid());
}

void ui_layer_style_page_captures_issue_606() {
  // GitHub issue 606: one capture per reworked page (Style Presets, Blending
  // Options, Bevel & Emboss, a color page, Inner Glow), opened from the main
  // window so the theme sheet and the style library apply as in the app.
  patchy::ui::set_stored_layer_style_dialog_size({});
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Captures",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  layer.layer_style().bevels.push_back(bevel);
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.color = patchy::RgbColor{255, 255, 190};
  layer.layer_style().inner_glows.push_back(glow);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{220, 40, 40};
  layer.layer_style().color_overlays.push_back(overlay);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 120.0F;
  layer.layer_style().drop_shadows.push_back(shadow);
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Captures"));
  show_window(window);
  bool inspected = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    std::printf("layer style dialog opens %dx%d (minimum %dx%d)\n", dialog->width(), dialog->height(),
                dialog->minimumWidth(), dialog->minimumHeight());
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    const std::array<std::pair<const char*, const char*>, 6> pages{{
        {"Style Presets", "ui_layer_style_606_style_presets"},
        {"Blending Options", "ui_layer_style_606_blending_options"},
        {"Bevel & Emboss", "ui_layer_style_606_bevel_emboss"},
        {"Color Overlay", "ui_layer_style_606_color_overlay"},
        {"Inner Glow", "ui_layer_style_606_inner_glow"},
        {"Drop Shadow", "ui_layer_style_618_drop_shadow"},
    }};
    for (const auto& [title, artifact] : pages) {
      for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
        if (categories->item(row)->text() == QString::fromLatin1(title)) {
          categories->setCurrentRow(row);
          break;
        }
      }
      QApplication::processEvents();
      save_image_artifact(artifact, dialog->grab().toImage());
    }
    // A short dialog: the preset list scrolls, its buttons stay in view (GitHub
    // issue 606 follow-up).
    for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
      if (categories->item(row)->text() == QStringLiteral("Style Presets")) {
        categories->setCurrentRow(row);
        break;
      }
    }
    dialog->resize(dialog->width(), dialog->minimumHeight());
    QApplication::processEvents();
    save_image_artifact("ui_layer_style_606_style_presets_short", dialog->grab().toImage());
    inspected = true;
    dialog->reject();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

QListWidgetItem* layer_style_category(QDialog& dialog, const QString& text) {
  auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
  CHECK(categories != nullptr);
  for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
    if (categories->item(row)->text() == text) {
      return categories->item(row);
    }
  }
  CHECK(false);
  return nullptr;
}

void select_layer_style_category(QDialog& dialog, const QString& text) {
  if (auto* item = layer_style_category(dialog, text); item != nullptr) {
    item->listWidget()->setCurrentItem(item);
  }
  QApplication::processEvents();
}

void ui_layer_style_page_titles_show_ampersands() {
  // GitHub issue 606: a QGroupBox title reads "&" as a mnemonic, so the Bevel &
  // Emboss page title showed an underlined space instead of the ampersand.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Titles",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    bool saw_bevel = false;
    for (auto* group : dialog->findChildren<QGroupBox*>()) {
      auto title = group->title();
      if (title == QStringLiteral("Bevel && Emboss")) {
        saw_bevel = true;
      }
      // Every ampersand left is an escaped one: none turns into a mnemonic.
      title.replace(QStringLiteral("&&"), QString());
      CHECK(!title.contains(QLatin1Char('&')));
    }
    CHECK(saw_bevel);
    // The category list draws the name in a plain label, which keeps the "&".
    auto* bevel_item = layer_style_category(*dialog, QStringLiteral("Bevel & Emboss"));
    CHECK(bevel_item != nullptr);
    if (bevel_item != nullptr) {
      auto* row = bevel_item->listWidget()->itemWidget(bevel_item);
      auto* label = row != nullptr ? row->findChild<QLabel*>() : nullptr;
      CHECK(label != nullptr && label->text() == QStringLiteral("Bevel & Emboss"));
    }
    inspected = true;
    dialog->reject();
  }));
  (void)patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
}

void ui_layer_style_make_default_and_reset_to_default() {
  // GitHub issue 606: Make Default stores a page's values as that effect's
  // defaults; Reset to Default loads them back (the built-in ones when none
  // are stored); an effect added later starts from them.
  patchy::ui::clear_stored_layer_style_effect_defaults();
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Defaults",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto click = [](QDialog& dialog, const char* name) {
    auto* button = dialog.findChild<QPushButton*>(QString::fromLatin1(name));
    CHECK(button != nullptr);
    if (button != nullptr) {
      button->click();
    }
    QApplication::processEvents();
  };
  const auto spin = [](QDialog& dialog, const char* name) {
    auto* field = dialog.findChild<QSpinBox*>(QString::fromLatin1(name));
    CHECK(field != nullptr);
    return field;
  };

  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    // Pages without the pair.
    for (const auto* name : {"layerStyleBevelContourMakeDefaultButton", "layerStyleBevelTextureMakeDefaultButton",
                             "layerStyleBlendingMakeDefaultButton", "layerStyleStylesMakeDefaultButton"}) {
      CHECK(dialog->findChild<QPushButton*>(QString::fromLatin1(name)) == nullptr);
    }
    CHECK(dialog->findChildren<QPushButton*>(QRegularExpression(QStringLiteral("MakeDefaultButton$"))).size() == 10);
    CHECK(dialog->findChildren<QPushButton*>(QRegularExpression(QStringLiteral("ResetToDefaultButton$"))).size() ==
          10);

    // Nothing stored: Reset to Default gives the built-in values.
    select_layer_style_category(*dialog, QStringLiteral("Drop Shadow"));
    auto* size = spin(*dialog, "layerStyleDropShadowSizeSpin");
    auto* distance = spin(*dialog, "layerStyleDropShadowDistanceSpin");
    auto* red = spin(*dialog, "layerStyleDropShadowRedSpin");
    auto* continuous = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowContinuousCheck"));
    CHECK(size != nullptr && distance != nullptr && red != nullptr && continuous != nullptr);
    if (size == nullptr || distance == nullptr || red == nullptr || continuous == nullptr) {
      return;
    }
    size->setValue(40);
    click(*dialog, "layerStyleDropShadowResetToDefaultButton");
    CHECK(size->value() == 5);
    CHECK(distance->value() == 5);

    // Store new values, change them, and reset back to the stored ones.
    size->setValue(17);
    distance->setValue(9);
    red->setValue(200);
    continuous->setChecked(true);
    click(*dialog, "layerStyleDropShadowMakeDefaultButton");
    size->setValue(30);
    red->setValue(10);
    continuous->setChecked(false);
    click(*dialog, "layerStyleDropShadowResetToDefaultButton");
    CHECK(size->value() == 17);
    CHECK(distance->value() == 9);
    CHECK(red->value() == 200);
    CHECK(continuous->isChecked());
    inspected = true;
    dialog->reject();
  }));
  (void)patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);

  // Reopened, a drop shadow turned on starts from the stored values; the page
  // shows them before it is ticked.
  inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_layer_style_category(*dialog, QStringLiteral("Drop Shadow"));
    auto* size = spin(*dialog, "layerStyleDropShadowSizeSpin");
    CHECK(size != nullptr && size->value() == 17);
    if (auto* item = layer_style_category(*dialog, QStringLiteral("Drop Shadow")); item != nullptr) {
      item->setCheckState(Qt::Checked);
    }
    QApplication::processEvents();
    inspected = true;
    dialog->accept();
  }));
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  CHECK(settings.has_value());
  if (settings.has_value()) {
    CHECK(settings->style.drop_shadows.size() == 1U);
    if (!settings->style.drop_shadows.empty()) {
      const auto& shadow = settings->style.drop_shadows.front();
      CHECK(shadow.enabled);
      CHECK(std::abs(shadow.size - 17.0F) < 0.01F);
      CHECK(std::abs(shadow.distance - 9.0F) < 0.01F);
      CHECK(shadow.color.red == 200);
      CHECK(shadow.continuous);
    }
    // Effects without stored values keep the built-in defaults.
    CHECK(settings->style.strokes.empty());
  }

  // Bevel & Emboss: the page's values only; Reset keeps the effect's state.
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.size = 12.0F;
  bevel.direction_up = true;
  bevel.contour.enabled = true;
  layer.layer_style().bevels.push_back(bevel);
  inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_layer_style_category(*dialog, QStringLiteral("Bevel & Emboss"));
    auto* size = spin(*dialog, "layerStyleBevelSizeSpin");
    auto* down = dialog->findChild<QRadioButton*>(QStringLiteral("layerStyleBevelDirectionDownRadio"));
    CHECK(size != nullptr && down != nullptr);
    if (size == nullptr || down == nullptr) {
      return;
    }
    CHECK(size->value() == 12);
    size->setValue(21);
    down->setChecked(true);
    click(*dialog, "layerStyleBevelMakeDefaultButton");
    size->setValue(3);
    click(*dialog, "layerStyleBevelResetToDefaultButton");
    CHECK(size->value() == 21);
    CHECK(down->isChecked());
    inspected = true;
    dialog->accept();
  }));
  const auto bevel_settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  CHECK(bevel_settings.has_value());
  if (bevel_settings.has_value() && !bevel_settings->style.bevels.empty()) {
    const auto& result = bevel_settings->style.bevels.front();
    CHECK(result.enabled);
    CHECK(std::abs(result.size - 21.0F) < 0.01F);
    CHECK(!result.direction_up);
    // The Contour sub-option is its own page and keeps its state.
    CHECK(result.contour.enabled);
  }
  patchy::ui::clear_stored_layer_style_effect_defaults();
}

void click_widget_at(QWidget& widget, QPointF position) {
  send_mouse(widget, QEvent::MouseButtonPress, position.toPoint(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(widget, QEvent::MouseButtonRelease, position.toPoint(), Qt::LeftButton, Qt::NoButton);
}

void ui_angle_dial_sets_angle_and_altitude_from_clicks() {
  // GitHub issue 618: the round angle control beside the angle fields. 0 is to
  // the right and angles turn counterclockwise, as the number fields read.
  QWidget host;
  QSpinBox angle(&host);
  angle.setRange(-180, 180);
  angle.setValue(30);
  patchy::ui::AngleDial dial(patchy::ui::AngleDial::Mode::Angle, &host);
  patchy::ui::bind_angle_dial(dial, angle);
  CHECK(dial.angle() == 30.0);
  const auto center = dial.center();
  const auto r = dial.radius();
  click_widget_at(dial, center + QPointF(r - 1.0, 0.0));
  CHECK(angle.value() == 0);
  click_widget_at(dial, center + QPointF(0.0, -(r - 1.0)));
  CHECK(angle.value() == 90);
  click_widget_at(dial, center + QPointF(-(r - 1.0), 0.0));
  CHECK(angle.value() == 180);
  click_widget_at(dial, center + QPointF(0.0, r - 1.0));
  CHECK(angle.value() == -90);
  CHECK(dial.angle() == -90.0);
  // A drag follows the pointer and ends where it is released.
  send_mouse(dial, QEvent::MouseButtonPress, (center + QPointF(r - 1.0, 0.0)).toPoint(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(dial, QEvent::MouseMove, (center + QPointF(r - 4.0, -(r - 4.0))).toPoint(), Qt::NoButton,
             Qt::LeftButton);
  CHECK(std::abs(angle.value() - 45) <= 3);
  send_mouse(dial, QEvent::MouseButtonRelease, (center + QPointF(0.0, -(r - 1.0))).toPoint(), Qt::LeftButton,
             Qt::NoButton);
  CHECK(angle.value() == 90);
  // The field moves the dial; a load under a signal blocker syncs explicitly.
  angle.setValue(-45);
  CHECK(dial.angle() == -45.0);
  {
    const QSignalBlocker blocker(&angle);
    angle.setValue(60);
  }
  CHECK(dial.angle() == -45.0);
  patchy::ui::sync_angle_dial(angle);
  CHECK(dial.angle() == 60.0);
  const auto marker = dial.marker_position() - center;
  CHECK(marker.x() > 0.0 && marker.y() < 0.0);
  // Keyboard: arrows step 1, Shift 15, and a full-turn range wraps.
  QTest::keyClick(&dial, Qt::Key_Right);
  CHECK(angle.value() == 61);
  QTest::keyClick(&dial, Qt::Key_Left, Qt::ShiftModifier);
  CHECK(angle.value() == 46);
  angle.setValue(180);
  QTest::keyClick(&dial, Qt::Key_Up);
  CHECK(angle.value() == -179);
  // The wheel scrolls the page unless the dial has keyboard focus.
  QWheelEvent wheel(QPointF{center}, QPointF{center}, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(&dial, &wheel);
  CHECK(!wheel.isAccepted());
  CHECK(angle.value() == -179);

  // Angle and altitude (Bevel & Emboss): the distance from the center sets the
  // altitude, 90 at the center and 0 at the rim.
  QSpinBox bevel_angle(&host);
  bevel_angle.setRange(-180, 180);
  bevel_angle.setValue(120);
  QSpinBox altitude(&host);
  altitude.setRange(0, 90);
  altitude.setValue(30);
  patchy::ui::AngleDial shading(patchy::ui::AngleDial::Mode::AngleAltitude, &host);
  patchy::ui::bind_angle_dial(shading, bevel_angle, &altitude);
  CHECK(shading.altitude() == 30.0);
  CHECK(shading.width() > dial.width());
  const auto middle = shading.center();
  const auto rim = shading.radius();
  click_widget_at(shading, middle);
  CHECK(altitude.value() == 90);
  CHECK(bevel_angle.value() == 120);
  click_widget_at(shading, middle + QPointF(rim, 0.0));
  CHECK(altitude.value() == 0);
  CHECK(bevel_angle.value() == 0);
  click_widget_at(shading, middle + QPointF(0.0, -rim / 2.0));
  CHECK(std::abs(altitude.value() - 45) <= 3);
  CHECK(bevel_angle.value() == 90);
  altitude.setValue(45);
  const auto at = shading.marker_position() - middle;
  CHECK(std::abs(at.x()) < 0.5 && std::abs(at.y() + rim / 2.0) < 0.5);
  CHECK(patchy::ui::AngleDial::altitude_for_distance(0.0, 30.0) == 90);
  CHECK(patchy::ui::AngleDial::altitude_for_distance(30.0, 30.0) == 0);
  CHECK(patchy::ui::AngleDial::altitude_for_distance(90.0, 30.0) == 0);
  QTest::keyClick(&shading, Qt::Key_Up);
  CHECK(altitude.value() == 46);
  QTest::keyClick(&shading, Qt::Key_Right);
  CHECK(bevel_angle.value() == 91);
}

void ui_layer_style_angle_dials_drive_the_fields() {
  // GitHub issue 618: every Layer Style angle row has a dial, Bevel & Emboss
  // the angle-and-altitude one; a click previews live like the slider did and
  // OK commits one undo step.
  patchy::ui::set_stored_layer_style_dialog_size({});
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Dials",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 120.0F;
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  layer.layer_style().bevels.push_back(bevel);
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Dials"));
  show_window(window);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool inspected = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    for (const auto* name :
         {"layerStyleDropShadowAngleDial", "layerStyleInnerShadowAngleDial", "layerStyleSatinAngleDial",
          "layerStylePatternOverlayAngleDial", "layerStyleGradientAngleDial", "layerStyleStrokeGradientAngleDial"}) {
      auto* found = dialog->findChild<patchy::ui::AngleDial*>(QLatin1String(name));
      if (found == nullptr) {
        std::printf("missing angle dial: %s\n", name);
      }
      CHECK(found != nullptr && found->mode() == patchy::ui::AngleDial::Mode::Angle);
    }
    CHECK(dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowAngleSlider")) == nullptr);
    CHECK(dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelAltitudeSlider")) == nullptr);
    auto* shading = dialog->findChild<patchy::ui::AngleDial*>(QStringLiteral("layerStyleBevelAngleDial"));
    auto* bevel_angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelAngleSpin"));
    auto* bevel_altitude = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelAltitudeSpin"));
    CHECK(shading != nullptr && shading->mode() == patchy::ui::AngleDial::Mode::AngleAltitude);
    CHECK(bevel_angle != nullptr && bevel_altitude != nullptr);
    select_layer_style_category(*dialog, QStringLiteral("Bevel & Emboss"));
    if (shading != nullptr && bevel_angle != nullptr && bevel_altitude != nullptr) {
      CHECK(shading->angle() == bevel_angle->value());
      CHECK(shading->altitude() == bevel_altitude->value());
      click_widget_at(*shading, shading->center() + QPointF(-shading->radius(), 0.0));
      CHECK(bevel_angle->value() == 180);
      CHECK(bevel_altitude->value() == 0);
      bevel_altitude->setValue(60);
      CHECK(shading->altitude() == 60.0);
      // The number fields share the page's field column.
      const auto left = [dialog](const QWidget* widget) { return widget->mapTo(dialog, QPoint(0, 0)).x(); };
      CHECK(left(bevel_angle) == left(bevel_altitude));
    }
    select_layer_style_category(*dialog, QStringLiteral("Drop Shadow"));
    auto* dial = dialog->findChild<patchy::ui::AngleDial*>(QStringLiteral("layerStyleDropShadowAngleDial"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowAngleSpin"));
    CHECK(dial != nullptr && angle != nullptr);
    if (dial != nullptr && angle != nullptr) {
      CHECK(angle->value() == 120);
      CHECK(dial->angle() == 120.0);
      click_widget_at(*dial, dial->center() + QPointF(0.0, dial->radius() - 1.0));
      CHECK(angle->value() == -90);
      QTest::qWait(80);
      const auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
      CHECK(preview_layer != nullptr);
      if (preview_layer != nullptr && !preview_layer->layer_style().drop_shadows.empty()) {
        CHECK(std::abs(preview_layer->layer_style().drop_shadows.front().angle_degrees + 90.0F) < 0.5F);
      }
    }
    inspected = true;
    dialog->accept();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  const auto* committed = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(committed != nullptr);
  if (committed != nullptr) {
    CHECK(!committed->layer_style().drop_shadows.empty() &&
          std::abs(committed->layer_style().drop_shadows.front().angle_degrees + 90.0F) < 0.5F);
    CHECK(!committed->layer_style().bevels.empty() &&
          std::abs(committed->layer_style().bevels.front().angle_degrees - 180.0F) < 0.5F &&
          std::abs(committed->layer_style().bevels.front().altitude_degrees - 60.0F) < 0.5F);
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1U);
}

void ui_layer_style_issue_606_layout() {
  // GitHub issue 606: a smaller opening size, OK as wide as Cancel, radio
  // buttons for two-choice settings, colons on labels, one label column, the
  // Blending Options check boxes in two columns when wide, and the Style
  // Presets grid with its List/Grid buttons (Grid by default, remembered).
  patchy::ui::set_stored_layer_style_dialog_size({});
  patchy::ui::set_stored_layer_style_styles_grid_view(true);
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Layout",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layout"));
  show_window(window);

  const auto open_dialog = [&window](const std::function<void(QDialog&)>& inspect) {
    bool inspected = false;
    QTimer::singleShot(0, &window, guarded([&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      QApplication::processEvents();
      inspect(*dialog);
      inspected = true;
      dialog->reject();
    }));
    require_action(window, "layerBlendingOptionsAction")->trigger();
    QApplication::processEvents();
    CHECK(inspected);
  };

  open_dialog([](QDialog& dialog) {
    const auto available = dialog.screen()->availableGeometry();
    std::printf("layer style dialog opens %dx%d on %dx%d\n", dialog.width(), dialog.height(), available.width(),
                available.height());
    CHECK(dialog.height() <= std::max(520, available.height() * 3 / 4));

    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr);
    if (buttons != nullptr) {
      auto* ok = buttons->button(QDialogButtonBox::Ok);
      auto* cancel = buttons->button(QDialogButtonBox::Cancel);
      CHECK(ok != nullptr && cancel != nullptr && ok->width() == cancel->width());
    }

    // Two-choice settings are radio buttons.
    for (const auto* name : {"layerStyleBevelDirectionUpRadio", "layerStyleBevelDirectionDownRadio",
                             "layerStyleInnerGlowSourceCenterRadio", "layerStyleInnerGlowSourceEdgeRadio"}) {
      CHECK(dialog.findChild<QRadioButton*>(QString::fromLatin1(name)) != nullptr);
    }
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("layerStyleBevelDirectionCombo")) == nullptr);
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("layerStyleInnerGlowSourceCombo")) == nullptr);

    // Color pages: a swatch button and number fields, no R/G/B sliders.
    CHECK(dialog.findChild<QPushButton*>(QStringLiteral("layerStyleDropShadowColorPreview")) != nullptr);
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("layerStyleDropShadowRedSlider")) == nullptr);
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("layerStyleColorOverlayRedSlider")) == nullptr);

    // Every form label ends in a colon and every page's fields start at one x.
    auto* stack = dialog.findChild<QStackedWidget*>(QStringLiteral("layerStyleOptionsStack"));
    CHECK(stack != nullptr);
    if (stack != nullptr) {
      int label_width = -1;
      for (auto* form : stack->findChildren<QFormLayout*>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
          auto* item = form->itemAt(row, QFormLayout::LabelRole);
          auto* label = item != nullptr ? qobject_cast<QLabel*>(item->widget()) : nullptr;
          if (label == nullptr || label->text().isEmpty()) {
            continue;
          }
          if (!label->text().endsWith(QLatin1Char(':'))) {
            std::printf("label without a colon: %s\n", qPrintable(label->text()));
          }
          CHECK(label->text().endsWith(QLatin1Char(':')));
          if (label_width < 0) {
            label_width = label->minimumWidth();
          }
          CHECK(label->minimumWidth() == label_width);
        }
      }
    }

    // Blending Options: the mask and Show Effects boxes move beside the others
    // when wide and below them when narrow.
    select_layer_style_category(dialog, QStringLiteral("Blending Options"));
    auto* interior = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleBlendInteriorCheck"));
    auto* clipped = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleBlendClippedCheck"));
    auto* mask = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleMaskHidesEffectsCheck"));
    auto* show = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleShowEffectsCheck"));
    CHECK(interior != nullptr && clipped != nullptr && mask != nullptr && show != nullptr);
    if (interior != nullptr && clipped != nullptr && mask != nullptr && show != nullptr) {
      dialog.resize(std::min(available.width(), 1200), dialog.height());
      QApplication::processEvents();
      save_image_artifact("ui_layer_style_606_blending_options_wide", dialog.grab().toImage());
      CHECK(mask->x() > interior->x());
      CHECK(mask->y() == interior->y());
      CHECK(show->y() > mask->y());
      dialog.resize(dialog.minimumWidth(), dialog.height());
      QApplication::processEvents();
      save_image_artifact("ui_layer_style_606_blending_options_narrow", dialog.grab().toImage());
      CHECK(mask->x() == interior->x());
      CHECK(mask->y() > clipped->y());
      CHECK(!mask->visibleRegion().isEmpty() || mask->isVisible());
    }
    // Blend If (GitHub issue 606 follow-ups 1 and 2): only "Blend If:" and its
    // channel combo use the label and field columns; the This Layer and
    // Underlying Layer rows span the section from its left edge, each title on
    // the line with its Black and White values.
    const auto left = [&dialog](const QWidget* widget) { return widget->mapTo(&dialog, QPoint(0, 0)).x(); };
    const auto top = [&dialog](const QWidget* widget) { return widget->mapTo(&dialog, QPoint(0, 0)).y(); };
    auto* channel = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleBlendIfChannelCombo"));
    auto* blend_if = dialog.findChild<QWidget*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* knockout = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleKnockoutCombo"));
    CHECK(channel != nullptr && blend_if != nullptr && knockout != nullptr);
    if (channel != nullptr && blend_if != nullptr && knockout != nullptr) {
      auto* row = channel->parentWidget();
      auto* form = qobject_cast<QFormLayout*>(row->parentWidget()->layout());
      CHECK(form != nullptr);
      QLabel* label = form != nullptr ? qobject_cast<QLabel*>(form->labelForField(row)) : nullptr;
      CHECK(label != nullptr && label->text() == QStringLiteral("Blend If:"));
      CHECK(left(channel) == left(knockout));
      CHECK(left(blend_if) < left(knockout));
      if (label != nullptr) {
        CHECK(left(blend_if) == left(label));
      }
      for (const auto& [title, spin_name] :
           {std::pair{QStringLiteral("This Layer:"), QStringLiteral("layerStyleBlendIfThisBlackLowSpin")},
            std::pair{QStringLiteral("Underlying Layer:"), QStringLiteral("layerStyleBlendIfUnderlyingBlackLowSpin")}}) {
        QLabel* title_label = nullptr;
        for (auto* candidate : blend_if->findChildren<QLabel*>()) {
          if (candidate->text() == title) {
            title_label = candidate;
          }
        }
        auto* black = blend_if->findChild<QSpinBox*>(spin_name);
        CHECK(title_label != nullptr && black != nullptr);
        if (title_label != nullptr && black != nullptr) {
          CHECK(left(title_label) == left(blend_if));
          const auto title_middle = top(title_label) + title_label->height() / 2;
          CHECK(title_middle > top(black) && title_middle < top(black) + black->height());
          CHECK(left(black) > left(title_label) + title_label->width());
        }
      }
    }

    // GitHub issue 606 follow-up 3: a resize grip in the bottom right corner.
    auto* grip = dialog.findChild<QSizeGrip*>(QStringLiteral("layerStyleSizeGrip"));
    CHECK(grip != nullptr && grip->isVisible());
    if (grip != nullptr) {
      const QRect corner(grip->mapTo(&dialog, QPoint(0, 0)), grip->size());
      CHECK(dialog.rect().contains(corner));
      CHECK(dialog.width() - corner.right() < 24);
      CHECK(dialog.height() - corner.bottom() < 24);
    }

    // Changing pages starts the new page at its top, not where the last one
    // was scrolled to (issue 606 follow-up 3).
    auto* options_scroll = dialog.findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
    CHECK(options_scroll != nullptr);
    if (options_scroll != nullptr) {
      const auto saved_size = dialog.size();
      dialog.resize(dialog.width(), dialog.minimumHeight());
      QApplication::processEvents();
      auto* bar = options_scroll->verticalScrollBar();
      for (const auto& [from, to] : {std::pair{QStringLiteral("Blending Options"), QStringLiteral("Drop Shadow")},
                                     std::pair{QStringLiteral("Drop Shadow"), QStringLiteral("Bevel & Emboss")},
                                     std::pair{QStringLiteral("Bevel & Emboss"), QStringLiteral("Blending Options")}}) {
        select_layer_style_category(dialog, from);
        CHECK(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QApplication::processEvents();
        CHECK(bar->value() > 0);
        select_layer_style_category(dialog, to);
        if (bar->value() != 0) {
          std::printf("%s opened scrolled to %d after %s\n", qPrintable(to), bar->value(), qPrintable(from));
        }
        CHECK(bar->value() == 0);
      }
      dialog.resize(saved_size);
      QApplication::processEvents();
    }

    // Make Default and Reset to Default: one row under the scroll area, at its
    // left, holding the current effect page's pair; pages without defaults
    // hide it (issue 606 follow-up 3).
    auto* defaults_row = dialog.findChild<QWidget*>(QStringLiteral("layerStyleDefaultsRow"));
    CHECK(defaults_row != nullptr && options_scroll != nullptr);
    if (defaults_row != nullptr && options_scroll != nullptr) {
      for (const auto& [page, prefix] :
           {std::pair{QStringLiteral("Drop Shadow"), QStringLiteral("layerStyleDropShadow")},
            std::pair{QStringLiteral("Bevel & Emboss"), QStringLiteral("layerStyleBevel")},
            std::pair{QStringLiteral("Color Overlay"), QStringLiteral("layerStyleColorOverlay")}}) {
        select_layer_style_category(dialog, page);
        CHECK(defaults_row->isVisible());
        auto* make = dialog.findChild<QPushButton*>(prefix + QStringLiteral("MakeDefaultButton"));
        auto* reset = dialog.findChild<QPushButton*>(prefix + QStringLiteral("ResetToDefaultButton"));
        CHECK(make != nullptr && reset != nullptr);
        if (make == nullptr || reset == nullptr) {
          continue;
        }
        CHECK(make->isVisible() && reset->isVisible());
        CHECK(!options_scroll->isAncestorOf(make) && !options_scroll->isAncestorOf(reset));
        CHECK(left(make) == left(options_scroll));
        CHECK(left(reset) > left(make));
        CHECK(top(make) >= top(options_scroll) + options_scroll->height());
        CHECK(top(make) + make->height() <= dialog.height());
        int shown = 0;
        for (auto* button : defaults_row->findChildren<QPushButton*>()) {
          shown += button->isVisible() ? 1 : 0;
        }
        CHECK(shown == 2);
      }
      for (const auto& page : {QStringLiteral("Style Presets"), QStringLiteral("Blending Options"),
                               QStringLiteral("Contour"), QStringLiteral("Texture")}) {
        select_layer_style_category(dialog, page);
        if (defaults_row->isVisible()) {
          std::printf("defaults row shows on %s\n", qPrintable(page));
        }
        CHECK(!defaults_row->isVisible());
      }
      select_layer_style_category(dialog, QStringLiteral("Blending Options"));
    }

    // Style Presets: no hint text, the grid shown, an info icon with the export tip.
    select_layer_style_category(dialog, QStringLiteral("Style Presets"));
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("layerStyleStylesHint")) == nullptr);
    auto* views = dialog.findChild<QStackedWidget*>(QStringLiteral("layerStyleStylesViews"));
    auto* grid = dialog.findChild<QWidget*>(QStringLiteral("layerStyleStylesGrid"));
    auto* list_button = dialog.findChild<QToolButton*>(QStringLiteral("layerStyleStylesListViewButton"));
    auto* grid_button = dialog.findChild<QToolButton*>(QStringLiteral("layerStyleStylesGridViewButton"));
    auto* info = dialog.findChild<QLabel*>(QStringLiteral("layerStyleStylesInfoIcon"));
    CHECK(views != nullptr && grid != nullptr && list_button != nullptr && grid_button != nullptr);
    CHECK(info != nullptr && info->toolTip() ==
                                 QStringLiteral("Right-click a folder or style to export it as a Photoshop .asl file."));
    if (views != nullptr && list_button != nullptr && grid_button != nullptr) {
      CHECK(views->currentWidget() == grid);
      CHECK(grid_button->isChecked() && !list_button->isChecked());
      CHECK(views->height() >= std::min(500, std::max(240, available.height() / 2)) - 2);
      // GitHub issue 606 follow-up: New Style, Manage Styles and the view
      // buttons sit outside the scrolling pane, so they show at any height.
      auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
      auto* new_style = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleNewStyleButton"));
      auto* manage = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleManageStylesButton"));
      CHECK(scroll != nullptr && new_style != nullptr && manage != nullptr);
      if (scroll != nullptr && new_style != nullptr && manage != nullptr && info != nullptr) {
        const auto saved_size = dialog.size();
        dialog.resize(dialog.width(), dialog.minimumHeight());
        QApplication::processEvents();
        for (QWidget* button : std::initializer_list<QWidget*>{new_style, manage, list_button, grid_button, info}) {
          CHECK(!scroll->isAncestorOf(button));
          CHECK(button->isVisible());
          const QRect in_dialog(button->mapTo(&dialog, QPoint(0, 0)), button->size());
          CHECK(dialog.rect().contains(in_dialog));
        }
        CHECK(views->height() < std::min(500, std::max(240, available.height() / 2)));
        dialog.resize(saved_size);
        QApplication::processEvents();
      }
      list_button->click();
      QApplication::processEvents();
      CHECK(views->currentWidget() != grid);
      CHECK(list_button->isChecked());
    }
  });
  CHECK(!patchy::ui::stored_layer_style_styles_grid_view());
  // The choice is remembered.
  open_dialog([](QDialog& dialog) {
    auto* views = dialog.findChild<QStackedWidget*>(QStringLiteral("layerStyleStylesViews"));
    auto* grid = dialog.findChild<QWidget*>(QStringLiteral("layerStyleStylesGrid"));
    CHECK(views != nullptr && views->currentWidget() != grid);
  });
  patchy::ui::set_stored_layer_style_styles_grid_view(true);
  patchy::ui::set_stored_layer_style_dialog_size({});
}

void ui_layer_style_short_page_has_no_scroll_bar() {
  // GitHub issue 606 follow-up 4: the settings stack sized itself to its
  // tallest page, so a short page (Color Overlay) showed a scroll bar over
  // empty space. Only the page that shows sizes the pane's contents now; the
  // dialog keeps its size on every page, and a page taller than the pane (Bevel
  // & Emboss on a dialog dragged short) still scrolls.
  patchy::ui::set_stored_layer_style_dialog_size({});
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Short Page",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{220, 40, 40};
  layer.layer_style().color_overlays.push_back(overlay);
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Short Page"));
  show_window(window);

  bool inspected = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("layerStyleOptionsScroll"));
    auto* stack = dialog->findChild<QStackedWidget*>(QStringLiteral("layerStyleOptionsStack"));
    auto* color_page = dialog->findChild<QWidget*>(QStringLiteral("layerStyleColorOverlayPage"));
    CHECK(scroll != nullptr && stack != nullptr && color_page != nullptr);
    if (scroll == nullptr || stack == nullptr || color_page == nullptr) {
      dialog->reject();
      return;
    }
    auto* bar = scroll->verticalScrollBar();
    const auto opened = dialog->size();

    select_layer_style_category(*dialog, QStringLiteral("Color Overlay"));
    QApplication::processEvents();
    std::printf("color overlay: dialog %dx%d, viewport %d, page minimum %d, stack minimum %d, bar %s max %d\n",
                dialog->width(), dialog->height(), scroll->viewport()->height(),
                color_page->minimumSizeHint().height(), stack->minimumSizeHint().height(),
                bar->isVisible() ? "shown" : "hidden", bar->maximum());
    save_image_artifact("ui_layer_style_color_overlay_no_scroll_bar", dialog->grab().toImage());
    CHECK(stack->currentWidget() == color_page);
    CHECK(color_page->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!bar->isVisible());
    CHECK(bar->maximum() == 0);
    // Changing pages never resizes the dialog.
    CHECK(dialog->size() == opened);

    // The tallest pages still scroll once the dialog is shorter than them.
    dialog->resize(dialog->width(), dialog->minimumHeight());
    QApplication::processEvents();
    for (const auto& page : {QStringLiteral("Bevel & Emboss"), QStringLiteral("Blending Options")}) {
      select_layer_style_category(*dialog, page);
      QApplication::processEvents();
      if (!bar->isVisible()) {
        std::printf("%s shows no scroll bar at %d px\n", qPrintable(page), dialog->height());
      }
      CHECK(bar->isVisible());
      CHECK(bar->maximum() > 0);
    }

    // Back at the opening size, Color Overlay fits again.
    dialog->resize(opened);
    QApplication::processEvents();
    select_layer_style_category(*dialog, QStringLiteral("Color Overlay"));
    QApplication::processEvents();
    CHECK(!bar->isVisible());
    CHECK(dialog->size() == opened);
    inspected = true;
    dialog->reject();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  patchy::ui::set_stored_layer_style_dialog_size({});
}

void ui_layer_style_knockout_and_vector_mask_hides_effects_commit() {
  // GitHub issue 191: Blending Options gains Photoshop's Knockout (None,
  // Shallow, Deep) and Vector Mask Hides Effects. The layer carries the None
  // 'knko' block Photoshop writes on every layer; committing Deep patches it
  // in place, and the vector flag lands in the layer style.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Knockout",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.unknown_psd_blocks().push_back({"knko", {0, 0, 0, 0}});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Knockout"));
  show_window(window);
  bool inspected = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* knockout = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleKnockoutCombo"));
    auto* vector_hides = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleVectorMaskHidesEffectsCheck"));
    CHECK(knockout != nullptr);
    CHECK(vector_hides != nullptr);
    if (knockout == nullptr || vector_hides == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(knockout->isEnabled());
    CHECK(knockout->count() == 3);
    CHECK(knockout->currentData().toInt() == 0);
    CHECK(!vector_hides->isChecked());
    knockout->setCurrentIndex(knockout->findData(2));
    vector_hides->setChecked(true);
    QTest::qWait(80);
    // The live preview already renders the knockout from the patched block.
    const auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
    CHECK(preview_layer != nullptr);
    if (preview_layer != nullptr) {
      CHECK(patchy::layer_knockout_setting(*preview_layer) == std::optional<std::uint8_t>{std::uint8_t{2}});
    }
    if (auto* stack = dialog->findChild<QWidget*>(QStringLiteral("layerStyleOptionsStack")); stack != nullptr) {
      save_widget_artifact("ui_layer_style_blending_options_knockout", *stack);
    }
    inspected = true;
    dialog->accept();
  }));
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  const auto* committed = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(committed != nullptr);
  if (committed != nullptr) {
    CHECK(committed->layer_style().vector_mask_hides_effects);
    CHECK(patchy::layer_knockout_setting(*committed) == std::optional<std::uint8_t>{std::uint8_t{2}});
    CHECK(committed->unknown_psd_blocks().size() == 1U);
  }

  // A knko payload Patchy cannot interpret leaves the control disabled and
  // the commit cannot change it.
  patchy::Document other(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer unknown(other.allocate_layer_id(), "Unknown Knockout",
                        solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  unknown.unknown_psd_blocks().push_back({"knko", {9, 0, 0, 0}});
  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* knockout = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleKnockoutCombo"));
    CHECK(knockout != nullptr && !knockout->isEnabled());
    dialog->accept();
  }));
  CHECK(patchy::ui::request_layer_style_settings(nullptr, unknown, {}).has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_gradient_tests_part1() {
  return {
      {"ui_layer_style_dialog_fits_a_short_screen", ui_layer_style_dialog_fits_a_short_screen},
      {"ui_layer_style_dialog_resizes_below_its_tallest_page", ui_layer_style_dialog_resizes_below_its_tallest_page},
      {"ui_layer_style_page_captures_issue_606", ui_layer_style_page_captures_issue_606},
      {"ui_layer_style_page_titles_show_ampersands", ui_layer_style_page_titles_show_ampersands},
      {"ui_layer_style_make_default_and_reset_to_default", ui_layer_style_make_default_and_reset_to_default},
      {"ui_layer_style_issue_606_layout", ui_layer_style_issue_606_layout},
      {"ui_layer_style_short_page_has_no_scroll_bar", ui_layer_style_short_page_has_no_scroll_bar},
      {"ui_angle_dial_sets_angle_and_altitude_from_clicks", ui_angle_dial_sets_angle_and_altitude_from_clicks},
      {"ui_layer_style_angle_dials_drive_the_fields", ui_layer_style_angle_dials_drive_the_fields},
      {"ui_layer_style_knockout_and_vector_mask_hides_effects_commit",
       ui_layer_style_knockout_and_vector_mask_hides_effects_commit},
      {"ui_layer_style_shadow_distances_accept_photoshop_30000",
       ui_layer_style_shadow_distances_accept_photoshop_30000},
      {"ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks",
       ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks},
      {"ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render",
       ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render},
      {"ui_layer_style_dialog_does_not_open_a_second_dialog",
       ui_layer_style_dialog_does_not_open_a_second_dialog},
      {"ui_layer_style_stroke_size_drag_stays_responsive_on_large_image",
       ui_layer_style_stroke_size_drag_stays_responsive_on_large_image},
      {"ui_levels_drag_stays_responsive_on_large_image", ui_levels_drag_stays_responsive_on_large_image},
      {"ui_layer_style_drag_on_zoomed_out_large_image_never_stalls",
       ui_layer_style_drag_on_zoomed_out_large_image_never_stalls},
      {"ui_gaussian_blur_drag_stays_responsive_on_large_image",
       ui_gaussian_blur_drag_stays_responsive_on_large_image},
      {"ui_slider_drag_moves_coalesce_to_the_latest_position",
       ui_slider_drag_moves_coalesce_to_the_latest_position},
      {"ui_blend_if_range_editor_renders_and_edits_split_handles",
       ui_blend_if_range_editor_renders_and_edits_split_handles},
      {"ui_layer_style_blend_if_controls_load_channels_and_map_settings",
       ui_layer_style_blend_if_controls_load_channels_and_map_settings},
      {"ui_layer_style_slider_rows_have_step_buttons", ui_layer_style_slider_rows_have_step_buttons},
      {"ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores",
       ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores},
      {"ui_layer_style_blend_if_unsupported_requires_explicit_replace",
       ui_layer_style_blend_if_unsupported_requires_explicit_replace},
      {"ui_layer_style_gradient_editor_drag_updates_stops_and_previews",
       ui_layer_style_gradient_editor_drag_updates_stops_and_previews},
      {"ui_layer_style_gradient_stop_picker_follows_the_selected_stop",
       ui_layer_style_gradient_stop_picker_follows_the_selected_stop},
      {"ui_layer_style_ok_keeps_the_open_stop_picker_color", ui_layer_style_ok_keeps_the_open_stop_picker_color},
      {"ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops",
       ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops},
      {"ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks",
       ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks},
      {"ui_layer_style_gradient_page_controls_map_to_settings",
       ui_layer_style_gradient_page_controls_map_to_settings},
      {"ui_offscreen_dialog_capture_uses_active_theme", ui_offscreen_dialog_capture_uses_active_theme},
      {"ui_layer_style_noise_gradient_overlay_loads_into_dialog",
       ui_layer_style_noise_gradient_overlay_loads_into_dialog},
      {"ui_layer_style_preview_is_transient_and_show_effects_persists",
       ui_layer_style_preview_is_transient_and_show_effects_persists},
      {"ui_layer_style_blending_options_round_trip_the_interior_group_flag",
       ui_layer_style_blending_options_round_trip_the_interior_group_flag},
      {"ui_layer_style_blending_options_round_trip_the_clipped_group_flag",
       ui_layer_style_blending_options_round_trip_the_clipped_group_flag},
      {"ui_layer_style_gradient_stroke_controls_map_to_settings",
       ui_layer_style_gradient_stroke_controls_map_to_settings},
      {"ui_layer_style_stroke_gradient_fill_keeps_dialog_height",
       ui_layer_style_stroke_gradient_fill_keeps_dialog_height},
      {"ui_layer_style_bevel_lighting_controls_map_to_settings",
       ui_layer_style_bevel_lighting_controls_map_to_settings},
      {"ui_layer_style_satin_controls_map_to_settings", ui_layer_style_satin_controls_map_to_settings},
      {"ui_layer_style_outer_glow_technique_and_range_map_to_settings",
       ui_layer_style_outer_glow_technique_and_range_map_to_settings},
      {"ui_layer_style_continuous_shadow_controls_map_to_settings_and_mark_patchy_only",
       ui_layer_style_continuous_shadow_controls_map_to_settings_and_mark_patchy_only},
      {"ui_layer_style_drop_shadow_contour_keeps_a_custom_curve", ui_layer_style_drop_shadow_contour_keeps_a_custom_curve},
      {"ui_layer_style_inner_glow_technique_and_range_map_to_settings",
       ui_layer_style_inner_glow_technique_and_range_map_to_settings},
      {"ui_layer_style_pattern_warning_follows_resolvability",
       ui_layer_style_pattern_warning_follows_resolvability},
      {"ui_layer_style_bevel_texture_missing_pattern_presents_unchecked",
       ui_layer_style_bevel_texture_missing_pattern_presents_unchecked},
      {"ui_layer_style_pattern_overlay_controls_map_to_settings",
       ui_layer_style_pattern_overlay_controls_map_to_settings},
      {"ui_layer_style_blending_options_round_trip_channel_restrictions",
       ui_layer_style_blending_options_round_trip_channel_restrictions},
      {"ui_layer_style_channel_checkboxes_disabled_for_unsupported_payload",
       ui_layer_style_channel_checkboxes_disabled_for_unsupported_payload},
  };
}
